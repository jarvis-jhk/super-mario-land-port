/* SHA-256 (FIPS 180-4), used to verify downloaded updates. */
#ifndef SML_SHA256_H
#define SML_SHA256_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    size_t used;
    uint64_t total;
} Sha256;
void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t len);
/* digest as 64 lowercase hex characters + NUL */
void sha256_hex_final(Sha256 *s, char out[65]);
/* 0 and the digest of a whole file, -1 if it cannot be read */
int sha256_file(const char *path, char out[65], uint64_t *size);
#endif
