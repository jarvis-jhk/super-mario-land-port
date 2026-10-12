/*
 * Internals of the sound engine, shared by sound_engine.c, sound_sfx.c and
 * sound_music.c: addresses of its variables and of its data in ROM bank 3.
 * See docs/SOUND_ENGINE.md for the big picture.
 *
 * Everything here is accessed directly (plain WRAM/HRAM), because the engine
 * runs inside the timer interrupt. The one exception is init_sound(), which
 * the main program calls and which therefore uses shared_write().
 */
#ifndef SML_SOUND_PRIVATE_H
#define SML_SOUND_PRIVATE_H

#include <stdint.h>
#include "rt.h"
#include "sound.h"
#include "sound_engine.h"

/* ---- RAM layout ---------------------------------------------------- */

/* Sound effects. Each effect channel (square, wave, noise) has a block of
   eight bytes whose first byte is the request mailbox (see sound.h). */
enum {
    SQUARE_SFX = 0xDFE0, WAVE_SFX = 0xDFF0, NOISE_SFX = 0xDFF8,
    /* offsets in a block */
    SFX_REQUEST_OFS  = 0,   /* sound number the game asks for */
    SFX_PLAYING_OFS  = 1,   /* number of the sound being played, 0 = none */
    SFX_POSITION_OFS = 2,   /* ticks played so far in the current step */
    SFX_DURATION_OFS = 3,   /* ticks per step */
    SFX_STATE_OFS    = 4,   /* two bytes of per-sound state */
    SFX_STATE2_OFS   = 5,
    SFX_STATE3_OFS   = 6,   /* (square block only) */
};

#define square_sfx_request   WRAM(SQUARE_SFX + SFX_REQUEST_OFS)    /* DFE0 */
#define square_sfx_playing   WRAM(SQUARE_SFX + SFX_PLAYING_OFS)    /* DFE1 */
#define square_sfx_counter   WRAM(SQUARE_SFX + SFX_STATE_OFS)      /* DFE4 */
#define square_sfx_sweep     WRAM(SQUARE_SFX + SFX_STATE3_OFS)     /* DFE6 */
#define music_request        WRAM(SOUND_MUSIC_REQUEST)             /* DFE8 */
#define music_playing        WRAM(SOUND_MUSIC_PLAYING)             /* DFE9 */
#define wave_sfx_request     WRAM(WAVE_SFX + SFX_REQUEST_OFS)      /* DFF0 */
#define wave_sfx_playing     WRAM(WAVE_SFX + SFX_PLAYING_OFS)      /* DFF1 */
#define wave_sfx_counter     WRAM(WAVE_SFX + SFX_STATE_OFS)        /* DFF4 */
#define wave_sfx_pitch       WRAM(WAVE_SFX + SFX_STATE2_OFS)       /* DFF5 */
#define noise_sfx_request    WRAM(NOISE_SFX + SFX_REQUEST_OFS)     /* DFF8 */
#define noise_sfx_playing    WRAM(NOISE_SFX + SFX_PLAYING_OFS)     /* DFF9 */
#define noise_sfx_counter    WRAM(NOISE_SFX + SFX_STATE_OFS)       /* DFFC */

/* The lock byte of each music channel: bit 7 is set while a sound effect
   owns the channel (the music then plays silently on). The byte is the
   last one of the channel's 16-byte block, see below. */
enum {
    LOCK_SQUARE1 = 0xDF1F, LOCK_SQUARE2 = 0xDF2F, LOCK_WAVE = 0xDF3F, LOCK_NOISE = 0xDF4F,
};

/* Music state: a header at DF00 and four channel blocks of 16 bytes at
   DF10 (square 1), DF20 (square 2), DF30 (wave), DF40 (noise). */
enum {
    MUSIC_HEADER           = 0xDF00,
    MUSIC_NOTE_LENGTHS_PTR = 0xDF01,    /* pointer to the song's note length table */
    MUSIC_CHANNELS         = 0xDF00,    /* channel n lives at MUSIC_CHANNELS + 16 * n */
    /* offsets in a channel block */
    CH_PATTERN_LIST   = 0x0,    /* pointer to the current entry of the channel's pattern list */
    CH_TICKS_LEFT     = 0x2,    /* ticks until the next command is read */
    CH_NOTE_LENGTH    = 0x3,    /* length of the current note in ticks */
    CH_STREAM         = 0x4,    /* pointer to the next command in the pattern */
    CH_PARAM_LO       = 0x6,    /* square/noise: volume envelope (NRx2). wave: wave pattern pointer */
    CH_PARAM_HI       = 0x7,    /*   square/noise: if nonzero, NRx2 keeps its previous register value (bug) */
    CH_DUTY           = 0x8,    /* square/noise: duty and length (NRx1). wave: output level (NR32) */
    CH_FREQ           = 0x9,    /* frequency (two bytes) */
    CH_REST           = 0xB,    /* nonzero while the note is a rest */
    CH_VIBRATO_STEP   = 0xE,    /* counts ticks since the note started (pitch wobble position) */
    CH_LOCK           = 0xF,    /* bit 7: owned by a sound effect; bit 0: cleared at each note start */
    MUSIC_WAVE_LEVEL  = 0xDF38, /* wave channel's output level byte (CH_DUTY of channel 3) */
    MUSIC_WAVE_TICKS  = 0xDF32,
};
#define music_channel_base(n) ((uint16_t)(MUSIC_CHANNELS + 0x10 * (n)))

