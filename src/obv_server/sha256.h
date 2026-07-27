/* Public-domain / CC0 portable SHA-256 (Brad Conte style). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]);

/* Convenience: hash bytes to 64-char lowercase hex (NUL-terminated, needs 65 bytes). */
void sha256_hex(const void *data, size_t len, char out_hex[65]);

#ifdef __cplusplus
}
#endif
