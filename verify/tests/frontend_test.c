/*
 * Unit tests for the frontend modules without SDL: the settings file
 * (src/platform/settings.c) and audio rate control (audio_sync.c).
 *
 *   build/frontend_test      exit 0 when everything passes
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/platform/audio_sync.h"
#include "../../src/platform/settings.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const char *tmp_path(const char *name)
{
    static char path[512];
    const char *dir = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/%s", dir && dir[0] ? dir : "/tmp", name);
    return path;
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static void test_defaults_round_trip(void)
{
    Settings a, b;
    char err[300];
    settings_defaults(&a);
    CHECK(a.scale == 4 && a.scaling == SCALE_INTEGER && a.volume == 100);
    CHECK(!strcmp(a.keys[ACT_A][0], "X"));
    CHECK(!strcmp(a.keys[ACT_SELECT][0], "Right Shift") && !strcmp(a.keys[ACT_SELECT][1], "Backspace"));
    CHECK(!strcmp(a.buttons[ACT_B][1], "x"));
    CHECK(a.buttons[ACT_QUIT][0][0] == 0);

    /* change a bit of everything, save, load into fresh defaults */
    a.scale = 3;
    a.scaling = SCALE_SHARP;
    a.sync = SYNC_CLOCK;
    a.fullscreen = 1;
    a.volume = 40;
    a.latency_ms = 100;
    a.palette[2] = 0xFF123456;
    a.deadzone = 9000;
    a.fast_forward_speed = 4;
    memset(a.keys[ACT_QUIT], 0, sizeof a.keys[ACT_QUIT]);
    snprintf(a.keys[ACT_START][1], BIND_NAME, "Keypad Enter");
    const char *path = tmp_path("sml_frontend_test.ini");
    CHECK(settings_save(&a, path) == 0);
    settings_defaults(&b);
    CHECK(settings_load(&b, path, err, sizeof err) == 0);
    CHECK(!memcmp(&a, &b, sizeof a));
    remove(path);
}

static void test_parse(void)
{
    Settings s;
    char err[300] = "";
    settings_defaults(&s);
    const char *path = tmp_path("sml_frontend_parse.ini");
    write_text(path,
               "# comment\n"
               "[video]\n"
               "  scaling = nearest  \n"
               "palette = #000000 #555555, #AAAAAA #FFFFFF\n"
               "[keyboard]\n"
               "a = Space, J\n"
               "fast_forward = none\n"
               "[controller]\n"
               "stick = no\n"
               "start = start, guide\n");
    CHECK(settings_load(&s, path, err, sizeof err) == 0);
    CHECK(s.scaling == SCALE_NEAREST);
    CHECK(s.palette[0] == 0xFF000000 && s.palette[3] == 0xFFFFFFFF);
    CHECK(!strcmp(s.keys[ACT_A][0], "Space") && !strcmp(s.keys[ACT_A][1], "J") && !s.keys[ACT_A][2][0]);
    CHECK(!s.keys[ACT_FAST_FORWARD][0][0]);
    CHECK(s.stick == 0);
    CHECK(!strcmp(s.buttons[ACT_START][1], "guide"));

    /* errors report the first bad line; later lines still apply */
    settings_defaults(&s);
    write_text(path,
               "[video]\n"
               "scale = 99\n"
               "bogus = 1\n"
               "[audio]\n"
               "volume = 25\n");
    CHECK(settings_load(&s, path, err, sizeof err) == -1);
    CHECK(strstr(err, ":2: ") != NULL);
    CHECK(s.scale == 4 && s.volume == 25);
    CHECK(settings_load(&s, tmp_path("sml_frontend_missing.ini"), err, sizeof err) == 1);

    CHECK(settings_set(&s, "video", "palette", "pocket", err, sizeof err) == 0);
    uint32_t pocket[4];
    CHECK(settings_palette_preset("pocket", pocket) == 2 && !memcmp(pocket, s.palette, sizeof pocket));
    CHECK(settings_set(&s, "video", "palette", "#12345 #000000 #000000 #000000", err, sizeof err) == -1);
    CHECK(settings_set(&s, "keyboard", "a", "1,2,3,4,5", err, sizeof err) == -1);
    CHECK(settings_set(&s, "nope", "a", "1", err, sizeof err) == -1);
    remove(path);
}

