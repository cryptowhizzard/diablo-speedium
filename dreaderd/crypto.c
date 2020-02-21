/*
 * crypto.c	Modern crypt/hash functions.
 *
 * aes_encrypt:	Appplies the same encryption method as the openssl command:
 *		echo "plaintext" | openssl enc -aes-256-cbc -base64
 *
 *		So the data can be decrypted with the openssl command:
 *		echo "crypted" | openssl enc -d -aes-256-cbc -base64
 *
 * sha256hash:	What it says on the label.
 *
 */
#include "defs.h"

#ifdef POST_CRYPTXTRACE

#include <string.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#define ASSERT(x) \
    do { \
        if ((x) == 0) { \
            fprintf(stderr, "%s:%d: %s: Assertion %s failed.\n", \
                __FILE__, __LINE__, __FUNCTION__, #x); \
                goto fail; \
        } \
    } while(0)

static char *base64(unsigned char *data, size_t data_sz) {

    char *result = NULL;

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    ASSERT(bio != NULL);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    ASSERT(BIO_write(bio, data, data_sz) == data_sz);
    BIO_flush(bio);

    BUF_MEM *bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    result = malloc(bufferPtr->length + 1);
    memcpy(result, bufferPtr->data, bufferPtr->length);
    result[bufferPtr->length] = 0;

fail:
    BIO_free_all(bio);

    return result;
}

char *aes_encrypt(char *pass, unsigned char *data, size_t data_sz) {
    unsigned char salt[8] = { 0 };
    unsigned char key[32] = { 0 };
    unsigned char iv[16]  = { 0 };
    unsigned char *result = NULL;
    char *result_b64 = NULL;

    const EVP_CIPHER *cipher = EVP_aes_256_cbc();
    const EVP_MD *md = EVP_sha256();

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    ASSERT(RAND_bytes(salt, sizeof(salt)) == 1);
    const size_t pass_sz = strlen(pass);
    ASSERT(EVP_BytesToKey(cipher, md, salt, (unsigned char *)pass, pass_sz, 1, key, iv) == 32);

    ASSERT(EVP_EncryptInit_ex(ctx, cipher, 0, key, iv) == 1);

    size_t ctxbz = EVP_CIPHER_CTX_block_size(ctx);
    result = calloc(1, 16 + data_sz + ctxbz);

    memcpy(result, "Salted__", 8);
    memcpy(result + 8, salt, 8);
    unsigned char *enc = result + 16;

    int enc_sz;
    ASSERT(EVP_EncryptUpdate(ctx, enc, &enc_sz, data, data_sz) == 1);

    int enc_sz2;
    ASSERT(EVP_EncryptFinal_ex(ctx, enc + enc_sz, &enc_sz2) == 1);
    enc_sz += enc_sz2;

    ASSERT(EVP_CIPHER_CTX_cleanup(ctx) == 1);

    result_b64 = base64(result, 16 + enc_sz);
    goto exit;

fail:
    free(result_b64);
    result_b64 = NULL;

exit:
    free(result);
    EVP_CIPHER_CTX_free(ctx);

    return result_b64;
}

unsigned char *sha256hash(char *input)
{
	static unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, (unsigned char *)input, strlen(input));
	SHA256_Final(hash, &sha256);
	return hash;
}

#endif /* POST_CRYPTXTRACE */
