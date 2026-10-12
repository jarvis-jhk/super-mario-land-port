/*
 * Variables shared between interrupt handlers and the main program.
 *
 * The timer interrupt (music) and the LCD STAT interrupt (HUD split, game
 * over window) can fire in the middle of the main program's work. So can
 * the vblank handler when a frame lags: the main program has not finished
 * its frame and reached the halt in the main loop, and the handler (OAM
 * DMA, DrawColumn, block events, lives, score, timer, background animation)
 * interrupts it. Main-loop code must access the variables below with
 * shared_read()/shared_write() (rt.h), so the timing model can put the
 * access in the right order relative to those interrupts. Code that runs
 * inside the interrupt handlers uses plain accesses.
 *
 * The list comes from `emurun --shared-report` over the TAS movies: addresses
 * touched both by an interruptible main program and by the STAT/timer
 * handlers or a lag-frame vblank handler (whose OAM DMA reads the whole
 * sprite buffer). Arrays are listed whole, not only the elements the movies
 * happened to touch. Code running inside a handler (sound engine, STAT
 * handler, vblank handler) accesses them directly: handlers do not
 * interrupt each other.
 */
#ifndef SML_SHARED_H
#define SML_SHARED_H
#include <stdint.h>

static inline int is_shared_address(uint16_t a)
{
    if (a >= 0xC000 && a < 0xC0A5) return 1;   /* sprite buffer (OAM DMA), score, lives change, game over */
    if (a >= 0xC0B0 && a < 0xC0C0) return 1;   /* next level column (DrawColumn) */
    if (a >= 0xC800 && a < 0xCA40) return 1;   /* level overlay map (block events) */
    switch (a) {
    /* sound engine mailboxes: requests from the game, status for the game */
    case 0xDF1F: case 0xDF2F: case 0xDF3F: case 0xDF4F:
    case 0xDFE0: case 0xDFE1: case 0xDFE8: case 0xDFE9:
    case 0xDFF0: case 0xDFF1: case 0xDFF8: case 0xDFF9:
    case 0xFFD3: case 0xFFDE: case 0xFFDF:
    /* timer interrupt bank restoration */
    case 0xFFFD:   /* active ROM bank; synchronize before leaving banked code */
    /* LCD STAT handler */
    case 0xC0A5:   /* game over window enabled */
    case 0xC0AD:   /* game over timer expired */
    case 0xC0DE:   /* vertical scroll active */
    case 0xC0DF:   /* vertical scroll */
    case 0xFFA4:   /* horizontal scroll */
    case 0xFFB3:   /* game state (also the vblank handler) */
    case 0xFFFB:   /* game over countdown */
    /* vblank handler in a lag frame */
    case 0xD014:   /* background animated */
    case 0xDA00: case 0xDA01: case 0xDA02:   /* game timer */
    case 0xFF85:   /* vblank occurred */
    case 0xFF9F:   /* demo mode */
    case 0xFFAC:   /* frame counter */
    case 0xFFB1:   /* score changed */
    case 0xFFB4:   /* world and level */
    case 0xFFE9: case 0xFFEA:                /* column drawing */
    case 0xFFEE: case 0xFFEF: case 0xFFF0:   /* block event */
    case 0xFFFE:   /* HUD coin flag */
        return 1;
    }
    return 0;
}

#endif
