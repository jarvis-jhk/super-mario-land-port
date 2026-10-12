/*
 * Audio output with dynamic rate control.
 *
 * The emulated APU produces 48000 stereo frames per emulated second, and the
 * frontend runs emulated seconds at the host's pace (the clock or the display
 * refresh). Host audio clocks never match exactly, so a fixed-rate stream
 * would slowly underrun or build up latency. The resampler converts the APU
 * output to the device rate and nudges the ratio by at most max_adjust
 * (0.5 %, inaudible) to keep the device queue at its target fill.
 *
 * Presentation only: the APU itself always runs at its exact cycles.
 */
#ifndef SML_AUDIO_SYNC_H
#define SML_AUDIO_SYNC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double step;         /* nominal input frames per output frame */
    double max_adjust;   /* largest relative ratio change */
    double pos;          /* next output position; -1..0 lies between prev and in[0] */
    int16_t prev[2];     /* last input frame of the previous block */
    double last_adjust;  /* ratio change applied to the last block (for tests/stats) */
} AudioSync;

void audio_sync_init(AudioSync *s, int in_rate, int out_rate, double max_adjust);

/* Changes the nominal ratio without a discontinuity: in_rate may be
   fractional (for example 48000 * 60 / 59.73 when one emulated frame is
   shown per 60 Hz display refresh). */
void audio_sync_set_rate(AudioSync *s, double in_rate, int out_rate);

/* Resamples in_frames interleaved stereo frames into out (at most out_cap
   frames). queued is the number of output frames still waiting in the
   device, target the wanted number. Returns the frames written. */
size_t audio_sync_resample(AudioSync *s, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, double queued, double target);

/* Scales interleaved samples by volume/100 (0..100). */
void audio_apply_volume(int16_t *samples, size_t count, int volume);

#endif
