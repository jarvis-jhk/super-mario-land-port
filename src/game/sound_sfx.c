/*
 * Sound effects: the square-wave effects (jump, coin, stomp, ...), the noise
 * effects (explosion, bricks, ...) and the boss cry on the wave channel.
 *
 * Every effect has a "start" routine (called once, on the tick after the
 * request) that writes the channel registers, and usually a "continue"
 * routine (called on each following tick) that steps through the effect
 * until it stops itself. The data (register values, envelopes) is read from
 * ROM bank 3. A step of an effect lasts `duration` ticks.
 */
#include "game.h"
#include "sound_private.h"

/* ---- common parts ---- */

/*
 * Begin an effect (03:69C6). position_addr is the effect block's position
 * byte (DFE2, DFFA). Records the number of the effect (left in
 * snd_sound_number by the table lookup), its step duration, clears the
 * position and the two state bytes, and loads the first register values
 * from ROM. The channel is told apart by the address.
 */
void start_sfx(uint16_t position_addr, uint8_t duration, uint16_t channel_data)
{
    WRAM(position_addr - 1) = snd_sound_number;
    WRAM(position_addr + 1) = duration;
    WRAM(position_addr) = 0;
    WRAM(position_addr + 2) = 0;
    WRAM(position_addr + 3) = 0;
    uint8_t end = (uint8_t)(position_addr + 3);
    if (taken(end == 0xE5))
        setup_channel_square1(channel_data);
    else if (taken(end == 0xF5))
        setup_channel_wave(channel_data);
    else if (taken(end == 0xFD))
        setup_channel_noise(channel_data);
}

/* Start a square effect: position byte DFE2. */
static void start_square(uint8_t duration, uint16_t data)
{
    start_sfx(SQUARE_SFX + SFX_POSITION_OFS, duration, data);
}

/* Start a noise effect: position byte DFFA. */
static void start_noise(uint8_t duration, uint16_t data)
{
    start_sfx(NOISE_SFX + SFX_POSITION_OFS, duration, data);
}

/*
 * Square effects have priorities: a new effect does not start while a more
 * important one is playing. The checks (03:6791-03:67B2) run down a chain of
 * effect numbers; each function returns true (the original's Z flag) if one
 * of the effects from its entry point on is playing.
 */
static bool grow_or_higher_playing(uint8_t playing)
{
    if (taken(playing == SFX_GROW)) return true;
    if (taken(playing == SFX_INJURY)) return true;
    if (taken(playing == SFX_ONE_UP)) return true;
    return taken(playing == SFX_FLOWER);
}

/* Coin: blocked by mushroom, injury, 1-UP and flower (03:6791). */
bool sfx_outranked_by_grow(void)
{
    return grow_or_higher_playing(square_sfx_playing);
}

/* Stomp, bump, gira: additionally blocked by the coin sound (03:679C). */
bool sfx_outranked_by_coin(void)
{
    if (taken(square_sfx_playing == SFX_COIN))
        return true;
    return grow_or_higher_playing(square_sfx_playing);
}

/* Jump, superball: additionally blocked by the stomp sound (03:6796). */
bool sfx_outranked_by_stomp(void)
{
    if (taken(square_sfx_playing == SFX_STOMP))
        return true;
    return sfx_outranked_by_coin();
}

/* Death cry and brick shatter do not interrupt an explosion (03:695F). */
bool sfx_outranked_by_explosion(void)
{
    return taken(noise_sfx_playing == NOISE_EXPLOSION);
}

/* ---- square effects, stopping ---- */

/*
 * Silence square 1 and hand it back to the music (03:67F5): clears the
 * playing effect, sets volume 0 with a retrigger and removes the lock.
 */
void stop_square_sfx(void)
{
    square_sfx_playing = 0;
    io_write(rNR10, 0x00);
    io_write(rNR12, 0x08);
    io_write(rNR14, 0x80);
    WRAM(LOCK_SQUARE1) &= 0x7F;
}

