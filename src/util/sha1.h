/* SHA-1 (FIPS 180-1), used to identify the user's ROM. */
#ifndef SML_SHA1_H
#define SML_SHA1_H
#include <stdint.h>
#include <stddef.h>
void sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
/* digest as 40 lowercase hex characters + NUL */
void sha1_hex(const uint8_t *data, size_t len, char out[41]);
#endif
