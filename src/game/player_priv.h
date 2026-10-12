/* Helpers shared by the player/collision sources (not part of the API). */
#ifndef SML_PLAYER_PRIV_H
#define SML_PLAYER_PRIV_H

#include "enemies.h"
#include "game.h"
#include "player.h"
#include "ram.h"
#include "sound.h"

/* hGameState and hScrollX are shared with interrupt handlers */
enum { ADDR_GAME_STATE = 0xFFB3, ADDR_SCROLL_X = 0xFFA4, ADDR_SLOT_ID_SCRATCH = 0xFFFB };
static inline uint8_t game_state(void) { return shared_read(ADDR_GAME_STATE); }
static inline void set_game_state(uint8_t s) { shared_write(ADDR_GAME_STATE, s); }
static inline uint8_t read_scroll_x(void) { return shared_read(ADDR_SCROLL_X); }

/* Sound requests from main-program code */
static inline void request_sfx(uint8_t n) { shared_write(SOUND_SFX_REQUEST, n); }
static inline void request_noise(uint8_t n) { shared_write(SOUND_NOISE_REQUEST, n); }
static inline void request_music(uint8_t n) { shared_write(SOUND_MUSIC_REQUEST, n); }

/* The enemy slot (or other 16-byte object) at an address */
static inline EnemySlot *object_at(uint16_t addr)
{
    return (EnemySlot *)&g_hw->wram[(addr - 0xC000u) & 0x1FFF];
}

/* The tile address of the last tile lookup (HL after LookupTile) */
static inline uint16_t looked_up_tile_address(void)
{
    return (uint16_t)(tile_addr_hi << 8 | tile_addr_lo);
}

/* Each level has an "overlay" at C800-CA3F, the same shape as the
   background map at 9800-9BFF: it says what is inside a block ($C0 coins,
   $F0 nothing, a powerup id, ...) or that a block is hidden. The original
   forms the address by adding $30 to the high byte of the map address. */
static inline uint8_t block_contents(uint16_t map_address)
{
    return shared_read((uint16_t)(map_address + 0x3000));
}

/* Block sprite (the bumped block shown above Mario) in the OAM buffer: the
   twelfth sprite, C02C-C02F */
enum { BLOCK_SPRITE = 0x2C };
static inline void set_block_sprite(int field, uint8_t v)
{
    shared_write((uint16_t)(ADDR_OAM_BUFFER + BLOCK_SPRITE + field), v);
}

/* The request a hit block left for the vblank handler (block_events), 0 if
   none; and leaving one: the event code and the block's map address. */
static inline uint8_t pending_block_event(void) { return shared_read(ADDR_BLOCK_EVENT); }
static inline void post_block_event(uint8_t event, uint16_t address)
{
    shared_write(ADDR_BLOCK_EVENT, event);
    shared_write(ADDR_BLOCK_EVENT_HI, (uint8_t)(address >> 8));
    shared_write(ADDR_BLOCK_EVENT_LO, (uint8_t)address);
}

#endif
