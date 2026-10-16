/* Built-in pixel font for frontend messages and the changelog screen. */
#ifndef SML_TEXT_H
#define SML_TEXT_H

#include <SDL.h>
#include <stddef.h>

#define TEXT_CELL_W 6   /* 5 pixels and a gap */
#define TEXT_CELL_H 9

/* Draws one line in the renderer's current draw colour; px = pixel size. */
void text_draw(SDL_Renderer *r, int x, int y, int px, const char *s);

/* Word-wraps text to `cols` columns ("\n"-separated output, list items
   indented). Returns the number of lines. */
int text_wrap(const char *in, int cols, char *out, size_t out_len);

#endif
