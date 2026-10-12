/*
 * The sound engine's main entry points: the timer interrupt tick (with the
 * pause beeps), initialisation, channel register setup and stereo panning.
 * The sound effects are in sound_sfx.c and the music player in sound_music.c;
 * docs/SOUND_ENGINE.md describes how it all fits together.
 */
#include "game.h"
#include "sound_private.h"

/*
 * One tick of the sound engine (03:7FF0, called by the timer interrupt
 * handler at 00:0050; the code is at 03:6662). While the game is paused only
 * two short beeps are produced; otherwise every sound channel is advanced by
 * one tick. At the end of the tick all requests are discarded: a request is
 * only seen by the tick that follows it.
 */
void sound_timer_tick(void)
{
    /* The original starts by writing 3 to $00FF "to enable cartridge RAM",
       which does nothing on this cartridge (it has no RAM, and 3 is not the
       enable value $0A), and ends by writing 7. A bug in the original, kept
       because the write goes to the memory bank controller. */
    bus_write(0x00FF, 3);

    bool play = true;
    uint8_t pause_request = snd_pause_request;
    if (taken(pause_request == SOUND_PAUSE)) {
        /* The game was paused: silence everything, forget the effects and
           start the beep sequence (timer counts down from $30). */
        mute_channels();
        square_sfx_playing = 0;
        wave_sfx_playing = 0;
        noise_sfx_playing = 0;
        snd_pause_timer = 0x30;
        setup_channel_square2(ROM_PAUSE_NOTE_1);
        play = false;
    } else if (taken(pause_request == SOUND_UNPAUSE)) {
        snd_pause_timer = 0;
    } else if (taken(snd_pause_timer != 0)) {
        /* Paused: the pause timer runs down and plays the second beep
           (C6 then C7) at $28, the first at $20, the second at $18. At $10
           it is held (decrement and increment again) until unpaused. */
        uint8_t timer = --snd_pause_timer;
        if (taken(timer == 0x28))
            setup_channel_square2(ROM_PAUSE_NOTE_2);
        else if (taken(timer == 0x20))
            setup_channel_square2(ROM_PAUSE_NOTE_1);
        else if (taken(timer == 0x18))
            setup_channel_square2(ROM_PAUSE_NOTE_2);
        else if (!taken(timer != 0x10))
            snd_pause_timer++;
        play = false;
    } else {
        /* Extra life (set together with the request by the game in vblank
           code): re-post the 1-UP jingle here, overriding whatever other
           effect was requested in the meantime. */
        uint8_t one_up = snd_one_up_pending;
        if (!taken(one_up == 0)) {
            snd_one_up_pending = 0;
            square_sfx_request = SFX_ONE_UP;
        }
    }

    if (play) {
        play_square_sfx();
        play_noise_sfx();
        play_wave_sfx();
        start_music();
        play_music();
        pan_stereo();
    }

    /* requests are consumed */
    square_sfx_request = 0;
    music_request = 0;
    wave_sfx_request = 0;
    noise_sfx_request = 0;
    snd_pause_request = 0;
    bus_write(0x00FF, 7);
}

/*
 * Reset the sound engine (03:7FF3, called by the main program at start-up
 * and when a level or screen starts: from outside the interrupt, so the
 * shared variables are accessed with shared_write). Nothing is playing
 * afterwards, all channels are free and routed to both speakers.
 */
void init_sound(void)
{
    shared_write(SOUND_SFX_PLAYING, 0);
    shared_write(SOUND_MUSIC_PLAYING, 0);
    shared_write(SOUND_WAVE_PLAYING, 0);
    shared_write(SOUND_NOISE_PLAYING, 0);
    shared_write(LOCK_SQUARE1, 0);
    shared_write(LOCK_SQUARE2, 0);
    shared_write(LOCK_WAVE, 0);
    shared_write(LOCK_NOISE, 0);
    shared_write(SOUND_PAUSE_REQUEST, 0);
    shared_write(0xFFDE, 0);                /* pause beep timer */
    io_write(rNR51, 0xFF);                  /* all channels to both outputs */
    mute_channels();
}

/*
 * Silence the four channels (03:6A54): volume 0 with the envelope in "add"
 * mode and a retrigger, sweep off, wave DAC off.
 */
