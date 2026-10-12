/*
 * Functions of the translated game, grouped by source file. Each comment
 * names the original routine (bank:address) it replaces.
 */
#ifndef SML_GAME_H
#define SML_GAME_H

#include <stdint.h>
#include <stdbool.h>

/* Subsystem headers (each declares its own functions) */
#if __has_include("sound_engine.h")
#include "sound_engine.h"
#endif
#if __has_include("enemies.h")
#include "enemies.h"
#endif
#if __has_include("player.h")
#include "player.h"
#endif
#if __has_include("level.h")
#include "level.h"
#endif

/* main.c */
void initialize_game(void);            /* 00:0185 Init */
void run_game(void);                   /* 00:0226 main loop */
void dispatch_game_state(void);         /* 00:02A3, 00:0028 TableJump */
bool pause_or_reset(void);              /* 00:07DA pauseOrReset */

/* interrupts.c */
void dma_sprites(void);                 /* 00:FFB6 hDMARoutine */
void vblank_interrupt(uint8_t saved_b); /* 00:0040 VBlank */
void stat_interrupt(void);              /* 00:0048 LCDStatus */
void timer_interrupt(void);             /* 00:0050 timer */

/* hud.c */
void prepare_hud(void);                /* 00:060F PrepareHUD */
void update_lives(void);                /* 00:1C33 UpdateLives (vblank) */
void display_lives(void);               /* 00:1C56 UpdateLives.displayLives */
void display_coins(void);               /* 00:1C1B DisplayCoins */
void print_bcd6(uint16_t src, uint16_t dst); /* 00:3F4D DisplayScore.fromDEtoHL */
void display_score(void);               /* 00:3F39 DisplayScore (vblank) */

/* joypad.c */
void read_joypad(void);                 /* 03:47F2 ReadJoypad */

/* score.c */
void add_score(uint16_t bcd_points);    /* 00:0166 AddScore */

/* video.c */
void load_level_graphics(void);        /* 00:05E7 Call_5E7 */
void copy_data(uint16_t src, uint16_t dst, uint16_t count); /* 00:05DE CopyData */
void clear_bg_maps(void);               /* 00:05CF */
void erase_tile_map(uint16_t addr, uint16_t count); /* 00:05D5 EraseTileMap */
uint16_t tile_address_at(void);         /* 00:3EE6 _LookupTile */
uint8_t lookup_tile(void);              /* 00:0153 LookupTile */
void tile_address_to_pixel(void);       /* 00:3F13 */

#endif
