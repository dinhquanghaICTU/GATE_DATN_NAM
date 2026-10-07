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

/**
 * @brief Mã hóa chuỗi JSON plaintext thành định dạng WebSocket của Hunonic: "nonce_b64:ciphertext_b64"
 * 
 * @param plaintext Chuỗi JSON đầu vào cần mã hóa
 * @param out_b64 Buffer đầu ra nhận chuỗi "nonce_b64:ct_b64"
 * @param out_max_len Kích thước tối đa của out_b64
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t hunonic_crypto_encrypt(const char *plaintext, char *out_b64,
                                 size_t out_max_len);

/**
 * @brief Giải mã chuỗi "nonce_b64:ciphertext_b64" nhận từ WebSocket thành JSON plaintext
 * 
 * @param in_b64 Chuỗi nhận từ WebSocket (dạng "nonce_b64:ct_b64")
 * @param out_plaintext Buffer đầu ra nhận chuỗi JSON đã giải mã
 * @param out_max_len Kích thước tối đa của out_plaintext
 * @return esp_err_t ESP_OK nếu giải mã và kiểm tra tag xác thực thành công
 */
esp_err_t hunonic_crypto_decrypt(const char *in_b64, char *out_plaintext,
                                 size_t out_max_len);

#ifdef __cplusplus
}
#endif

#endif // __HUNONIC_CRYPTO_H__
