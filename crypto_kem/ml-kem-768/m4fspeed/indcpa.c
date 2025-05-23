#include "indcpa.h"
#include "ntt.h"
#include "poly.h"
#include "polyvec.h"
#include "randombytes.h"
#include "symmetric.h"
#include "matacc.h"

#include <string.h>
#include <stdint.h>
/*************************************************
* Name:        indcpa_keypair_derand
*
* Description: Generates public and private key for the CPA-secure
*              public-key encryption scheme underlying Kyber
*
* Arguments:   - uint8_t *pk: pointer to output public key
*                             (of length KYBER_INDCPA_PUBLICKEYBYTES bytes)
*              - uint8_t *sk: pointer to output private key
*                             (of length KYBER_INDCPA_SECRETKEYBYTES bytes)
*              - const uint8_t *coins: pointer to input randomness
*                             (of length KYBER_SYMBYTES bytes)
**************************************************/
void indcpa_keypair_derand(unsigned char *pk,
                    unsigned char *sk, 
                    const unsigned char *coins){
    polyvec skpv, skpv_prime;
    poly pkp;
    unsigned char buf[2 * KYBER_SYMBYTES];
    unsigned char *publicseed = buf;
    unsigned char *noiseseed = buf + KYBER_SYMBYTES;
    int i;
    unsigned char nonce = 0;

    memcpy(buf, coins, KYBER_SYMBYTES);
    buf[KYBER_SYMBYTES] = KYBER_K;
    hash_g(buf, buf, KYBER_SYMBYTES + 1);

    for (i = 0; i < KYBER_K; i++)
        poly_getnoise(skpv.vec + i, noiseseed, nonce++);

    polyvec_ntt(&skpv);
    
    // i = 0
    matacc_cache32(&pkp, &skpv, &skpv_prime, 0, publicseed, 0);
    poly_invntt(&pkp);

    poly_addnoise(&pkp, noiseseed, nonce++);
    poly_ntt(&pkp);

    poly_tobytes(pk, &pkp);
    for (i = 1; i < KYBER_K; i++) {
        matacc_opt32(&pkp, &skpv, &skpv_prime, i, publicseed, 0);
        poly_invntt(&pkp);

        poly_addnoise(&pkp, noiseseed, nonce++);
        poly_ntt(&pkp);

        poly_tobytes(pk+i*KYBER_POLYBYTES, &pkp);
    }
    polyvec_tobytes(sk, &skpv);
    memcpy(pk + KYBER_POLYVECBYTES, publicseed, KYBER_SYMBYTES); // Pack the public seed in the public key
}

/*************************************************
* Name:        indcpa_enc
*
* Description: Encryption function of the CPA-secure
*              public-key encryption scheme underlying Kyber.
*
* Arguments:   - unsigned char *c:          pointer to output ciphertext (of length KYBER_INDCPA_BYTES bytes)
*              - const unsigned char *m:    pointer to input message (of length KYBER_INDCPA_MSGBYTES bytes)
*              - const unsigned char *pk:   pointer to input public key (of length KYBER_INDCPA_PUBLICKEYBYTES bytes)
*              - const unsigned char *coin: pointer to input random coins used as seed (of length KYBER_SYMBYTES bytes)
*                                           to deterministically generate all randomness
**************************************************/
void indcpa_enc(unsigned char *c,
               const unsigned char *m,
               const unsigned char *pk,
               const unsigned char *coins) {
    polyvec sp, sp_prime;
    poly bp;
    poly *pkp = &bp;
    poly *k = &bp;
    poly *v = &sp.vec[0];
    const unsigned char *seed = pk+KYBER_POLYVECBYTES;
    int i;
    unsigned char nonce = 0;

    for (i = 0; i < KYBER_K; i++)
        poly_getnoise(sp.vec + i, coins, nonce++);

    polyvec_ntt(&sp);

    // i = 0
    matacc_cache32(&bp, &sp, &sp_prime, 0, seed, 1);
    poly_invntt(&bp);
    poly_addnoise(&bp, coins, nonce++);
    poly_reduce(&bp);
    poly_packcompress(c, &bp, 0);
    for (i = 1; i < KYBER_K; i++) {
        matacc_opt32(&bp, &sp, &sp_prime, i, seed, 1);
        poly_invntt(&bp);

        poly_addnoise(&bp, coins, nonce++);
        poly_reduce(&bp);

        poly_packcompress(c, &bp, i);
    }

    poly_frombytes(pkp, pk);
    int32_t v_tmp[KYBER_N];
    
    poly_basemul_opt_16_32(v_tmp, &sp.vec[0], pkp, &sp_prime.vec[0]);
    for (i = 1; i < KYBER_K - 1; i++) {
        poly_frombytes(pkp, pk + i*KYBER_POLYBYTES);
        poly_basemul_acc_opt_32_32(v_tmp, &sp.vec[i], pkp, &sp_prime.vec[i]);
    }
    poly_frombytes(pkp, pk + i*KYBER_POLYBYTES);
    poly_basemul_acc_opt_32_16(v, &sp.vec[i], pkp, &sp_prime.vec[i], v_tmp);

    poly_invntt(v);

    poly_addnoise(v, coins, nonce++);

    poly_frommsg(k, m);
    poly_add(v, v, k);
    poly_reduce(v);

    poly_compress(c + KYBER_POLYVECCOMPRESSEDBYTES, v);
}

