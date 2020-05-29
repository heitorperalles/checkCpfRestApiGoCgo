#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

// Function to validate CPF
//
// Param: Cpf
// Return:
//		200 (CPF OK)
//		400 (Invalid CPF format)
//		403 (CPF not regular or not existant)
//		500 (Communication problem)
int validateCpf(const char * cpf);

#endif /* MIDDLEWARE_H */
