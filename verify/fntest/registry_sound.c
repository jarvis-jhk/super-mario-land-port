/*
 * Sound engine functions under test (ROM bank 3, 6600-7FFF), with adapters
 * from the original's register interface to the C signatures.
 * Keep sorted by address. All run inside the timer interrupt except
 * InitSound (called by the main program).
 */
#include "fntest.h"
#include "../../src/game/game.h"
#include "../../src/game/ram.h"

#define HL(r) ((uint16_t)((r)->h << 8 | (r)->l))
#define DE(r) ((uint16_t)((r)->d << 8 | (r)->e))

static void set_hl(Sm83Regs *r, uint16_t v) { r->h = v >> 8; r->l = v & 0xFF; }
static void set_z(Sm83Regs *r, int z) { r->f = (uint8_t)((r->f & ~0x80) | (z ? 0x80 : 0)); }

static void t_timer_tick(Sm83Regs *r) { (void)r; sound_timer_tick(); }
static void t_play_wave_sfx(Sm83Regs *r) { (void)r; play_wave_sfx(); }
static void t_start_timer_tick(Sm83Regs *r) { (void)r; start_timer_tick_sfx(); }
static void t_start_gira(Sm83Regs *r) { (void)r; start_gira_sfx(); }
static void t_outranked_grow(Sm83Regs *r) { set_z(r, sfx_outranked_by_grow()); }
static void t_outranked_stomp(Sm83Regs *r) { set_z(r, sfx_outranked_by_stomp()); }
static void t_outranked_coin(Sm83Regs *r) { set_z(r, sfx_outranked_by_coin()); }
static void t_start_jump(Sm83Regs *r) { (void)r; start_jump_sfx(); }
static void t_continue_jump(Sm83Regs *r) { (void)r; continue_jump_sfx(); }
static void t_start_superball(Sm83Regs *r) { (void)r; start_superball_sfx(); }
static void t_continue_square(Sm83Regs *r) { (void)r; continue_square_sfx(); }
static void t_stop_square(Sm83Regs *r) { (void)r; stop_square_sfx(); }
static void t_start_coin(Sm83Regs *r) { (void)r; start_coin_sfx(); }
static void t_continue_coin(Sm83Regs *r) { (void)r; continue_coin_sfx(); }
static void t_start_stomp(Sm83Regs *r) { (void)r; start_stomp_sfx(); }
static void t_continue_stomp(Sm83Regs *r) { (void)r; continue_stomp_sfx(); }
static void t_start_flower(Sm83Regs *r) { (void)r; start_flower_sfx(); }
static void t_start_grow(Sm83Regs *r) { (void)r; start_grow_sfx(); }
static void t_continue_sweep(Sm83Regs *r) { (void)r; continue_sweep_square_sfx(); }
static void t_start_bump(Sm83Regs *r) { (void)r; start_bump_sfx(); }
static void t_start_injury(Sm83Regs *r) { (void)r; start_injury_sfx(); }
static void t_continue_injury(Sm83Regs *r) { (void)r; continue_injury_sfx(); }
static void t_start_one_up(Sm83Regs *r) { (void)r; start_one_up_sfx(); }
static void t_continue_one_up(Sm83Regs *r) { (void)r; continue_one_up_sfx(); }
static void t_start_explosion(Sm83Regs *r) { (void)r; start_explosion_sfx(); }
static void t_outranked_explosion(Sm83Regs *r) { set_z(r, sfx_outranked_by_explosion()); }
static void t_start_death_cry(Sm83Regs *r) { (void)r; start_death_cry_sfx(); }
static void t_continue_death_cry(Sm83Regs *r) { (void)r; continue_death_cry_sfx(); }
static void t_start_fire_breath(Sm83Regs *r) { (void)r; start_fire_breath_sfx(); }
static void t_start_brick_shatter(Sm83Regs *r) { (void)r; start_brick_shatter_sfx(); }
static void t_continue_noise(Sm83Regs *r) { (void)r; continue_noise_sfx(); }
static void t_stop_noise(Sm83Regs *r) { (void)r; stop_noise_sfx(); }
static void t_start_sfx(Sm83Regs *r) { start_sfx(DE(r), r->a, HL(r)); }
static void t_setup_square1(Sm83Regs *r) { setup_channel_square1(HL(r)); }
static void t_setup_square2(Sm83Regs *r) { setup_channel_square2(HL(r)); }
static void t_setup_wave(Sm83Regs *r) { setup_channel_wave(HL(r)); }
static void t_setup_noise(Sm83Regs *r) { setup_channel_noise(HL(r)); }
static void t_lookup_new(Sm83Regs *r)
{
    uint16_t p = lookup_new_sound_pointer(r->a, HL(r));
    set_hl(r, p);
    r->a = r->h;
    r->e = (uint8_t)(r->e + 2);
}
static void t_lookup(Sm83Regs *r)
{
    uint16_t p = lookup_sound_pointer(r->a, HL(r));
    set_hl(r, p);
    r->a = r->h;
    r->e = (uint8_t)(r->e + 1);
}
static void t_update_progress(Sm83Regs *r) { r->a = update_sound_progress(DE(r)); }
static void t_setup_wave_pattern(Sm83Regs *r) { setup_wave_pattern(HL(r)); }
static void t_init_sound(Sm83Regs *r) { (void)r; init_sound(); }
static void t_mute_channels(Sm83Regs *r) { (void)r; mute_channels(); }
static void t_play_square(Sm83Regs *r) { (void)r; play_square_sfx(); }
static void t_play_noise(Sm83Regs *r) { (void)r; play_noise_sfx(); }
static void t_start_music(Sm83Regs *r) { (void)r; start_music(); }
static void t_pan_explosion(Sm83Regs *r) { pan_explosion(HL(r)); }
static void t_pan_stereo(Sm83Regs *r) { (void)r; pan_stereo(); }
static void t_copy_pointer_indirect(Sm83Regs *r) { r->a = copy_pointer_indirect(HL(r), DE(r)); }
static void t_copy_pointer(Sm83Regs *r) { copy_pointer(HL(r), DE(r)); }
static void t_setup_song(Sm83Regs *r) { setup_song(HL(r)); }
static void t_increment_pointer(Sm83Regs *r) { increment_pointer(HL(r)); }
static void t_increment_pointer_twice(Sm83Regs *r) { increment_pointer_twice(HL(r)); }
static void t_load_from_pointer(Sm83Regs *r) { r->a = r->b = load_from_pointer(HL(r)); }
static void t_play_music(Sm83Regs *r) { (void)r; play_music(); }
static void t_vibrato_offset(Sm83Regs *r) { r->e = vibrato_offset_byte(r->b); }
static void t_apply_vibrato(Sm83Regs *r) { apply_vibrato(HL(r) & 0xFFF0, HRAM(0xFFD0)); }


