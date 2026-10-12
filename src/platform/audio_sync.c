/* Audio output with dynamic rate control, see audio_sync.h. */
#include "audio_sync.h"

void audio_sync_init(AudioSync *s, int in_rate, int out_rate, double max_adjust)
{
    s->step = (double)in_rate / (double)out_rate;
    s->max_adjust = max_adjust;
    s->pos = -1.0;
    s->prev[0] = s->prev[1] = 0;
    s->last_adjust = 0;
}

void audio_sync_set_rate(AudioSync *s, double in_rate, int out_rate)
{
    s->step = in_rate / (double)out_rate;
}

size_t audio_sync_resample(AudioSync *s, const int16_t *in, size_t in_frames,
                           int16_t *out, size_t out_cap, double queued, double target)
{
    if (in_frames == 0)
        return 0;
    /* Below the target: produce more output (smaller step), above: less. */
    double adjust = 0;
    if (target > 0) {
        adjust = (target - queued) / target;
        if (adjust > 1) adjust = 1;
        if (adjust < -1) adjust = -1;
        adjust *= s->max_adjust;
    }
    s->last_adjust = adjust;
    double step = s->step * (1.0 - adjust);

    size_t n = 0;
    double last = (double)(in_frames - 1);
    while (s->pos < last && n < out_cap) {
        long i = (long)(s->pos + 1.0) - 1;     /* floor for pos >= -1 */
        double frac = s->pos - (double)i;
        for (int c = 0; c < 2; c++) {
            double a = i < 0 ? s->prev[c] : in[i * 2 + c];
            double b = in[(i + 1) * 2 + c];
            double v = a + (b - a) * frac;
            out[n * 2 + c] = (int16_t)(v < 0 ? v - 0.5 : v + 0.5);
        }
        n++;
        s->pos += step;
    }
    if (s->pos < last)          /* out of space: drop the rest of the block */
        s->pos = last;
    s->pos -= (double)in_frames;
    s->prev[0] = in[(in_frames - 1) * 2];
    s->prev[1] = in[(in_frames - 1) * 2 + 1];
    return n;
}

void audio_apply_volume(int16_t *samples, size_t count, int volume)
{
    if (volume >= 100)
        return;
    if (volume < 0)
        volume = 0;
    for (size_t i = 0; i < count; i++)
        samples[i] = (int16_t)(samples[i] * volume / 100);
}