/* A sine through the resampler: right length, continuous across blocks. */
static void test_resample(void)
{
    AudioSync s;
    enum { BLOCK = 800, BLOCKS = 600 };
    static int16_t in[BLOCK * 2], out[4096 * 2];
    audio_sync_init(&s, 48000, 44100, 0.005);
    double phase = 0, total = 0, max_jump = 0;
    int16_t last = 0;
    int have_last = 0;
    for (int b = 0; b < BLOCKS; b++) {
        for (int i = 0; i < BLOCK; i++) {
            int16_t v = (int16_t)(10000 * sin(phase));
            phase += 2 * M_PI * 440 / 48000;
            in[i * 2] = in[i * 2 + 1] = v;
        }
        /* queue exactly at the target: no adjustment */
        size_t n = audio_sync_resample(&s, in, BLOCK, out, 4096, 2000, 2000);
        CHECK(s.last_adjust == 0);
        for (size_t i = 0; i < n; i++) {
            CHECK(out[i * 2] == out[i * 2 + 1]);
            if (have_last && fabs((double)out[i * 2] - last) > max_jump)
                max_jump = fabs((double)out[i * 2] - last);
            last = out[i * 2];
            have_last = 1;
        }
        total += (double)n;
    }
    double expected = (double)BLOCK * BLOCKS * 44100 / 48000;
    CHECK(fabs(total - expected) <= 2);
    /* 440 Hz at amplitude 10000 changes by at most ~627 per 44.1 kHz sample */
    CHECK(max_jump < 700);

    /* empty queue: produce up to 0.5 % more; full queue: up to 0.5 % less */
    audio_sync_init(&s, 48000, 48000, 0.005);
    size_t more = 0, less = 0;
    for (int b = 0; b < 100; b++)
        more += audio_sync_resample(&s, in, BLOCK, out, 4096, 0, 1000);
    CHECK(fabs(s.last_adjust - 0.005) < 1e-12);
    audio_sync_init(&s, 48000, 48000, 0.005);
    for (int b = 0; b < 100; b++)
        less += audio_sync_resample(&s, in, BLOCK, out, 4096, 5000, 1000);
    CHECK(fabs(s.last_adjust + 0.005) < 1e-12);
    CHECK(fabs((double)more / (BLOCK * 100) - 1 / 0.995) < 1e-3);
    CHECK(fabs((double)less / (BLOCK * 100) - 1 / 1.005) < 1e-3);

    /* a fractional nominal rate (vsync pacing on a 60 Hz display) */
    audio_sync_init(&s, 48000, 48000, 0.005);
    audio_sync_set_rate(&s, 48000.0 * 60 / (4194304.0 / 70224.0), 48000);
    size_t vs = 0;
    for (int b = 0; b < 100; b++)
        vs += audio_sync_resample(&s, in, BLOCK, out, 4096, 1000, 1000);
    CHECK(fabs((double)vs / (BLOCK * 100) - (4194304.0 / 70224.0) / 60) < 1e-3);

    /* a too-small output buffer drops the rest of the block, no overflow */
    audio_sync_init(&s, 48000, 48000, 0.005);
    CHECK(audio_sync_resample(&s, in, BLOCK, out, 10, 0, 0) == 10);
    CHECK(audio_sync_resample(&s, in, 0, out, 4096, 0, 0) == 0);

    int16_t v[4] = { 1000, -1000, 32767, -32768 };
    audio_apply_volume(v, 4, 50);
    CHECK(v[0] == 500 && v[1] == -500 && v[2] == 16383 && v[3] == -16384);
}

int main(void)
{
    test_defaults_round_trip();
    test_parse();
    test_resample();
    if (failures) {
        fprintf(stderr, "frontend_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("frontend_test: all passed\n");
    return 0;
}