void mute_channels(void)
{
    io_write(rNR12, 0x08);
    io_write(rNR22, 0x08);
    io_write(rNR42, 0x08);
    io_write(rNR14, 0x80);
    io_write(rNR24, 0x80);
    io_write(rNR44, 0x80);
    io_write(rNR10, 0x00);                  /* no sweep */
    io_write(rNR30, 0x00);                  /* wave channel off */
}

/* Copy `count` bytes from ROM to the sound registers starting at `reg`. */
static void copy_to_channel(uint16_t reg, uint16_t data, int count)
{
    do {
        io_write(reg++, sound_read(data++));
    } while (taken(--count != 0));
}

/* Load a square 1 note or effect: NR10-NR14 from 5 bytes at `data` (03:69E5). */
void setup_channel_square1(uint16_t data) { copy_to_channel(rNR10, data, 5); }

/* Square 2: NR21-NR24 from 4 bytes (03:69EC). */
void setup_channel_square2(uint16_t data) { copy_to_channel(rNR21, data, 4); }

/* Wave: NR30-NR34 from 5 bytes (03:69F3). */
void setup_channel_wave(uint16_t data) { copy_to_channel(rNR30, data, 5); }

/* Noise: NR41-NR44 from 4 bytes (03:69FA). */
void setup_channel_noise(uint16_t data) { copy_to_channel(rNR41, data, 4); }

/* Copy a 16-byte wave pattern to wave RAM, FF30-FF3F (03:6A26). */
void setup_wave_pattern(uint16_t data)
{
    uint8_t reg = 0x30;
    do {
        io_write(0xFF00 + reg, sound_read(data++));
        reg++;
    } while (taken(reg != 0x40));
}

/*
 * Find the routine for a sound number in a table of 16-bit pointers
 * (03:6A0A): entry number-1. Returns the pointer (the original jumps to it).
 */
uint16_t lookup_sound_pointer(uint8_t number, uint16_t table)
{
    uint8_t offset = (uint8_t)((uint8_t)(number - 1) << 1);   /* 8-bit: sla a */
    uint16_t pointer = sound_read16((uint16_t)(table + offset));
    reg_b((uint8_t)(pointer >> 8));
    return pointer;
}

/* The same, remembering the number of the sound being started (03:6A07). */
uint16_t lookup_new_sound_pointer(uint8_t number, uint16_t table)
{
    snd_sound_number = number;
    return lookup_sound_pointer(number, table);
}

/*
 * Advance an effect by one tick (03:6A19). position_addr is the effect's
 * position byte, followed by its duration byte. Returns the new position, or
 * 0 when the position has reached the duration: the step is over (and the
 * position starts again from 0).
 */
uint8_t update_sound_progress(uint16_t position_addr)
{
    uint8_t position = (uint8_t)(WRAM(position_addr) + 1);
    WRAM(position_addr) = position;
    if (taken(position != WRAM(position_addr + 1)))
        return position;
    WRAM(position_addr) = 0;
    return 0;
}

/*
 * While the explosion noise plays, flip the noise channel between the left
 * and the right speaker every two ticks (03:6AF6). The original is passed
 * the address of the pan timer (always FFD5) in HL, which it reads as the
 * tick counter. Only stereo songs get here (see pan_stereo).
 */
void pan_explosion(uint16_t timer_addr)
{
    if (taken(noise_sfx_playing != NOISE_EXPLOSION))
        return;
    uint8_t timer = ram_read(timer_addr);
    uint8_t mask = 0xF7;                    /* noise only on the left... */
    if (!taken((timer & 2) == 0))
        mask = 0x7F;                        /* ...or only on the right */
    io_write(rNR51, mask);
}

/*
 * Stereo panning of the music (03:6B09). Stereo songs swap the channel
 * routing in NR51 between two masks every `interval` ticks, which makes the
 * sound move between the speakers; mono songs leave NR51 alone. The song's
 * parameters come from the table at 03:6B2F (loaded by start_music).
 * Quirks of the original: the mono check also skips the explosion panning;
 * only bit 0 of the swap counter is used.
 */
void pan_stereo(void)
{
    if (taken(music_playing == 0))
        return;
    if (taken(snd_stereo_mode == 1))
        return;
    pan_explosion(0xFFD5);
    uint8_t timer = ++snd_pan_timer;
    if (taken(timer != snd_pan_interval))
        return;
    snd_pan_timer = 0;
    snd_pan_counter++;
    uint8_t mask = snd_pan_mask_a;
    if (!taken((snd_pan_counter & 1) == 0))
        mask = snd_pan_mask_b;
    io_write(rNR51, mask);
}