/* Effects that only play one note: stop when the duration is up (03:67F0). */
void continue_square_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) != 0))
        return;
    stop_square_sfx();
}

/* ---- square effects ---- */

/* The time bonus ticking down (03:676E): a short click. */
void start_timer_tick_sfx(void)
{
    start_square(0x03, ROM_TIMER_TICK_DATA);
}

/* Gira (03:677B), a C#3 blip. Rarely used. */
void start_gira_sfx(void)
{
    if (taken(sfx_outranked_by_coin()))
        return;
    start_square(0x0E, ROM_GIRA_DATA);
}

/*
 * Jump (03:67AF). The pitch rises during the sound: the current frequency
 * is kept in the effect's state bytes (DFE4/DFE5).
 */
void start_jump_sfx(void)
{
    if (taken(sfx_outranked_by_stomp()))
        return;
    start_square(0x10, ROM_JUMP_DATA);
    wram_write16(SQUARE_SFX + SFX_STATE_OFS, 0x860A);   /* NR13/NR14 of the first note */
}

/* Jump, each tick: raise the frequency by 15 (03:67C4). Stops after 16 ticks. */
void continue_jump_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) == 0)) {
        stop_square_sfx();
        return;
    }
    uint16_t freq = (uint16_t)(wram_read16(SQUARE_SFX + SFX_STATE_OFS) + 0x000F);
    io_write(rNR13, (uint8_t)freq);
    reg_b((uint8_t)freq);
    io_write(rNR14, (freq >> 8) & 0x3F);        /* no retrigger */
    WRAM(SQUARE_SFX + SFX_STATE2_OFS) = (freq >> 8) & 0x3F;
    WRAM(SQUARE_SFX + SFX_STATE_OFS) = (uint8_t)freq;
}

/* Superball (03:67E4): one note, C5. */
void start_superball_sfx(void)
{
    if (taken(sfx_outranked_by_stomp()))
        return;
    start_square(0x03, ROM_SUPERBALL_DATA);
}

/*
 * Coin (03:6813). Its duration byte is whatever effect was playing before
 * (the original leaves that number in the register it then uses as duration
 * for the shared start code): harmless, the coin sound does not use it.
 */
void start_coin_sfx(void)
{
    uint8_t before = square_sfx_playing;
    if (taken(sfx_outranked_by_grow()))
        return;
    start_square(before, ROM_COIN_DATA_1);
}

/*
 * Coin, each tick (03:681D): after 3 ticks switch to the higher second note
 * (a B-E "ding"), stop after 23.
 */
void continue_coin_sfx(void)
{
    uint8_t ticks = ++square_sfx_counter;
    if (taken(ticks == 0x04)) {
        setup_channel_square1(ROM_COIN_DATA_2);
        return;
    }
    if (taken(ticks == 0x18))
        stop_square_sfx();
}

/* Stomping an enemy (03:683D): two close notes. */
void start_stomp_sfx(void)
{
    if (taken(sfx_outranked_by_coin()))
        return;
    start_square(0x08, ROM_STOMP_DATA_1);
}

/* Stomp, when a step is over (03:6849): second note, then stop. */
void continue_stomp_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) != 0))
        return;
    uint8_t step = square_sfx_counter++;
    if (taken(step == 0)) {
        setup_channel_square1(ROM_STOMP_DATA_2);
        return;
    }
    if (taken(step == 1))
        stop_square_sfx();
}

/* Fire flower (03:6868): a rising sweep that starts at pitch $60 (the sweep
   value lives in DFE6). */
void start_flower_sfx(void)
{
    square_sfx_sweep = 0x60;
    start_square(0x05, ROM_FLOWER_DATA);
}

/* Mushroom growing (03:687A): the same sweep, starting at $10. */
void start_grow_sfx(void)
{
    square_sfx_sweep = 0x10;
    start_square(0x05, ROM_GROW_DATA);
}

/*
 * Flower and mushroom, when a step is over (03:6887): the low frequency byte
 * rises by $10 and is written with a retrigger; stops when it reaches $E0.
 */