/*************************************************
* Name:        indcpa_enc_cmp
*
* Description: Re-encryption function.
*              Compares the re-encypted ciphertext with the original ciphertext byte per byte.
*              The comparison is performed in a constant time manner.
*
*
* Arguments:   - unsigned char *ct:         pointer to input ciphertext to compare the new ciphertext with (of length KYBER_INDCPA_BYTES bytes)
*              - const unsigned char *m:    pointer to input message (of length KYBER_INDCPA_MSGBYTES bytes)
*              - const unsigned char *pk:   pointer to input public key (of length KYBER_INDCPA_PUBLICKEYBYTES bytes)
*              - const unsigned char *coin: pointer to input random coins used as seed (of length KYBER_SYMBYTES bytes)
*                                           to deterministically generate all randomness
* Returns:     - boolean byte indicating that re-encrypted ciphertext is NOT equal to the original ciphertext
**************************************************/
unsigned char indcpa_enc_cmp(const unsigned char *c,
                             const unsigned char *m,
                             const unsigned char *pk,
                             const unsigned char *coins) {
    uint64_t rc = 0;
    polyvec sp, sp_prime;
    poly bp;
    poly *pkp = &bp;
    poly *k = &bp;
    poly *v = &sp.vec[0];
    const unsigned char *seed = pk+KYBER_POLYVECBYTES;
    int i;
    unsigned char nonce = 0;

    for (i = 0; i < KYBER_K; i++)
        poly_getnoise(sp.vec + i, coins, nonce++);

    polyvec_ntt(&sp);
    
    // i = 0
    matacc_cache32(&bp, &sp, &sp_prime, 0, seed, 1);
    poly_invntt(&bp);
    poly_addnoise(&bp, coins, nonce++);
    poly_reduce(&bp);
    rc |= cmp_poly_packcompress(c, &bp, 0);
    for (i = 1; i < KYBER_K; i++) {
        matacc_opt32(&bp, &sp, &sp_prime, i, seed, 1);
        poly_invntt(&bp);

        poly_addnoise(&bp, coins, nonce++);
        poly_reduce(&bp);

        rc |= cmp_poly_packcompress(c, &bp, i);
    }

    poly_frombytes(pkp, pk);
    int32_t v_tmp[KYBER_N];
    
    poly_basemul_opt_16_32(v_tmp, &sp.vec[0], pkp, &sp_prime.vec[0]);
    for (i = 1; i < KYBER_K - 1; i++) {
        poly_frombytes(pkp, pk + i*KYBER_POLYBYTES);
        poly_basemul_acc_opt_32_32(v_tmp, &sp.vec[i], pkp, &sp_prime.vec[i]);
    }
    poly_frombytes(pkp, pk + i*KYBER_POLYBYTES);
    poly_basemul_acc_opt_32_16(v, &sp.vec[i], pkp, &sp_prime.vec[i], v_tmp);

    poly_invntt(v);

    poly_addnoise(v, coins, nonce++);
    poly_frommsg(k, m);
    poly_add(v, v, k);
    poly_reduce(v);

    rc |= cmp_poly_compress(c + KYBER_POLYVECCOMPRESSEDBYTES, v);

    rc = ~rc + 1;
    rc >>= 63;
    return (unsigned char)rc;
}

/*************************************************
* Name:        indcpa_dec
*
* Description: Decryption function of the CPA-secure
*              public-key encryption scheme underlying Kyber.
*
* Arguments:   - unsigned char *m:        pointer to output decrypted message (of length KYBER_INDCPA_MSGBYTES)
*              - const unsigned char *c:  pointer to input ciphertext (of length KYBER_INDCPA_BYTES)
*              - const unsigned char *sk: pointer to input secret key (of length KYBER_INDCPA_SECRETKEYBYTES)
**************************************************/
void __attribute__ ((noinline)) indcpa_dec(unsigned char *m,
                                           const unsigned char *c,
                                           const unsigned char *sk) {
    poly mp, bp;
    poly *v = &bp;
    int32_t r_tmp[KYBER_N];
    int i;

    //NTT(Decomp(u')) * s
    poly_unpackdecompress(&mp, c, 0);
    poly_ntt(&mp);
    poly_frombytes_mul_16_32(r_tmp, &mp, sk);
    for(i = 1; i < KYBER_K - 1; i++) {
        poly_unpackdecompress(&bp, c, i);
        poly_ntt(&bp);
        poly_frombytes_mul_32_32(r_tmp, &bp, sk + i*KYBER_POLYBYTES);
    }
    poly_unpackdecompress(&bp, c, i);
    poly_ntt(&bp);
    poly_frombytes_mul_32_16(&mp, &bp, sk + i*KYBER_POLYBYTES, r_tmp);

    //v' - INTT(u'*s)
    poly_invntt(&mp);
    poly_decompress(v, c+KYBER_POLYVECCOMPRESSEDBYTES);
    poly_sub(&mp, v, &mp);
    poly_reduce(&mp);

    //Decode(mp)
    poly_tomsg(m, &mp);
}