/* ---- High RAM ------------------------------------------------------- */
#define snd_current_channel  HRAM(0xFFD0)   /* hCurrentChannel: music channel being processed (1-4) */
#define snd_sound_number     HRAM(0xFFD1)   /* sound effect number being started (also scratch in vibrato) */
#define snd_one_up_pending   HRAM(SOUND_ONE_UP_PENDING)
#define snd_pan_timer        HRAM(0xFFD5)   /* hPanTimer */
#define snd_pan_interval     HRAM(0xFFD6)   /* hPanInterval: ticks between channel mask swaps */
#define snd_pan_counter      HRAM(0xFFD7)   /* hPanCounter: only its lowest bit matters */
#define snd_stereo_mode      HRAM(0xFFD8)   /* hMonoOrStereo: 1 = mono, 2 = stereo */
#define snd_pan_mask_a       HRAM(0xFFD9)   /* hChannelEnableMask1: NR51 value for even phases */
#define snd_pan_mask_b       HRAM(0xFFDA)   /* hChannelEnableMask2: NR51 value for odd phases */
#define snd_pause_timer      HRAM(0xFFDE)   /* hPauseTuneTimer */
#define snd_pause_request    HRAM(SOUND_PAUSE_REQUEST)  /* hPauseUnpauseMusic */

/* ---- ROM bank 3 data ------------------------------------------------ */
enum {
    ROM_START_SFX_TABLE    = 0x6600,    /* routines starting square effects 1-11 */
    ROM_CONTINUE_SFX_TABLE = 0x6616,    /* routines continuing them */
    ROM_START_NOISE_TABLE  = 0x662C,    /* noise effects 1-4 */
    ROM_CONTINUE_NOISE_TABLE = 0x6634,
    ROM_SONG_TABLE         = 0x663C,    /* pointers to the 19 songs */

    ROM_PAUSE_NOTE_1       = 0x66EE,    /* NR21-NR24 of the two pause beeps */
    ROM_PAUSE_NOTE_2       = 0x66F2,
    ROM_BOSS_CRY_CHANNEL   = 0x6705,    /* NR30-NR34 */
    ROM_BOSS_CRY_WAVE      = 0x6F4B,

    ROM_TIMER_TICK_DATA    = 0x6769,    /* NR10-NR14 of the square effects */
    ROM_GIRA_DATA          = 0x6776,
    ROM_JUMP_DATA          = 0x6787,
    ROM_SUPERBALL_DATA     = 0x678C,
    ROM_COIN_DATA_1        = 0x6809,
    ROM_COIN_DATA_2        = 0x680E,
    ROM_STOMP_DATA_1       = 0x6833,
    ROM_STOMP_DATA_2       = 0x6838,
    ROM_FLOWER_DATA        = 0x6863,
    ROM_GROW_DATA          = 0x6875,
    ROM_BUMP_DATA          = 0x68A0,
    ROM_INJURY_DATA        = 0x68B1,
    ROM_INJURY_ENVELOPES   = 0x68B6,    /* NR12 values, 0-terminated */
    ROM_ONE_UP_NOTES       = 0x68F8,    /* six notes of 5 bytes */

    ROM_EXPLOSION_DATA     = 0x6953,    /* NR41-NR44 of the noise effects */
    ROM_DEATH_CRY_DATA     = 0x6966,
    ROM_DEATH_CRY_SHAPES   = 0x696A,    /* NR43 values, 0-terminated */
    ROM_FIRE_BREATH_DATA   = 0x6993,
    ROM_BRICK_SHATTER_DATA = 0x699F,

    ROM_STEREO_DATA        = 0x6B2F,    /* 4 bytes per song */
    ROM_VIBRATO_TABLE      = 0x6E3D,
    ROM_NOTE_PITCHES       = 0x6E74,
    ROM_NOISE_INSTRUMENTS  = 0x6F06,    /* 5 bytes per drum */
};

/* One byte of whatever the original's pointer reads: bank 3 data normally.
   The pointers come from the RAM state, so (in a corrupt state) they could
   point anywhere; I/O registers are read as the original would. */
static inline uint8_t sound_read(uint16_t addr)
{
    if (addr < 0x8000) return rom_byte(3, addr);
    if ((addr >= 0xFF00 && addr < 0xFF80) || addr == 0xFFFF) return io_read(addr);
    return mem_read(addr);
}
static inline uint16_t sound_read16(uint16_t addr)
{
    return (uint16_t)(sound_read(addr) | sound_read((uint16_t)(addr + 1)) << 8);
}
static inline uint16_t wram_read16(uint16_t addr)
{
    return (uint16_t)(WRAM(addr) | WRAM(addr + 1) << 8);
}
static inline void wram_write16(uint16_t addr, uint16_t value)
{
    WRAM(addr) = (uint8_t)value;
    WRAM(addr + 1) = (uint8_t)(value >> 8);
}

#endif
