/*
 * Enemies and other moving objects (powerups, platforms, projectiles).
 *
 * The game keeps ten "enemy slots" of 16 bytes at D100-D19F. Anything that
 * moves on its own and is not Mario lives in one: Goombas, Koopas, lifts,
 * mushrooms, fireballs, explosions, bosses. Slot 9 (D190) is reserved for
 * the powerup that pops out of a block. A free slot starts with $FF.
 *
 * To update or draw an object, the game copies its slot into a 13-byte work
 * buffer in high RAM (FFC0-FFCC), works on the buffer, and copies it back.
 * The buffer has exactly the layout of the slot, so the C code overlays the
 * same struct on both (EnemySlot below).
 *
 * Behaviour is driven by small byte-code scripts (see docs/ENEMIES.md).
 */
#ifndef SML_ENEMIES_H
#define SML_ENEMIES_H

#include <stdint.h>
#include <stddef.h>
#include "rt.h"

/* One enemy slot / the work buffer. Offsets are the original's D1x0... */
typedef struct {
    uint8_t id;            /* 0: enemy type (EnemyId), $FF = slot free */
    uint8_t speed;         /* 1: current speed; low nibble pixels/frame in X, high nibble in Y;
                                  set by the speed commands of the script */
    uint8_t y;             /* 2: screen position of the top of the object, +16 */
    uint8_t x;             /* 3: screen position, +8; the screen is 160 wide, so x >= $E0
                                  means "left of the screen" and removes the object */
    uint8_t script_pos;    /* 4: offset of the next command in the script */
    uint8_t facing;        /* 5: bit 0 faces/moves right, bit 1 moves down (selects the
                                  left/right sprite set and the gravity direction) */
    uint8_t sprite;        /* 6: sprite (animation frame) number, indexes Data_2FE2/30B4 */
    uint8_t move_flags;    /* 7: movement behaviour, see EnemyMove */
    uint8_t wait;          /* 8: frames left to keep moving at the current speed */
    uint8_t tick;          /* 9: frame divider for wait: low nibble counts up to the high nibble
                                  (set by script command F4) */
    uint8_t dimensions;    /* A: bit 7 = cannot be killed, bits 4-6 width in tiles, bits 0-2 height */
    uint8_t carries_mario; /* B: nonzero while Mario stands on it (lift): Mario moves with it */
    uint8_t health;        /* C: lower 6 bits hit points; $C0 and up = boss */
    uint8_t spare[3];      /* D-F: unused */
} EnemySlot;

_Static_assert(sizeof(EnemySlot) == 16, "an enemy slot is 16 bytes");
_Static_assert(offsetof(EnemySlot, x) == 3, "slot layout");
_Static_assert(offsetof(EnemySlot, move_flags) == 7, "slot layout");
_Static_assert(offsetof(EnemySlot, dimensions) == 0xA, "slot layout");
_Static_assert(offsetof(EnemySlot, health) == 0xC, "slot layout");

enum {
    ENEMY_SLOTS = 10,          /* D100-D19F */
    POWERUP_SLOT = 9,          /* D190: where blocks release powerups */
    ENEMY_FREE = 0xFF,         /* id of an unused slot */
    ENEMY_SLOT_BASE = 0xD100,
    ENEMY_BUFFER = 0xFFC0,
};

/* Bits of EnemySlot.move_flags (script command F2 sets them) */
enum {
    MOVE_STAY_ON_LEDGE = 0x01,    /* turn around instead of walking off an edge */
    MOVE_GRAVITY       = 0x02,    /* falls when there is no floor below */
    MOVE_WALL_MASK     = 0x0C,    /* blocked sideways: 0 keep going, 4 turn around, $C restart the script */
    MOVE_FLOOR_MASK    = 0x30,    /* blocked while moving down: 0 keep going, $10 move up instead, $30 restart */
    MOVE_CEILING_MASK  = 0xC0,    /* blocked while moving up: 0 keep going, $40 move down instead, $C0 restart */
};

/* Enemy ids (reference/disasm/enemies.asm) used by the C code */
enum EnemyId {
    ENEMY_CHIBIBO = 0x00,
    ENEMY_KING_TOTOMESU = 0x08,
    ENEMY_DRAGONZAMASU = 0x1A,
    ENEMY_EXPLOSION = 0x27,
    ENEMY_HIYOIHOI = 0x32,
    ENEMY_TATANGA = 0x60,
    ENEMY_BIOKINTON = 0x61,
};