void continue_sweep_square_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) != 0))
        return;
    uint8_t freq = (uint8_t)(square_sfx_sweep + 0x10);
    square_sfx_sweep = freq;
    if (taken(freq == 0xE0)) {
        stop_square_sfx();
        return;
    }
    io_write(rNR13, freq);
    io_write(rNR14, 0x86);
}

/* Bumping a block from below (03:68A5). */
void start_bump_sfx(void)
{
    if (taken(sfx_outranked_by_coin()))
        return;
    start_square(0x08, ROM_BUMP_DATA);
}

/* Mario gets hurt (03:68C4): only the 1-UP jingle may not be interrupted. */
void start_injury_sfx(void)
{
    uint8_t playing = square_sfx_playing;
    if (taken(playing == SFX_ONE_UP))
        return;
    start_square(0x06, ROM_INJURY_DATA);
}

/* Injury, when a step is over (03:68D2): next volume of the fading envelope. */
void continue_injury_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) != 0))
        return;
    uint8_t index = square_sfx_counter++;
    uint8_t envelope = rom_byte(3, (uint16_t)(ROM_INJURY_ENVELOPES + index));
    if (taken(envelope == 0)) {
        stop_square_sfx();
        return;
    }
    io_write(rNR12, envelope);
    io_write(rNR14, 0x87);
}

/* 1-UP jingle (03:68F0): six notes, each 6 ticks long. */
void start_one_up_sfx(void)
{
    start_square(0x06, ROM_ONE_UP_NOTES);
}

/* 1-UP, when a note is over (03:6916): play the next one. */
void continue_one_up_sfx(void)
{
    if (taken(update_sound_progress(SQUARE_SFX + SFX_POSITION_OFS) != 0))
        return;
    uint8_t note = (uint8_t)(square_sfx_counter + 1);
    square_sfx_counter = note;
    if (taken(note == 1) || taken(note == 2) || taken(note == 3) ||
        taken(note == 4) || taken(note == 5)) {
        setup_channel_square1((uint16_t)(ROM_ONE_UP_NOTES + 5 * note));
        return;
    }
    stop_square_sfx();
}

/* ---- noise effects ---- */

/*
 * Silence the noise channel and release it (03:69B4): the original sets
 * volume 0 and triggers.
 */
void stop_noise_sfx(void)
{
    noise_sfx_playing = 0;
    io_write(rNR42, 0x08);
    io_write(rNR44, 0x80);
    WRAM(LOCK_NOISE) &= 0x7F;
}

/* Noise effects that play one sound (03:69AF). */
void continue_noise_sfx(void)
{
    if (taken(update_sound_progress(NOISE_SFX + SFX_POSITION_OFS) != 0))
        return;
    stop_noise_sfx();
}

/* Explosion (03:6957). */
void start_explosion_sfx(void)
{
    start_noise(0x30, ROM_EXPLOSION_DATA);
}

/* Enemy or Mario dies (03:6970): not while an explosion plays. */
void start_death_cry_sfx(void)
{
    if (taken(sfx_outranked_by_explosion()))
        return;
    start_noise(0x06, ROM_DEATH_CRY_DATA);
}

/*
 * Death cry, when a step is over (03:697C): the next noise shape (NR43)
 * from a list; the list ends with 0.
 */
void continue_death_cry_sfx(void)
{
    if (taken(update_sound_progress(NOISE_SFX + SFX_POSITION_OFS) != 0))
        return;
    uint8_t index = noise_sfx_counter++;
    uint8_t shape = rom_byte(3, (uint16_t)(ROM_DEATH_CRY_SHAPES + index));
    if (taken(shape == 0)) {
        stop_noise_sfx();
        return;
    }
    io_write(rNR43, shape);
}

/* Fire breath of the boss (03:6997). */
void start_fire_breath_sfx(void)
{
    start_noise(0x16, ROM_FIRE_BREATH_DATA);
}

