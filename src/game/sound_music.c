/*
 * The music player. A song is a small header in ROM (see docs/SOUND_ENGINE.md)
 * that points to four channel streams. Once per tick every channel counts
 * down its current note and, when the note is over, reads commands from its
 * stream until one starts a new note.
 *
 * Channel streams are lists of pointers to "patterns" (byte sequences):
 *   00         end of the pattern: continue with the next pointer of the list
 *              (pointer 0000 ends the song, FFFF restarts the channel from the
 *              pointer that follows)
 *   9D p q r   set the instrument: p, q, r go to CH_PARAM_LO, CH_PARAM_HI and
 *              CH_DUTY (square: envelope, 0, duty; wave: pattern pointer p+q*256
 *              and output level r; noise: unused)
 *   A0-AF      set the note length to entry (n & 15) of the song's length table;
 *              the note follows in the next byte
 *   01         a rest
 *   other      a note: the byte is the offset into the pitch table at 03:6E74
 *              (noise channel: offset of the drum in the table at 03:6F06)
 */
#include "game.h"
#include "sound_private.h"

/* ---- pointer helpers ---- */

/* Copy a 16-bit value from ROM to RAM (03:6B86). */
void copy_pointer(uint16_t src, uint16_t dst)
{
    WRAM(dst) = sound_read(src);
    WRAM(dst + 1) = sound_read((uint16_t)(src + 1));
}

/*
 * Copy the two bytes that the pointer stored at `src` points to, to `dst`
 * (03:6B7B). Returns the second byte (the original leaves it in A).
 */
uint8_t copy_pointer_indirect(uint16_t src, uint16_t dst)
{
    uint16_t target = wram_read16(src);
    reg_b((uint8_t)(target >> 8));
    WRAM(dst) = sound_read(target);
    target = inc_bc(target);
    WRAM(dst + 1) = sound_read(target);
    return WRAM(dst + 1);
}

/* Add 1 to the 16-bit pointer stored at addr (03:6C30). */
void increment_pointer(uint16_t addr)
{
    wram_write16(addr, (uint16_t)(wram_read16(addr) + 1));
}

/* Add 2 (03:6C3C). */
void increment_pointer_twice(uint16_t addr)
{
    wram_write16(addr, (uint16_t)(wram_read16(addr) + 2));
}

/* The byte that the pointer stored at addr points to (03:6C45). */
uint8_t load_from_pointer(uint16_t addr)
{
    uint16_t pointer = wram_read16(addr);
    reg_b((uint8_t)(pointer >> 8));
    return reg_b(sound_read(pointer));          /* left in A and B */
}

/* ---- starting a song ---- */

/*
 * Set up the player state for the song whose header is at `song` (03:6B8C).
 * Header: a byte (unused, copied to DF00), the pointer to the note length
 * table, and one pointer to the pattern list of each channel. The first
 * pattern of each channel is loaded, with a tick count of 1 so that it
 * starts at the next tick.
 */
void setup_song(uint16_t song)
{
    mute_channels();
    snd_pan_timer = 0;
    snd_pan_counter = 0;
    WRAM(MUSIC_HEADER) = sound_read(song);
    copy_pointer((uint16_t)(song + 1), MUSIC_NOTE_LENGTHS_PTR);
    for (int ch = 1; ch <= 4; ch++)
        copy_pointer((uint16_t)(song + 1 + 2 * ch), music_channel_base(ch) + CH_PATTERN_LIST);
    for (int ch = 1; ch <= 4; ch++)
        copy_pointer_indirect(music_channel_base(ch) + CH_PATTERN_LIST,
                              music_channel_base(ch) + CH_STREAM);
    int channels_left = 4;
    int ch = 1;
    do {
        WRAM(music_channel_base(ch) + CH_TICKS_LEFT) = 1;
        ch++;
    } while (taken(--channels_left != 0));
    WRAM(music_channel_base(1) + CH_VIBRATO_STEP) = 0;
    WRAM(music_channel_base(2) + CH_VIBRATO_STEP) = 0;
    WRAM(music_channel_base(3) + CH_VIBRATO_STEP) = 0;
}

/*
 * Start the song requested in the music mailbox, if any (03:6AB5). The
 * song numbers are 1-19. Also loads the panning parameters (four bytes per
 * song at 03:6B2F): mono/stereo, the swap interval and the two NR51 masks;
 * the first mask is applied at once.
 */
void start_music(void)
{
    uint8_t number = music_request;
    if (taken(number == 0))
        return;
    if (taken(number >= 0x14))
        return;
    music_playing = number;
    if (taken(number == 0xFF)) {        /* can never happen (excluded above) */
        init_sound();
        return;
    }
    reg_b(number);
    uint16_t song = lookup_sound_pointer(number & 0x1F, ROM_SONG_TABLE);
    setup_song(song);

    uint8_t entries = music_playing;
    uint16_t stereo = ROM_STEREO_DATA;
    while (!taken(--entries == 0))
        stereo += 4;
    snd_stereo_mode = sound_read(stereo);
    snd_pan_interval = sound_read((uint16_t)(stereo + 1));
    snd_pan_mask_a = sound_read((uint16_t)(stereo + 2));
    io_write(rNR51, snd_pan_mask_a);
    snd_pan_mask_b = sound_read((uint16_t)(stereo + 3));
    snd_pan_timer = 0;
    snd_pan_counter = 0;
}

