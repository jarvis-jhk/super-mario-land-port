/*
 * Requests to the sound engine (it runs from the timer interrupt, see
 * docs/SOUND_ENGINE.md).
 *
 * The game asks for a sound by writing its number into one of four mailbox
 * bytes; the engine picks the request up at its next tick (64 times per
 * second at normal tempo) and clears all mailboxes at the end of the tick.
 */
#ifndef SML_SOUND_H
#define SML_SOUND_H

/* Mailbox addresses: the game writes a sound number, the engine picks it up
   at its next tick (64 times per second). */
enum {
    SOUND_SFX_REQUEST   = 0xDFE0,   /* square-wave sound effects */
    SOUND_MUSIC_REQUEST = 0xDFE8,
    SOUND_WAVE_REQUEST  = 0xDFF0,   /* wave channel effects (boss cry) */
    SOUND_NOISE_REQUEST = 0xDFF8,   /* noise effects (explosions, bricks) */
};

/* Status bytes the game looks at ("what is playing now"; 0 = nothing).
   The byte after each mailbox is the number of the sound currently playing. */
enum {
    SOUND_SFX_PLAYING   = 0xDFE1,
    SOUND_MUSIC_PLAYING = 0xDFE9,   /* the game waits for this to become 0 at the end of jingles */
    SOUND_WAVE_PLAYING  = 0xDFF1,
    SOUND_NOISE_PLAYING = 0xDFF9,
};

/* High RAM bytes shared between the game and the engine */
enum {
    SOUND_ONE_UP_PENDING = 0xFFD3,  /* the game sets it (together with the 1-UP request); the engine
                                       re-posts the 1-UP jingle at its next tick, overriding other effects */
    SOUND_PAUSE_REQUEST  = 0xFFDF,  /* the game writes SOUND_PAUSE / SOUND_UNPAUSE */
};
enum { SOUND_PAUSE = 1, SOUND_UNPAUSE = 2 };

/* Square-wave effects (SOUND_SFX_REQUEST) */
enum {
    SFX_JUMP        = 0x01,
    SFX_SUPERBALL   = 0x02,     /* Superball Mario's ball */
    SFX_STOMP       = 0x03,
    SFX_GROW        = 0x04,     /* mushroom */
    SFX_COIN        = 0x05,
    SFX_INJURY      = 0x06,     /* Mario gets hurt / shrinks */
    SFX_BUMP        = 0x07,     /* head bump */
    SFX_ONE_UP      = 0x08,
    SFX_GIRA        = 0x09,
    SFX_TIMER_TICK  = 0x0A,     /* the time bonus counting down */
    SFX_FLOWER      = 0x0B,
};

/* Noise effects (SOUND_NOISE_REQUEST) */
enum {
    NOISE_EXPLOSION     = 0x01,
    NOISE_BRICK_SHATTER = 0x02,
    NOISE_DEATH_CRY     = 0x03,
    NOISE_FIRE_BREATH   = 0x04,
};

/* Wave channel effects (SOUND_WAVE_REQUEST) */
enum { WAVE_BOSS_CRY = 0x01, WAVE_BOSS_HIT = WAVE_BOSS_CRY /* the cry is played when a boss is hit */ };

/* Songs (SOUND_MUSIC_REQUEST) */
enum {
    MUS_LEVEL_COMPLETE  = 0x01,
    MUS_DEATH           = 0x02,
    MUS_EASTON          = 0x03,     /* level 1-1 style overworld */
    MUS_UNDERGROUND     = 0x04,
    MUS_AUTOSCROLL      = 0x05,
    MUS_CHAI            = 0x06,
    MUS_BIRABUTO        = 0x07,
    MUS_MUDA            = 0x08,
    MUS_BONUS_GAME      = 0x09,
    MUS_BONUS_GAME_WALK = 0x0A,
    MUS_BOSS            = 0x0B,
    MUS_STAR            = 0x0C,
    MUS_BONUS_GAME_WIN  = 0x0D,
    MUS_BONUS_GAME_LOSE = 0x0E,
    MUS_OH_DAISY        = 0x0F,
    MUS_GAME_OVER       = 0x10,
    MUS_VICTORY         = 0x11,
    MUS_FAKE_DAISY      = 0x12,
    MUS_FINAL_BOSS      = 0x13,
};

#endif
