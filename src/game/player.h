/*
 * Mario and his collisions with the level and with other objects.
 * See docs/PLAYER.md for the overview.
 */
#ifndef SML_PLAYER_H
#define SML_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "constants.h"

/* Values of super_status (hSuperStatus) */
enum {
    SUPER_SMALL = 0,       /* small Mario */
    SUPER_GROWING = 1,     /* mushroom just picked up (growth animation, 80 frames) */
    SUPER_BIG = 2,         /* super Mario (with the flower: superball_mario is set too) */
    SUPER_SHRINKING = 3,   /* just hurt: shrink animation (80 frames) */
    SUPER_HURT = 4,        /* invulnerable for a while after being hurt */
};

/* Codes in block_event (hBlockEvent) */
enum {
    BLOCK_EVENT_NONE = 0,
    BLOCK_EVENT_BREAK = 1,      /* a block is breaking */
    BLOCK_EVENT_BOUNCE = 2,     /* a block starts bouncing */
    BLOCK_EVENT_BOUNCING = 3,
    BLOCK_EVENT_BOUNCE_DONE = 4,
    BLOCK_EVENT_COIN = 0xC0,    /* a coin tile is collected */
};

/* Result of the hitbox test: whether the boxes overlap, plus the values
   the original leaves in B and HL (the caller continues with them). */
typedef struct {
    bool hit;
    uint8_t b;            /* enemy's right edge when the test got that far */
    uint8_t hl_offset;    /* HL = object address + this */
} HitboxResult;

/* player.c: injury, death, objects touching Mario */
void injure_mario(void);                          /* 00:09E0 InjureMario */
void kill_mario(void);                            /* 00:09F1 KillMario */
void mario_object_collisions(void);               /* 00:084E Call_84E */
void set_stomp_reward(void);                      /* 00:0A10 Call_A10 */
void bounce_block_hits_enemies(void);             /* 00:0A2D Call_A2D */
HitboxResult hitbox_overlap(uint16_t obj, uint8_t dimensions); /* 00:0AAF Call_AAF */
void mario_rides_platforms(void);                 /* 00:0AEA Call_AEA */
void clear_object_sprites(void);                  /* 00:1ED4 Call_1ED4 */
void invincibility_tick(void);                    /* 00:1F03 Call_1F03 */
void feed_demo_joypad(void);                            /* 00:2113 Call_2113 */
void play_demo_input(void);                       /* 03:4A94 */
void mario_falls_in_pit(void);                    /* 03:4B6F */
void mario_growing(void);                         /* 03:4B8A */
void mario_hurt_blinking(void);                   /* 03:4BB5 */

/* player_superball.c: superballs and bullets */
void update_superballs(void);                     /* 00:1F2D Call_1F2D */
uint8_t find_neighboring_tile(uint8_t x);         /* 00:1FD2 FindNeighboringTile */
void superball_hits_enemies(uint16_t ball_x);     /* 00:200A Call_200A */
void break_block_in_autoscroll(uint8_t tile, uint16_t address); /* 00:2097 Call_200A.breakBlockInAutoscroll */
void start_block_fragments(uint8_t y, uint8_t x); /* 00:1A02 / 00:20C1 */
void move_block_fragments(void);                  /* 03:4AEA */

/* player_draw.c: Mario's sprites */
void draw_mario(void);                            /* 00:1736 Call_1736 */
void draw_objects(uint16_t obj);                  /* 03:4823 Call_4823 */

/* player_move.c: walking */
void mario_animate_and_walk(void);                /* 00:16F5 Call_16F5 */
void mario_walk(void);                            /* 00:1D26 Call_1D26 */
void shift_object_sprites(uint8_t pixels);        /* 00:1EA4 Call_1D26.call_1EA4 */
uint8_t walk_step(void);                          /* 00:1EB4 Call_1D26.call_1EB4 */
void apply_pending_jump_step(void);               /* 03:48FC Call_4823.jmp_48FC */
void move_along_jump_arc(uint16_t obj, uint16_t arc); /* 03:490D Call_490D */

/* player_input.c: jump height, running and projectiles */
void mario_jump_and_fire(void);                   /* 03:498B (+ internal 4966/4975 paths) */

/* collision.c: Mario against the level, blocks and coins */
void clear_block_overlay(void);                   /* 00:165E Call_165E */
uint8_t filter_passthrough_tile(uint8_t tile);    /* 00:1A6B Call_1A6B */
uint8_t mario_side_collision(void);               /* 00:1AAD Call_1AAD */
void mario_floor_collision(void);                 /* 00:17BC Call_17BC */
void mario_head_collision(void);                  /* 00:198C */
bool add_coin(void);                              /* 00:1BFF AddCoin (true if the counter was redrawn) */
void block_events(uint8_t entry_b);                /* 00:1B86 Call_1B86 (vblank) */
void mario_reaches_goal(void);                    /* 00:1B45 Jmp_1B45 */
void bounce_block_sprite(void);                   /* 03:4B3C */

#endif
