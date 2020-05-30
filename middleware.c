//------------------------------------------------------------------------------
// From http://github.com/heitorperalles/checkCpfRestApiGoCgo
//
// Distributed under The MIT License (MIT) <http://opensource.org/licenses/MIT>
//
// Copyright (c) 2020 Heitor Peralles <heitorgp@gmail.com>
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//------------------------------------------------------------------------------
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "middleware.h"

// Public URL to request CPF status on SERPRO API
#define SERPRO_URL "https://apigateway.serpro.gov.br/consulta-cpf-df-trial/v1/cpf/"

// Token to be used setting requests on SERPRO API
#define SERPRO_TOKEN "4e1a1858bdd584fdc077fb7d80f39283"

// Prefix of the Bearer to be inserted on the request header
#define AUTHENTICATION_TOKEN_PREFIX "Authorization: Bearer "

// Authentication Header
#define REQUEST_HEADER AUTHENTICATION_TOKEN_PREFIX SERPRO_TOKEN

// Maximum size of the provided CPF
#define MAX_SIZE_CPF 128

// Maximum size of the complete URL
#define MAX_SIZE_URL 1024

// Maximum size to read from returned JSON from SERPRO
#define MAX_SIZE_RESPONSE_JSON 1024

// validateCpf possible return codes
#define CODE_200_CPF_OK						200
#define CODE_400_INVALID_FORMAT		400
#define CODE_403_SUBJECT_REJECTED	403
#define CODE_500_SERVER_PROBLEM		500

// Logging
#define VERBOSE 0
#if (VERBOSE)
#define log(format, ...) fprintf(stderr, __DATE__ " " __TIME__ " " format "\n", ## __VA_ARGS__)
#else
#define log(format, ...) do { } while ((void)0,0)
#endif

// CPF pre-processing method
//
// Param:
//	Cpf	provided
//	treatedCpf destination
// Return:
//		Pointer to the string with treated CPF or NULL if invalid
char * treatCpf(const char * cpf, char * treatedCpf) {

	log("Verifying CPF [%s]", cpf);
	int len = strlen(cpf);
	if (len < 1) {
		log("Empty CPF string, nothing to do.");
		return NULL;
	}
	else if (len > MAX_SIZE_CPF-1) {
			log("CPF too big, aborting.");
			return NULL;
	}
	int pos=0;
	for (int i=0; i<len; i++) {
		if (cpf[i] >= '0' && cpf[i] <= '9') {
			treatedCpf[pos++]=cpf[i];
		}
	}
	treatedCpf[pos]='\0';
	if (strlen(treatedCpf) < 1) {
		log("CPF with no numbers received, aborting.");
		return NULL;
	}
	log("Post-processed CPF [%s]", treatedCpf);
	return treatedCpf;
}

// Function to convert HTTP code of SERPRO response
//
// Param: code
// Return:
//		200 (Existant CPF)
//		400 (Invalid CPF format)
//		403 (CPF not regular or not existant)
//		500 (Communication problem)
int convertHttpCode(int code) {
	switch (code) {
		case 200:
				log("[SERPRO] Status code 200: Request has been succeeded");
				return CODE_200_CPF_OK;
		case 206:
				log("[SERPRO] Status code 206: Warning, Partial content returned");
				return CODE_200_CPF_OK;
		case 400:
				log("[SERPRO] Status code 400: Invalid CPF format");
				return CODE_400_INVALID_FORMAT;
		case 401:
				log("[SERPRO] Status code 401: Unauthorized, please review the app TOKEN");
				return CODE_500_SERVER_PROBLEM;
		case 404:
				log("[SERPRO] Status code 404: Not existant CPF");
				return CODE_403_SUBJECT_REJECTED;
		case 500:
				log("[SERPRO] Status code 500: Internal Server error");
				return CODE_500_SERVER_PROBLEM;
		default:
				log("[SERPRO] Unknown Status code [%d]", code);
				return CODE_500_SERVER_PROBLEM;
	}
	return CODE_200_CPF_OK;
}

// Function to treat received JSON on SERPRO response
//
// Param: body
// Return:
//		200 (CPF OK)
//		403 (CPF not regular or not existant)
//		500 (Communication problem)
int treatResponseData(const char * body) {

	// Body example:
	// {"ni":"40442820135","nome":"Nome","situacao":{"codigo":"0","descricao":"Regular"}}

	log("Verifying body [%s]", body);

	char * pos = strstr(body, "codigo");
  if (pos == NULL || strlen(pos) < 10) {
		log("[SERPRO] Problem trying to decode received JSON [%s]", body);
		return CODE_500_SERVER_PROBLEM;
	}
	char status = pos[9];
	log("[SERPRO] CPF Status Code: [%c]", status);
	if (status != '0') {
		return CODE_403_SUBJECT_REJECTED;
	}
	return CODE_200_CPF_OK;
}

