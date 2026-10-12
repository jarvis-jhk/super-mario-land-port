/*
 * Input movies.
 *
 * A movie is the list of joypad states the game sees, one entry per joypad
 * poll. Super Mario Land polls the joypad exactly once per iteration of its
 * main loop (ReadJoypad), so "one entry per poll" means "one entry per game
 * frame" and the same movie drives both the original ROM (in the reference
 * emulator) and the C port, independent of how long either takes per frame.
 *
 * Text format (UTF-8, '#' starts a comment):
 *
 *     RIGHT+B *120     hold Right and B for 120 polls
 *     RIGHT+B+A        one poll
 *     - *30            nothing pressed for 30 polls
 *
 * Button names: A B SELECT START RIGHT LEFT UP DOWN ('-' = none).
 */
#ifndef SML_MOVIE_H
#define SML_MOVIE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *inputs;   /* joypad bits, same layout as hJoyHeld */
    size_t count;
    size_t capacity;
} Movie;

int movie_load(Movie *m, const char *path);           /* 0 on success */
int movie_save(const Movie *m, const char *path);
void movie_append(Movie *m, uint8_t buttons);
void movie_free(Movie *m);
/* Input for poll i (0 = nothing pressed past the end of the movie). */
static inline uint8_t movie_get(const Movie *m, size_t i)
{
    return i < m->count ? m->inputs[i] : 0;
}

#endif