/* Bricks breaking (03:69A3): not while an explosion plays. */
void start_brick_shatter_sfx(void)
{
    if (taken(sfx_outranked_by_explosion()))
        return;
    start_noise(0x15, ROM_BRICK_SHATTER_DATA);
}

/* ---- dispatch ---- */

/*
 * Run the routine found in one of the tables at 03:6600 (start) / 03:6616
 * (continue) for the square effects and 03:662C / 03:6634 for the noise
 * effects. The original jumps to the pointer with `jp hl`; the jump is
 * reported with computed_jump() and the routines are called by address
 * here. Only the addresses a valid sound number can lead to are handled:
 * the lookup has no range check on the "continue" side, but the number
 * playing was always set from a checked request.
 */
enum {
    ENTRY_START_JUMP = 0x67AF,      ENTRY_CONTINUE_JUMP = 0x67C4,
    ENTRY_START_SUPERBALL = 0x67E4, ENTRY_CONTINUE_SQUARE = 0x67F0,
    ENTRY_START_COIN = 0x6813,      ENTRY_CONTINUE_COIN = 0x681D,
    ENTRY_START_STOMP = 0x683D,     ENTRY_CONTINUE_STOMP = 0x6849,
    ENTRY_START_FLOWER = 0x6868,    ENTRY_START_GROW = 0x687A,
    ENTRY_CONTINUE_SWEEP = 0x6887,  ENTRY_START_BUMP = 0x68A5,
    ENTRY_START_INJURY = 0x68C4,    ENTRY_CONTINUE_INJURY = 0x68D2,
    ENTRY_START_ONE_UP = 0x68F0,    ENTRY_CONTINUE_ONE_UP = 0x6916,
    ENTRY_START_GIRA = 0x677B,      ENTRY_START_TIMER_TICK = 0x676E,
    ENTRY_START_EXPLOSION = 0x6957, ENTRY_START_BRICK_SHATTER = 0x69A3,
    ENTRY_START_DEATH_CRY = 0x6970, ENTRY_START_FIRE_BREATH = 0x6997,
    ENTRY_CONTINUE_NOISE = 0x69AF,  ENTRY_CONTINUE_DEATH_CRY = 0x697C,
};

static void run_sound_routine(uint16_t target)
{
    computed_jump(target);
    switch (target) {
    case ENTRY_START_JUMP:        start_jump_sfx(); break;
    case ENTRY_CONTINUE_JUMP:     continue_jump_sfx(); break;
    case ENTRY_START_SUPERBALL:   start_superball_sfx(); break;
    case ENTRY_CONTINUE_SQUARE:   continue_square_sfx(); break;
    case ENTRY_START_COIN:        start_coin_sfx(); break;
    case ENTRY_CONTINUE_COIN:     continue_coin_sfx(); break;
    case ENTRY_START_STOMP:       start_stomp_sfx(); break;
    case ENTRY_CONTINUE_STOMP:    continue_stomp_sfx(); break;
    case ENTRY_START_FLOWER:      start_flower_sfx(); break;
    case ENTRY_START_GROW:        start_grow_sfx(); break;
    case ENTRY_CONTINUE_SWEEP:    continue_sweep_square_sfx(); break;
    case ENTRY_START_BUMP:        start_bump_sfx(); break;
    case ENTRY_START_INJURY:      start_injury_sfx(); break;
    case ENTRY_CONTINUE_INJURY:   continue_injury_sfx(); break;
    case ENTRY_START_ONE_UP:      start_one_up_sfx(); break;
    case ENTRY_CONTINUE_ONE_UP:   continue_one_up_sfx(); break;
    case ENTRY_START_GIRA:        start_gira_sfx(); break;
    case ENTRY_START_TIMER_TICK:  start_timer_tick_sfx(); break;
    case ENTRY_START_EXPLOSION:   start_explosion_sfx(); break;
    case ENTRY_START_BRICK_SHATTER: start_brick_shatter_sfx(); break;
    case ENTRY_START_DEATH_CRY:   start_death_cry_sfx(); break;
    case ENTRY_START_FIRE_BREATH: start_fire_breath_sfx(); break;
    case ENTRY_CONTINUE_NOISE:    continue_noise_sfx(); break;
    case ENTRY_CONTINUE_DEATH_CRY: continue_death_cry_sfx(); break;
    default: break;     /* only reachable with a corrupt sound number */
    }
}

