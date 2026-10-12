/* Game Boy APU model. See apu.h. */
#include "apu.h"
#include <string.h>

enum { SQ1, SQ2, WAVE, NOISE };

#define R(addr) a->regs[(addr) - 0xFF10]

static const uint8_t duty_table[4] = { 0x01, 0x81, 0x87, 0x7E };   /* bit 7 = first step */
static const uint8_t read_mask[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,     /* NR10-NR14 */
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,     /* --, NR21-NR24 */
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,     /* NR30-NR34 */
    0xFF, 0xFF, 0x00, 0x00, 0xBF,     /* --, NR41-NR44 */
    0x00, 0x00, 0x70,                 /* NR50-NR52 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static int square_period(const ApuChannel *c) { return (2048 - c->freq) * 4; }
static int wave_period(const ApuChannel *c) { return (2048 - c->freq) * 2; }

static int noise_period(const Apu *a)
{
    static const int divisors[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
    uint8_t nr43 = R(0xFF22);
    return divisors[nr43 & 7] << (nr43 >> 4);
}

void apu_init(Apu *a)
{
    int16_t *out = a->out;
    size_t cap = a->out_cap;
    memset(a, 0, sizeof *a);
    a->out = out;
    a->out_cap = cap;
    a->ch[NOISE].lfsr = 0x7FFF;
}

/* ---- frame sequencer units ---- */

static void clock_length(ApuChannel *c)
{
    if (c->length_enable && c->length > 0 && --c->length == 0)
        c->enabled = 0;
}

static void clock_envelope(ApuChannel *c)
{
    if (c->env_period == 0)
        return;
    if (--c->env_timer > 0)
        return;
    c->env_timer = c->env_period;
    if (c->env_dir && c->volume < 15) c->volume++;
    else if (!c->env_dir && c->volume > 0) c->volume--;
}

static int sweep_calc(Apu *a)
{
    ApuChannel *c = &a->ch[SQ1];
    uint8_t nr10 = R(0xFF10);
    int delta = c->sweep_shadow >> (nr10 & 7);
    int f;
    if (nr10 & 8) {
        f = c->sweep_shadow - delta;
        c->sweep_negated = 1;
    } else {
        f = c->sweep_shadow + delta;
    }
    if (f > 2047)
        c->enabled = 0;
    return f;
}

static void clock_sweep(Apu *a)
{
    ApuChannel *c = &a->ch[SQ1];
    if (--c->sweep_timer > 0)
        return;
    uint8_t nr10 = R(0xFF10);
    int period = (nr10 >> 4) & 7;
    c->sweep_timer = period ? period : 8;
    if (!c->sweep_enabled || period == 0)
        return;
    int f = sweep_calc(a);
    if (f <= 2047 && (nr10 & 7)) {
        c->sweep_shadow = f;
        c->freq = f;
        R(0xFF13) = f & 0xFF;
        R(0xFF14) = (R(0xFF14) & 0xF8) | (f >> 8);
        sweep_calc(a);
    }
}

void apu_frame_sequencer(Apu *a)
{
    if (!a->power)
        return;
    int s = a->fs_step;
    if ((s & 1) == 0)
        for (int i = 0; i < 4; i++)
            clock_length(&a->ch[i]);
    if (s == 2 || s == 6)
        clock_sweep(a);
    if (s == 7) {
        clock_envelope(&a->ch[SQ1]);
        clock_envelope(&a->ch[SQ2]);
        clock_envelope(&a->ch[NOISE]);
    }
    a->fs_step = (s + 1) & 7;
}

/* ---- register writes ---- */

static void write_length_enable(Apu *a, ApuChannel *c, uint8_t v)
{
    int was = c->length_enable;
    c->length_enable = (v & 0x40) != 0;
    /* Enabling the length counter during a frame-sequencer step that does
       not clock length clocks it once extra. */
    if (!was && c->length_enable && (a->fs_step & 1) && c->length > 0) {
        if (--c->length == 0 && !(v & 0x80))
            c->enabled = 0;
    }
}

static void trigger(Apu *a, int n, uint8_t v)
{
    ApuChannel *c = &a->ch[n];
    int max_len = n == WAVE ? 256 : 64;
    if (c->length == 0) {
        c->length = max_len;
        if (c->length_enable && (a->fs_step & 1))
            c->length--;
    }
    (void)v;
    c->enabled = c->dac_on;
    switch (n) {
    case SQ1: case SQ2:
        c->timer = square_period(c);
        break;
    case WAVE:
        c->timer = wave_period(c) + 6;
        c->pos = 0;
        break;
    case NOISE:
        c->timer = noise_period(a);
        c->lfsr = 0x7FFF;
        break;
    }
    if (n != WAVE) {
        static const uint16_t envelope_reg[4] = { 0xFF12, 0xFF17, 0, 0xFF21 };
        uint8_t env = R(envelope_reg[n]);
        c->volume = env >> 4;
        c->env_dir = (env >> 3) & 1;
        c->env_period = env & 7;
        c->env_timer = c->env_period;
    }
    if (n == SQ1) {
        uint8_t nr10 = R(0xFF10);
        c->sweep_shadow = c->freq;
        int period = (nr10 >> 4) & 7;
        c->sweep_timer = period ? period : 8;
        c->sweep_enabled = period || (nr10 & 7);
        c->sweep_negated = 0;
        if (nr10 & 7)
            sweep_calc(a);
    }
}

void apu_write(Apu *a, uint16_t addr, uint8_t v)
{
    if (addr >= 0xFF30) {             /* wave RAM */
        R(addr) = v;
        return;
    }
    if (!a->power && addr != 0xFF26) {
        /* While powered off only the length registers can be written (DMG). */
        if (addr == 0xFF11) a->ch[SQ1].length = 64 - (v & 63);
        else if (addr == 0xFF16) a->ch[SQ2].length = 64 - (v & 63);
        else if (addr == 0xFF1B) a->ch[WAVE].length = 256 - v;
        else if (addr == 0xFF20) a->ch[NOISE].length = 64 - (v & 63);
        return;
    }
    switch (addr) {
    case 0xFF10:
        if ((R(addr) & 8) && !(v & 8) && a->ch[SQ1].sweep_negated)
            a->ch[SQ1].enabled = 0;
        break;
    case 0xFF11: a->ch[SQ1].length = 64 - (v & 63); break;
    case 0xFF16: a->ch[SQ2].length = 64 - (v & 63); break;
    case 0xFF1B: a->ch[WAVE].length = 256 - v; break;
    case 0xFF20: a->ch[NOISE].length = 64 - (v & 63); break;
    case 0xFF12: case 0xFF17: case 0xFF21: {
        ApuChannel *c = &a->ch[addr == 0xFF12 ? SQ1 : addr == 0xFF17 ? SQ2 : NOISE];
        c->dac_on = (v & 0xF8) != 0;
        if (!c->dac_on) c->enabled = 0;
        break;
    }
    case 0xFF1A:
        a->ch[WAVE].dac_on = (v & 0x80) != 0;
        if (!a->ch[WAVE].dac_on) a->ch[WAVE].enabled = 0;
        break;
    case 0xFF13: a->ch[SQ1].freq = (a->ch[SQ1].freq & 0x700) | v; break;
    case 0xFF18: a->ch[SQ2].freq = (a->ch[SQ2].freq & 0x700) | v; break;
    case 0xFF1D: a->ch[WAVE].freq = (a->ch[WAVE].freq & 0x700) | v; break;
    case 0xFF14: case 0xFF19: case 0xFF1E: case 0xFF23: {
        int n = addr == 0xFF14 ? SQ1 : addr == 0xFF19 ? SQ2 : addr == 0xFF1E ? WAVE : NOISE;
        ApuChannel *c = &a->ch[n];
        if (n != NOISE)
            c->freq = (c->freq & 0xFF) | ((v & 7) << 8);
        write_length_enable(a, c, v);
        if (v & 0x80)
            trigger(a, n, v);
        break;
    }
    case 0xFF26:
        if (!(v & 0x80) && a->power) {
            /* power off: clear all registers, keep wave RAM and lengths */
            uint8_t wave[16];
            memcpy(wave, a->regs + 0x20, 16);
            int lens[4];
            for (int i = 0; i < 4; i++) lens[i] = a->ch[i].length;
            memset(a->regs, 0, 0x20);
            memcpy(a->regs + 0x20, wave, 16);
            for (int i = 0; i < 4; i++) {
                memset(&a->ch[i], 0, sizeof a->ch[i]);
                a->ch[i].length = lens[i];
            }
            a->ch[NOISE].lfsr = 0x7FFF;
            a->power = 0;
        } else if ((v & 0x80) && !a->power) {
            a->power = 1;
            a->fs_step = 0;
        }
        R(addr) = v & 0x80;
        return;
    }
    R(addr) = v;
}

uint8_t apu_read(const Apu *a, uint16_t addr)
{
    if (addr >= 0xFF30)
        return R(addr);
    if (addr == 0xFF26) {
        uint8_t v = (a->power ? 0x80 : 0) | 0x70;
        for (int i = 0; i < 4; i++)
            if (a->ch[i].enabled) v |= 1 << i;
        return v;
    }
    return R(addr) | read_mask[addr - 0xFF10];
}

/* ---- waveform generation ---- */

static int channel_output(const Apu *a, int n)
{
    const ApuChannel *c = &a->ch[n];
    if (!c->enabled)
        return 0;
    switch (n) {
    case SQ1: case SQ2: {
        int duty = R(n == SQ1 ? 0xFF11 : 0xFF16) >> 6;
        return ((duty_table[duty] >> (7 - c->pos)) & 1) ? c->volume : 0;
    }
    case WAVE: {
        static const int shift[4] = { 4, 0, 1, 2 };
        return c->sample >> shift[(R(0xFF1C) >> 5) & 3];
    }
    default:
        return (c->lfsr & 1) ? 0 : c->volume;
    }
}

static void step_channels(Apu *a, int n)
{
    for (int i = 0; i < 4; i++) {
        ApuChannel *c = &a->ch[i];
        if (!c->enabled)
            continue;
        c->timer -= n;
        while (c->timer <= 0) {
            switch (i) {
            case SQ1: case SQ2:
                c->timer += square_period(c);
                c->pos = (c->pos + 1) & 7;
                break;
            case WAVE: {
                c->timer += wave_period(c);
                c->pos = (c->pos + 1) & 31;
                uint8_t b = R(0xFF30 + c->pos / 2);
                c->sample = (c->pos & 1) ? (b & 0x0F) : (b >> 4);
                break;
            }
            default: {
                c->timer += noise_period(a);
                int bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
                c->lfsr = (uint16_t)((c->lfsr >> 1) | (bit << 14));
                if (R(0xFF22) & 8)
                    c->lfsr = (uint16_t)((c->lfsr & ~0x40) | (bit << 6));
                break;
            }
            }
        }
    }
}

static void mix(const Apu *a, int *l, int *r)
{
    uint8_t nr51 = R(0xFF25), nr50 = R(0xFF24);
    int sl = 0, sr = 0;
    for (int i = 0; i < 4; i++) {
        if (!a->ch[i].dac_on)
            continue;
        int v = channel_output(a, i) * 2 - 15;   /* DAC: 0..15 -> +-15 */
        if (nr51 & (0x10 << i)) sl += v;
        if (nr51 & (0x01 << i)) sr += v;
    }
    *l = sl * (((nr50 >> 4) & 7) + 1);
    *r = sr * ((nr50 & 7) + 1);
}

static void emit_sample(Apu *a)
{
    int32_t l = a->acc_cycles ? a->acc_l / a->acc_cycles : 0;
    int32_t r = a->acc_cycles ? a->acc_r / a->acc_cycles : 0;
    a->acc_l = a->acc_r = a->acc_cycles = 0;
    /* scale: max |l| = 60 * 8 = 480 -> about 15000 */
    l *= 30;
    r *= 30;
    /* DC-blocking high-pass (like the capacitor on the real output) */
    l <<= 16;
    r <<= 16;
    int32_t ol = l - a->hp_l, or_ = r - a->hp_r;
    a->hp_l = l - (int32_t)(((int64_t)ol * 65533) >> 16);
    a->hp_r = r - (int32_t)(((int64_t)or_ * 65533) >> 16);
    ol >>= 16;
    or_ >>= 16;
    if (ol > 32767) ol = 32767;
    if (ol < -32768) ol = -32768;
    if (or_ > 32767) or_ = 32767;
    if (or_ < -32768) or_ = -32768;
    if (a->out && a->out_len < a->out_cap) {
        a->out[a->out_len * 2] = (int16_t)ol;
        a->out[a->out_len * 2 + 1] = (int16_t)or_;
        a->out_len++;
    }
}

void apu_tick(Apu *a, int n)
{
    if (a->power)
        step_channels(a, n);
    int l = 0, r = 0;
    if (a->power)
        mix(a, &l, &r);
    a->acc_l += l * n;
    a->acc_r += r * n;
    a->acc_cycles += n;
    a->rate_acc += (uint32_t)APU_SAMPLE_RATE * n;
    if (a->rate_acc >= APU_CLOCK) {
        a->rate_acc -= APU_CLOCK;
        emit_sample(a);
    }
}