/* ---- playing ---- */

/*
 * Pitch wobble for the note being held (the byte at `step` >> 1 of a table
 * at 03:6E3D holds two signed nibbles). 03:6DD3.
 */
uint8_t vibrato_offset_byte(uint8_t step)
{
    return rom_byte(3, (uint16_t)(ROM_VIBRATO_TABLE + (step >> 1)));
}

/*
 * Vibrato (03:6DDC) for the channel whose state block starts at `b`
 * (channel is its number, 1-3): while a note sounds, its frequency register is
 * rewritten every tick with the note's frequency plus a small signed offset
 * that follows a fixed pattern (CH_VIBRATO_STEP counts the ticks). Only the
 * channels that have a nonzero low nibble in CH_DUTY get it; the noise
 * channel never. The new frequency is written without the trigger bit.
 */
void apply_vibrato(uint16_t b, int channel)
{
    uint8_t depth = WRAM(b + CH_DUTY) & 0x0F;
    if (taken(depth == 0))
        return;
    snd_sound_number = depth;
    uint16_t reg;
    if (taken(channel == 1)) reg = rNR13;
    else if (taken(channel == 2)) reg = rNR23;
    else if (taken(channel == 3)) reg = rNR33;
    else return;

    uint16_t freq = wram_read16(b + CH_FREQ);
    uint8_t step = reg_b(WRAM(b + CH_VIBRATO_STEP));
    uint8_t pair = vibrato_offset_byte(step);
    if (!taken((step & 1) != 0))
        pair = (uint8_t)(pair << 4 | pair >> 4);        /* swap: even steps use the high nibble */
    uint8_t nibble = pair & 0x0F;
    uint16_t offset;
    if (!taken((nibble & 8) == 0))
        offset = (uint16_t)(0xFF00 | (nibble | 0xF0));  /* negative: sign extend */
    else
        offset = nibble;
    freq = (uint16_t)(freq + offset);
    io_write(reg, (uint8_t)freq);
    io_write(reg + 1, (uint8_t)(freq >> 8));
}

/* The command 9D: change the channel's instrument (03:6C00, 03:6BF4). */
static void set_instrument(int channel)
{
    uint16_t b = music_channel_base(channel);
    increment_pointer(b + CH_STREAM);
    uint8_t lo = load_from_pointer(b + CH_STREAM);
    increment_pointer(b + CH_STREAM);
    uint8_t hi = load_from_pointer(b + CH_STREAM);
    increment_pointer(b + CH_STREAM);
    uint8_t third = load_from_pointer(b + CH_STREAM);
    WRAM(b + CH_PARAM_LO) = lo;
    WRAM(b + CH_PARAM_HI) = hi;
    WRAM(b + CH_DUTY) = third;
    if (taken(channel == 3)) {
        io_write(rNR30, 0x00);                          /* DAC off while the wave RAM changes */
        setup_wave_pattern((uint16_t)(hi << 8 | lo));
    }
    increment_pointer(b + CH_STREAM);
}

/*
 * The end of a pattern (03:6C7F): go to the next pointer of the channel's
 * list. Pointer FFFF restarts the list from the address behind it; pointer
 * 0000 ends the song (returns false after silencing everything).
 */
static bool next_pattern(int channel)
{
    uint16_t b = music_channel_base(channel);
    increment_pointer_twice(b + CH_PATTERN_LIST);
    for (;;) {
        uint8_t high = copy_pointer_indirect(b + CH_PATTERN_LIST, b + CH_STREAM);
        if (taken(high == 0)) {
            music_playing = 0;
            io_write(rNR51, 0xFF);
            mute_channels();
            return false;
        }
        if (!taken(high == 0xFF))
            return true;
        /* FFFF: the pointer after it says where the list starts again */
        increment_pointer_twice(b + CH_PATTERN_LIST);
        uint8_t lo = load_from_pointer(b + CH_PATTERN_LIST);
        increment_pointer(b + CH_PATTERN_LIST);
        uint8_t hi = load_from_pointer(b + CH_PATTERN_LIST);
        WRAM(b + CH_PATTERN_LIST) = lo;
        WRAM(b + CH_PATTERN_LIST + 1) = hi;
    }
}

/* A note that is still sounding (03:6C4F). */
static void continue_note(int channel)
{
    uint16_t b = music_channel_base(channel);
    if (!taken(channel != 3)) {
        /* wave channel with a loud output level: 6 ticks before the note
           ends, set the level to 100% */
        if (!taken((WRAM(MUSIC_WAVE_LEVEL) & 0x80) == 0))
            if (!taken(WRAM(MUSIC_WAVE_TICKS) != 6))
                io_write(rNR32, 0x40);
    }
    if (taken(WRAM(b + CH_REST) != 0))
        return;
    if (taken(WRAM(b + CH_LOCK) & 0x80))
        return;
    apply_vibrato(b, channel);
}

