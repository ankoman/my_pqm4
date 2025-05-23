#include "api.h"
#include "indcpa.h"
#include "params.h"
#include "randombytes.h"
#include "symmetric.h"
#include "verify.h"
#include "poly.h"

#include <stdlib.h>

#include <stdlib.h>

#include <string.h>


/*************************************************
* Name:        crypto_kem_keypair_derand
*
* Description: Generates public and private key
*              for CCA-secure Kyber key encapsulation mechanism
*
* Arguments:   - uint8_t *pk: pointer to output public key
*                (an already allocated array of KYBER_PUBLICKEYBYTES bytes)
*              - uint8_t *sk: pointer to output private key
*                (an already allocated array of KYBER_SECRETKEYBYTES bytes)
*              - uint8_t *coins: pointer to input randomness
*                (an already allocated array filled with 2*KYBER_SYMBYTES random bytes)
**
* Returns 0 (success)
**************************************************/
static int crypto_kem_keypair_derand(uint8_t *pk,
        uint8_t *sk,
        const uint8_t *coins) {
    indcpa_keypair_derand(pk, sk, coins);
    memcpy(sk + KYBER_INDCPA_SECRETKEYBYTES, pk, KYBER_PUBLICKEYBYTES);
    hash_h(sk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, pk, KYBER_PUBLICKEYBYTES);
    /* Value z for pseudo-random output on reject */
    memcpy(sk + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, coins + KYBER_SYMBYTES, KYBER_SYMBYTES);
    return 0;
}

/*************************************************
* Name:        crypto_kem_keypair
*
* Description: Generates public and private key
*              for CCA-secure Kyber key encapsulation mechanism
*
* Arguments:   - unsigned char *pk: pointer to output public key (an already allocated array of CRYPTO_PUBLICKEYBYTES bytes)
*              - unsigned char *sk: pointer to output private key (an already allocated array of CRYPTO_SECRETKEYBYTES bytes)
*
* Returns 0 (success)
**************************************************/
int crypto_kem_keypair(unsigned char *pk, unsigned char *sk) {
    uint8_t coins[2 * KYBER_SYMBYTES];
    randombytes(coins, 2 * KYBER_SYMBYTES);
    crypto_kem_keypair_derand(pk, sk, coins);
    return 0;
}


/*************************************************
* Name:        crypto_kem_enc_derand
*
* Description: Generates cipher text and shared
*              secret for given public key
*
* Arguments:   - uint8_t *ct: pointer to output cipher text
*                (an already allocated array of KYBER_CIPHERTEXTBYTES bytes)
*              - uint8_t *ss: pointer to output shared secret
*                (an already allocated array of KYBER_SSBYTES bytes)
*              - const uint8_t *pk: pointer to input public key
*                (an already allocated array of KYBER_PUBLICKEYBYTES bytes)
*              - const uint8_t *coins: pointer to input randomness
*                (an already allocated array filled with KYBER_SYMBYTES random bytes)
**
* Returns 0 (success)
**************************************************/
static int crypto_kem_enc_derand(uint8_t *ct,
        uint8_t *ss,
        const uint8_t *pk,
        const uint8_t *coins) {
    uint8_t buf[2 * KYBER_SYMBYTES];
    /* Will contain key, coins */
    uint8_t kr[2 * KYBER_SYMBYTES];

    memcpy(buf, coins, KYBER_SYMBYTES);

    /* Multitarget countermeasure for coins + contributory KEM */
    hash_h(buf + KYBER_SYMBYTES, pk, KYBER_PUBLICKEYBYTES);
    hash_g(kr, buf, 2 * KYBER_SYMBYTES);

    /* coins are in kr+KYBER_SYMBYTES */
    indcpa_enc(ct, buf, pk, kr + KYBER_SYMBYTES);

    memcpy(ss, kr, KYBER_SYMBYTES);
    return 0;
}

/*************************************************
* Name:        crypto_kem_enc
*
* Description: Generates cipher text and shared
*              secret for given public key
*
* Arguments:   - uint8_t *ct: pointer to output cipher text
*                (an already allocated array of KYBER_CIPHERTEXTBYTES bytes)
*              - uint8_t *ss: pointer to output shared secret
*                (an already allocated array of KYBER_SSBYTES bytes)
*              - const uint8_t *pk: pointer to input public key
*                (an already allocated array of KYBER_PUBLICKEYBYTES bytes)
*
* Returns 0 (success)
**************************************************/
int crypto_kem_enc(uint8_t *ct,
        uint8_t *ss,
        const uint8_t *pk) {
    uint8_t coins[KYBER_SYMBYTES];
    randombytes(coins, KYBER_SYMBYTES);
    crypto_kem_enc_derand(ct, ss, pk, coins);
    return 0;
}

/*************************************************
* Name:        crypto_kem_dec
*
* Description: Generates shared secret for given
*              cipher text and private key
*
* Arguments:   - unsigned char *ss:       pointer to output shared secret (an already allocated array of CRYPTO_BYTES bytes)
*              - const unsigned char *ct: pointer to input cipher text (an already allocated array of CRYPTO_CIPHERTEXTBYTES bytes)
*              - const unsigned char *sk: pointer to input private key (an already allocated array of CRYPTO_SECRETKEYBYTES bytes)
*
* Returns 0.
*
* On failure, ss will contain a pseudo-random value.
**************************************************/
int crypto_kem_dec(unsigned char *ss, const unsigned char *ct, const unsigned char *sk) {
    int fail;
    uint8_t buf[2 * KYBER_SYMBYTES];
    /* Will contain key, coins */
    uint8_t kr[2 * KYBER_SYMBYTES];
    const uint8_t *pk = sk + KYBER_INDCPA_SECRETKEYBYTES;

    indcpa_dec(buf, ct, sk);

    /* Multitarget countermeasure for coins + contributory KEM */
    memcpy(buf + KYBER_SYMBYTES, sk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES);
    hash_g(kr, buf, 2 * KYBER_SYMBYTES);

    /* coins are in kr+KYBER_SYMBYTES */
    fail = indcpa_enc_cmp(ct, buf, pk, kr + KYBER_SYMBYTES);

    /* Compute rejection key */
    rkprf(ss, sk + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, ct);

    /* Copy true key to return buffer if fail is false */
    cmov(ss, kr, KYBER_SYMBYTES, (uint8_t) (1 - fail));

    return 0;
}

int crypto_kem_ct_check( const unsigned char *ct, const unsigned char *sk) {
    char buf1[100];
    /* Will contain key, coins */
    const uint8_t *pk = sk + KYBER_INDCPA_SECRETKEYBYTES;

    poly poly0;
    int i, j, k, z;

    for(i = 0; i < KYBER_K; i++) {
        poly_unpackdecompress(&poly0, ct, 0);    // First polynomial of u
        for (z = 0; z < KYBER_N; z++) {
            if (poly0.coeffs[z] == 0) {
                sprintf(buf1, "Zero coeff detected: %d", poly0.coeffs[z]);
                hal_send_str(buf1);
                z++;
            }
        }
    }

    return 0;
}