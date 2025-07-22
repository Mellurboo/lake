#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../../nob.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "post_quantum_cryptography.h"

int main(int argc, char** argv){
    if(argc < 2) {
        fprintf(stderr, "ERROR: Provide output filename");
        return 1;
    }

    uint8_t pk[KYBER_PUBLICKEYBYTES] = {0};
    uint8_t sk[KYBER_SECRETKEYBYTES] = {0};

    crypto_kem_keypair(pk, sk);

    size_t filepath_len = strlen(argv[1]);
    if(argv[1][filepath_len - 1] == '/' || argv[1][filepath_len - 1] == '\\') argv[1][filepath_len - 1] = 0;

    write_entire_file(temp_sprintf("%s.pub", argv[1]), pk, KYBER_PUBLICKEYBYTES);
    write_entire_file(temp_sprintf("%s.priv", argv[1]), sk, KYBER_SECRETKEYBYTES);

    printf("Created %s.pub and %s.priv keys!\n", argv[1], argv[1]);

    return 0;
}