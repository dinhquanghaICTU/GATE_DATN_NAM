#ifndef __HUNONIC_CRYPTO_H__
#define __HUNONIC_CRYPTO_H__

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUNONIC_AES_KEY     "01234567890123456789012345678901"
#define HUNONIC_KEY_LEN     32
#define HUNONIC_NONCE_LEN   12
#define HUNONIC_TAG_LEN     16

esp_err_t hunonic_crypto_encrypt(const char *plaintext, char *out_b64,
                                 size_t out_max_len);

esp_err_t hunonic_crypto_decrypt(const char *in_b64, char *out_plaintext,
                                 size_t out_max_len);

#ifdef __cplusplus
}
#endif

#endif