/*
 * Square effects, once per tick (03:6A6A). A new request (1-11) takes the
 * square 1 channel away from the music (lock bit) and starts that effect;
 * otherwise the effect playing is advanced.
 */
void play_square_sfx(void)
{
    uint8_t request = square_sfx_request;
    if (!taken(request == 0) && !taken(request >= 12)) {
        WRAM(LOCK_SQUARE1) |= 0x80;
        run_sound_routine(lookup_new_sound_pointer(request, ROM_START_SFX_TABLE));
        return;
    }
    uint8_t playing = square_sfx_playing;
    if (taken(playing == 0))
        return;
    run_sound_routine(lookup_sound_pointer(playing, ROM_CONTINUE_SFX_TABLE));
}

/* Noise effects, once per tick (03:6A8E); the same scheme with 4 effects. */
void play_noise_sfx(void)
{
    uint8_t request = noise_sfx_request;
    if (!taken(request == 0) && !taken(request >= 5)) {
        WRAM(LOCK_NOISE) |= 0x80;
        run_sound_routine(lookup_new_sound_pointer(request, ROM_START_NOISE_TABLE));
        return;
    }
    uint8_t playing = noise_sfx_playing;
    if (taken(playing == 0))
        return;
    run_sound_routine(lookup_sound_pointer(playing, ROM_CONTINUE_NOISE_TABLE));
}

/* ---- wave channel: the boss cry ---- */

/*
 * Wave channel effect, once per tick (03:66F6). The only one is the boss
 * cry: a ~195 Hz tone with a wavering pitch that rises for 13 ticks and
 * falls for 16 more. The wobble comes from the DIV register (see below).
 */
void play_wave_sfx(void)
{
    uint8_t request = wave_sfx_request;
    if (taken(request == WAVE_BOSS_CRY)) {
        /* start: the music loses the wave channel and gets a new wave RAM */
        wave_sfx_playing = request;
        WRAM(LOCK_WAVE) |= 0x80;
        wave_sfx_counter = 0;
        io_write(rNR30, 0x00);
        setup_wave_pattern(ROM_BOSS_CRY_WAVE);
        /* Supposedly random, but the engine is tied to the timer interrupt,
           so DIV has nearly the same value at each tick (original comment:
           "always ~D1?"). */
        uint8_t random = reg_b(io_read(rDIV) & 0x1F);
        wave_sfx_pitch = (uint8_t)(0xD0 + random);
        setup_channel_wave(ROM_BOSS_CRY_CHANNEL);
        return;
    }
    if (!taken(wave_sfx_playing == WAVE_BOSS_CRY))
        return;

    /* continue */
    uint8_t wobble = reg_b(io_read(rDIV) & 0x07);          /* 0-7, again nearly constant */
    uint8_t ticks = ++wave_sfx_counter;
    if (!taken(ticks >= 0x0E)) {
        wave_sfx_pitch = (uint8_t)(wave_sfx_pitch + 2);     /* rising */
    } else {
        if (taken(ticks == 0x1E)) {
            /* the cry is over: give the wave channel back to the music and
               restore the music's wave pattern */
            wave_sfx_playing = 0;
            io_write(rNR30, 0x00);
            WRAM(LOCK_WAVE) &= 0x7F;
            setup_wave_pattern(wram_read16(MUSIC_CHANNELS + 0x30 + CH_PARAM_LO));
            return;
        }
        wave_sfx_pitch = (uint8_t)(wave_sfx_pitch - 3);     /* falling */
    }
    /* write the low frequency byte: the pitch with its low 3 bits replaced
       by the random wobble */
    io_write(rNR33, (wave_sfx_pitch & 0xF8) | wobble);
}
