/* Minimal PNG writer (uncompressed deflate), no external dependencies. */
#ifndef VERIFY_PNG_H
#define VERIFY_PNG_H
#include <stdint.h>
/* Writes an 8-bit RGB image. Returns 0 on success. */
int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);
/* Writes a Game Boy frame (shades 0..3) using the classic green palette,
   scaled by an integer factor. */
int png_write_shades(const char *path, const uint8_t *shades, int w, int h, int scale);
#endif
