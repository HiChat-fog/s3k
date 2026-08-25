/*
 * sha256.c - minimal SHA-256 (RFC 6234 core), compact for bare-metal use.
 */
#include "sha256.h"

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(sha256_ctx *c, const uint8_t block[64])
{
	uint32_t w[64], a, b, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)block[i * 4] << 24) |
		       ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) |
		       (uint32_t)block[i * 4 + 3];
	for (i = 16; i < 64; i++)
		w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];

	a = c->state[0]; b = c->state[1]; c->state[2] = c->state[2];
	{
		uint32_t cc = c->state[2];
		uint32_t dd = c->state[3];
		uint32_t ee = c->state[4];
		uint32_t ff = c->state[5];
		uint32_t gg = c->state[6];
		uint32_t hh = c->state[7];
		for (i = 0; i < 64; i++) {
			t1 = hh + EP1(ee) + CH(ee, ff, gg) + K[i] + w[i];
			t2 = EP0(a) + MAJ(a, b, cc);
			hh = gg; gg = ff; ff = ee;
			ee = dd + t1;
			dd = cc; cc = b; b = a;
			a = t1 + t2;
		}
		c->state[0] += a; c->state[1] += b; c->state[2] += cc;
		c->state[3] += dd; c->state[4] += ee; c->state[5] += ff;
		c->state[6] += gg; c->state[7] += hh;
	}
}

void sha256_init(sha256_ctx *c)
{
	c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
	c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
	c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
	c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
	c->bitlen = 0; c->buflen = 0;
}

void sha256_update(sha256_ctx *c, const void *data, size_t len)
{
	const uint8_t *p = data;
	c->bitlen += (uint64_t)len * 8;
	while (len >= 64 - c->buflen) {
		size_t n = 64 - c->buflen;
		for (size_t i = 0; i < n; i++)
			c->buf[c->buflen + i] = p[i];
		sha256_transform(c, c->buf);
		p += n; len -= n; c->buflen = 0;
		if (len == 0)
			return;
	}
	for (size_t i = 0; i < len; i++)
		c->buf[c->buflen + i] = p[i];
	c->buflen += len;
}

void sha256_final(sha256_ctx *c, uint8_t out[32])
{
	uint64_t bitlen = c->bitlen;
	uint8_t pad = 0x80;
	uint8_t zero = 0;
	uint8_t lenb[8];
	int i;

	for (i = 0; i < 8; i++)
		lenb[i] = (uint8_t)(bitlen >> (56 - i * 8));

	sha256_update(c, &pad, 1);
	while (c->buflen != 56)
		sha256_update(c, &zero, 1);
	sha256_update(c, lenb, 8); /* final block processed inside update */

	for (i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(c->state[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
		out[i * 4 + 3] = (uint8_t)(c->state[i]);
	}
}