/*
 * Run one channel for this tick. Returns false if the song ended and the
 * player must stop (the other channels are not served any more).
 */
static bool play_channel(int channel)
{
    uint16_t b = music_channel_base(channel);
    if (taken(WRAM(b + 1) == 0))                    /* channel not used by this song */
        return true;
    if (taken(--WRAM(b + CH_TICKS_LEFT) != 0)) {
        continue_note(channel);
        return true;
    }

    /* The note is over: read commands until a note comes */
    uint8_t command;
    for (;;) {
        command = load_from_pointer(b + CH_STREAM);
        if (taken(command == 0)) {
            if (!next_pattern(channel))
                return false;
            continue;
        }
        if (taken(command == 0x9D)) {
            set_instrument(channel);
            continue;
        }
        break;
    }
    if (!taken((command & 0xF0) != 0xA0)) {
        /* note length command; the note itself follows */
        uint16_t table = wram_read16(MUSIC_NOTE_LENGTHS_PTR);
        WRAM(b + CH_NOTE_LENGTH) = sound_read((uint16_t)(table + (command & 0x0F)));
        increment_pointer(b + CH_STREAM);
        command = load_from_pointer(b + CH_STREAM);
    }
    uint8_t note = command;
    increment_pointer(b + CH_STREAM);

    uint16_t reg;               /* first register of the channel that is written: NRx1 */
    uint8_t volume;             /* NRx2 (wave: NR32) */
    uint8_t duty;               /* NRx1 */
    /* The original keeps the low byte of the CH_FREQ address (+1 after a
       real note; $4B for the noise channel) in register E; it is used as the volume when CH_PARAM_HI
       is nonzero (a bug, the volume is then not loaded). */
    uint8_t stale_e;

    if (taken(channel == 4)) {
        /* noise: copy the drum's five bytes to CH_PARAM_LO.. */
        uint16_t src = (uint16_t)(ROM_NOISE_INSTRUMENTS + note);
        uint8_t dst = 0x46;
        do {
            WRAM(0xDF00 + dst) = sound_read(src++);
            dst++;
        } while (taken(dst != 0x4B));
        stale_e = 0x4B;
        reg = rNR41;
    } else {
        stale_e = (uint8_t)(b + CH_FREQ);
        if (taken(note == 1)) {
            WRAM(b + CH_REST) = 1;
        } else {
            stale_e++;                      /* the original stepped E to the high frequency byte */
            WRAM(b + CH_REST) = 0;
            wram_write16(b + CH_FREQ, sound_read16((uint16_t)(ROM_NOTE_PITCHES + note)));
        }
        if (taken(channel == 1)) {
            reg = rNR11;
        } else if (taken(channel == 2)) {
            reg = rNR21;
        } else {
            /* wave: restart the DAC unless the boss cry owns the channel */
            if (!taken(WRAM(LOCK_WAVE) & 0x80)) {
                io_write(rNR30, 0x00);
                io_write(rNR30, 0x80);
            }
            reg = rNR31;
        }
    }

    if (channel == 3) {
        volume = WRAM(b + CH_DUTY);         /* output level */
        duty = 0;                           /* NR31: length */
    } else {
        if (taken(WRAM(b + CH_PARAM_HI) != 0))
            volume = stale_e;
        else
            volume = WRAM(b + CH_PARAM_LO);
        duty = WRAM(b + CH_DUTY);
    }

    if (!taken(WRAM(b + CH_REST) == 0))
        volume = 0x08;                      /* a rest: volume 0 */
    WRAM(b + CH_VIBRATO_STEP) = 0;
    if (!taken(WRAM(b + CH_LOCK) & 0x80)) {
        /* the channel is free: play the note */
        io_write(reg, duty);
        io_write(reg + 1, volume);
        io_write(reg + 2, WRAM(b + CH_FREQ));
        io_write(reg + 3, WRAM(b + CH_FREQ + 1) | 0x80);    /* trigger */
        WRAM(b + CH_LOCK) &= 0xFE;
    }
    WRAM(b + CH_TICKS_LEFT) = WRAM(b + CH_NOTE_LENGTH);
    return true;
}

/*
 * Advance the music by one tick (03:6CBE). Each channel is processed in
 * turn; afterwards the vibrato step counters of the three tone channels
 * advance.
 */
void play_music(void)
{
    if (taken(music_playing == 0))
        return;
    snd_current_channel = 1;
    for (int channel = 1;; channel++) {
        if (!play_channel(channel))
            return;
        if (taken(channel == 4))
            break;
        snd_current_channel = (uint8_t)(channel + 1);
    }
    WRAM(music_channel_base(1) + CH_VIBRATO_STEP)++;
    WRAM(music_channel_base(2) + CH_VIBRATO_STEP)++;
    WRAM(music_channel_base(3) + CH_VIBRATO_STEP)++;
}
