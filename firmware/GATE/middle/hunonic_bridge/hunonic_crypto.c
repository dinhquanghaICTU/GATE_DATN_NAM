#include "hunonic_crypto.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "HUNONIC_CRYPTO";

esp_err_t hunonic_crypto_encrypt(const char *plaintext, char *out_b64,
                                 size_t out_max_len) {
  if (!plaintext || !out_b64 || out_max_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t pt_len = strlen(plaintext);
  if (pt_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t nonce[HUNONIC_NONCE_LEN];
  esp_fill_random(nonce, sizeof(nonce));

  size_t ct_total_len = pt_len + HUNONIC_TAG_LEN;
  uint8_t *ct_buf = (uint8_t *)malloc(ct_total_len);
  if (!ct_buf) {
    ESP_LOGE(TAG, "Khong du bo nho cap phat ct_buf (%d bytes)",
             (int)ct_total_len);
    return ESP_ERR_NO_MEM;
  }

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                               (const unsigned char *)HUNONIC_AES_KEY, 256);
  if (ret != 0) {
    ESP_LOGE(TAG, "mbedtls_gcm_setkey that bai: %d", ret);
    mbedtls_gcm_free(&gcm);
    free(ct_buf);
    return ESP_FAIL;
  }

  uint8_t tag[HUNONIC_TAG_LEN];
  ret = mbedtls_gcm_crypt_and_tag(
      &gcm, MBEDTLS_GCM_ENCRYPT, pt_len, nonce, sizeof(nonce), NULL, 0,
      (const unsigned char *)plaintext, ct_buf, HUNONIC_TAG_LEN, tag);
  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    ESP_LOGE(TAG, "mbedtls_gcm_crypt_and_tag that bai: %d", ret);
    free(ct_buf);
    return ESP_FAIL;
  }

  memcpy(ct_buf + pt_len, tag, HUNONIC_TAG_LEN);

  char nonce_b64[32] = {0};
  size_t nonce_olen = 0;
  ret = mbedtls_base64_encode((unsigned char *)nonce_b64, sizeof(nonce_b64),
                              &nonce_olen, nonce, sizeof(nonce));
  if (ret != 0) {
    ESP_LOGE(TAG, "Base64 encode nonce that bai: %d", ret);
    free(ct_buf);
    return ESP_FAIL;
  }
  nonce_b64[nonce_olen] = '\0';

  size_t ct_b64_req_len = ((ct_total_len + 2) / 3) * 4 + 4;
  char *ct_b64 = (char *)malloc(ct_b64_req_len);
  if (!ct_b64) {
    ESP_LOGE(TAG, "Khong du bo nho cap phat ct_b64 (%d bytes)",
             (int)ct_b64_req_len);
    free(ct_buf);
    return ESP_ERR_NO_MEM;
  }

  size_t ct_olen = 0;
  ret = mbedtls_base64_encode((unsigned char *)ct_b64, ct_b64_req_len,
                              &ct_olen, ct_buf, ct_total_len);
  free(ct_buf);

  if (ret != 0) {
    ESP_LOGE(TAG, "Base64 encode ciphertext that bai: %d", ret);
    free(ct_b64);
    return ESP_FAIL;
  }
  ct_b64[ct_olen] = '\0';

  int written = snprintf(out_b64, out_max_len, "%s:%s", nonce_b64, ct_b64);
  free(ct_b64);

  if (written < 0 || (size_t)written >= out_max_len) {
    ESP_LOGE(TAG, "Buffer out_b64 qua nho (can %d, co %d)", written,
             (int)out_max_len);
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t hunonic_crypto_decrypt(const char *in_b64, char *out_plaintext,
                                 size_t out_max_len) {
  if (!in_b64 || !out_plaintext || out_max_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *colon = strchr(in_b64, ':');
  if (!colon) {
    ESP_LOGE(TAG, "Dinh dang khong hop le (thieu dau ':')");
    return ESP_ERR_INVALID_ARG;
  }

  size_t nonce_b64_len = colon - in_b64;
  const char *ct_b64 = colon + 1;
  size_t ct_b64_len = strlen(ct_b64);

  if (nonce_b64_len == 0 || ct_b64_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  char nonce_b64_buf[64] = {0};
  if (nonce_b64_len >= sizeof(nonce_b64_buf)) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(nonce_b64_buf, in_b64, nonce_b64_len);
  nonce_b64_buf[nonce_b64_len] = '\0';

  uint8_t nonce[32] = {0};
  size_t nonce_olen = 0;
  int ret = mbedtls_base64_decode(nonce, sizeof(nonce), &nonce_olen,
                                  (const unsigned char *)nonce_b64_buf,
                                  nonce_b64_len);
  if (ret != 0 || nonce_olen != HUNONIC_NONCE_LEN) {
    ESP_LOGE(TAG, "Decode nonce that bai (ret=%d, len=%d)", ret,
             (int)nonce_olen);
    return ESP_FAIL;
  }

  size_t raw_ct_max_len = (ct_b64_len / 4) * 3 + 4;
  uint8_t *raw_ct = (uint8_t *)malloc(raw_ct_max_len);
  if (!raw_ct) {
    ESP_LOGE(TAG, "Khong du bo nho cap phat raw_ct (%d bytes)",
             (int)raw_ct_max_len);
    return ESP_ERR_NO_MEM;
  }

  size_t raw_ct_len = 0;
  ret = mbedtls_base64_decode(raw_ct, raw_ct_max_len, &raw_ct_len,
                              (const unsigned char *)ct_b64, ct_b64_len);
  if (ret != 0 || raw_ct_len <= HUNONIC_TAG_LEN) {
    ESP_LOGE(TAG, "Decode ciphertext that bai (ret=%d, len=%d)", ret,
             (int)raw_ct_len);
    free(raw_ct);
    return ESP_FAIL;
  }

  size_t cipher_len = raw_ct_len - HUNONIC_TAG_LEN;
  const uint8_t *tag = raw_ct + cipher_len;

  if (cipher_len >= out_max_len) {
    ESP_LOGE(TAG, "Buffer out_plaintext qua nho (can %d, co %d)",
             (int)cipher_len + 1, (int)out_max_len);
    free(raw_ct);
    return ESP_ERR_NO_MEM;
  }

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                           (const unsigned char *)HUNONIC_AES_KEY, 256);
  if (ret != 0) {
    ESP_LOGE(TAG, "mbedtls_gcm_setkey that bai: %d", ret);
    mbedtls_gcm_free(&gcm);
    free(raw_ct);
    return ESP_FAIL;
  }

  ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, nonce, HUNONIC_NONCE_LEN,
                                 NULL, 0, tag, HUNONIC_TAG_LEN, raw_ct,
                                 (unsigned char *)out_plaintext);
  mbedtls_gcm_free(&gcm);
  free(raw_ct);

  if (ret != 0) {
    ESP_LOGE(TAG,
             "mbedtls_gcm_auth_decrypt that bai (Tag khong hop le hoac sai "
             "key): %d",
             ret);
    return ESP_FAIL;
  }

  out_plaintext[cipher_len] = '\0';
  return ESP_OK;
}