/* Fuzzing may put the engine into states the game never produces: sound
   numbers past the end of their tables (the original then jumps through a
   data byte into nonsense) and music data pointers outside ROM (the game
   only ever points them at songs in bank 3). Those states are skipped. */
/* Word at a sound data address (bank 3 for 4000-7FFF); -1 outside ROM. */
static int sound_rom_word(const Machine *m, unsigned addr)
{
    if (addr + 1 >= 0x8000)
        return -1;
    const uint8_t *rom = m->rom + (addr < 0x4000 ? addr : 3 * 0x4000 + (addr - 0x4000));
    return rom[0] | rom[1] << 8;
}

/* The stream pointer the channel's next pattern switch would load (03:6C7F:
   the entry after the current one; FFFF loops to the address behind it)
   is in ROM too, or 0000 (the song ends). An active channel with a fuzzed
   pattern list pointer otherwise loads a "stream" from random data. */
static int next_pattern_in_rom(const Machine *m, uint16_t base)
{
    unsigned list = (unsigned)(m->wram[base - 0xC000] | m->wram[base + 1 - 0xC000] << 8);
    int w = sound_rom_word(m, list + 2);
    if (w == 0xFFFF) {
        int start = sound_rom_word(m, list + 4);
        w = start < 0 ? -1 : sound_rom_word(m, (unsigned)start);
    }
    return w >= 0 && w < 0x8000;
}

static int sound_state_valid(const Machine *m)
{
#define W(a) (m->wram[(a) - 0xC000])
    if (W(0xDFE0) > 0x0B || W(0xDFE1) > 0x0B) return 0;   /* square effects 1-11 */
    if (W(0xDFF8) > 0x04 || W(0xDFF9) > 0x04) return 0;   /* noise effects 1-4 */
    if (W(0xDFF0) > 0x01 || W(0xDFF1) > 0x01) return 0;   /* the boss cry */
    if (W(0xDFE8) > 0x13 && W(0xDFE8) != 0xFF) return 0;  /* songs 1-19 */
    if (m->hram[0xFFDF - 0xFF80] > 2) return 0;           /* pause request */
    if (W(0xDF02) >= 0x80) return 0;                      /* note length table pointer */
    for (int ch = 1; ch <= 4; ch++) {
        uint16_t base = (uint16_t)(0xDF00 + 0x10 * ch);
        if (W(base + 1) >= 0x80 || W(base + 5) >= 0x80)   /* pattern list / stream pointers */
            return 0;
        if (W(base + 1) != 0 && !next_pattern_in_rom(m, base))
            return 0;
    }
#undef W
    return 1;
}

