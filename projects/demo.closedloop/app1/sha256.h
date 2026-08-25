/*
 * sha256.h - minimal SHA-256 for the trusted-boot negative test.
 * Public-domain style compact implementation (no S3K deps, no libc).
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint32_t state[8];
	uint64_t bitlen;
	uint8_t buf[64];
	size_t buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);

#endif /* SHA256_H */