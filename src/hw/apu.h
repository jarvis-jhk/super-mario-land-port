/*
 * Game Boy sound chip (APU): two square channels (the first with frequency
 * sweep), a 4-bit wave channel and a noise channel, mixed to stereo.
 *
 * Super Mario Land's music driver writes these registers 64 times per second
 * (from the timer interrupt); the port runs the same driver in C and feeds
 * this model at the same cycles, so the audio is sample-identical to the
 * original running on the reference emulator.
 *
 * All arithmetic is integer, so the output is identical on every platform.
 */
#ifndef SML_APU_H
#define SML_APU_H

#include <stdint.h>
#include <stddef.h>

#define APU_SAMPLE_RATE 48000
#define APU_CLOCK       4194304

typedef struct {
    int enabled;          /* channel is playing */
    int dac_on;
    int length;           /* remaining length steps */
    int length_enable;
    int volume, env_dir, env_period, env_timer;
    int freq;             /* 11-bit frequency register value */
    int timer;            /* T-cycles until the next waveform step */
    int pos;              /* duty / wave position */
    /* square 1 only */
    int sweep_enabled, sweep_timer, sweep_shadow, sweep_negated;
    /* wave only */
    uint8_t sample;       /* last 4-bit sample read */
    /* noise only */
    uint16_t lfsr;
} ApuChannel;

typedef struct {
    uint8_t regs[0x30];   /* FF10-FF3F as last written (wave RAM at 0x20) */
    int power;            /* NR52 bit 7 */
    int fs_step;          /* frame sequencer step 0..7 */
    ApuChannel ch[4];

    /* sample output */
    int32_t acc_l, acc_r;     /* box filter accumulators */
    int32_t acc_cycles;
    uint32_t rate_acc;        /* fractional sample clock */
    int32_t hp_l, hp_r;       /* high-pass filter state (16.16) */
    int16_t *out;             /* interleaved stereo buffer (owned by caller) */
    size_t out_len, out_cap;  /* in stereo frames */
} Apu;

void apu_init(Apu *a);
void apu_write(Apu *a, uint16_t addr, uint8_t v);
uint8_t apu_read(const Apu *a, uint16_t addr);
/* Advance n T-cycles. */
void apu_tick(Apu *a, int n);
/* Called on the falling edge of DIV bit 4 (512 Hz frame sequencer clock). */
void apu_frame_sequencer(Apu *a);

#endif