/* The work buffer and the slots in RAM */
#define enemy_buf (*(EnemySlot *)&HRAM(ENEMY_BUFFER))
#define enemy_slot(i) ((EnemySlot *)WRAM_PTR(ENEMY_SLOT_BASE + 16 * (i)))

/* ---- enemies.c (bank 0, 2442-2D05) ---- */
void reset_level_enemies(void);                   /* 00:2442 Call_2442 */
void init_enemy_slots(void);                      /* 00:245C InitEnemySlots */
void update_enemies(void);                        /* 00:2491 Call_2491 */
void spawn_enemies(void);                         /* 00:249B SpawnEnemies */
void launch_enemy_projectile(void);               /* 00:24D6 Call_24D6 */
void spawn_enemy_from_level_data(uint16_t entry); /* 00:24EF Call_24EF */
void spawn_powerup(uint8_t id);                   /* 00:254D Call_254D */
void draw_enemies(void);                          /* 00:2568 DrawEnemies */
void draw_enemy(void);                            /* 00:25C0 (DrawEnemies.drawEnemy) */
void update_enemy_scripts(void);                  /* 00:2648 Call_2648 */
void update_enemy(uint16_t script);               /* 00:2676 (Call_2648.updateEnemy) */

/* Responses to something hitting an enemy; slot = address of the enemy slot
   (D1x0). Return values (callers test them with `and a`): 0 = nothing
   happened, $FF = the enemy turned into another one (death animation etc.),
   $FE = a boss lost a hit point but lives; enemy_touches_mario returns
   nonzero when Mario is hurt. */
uint8_t stomp_enemy(uint16_t slot);               /* 00:2A01 Call_2A01 */
uint8_t hit_enemy_from_below(uint16_t slot);      /* 00:2A23 Call_2A23 */
uint8_t enemy_touches_mario(uint16_t slot);       /* 00:2A44 Call_2A44 */
uint8_t superball_hits_enemy(uint16_t slot);      /* 00:2A68 Call_2A68 */
uint8_t bullet_hits_enemy(uint16_t slot);         /* 00:2AAD Call_2AAD */
uint8_t star_mario_hits_enemy(uint16_t slot);     /* 00:2B06 Call_2B06 */
void explode_all_enemies(void);                   /* 00:2B2A ExplodeAllEnemies */

/* Collision probes for the object in the work buffer. They return true
   ("carry") when the tile is empty (< $5F or >= $F0), false when solid. */
bool enemy_probe_left(void);                      /* 00:2B84 Call_2B84 */
bool enemy_probe_right(void);                     /* 00:2B9A Call_2B9A */
bool enemy_probe_below(void);                     /* 00:2BBB Call_2BBB */
bool enemy_probe_below_left(void);                /* 00:2BE4 Call_2BE4 */
bool enemy_probe_below_right(void);               /* 00:2BFE Call_2BFE */
bool enemy_probe_above(void);                     /* 00:2C21 Call_2C21 */

void scroll_enemies(uint8_t pixels);              /* 00:2C9F Call_2C9F */
void init_enemy(uint16_t slot);                   /* 00:2CBB InitEnemy */
void copy_slot_to_buffer(uint8_t slot);           /* 00:2CE5 CopyEnemySlotToBuffer */
void copy_slot_to_buffer_at(uint16_t addr);       /* 00:2CEB (.fromHL) */
void copy_buffer_to_slot(uint8_t slot);           /* 00:2CF7 CopyBufferToEnemySlot */
void copy_buffer_to_slot_at(uint16_t addr);       /* 00:2CFD (.toHL) */


/* ---- objects_autoscroll.c (bank 1): the shooter levels ---- */
void autoscroll_step(void);                       /* 01:4FB2 */
void autoscroll_steer(void);                      /* 01:4FEC */
uint8_t autoscroll_check_above(void);             /* 01:5046 */
uint8_t autoscroll_check_below(void);             /* 01:5089 */
void autoscroll_update_shots(void);               /* 01:5118 */
void autoscroll_crush_check(void);                /* 01:515E */

#endif