const FnTest fn_tests_sound[] = {
    { "PlayWaveSFX",              3, 0x66F6, t_play_wave_sfx, 0, 1 },
    { "StartTimerTickSFX",        3, 0x676E, t_start_timer_tick, 0, 1 },
    { "StartGiraSFX",             3, 0x677B, t_start_gira, 0, 1 },
    { "Call_6791",                3, 0x6791, t_outranked_grow, OUT_ZF, 1 },
    { "Call_6796",                3, 0x6796, t_outranked_stomp, OUT_ZF, 1 },
    { "Call_679C",                3, 0x679C, t_outranked_coin, OUT_ZF, 1 },
    { "StartJumpSFX",             3, 0x67AF, t_start_jump, 0, 1 },
    { "ContinueJumpSFX",          3, 0x67C4, t_continue_jump, 0, 1 },
    { "StartSuperballSFX",        3, 0x67E4, t_start_superball, 0, 1 },
    { "ContinueSquareSFX",        3, 0x67F0, t_continue_square, 0, 1 },
    { "ContinueSquareSFX.stop",   3, 0x67F5, t_stop_square, 0, 1 },
    { "StartCoinSFX",             3, 0x6813, t_start_coin, 0, 1 },
    { "ContinueCoinSFX",          3, 0x681D, t_continue_coin, 0, 1 },
    { "StartStompSFX",            3, 0x683D, t_start_stomp, 0, 1 },
    { "ContinueStompSFX",         3, 0x6849, t_continue_stomp, 0, 1 },
    { "StartFlowerSFX",           3, 0x6868, t_start_flower, 0, 1 },
    { "StartGrowSFX",             3, 0x687A, t_start_grow, 0, 1 },
    { "ContinueSweepSquareSFX",   3, 0x6887, t_continue_sweep, 0, 1 },
    { "StartBumpSFX",             3, 0x68A5, t_start_bump, 0, 1 },
    { "StartInjurySFX",           3, 0x68C4, t_start_injury, 0, 1 },
    { "ContinueInjurySFX",        3, 0x68D2, t_continue_injury, 0, 1 },
    { "StartOneUpSFX",            3, 0x68F0, t_start_one_up, 0, 1 },
    { "ContinueOneUpSFX",         3, 0x6916, t_continue_one_up, 0, 1 },
    { "StartExplosionSFX",        3, 0x6957, t_start_explosion, 0, 1 },
    { "Call_695F",                3, 0x695F, t_outranked_explosion, OUT_ZF, 1 },
    { "StartDeathCrySFX",         3, 0x6970, t_start_death_cry, 0, 1 },
    { "ContinueDeathCrySFX",      3, 0x697C, t_continue_death_cry, 0, 1 },
    { "StartFireBreathSFX",       3, 0x6997, t_start_fire_breath, 0, 1 },
    { "StartBrickShatterSFX",     3, 0x69A3, t_start_brick_shatter, 0, 1 },
    { "ContinueNoiseSFX",         3, 0x69AF, t_continue_noise, 0, 1 },
    { "ContinueNoiseSFX.stop",    3, 0x69B4, t_stop_noise, 0, 1 },
    { "Jmp_69C6",                 3, 0x69C6, t_start_sfx, 0, 1 },
    { "SetupChannel.square1",     3, 0x69E5, t_setup_square1, 0, 1 },
    { "SetupChannel.square2",     3, 0x69EC, t_setup_square2, 0, 1 },
    { "SetupChannel.wave",        3, 0x69F3, t_setup_wave, 0, 1 },
    { "SetupChannel.noise",       3, 0x69FA, t_setup_noise, 0, 1 },
    { "LookupSoundPointer",       3, 0x6A07, t_lookup_new, OUT_A | OUT_HL | OUT_E, 1 },
    { "LookupSoundPointer.lookup", 3, 0x6A0A, t_lookup, OUT_A | OUT_HL | OUT_E, 1 },
    { "updateSoundProgress",      3, 0x6A19, t_update_progress, OUT_A, 1 },
    { "SetupWavePattern",         3, 0x6A26, t_setup_wave_pattern, 0, 1 },
    { "_InitSound.muteChannels",  3, 0x6A54, t_mute_channels, 0, 1 },
    { "PlaySquareSFX",            3, 0x6A6A, t_play_square, 0, 1, sound_state_valid },
    { "PlayNoiseSFX",             3, 0x6A8E, t_play_noise, 0, 1, sound_state_valid },
    { "StartMusic",               3, 0x6AB5, t_start_music, 0, 1 },
    { "PanExplosion",             3, 0x6AF6, t_pan_explosion, 0, 1 },
    { "PanStereo",                3, 0x6B09, t_pan_stereo, 0, 1 },
    { "CopyPointerIndirect",      3, 0x6B7B, t_copy_pointer_indirect, OUT_A, 1, sound_state_valid },
    { "CopyPointer",              3, 0x6B86, t_copy_pointer, 0, 1 },
    { "Call_6B8C",                3, 0x6B8C, t_setup_song, 0, 1 },
    { "IncrementPointer",         3, 0x6C30, t_increment_pointer, 0, 1 },
    { "IncrementPointerTwice",    3, 0x6C3C, t_increment_pointer_twice, 0, 1 },
    { "LoadFromHLindirect",       3, 0x6C45, t_load_from_pointer, OUT_A | OUT_B, 1, sound_state_valid },
    { "PlayMusic",                3, 0x6CBE, t_play_music, 0, 1, sound_state_valid },
    { "Call_6DD3",                3, 0x6DD3, t_vibrato_offset, OUT_E, 1 },
    { "Call_6DDC",                3, 0x6DDC, t_apply_vibrato, 0, 1 },
    { "Call_7FF0",                3, 0x7FF0, t_timer_tick, 0, 1, sound_state_valid },
    { "InitSound",                3, 0x7FF3, t_init_sound, 0, 0 },
    { 0 },
};