// Curl callback to write the received data.
// References to CURLOPT_WRITEFUNCTION from Lib Curl documentation.
//
// Param: ptr received data
// Param: size block length
// Param: nmemb amount of blocks
// Param: userdata set by CURLOPT_WRITEDATA
//
// Return: amount of treated bytes.
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {

	int received = size * nmemb;
	if (userdata != NULL && ptr != NULL && received > 0) {

		int current_size = strlen((char*)userdata);
		if (current_size+received < MAX_SIZE_RESPONSE_JSON) {

			memcpy(userdata+current_size, ptr, received);
			((char*)userdata)[current_size+received]='\0';
		}
		else {
			log("Problem: received data is bigger than [%d], discarding.", MAX_SIZE_RESPONSE_JSON);
		}
	}
	return received;
}

// Function to validate CPF
//
// Param: Cpf
// Return:
//		200 (CPF OK)
//		400 (Invalid CPF format)
//		403 (CPF not regular or not existant)
//		500 (Communication problem)
int validateCpf(const char * cpf) {

	char treatedCpf[MAX_SIZE_CPF];
	if (treatCpf(cpf, treatedCpf) == NULL) {
		log("Invalid CPF format [%s]", cpf);
		return CODE_400_INVALID_FORMAT;
	}

	log("[SERPRO] Creating Request...");

	// Initialization

	static bool global=false;
	if (!global){
		int res = curl_global_init(CURL_GLOBAL_ALL);
		if (res != 0) {
			log("Problem during curl_global_init: [%d]", res);
			return CODE_500_SERVER_PROBLEM;
		}
		global=true;
	}

	static CURL *curl=NULL;
	if (curl==NULL) {
		if (!(curl = curl_easy_init())) {
			log("Problem during curl_easy_init.");
			return CODE_500_SERVER_PROBLEM;
		}
	}
	else {
		curl_easy_reset(curl);
	}

	// Setting Curl properties

	if (curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY ) != CURLE_OK) {
		log("Problem setting CURLOPT_HTTPAUTH.");
	}
	if (curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L) != CURLE_OK) {
		log("Problem setting CURLOPT_FAILONERROR.");
	}
	if (curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
		log("Problem setting CURLOPT_NOSIGNAL.");
	}
	if (curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, -1L) != CURLE_OK) {
		log("Problem setting CURLOPT_DNS_CACHE_TIMEOUT.");
	}

	// Setting Function to get the returned Body

	if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback) != CURLE_OK) {
		log("Problem setting CURLOPT_WRITEFUNCTION.");
		return CODE_500_SERVER_PROBLEM;
	}

	char curlOutput[MAX_SIZE_RESPONSE_JSON];
	if (curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlOutput) != CURLE_OK) {
		log("Problem setting CURLOPT_WRITEDATA.");
		return CODE_500_SERVER_PROBLEM;
	}

	// Composing URL

	char completeUrl[MAX_SIZE_URL];
	strcpy(completeUrl, SERPRO_URL);
	strcat(completeUrl, treatedCpf);
	if (curl_easy_setopt(curl, CURLOPT_URL, completeUrl) != CURLE_OK) {
		log("Problem setting CURLOPT_URL.");
		return CODE_500_SERVER_PROBLEM;
	}

	// Setting Authentication TOKEN

	struct curl_slist *headers = NULL;
	if ((headers = curl_slist_append(headers, REQUEST_HEADER)) == NULL) {
		log("Problem during curl_slist_append.");
	}
	else if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) != CURLE_OK) {
		log("Problem setting CURLOPT_HTTPHEADER.");
	}

	// Actually calling the URL

	CURLcode res;
	res = curl_easy_perform(curl);

	if (headers != NULL) {
		curl_slist_free_all(headers);
	}

	// Treating Response HTTP Code

	int http_code = 0;
	if (curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK) {
		log("Problem obtaining CURLINFO_RESPONSE_CODE.");
		return CODE_500_SERVER_PROBLEM;
	}
	if ((http_code = convertHttpCode(http_code)) != CODE_200_CPF_OK) {
		return http_code;
	}

	// Treating Response JSON data
	return treatResponseData(curlOutput);
}
