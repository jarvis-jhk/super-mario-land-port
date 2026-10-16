/*
 * Widescreen view: the game's 160x144 picture in the middle, the level
 * continued to the left and right. Presentation only: it reads the machine
 * state after a frame and never changes it.
 *
 * Inside a level the background map holds the 32 columns drawn last; the
 * columns further right have not been drawn by the game yet and are decoded
 * from the level data in the ROM, the way load_next_column (00:21B1) does.
 * The parts of sprites that cross the screen's edges are drawn as well.
 * Enemies the game has not spawned yet are not visible. Outside levels (title,
 * bonus game, underground rooms, ending) the sides stay blank.
 */
#ifndef SML_WIDESCREEN_H
#define SML_WIDESCREEN_H

#include <stddef.h>
#include <stdint.h>
#include "../hw/machine.h"

#define WIDE_MAX_W 512          /* 32:9 at 144 lines */

/* Which level columns the background map holds: those the game drew since
   the level (or a checkpoint, or the way back from a pipe) was entered. */
typedef struct {
    int on;                     /* a level is on screen */
    int level_index;
    int first_column;           /* the first level column drawn */
    int last_next;
} WideState;

/* Call after every frame, also when nothing is drawn. */
void widescreen_track(WideState *ws, const Machine *m);

/* Renders a w x 144 picture (w even, 160..WIDE_MAX_W) of shades 0..3 into
   out. Returns 1 when the sides show the level, 0 when they are blank. */
int widescreen_render(const WideState *ws, const Machine *m, const uint8_t *rom, size_t rom_size,
                      int w, uint8_t *out);

#endif
