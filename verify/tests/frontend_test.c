/*
 * Unit tests for the frontend modules without SDL: the settings file
 * (src/platform/settings.c), audio rate control (audio_sync.c), and the
 * update logic (update_core.c, util/sha256.c) and the speedrun timer
 * (speedrun.c).
 *
 *   build/frontend_test      exit 0 when everything passes
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/platform/audio_sync.h"
#include "../../src/platform/settings.h"
#include "../../src/platform/speedrun.h"
#include "../../src/platform/update_core.h"
#include "../../src/util/sha256.h"

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
    a.update_check = 0;
    a.speedrun = 1;
    a.speedrun_category = SR_NORMAL_HARD;
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
    CHECK(s.update_check == 1);
    CHECK(settings_set(&s, "update", "check", "no", err, sizeof err) == 0 && s.update_check == 0);
    remove(path);
}

static void test_update(void)
{
    char hex[65];
    Sha256 h;
    sha256_init(&h);
    sha256_update(&h, "abc", 3);
    sha256_hex_final(&h, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha256_init(&h);
    for (int i = 0; i < 1000000; i++)
        sha256_update(&h, "a", 1);
    sha256_hex_final(&h, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    CHECK(version_compare("0.1.10", "0.1.9") == 1);
    CHECK(version_compare("v0.2", "0.2.0") == 0);
    CHECK(version_compare("0.1.0", "0.1.1") == -1);
    CHECK(version_compare("M0-dev", "0.1.0") == -2);
    CHECK(version_compare("0.1.0-rc1", "0.1.0") == -2);

    char tag[32];
    CHECK(update_latest_tag("[{\"tag_name\": \"v0.1.9\"},{\"tag_name\":\"v0.1.10\"},"
                            "{\"tag_name\":\"nightly\"},{\"tag_name\":\"v0.1.2\"}]", tag, sizeof tag) == 0);
    CHECK(!strcmp(tag, "0.1.10"));
    CHECK(update_latest_tag("[]", tag, sizeof tag) == -1);

    const char *manifest = "version 0.2.0\r\n"
        "file sml-linux-x86_64 123 BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n"
        "file sml-windows-x86_64.exe 0 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n";
    uint64_t size = 0;
    CHECK(update_manifest_find(manifest, "0.2.0", "sml-linux-x86_64", &size, hex) == 0);
    CHECK(size == 123 && !strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(update_manifest_find(manifest, "0.2.1", "sml-linux-x86_64", &size, hex) == -1);
    CHECK(update_manifest_find(manifest, "0.2.0", "sml-windows-x86_64.exe", &size, hex) == -1);
    CHECK(update_manifest_find(manifest, "0.2.0", "sml-linux", &size, hex) == -1);

    const char *log = "# Changelog\n\nintro\n\n## 0.3.0 - c\n- three\n\n## 0.2.0 - b\n- two\n\n## 0.1.0 - a\n- one\n";
    char *t = changelog_since(log, "0.1.0", "0.3.0");
    CHECK(t && !strcmp(t, "## 0.3.0 - c\n- three\n\n## 0.2.0 - b\n- two\n\n"));
    free(t);
    t = changelog_since(log, NULL, "0.2.0");
    CHECK(t && !strcmp(t, "## 0.2.0 - b\n- two\n\n"));
    free(t);
    t = changelog_since(log, "M0-dev", "0.1.0");
    CHECK(t && !strcmp(t, "## 0.1.0 - a\n- one\n"));
    free(t);
    CHECK(changelog_since(log, "0.3.0", "0.3.0") == NULL);
    t = changelog_reflow("## 0.2.0 - b\n\n- one\n  two\n- three\r\nfour\n\npara\ngraph\n");
    CHECK(t && !strcmp(t, "0.2.0 - b\n\n- one two\n- three four\n\npara graph\n"));
    free(t);
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

/* One frame of a synthetic game for the timer; each frame is 70224 cycles. */
static SrEvent sr_step(Speedrun *sr, uint64_t *cycles, int state, int wl, int boss, int tatanga)
{
    SrFrame f = { 0 };
    *cycles += 70224;
    f.cycles = *cycles;
    f.state = (uint8_t)state;
    f.world_level = (uint8_t)wl;
    f.boss_defeated = (uint8_t)boss;
    f.tatanga_visible = (uint8_t)tatanga;
    return speedrun_frame(sr, &f);
}

/* Plays all twelve levels; level n takes 100 + n + extra frames. */
static SrEvent sr_play(Speedrun *sr, uint64_t *c, int extra)
{
    static const int wl[12] = { 0x11, 0x12, 0x13, 0x21, 0x22, 0x23, 0x31, 0x32, 0x33, 0x41, 0x42, 0x43 };
    SrEvent last = SR_EV_NONE;
    for (int n = 0; n < 12; n++) {
        if (n) {
            sr_step(sr, c, 0x08, wl[n], 0, 0);            /* loading, LCD off */
            CHECK(sr_step(sr, c, n == 11 ? 0x0D : 0x00, wl[n], 0, 0) == SR_EV_SPLIT);
        }
        for (int i = 1; i < 100 + n + extra; i++)
            sr_step(sr, c, 0x00, wl[n], 0, n == 11);
    }
    sr_step(sr, c, 0x0D, 0x43, 0x60, 1);                  /* killed, still visible */
    last = sr_step(sr, c, 0x07, 0x43, 0x60, 0);
    CHECK(sr_step(sr, c, 0x06, 0x43, 0x60, 0) == SR_EV_NONE);
    return last;
}

static void test_speedrun(void)
{
    Speedrun sr;
    uint64_t c = 0;
    char t[24];
    speedrun_init(&sr, SR_ANY);
    CHECK(sr.nsplits == 12 && !strcmp(speedrun_split_name(&sr, 11), "4-3"));

    /* the attract demo never starts a run, nor does gameplay before a menu */
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_NONE);
    sr_step(&sr, &c, 0x0F, 0x11, 0, 0);
    SrFrame demo = { c + 70224, 0x00, 0x11, 0x40, 0, 0 };
    c += 70224;
    CHECK(speedrun_frame(&sr, &demo) == SR_EV_NONE && sr.phase == SR_ARMED);

    /* start: the frame before 1-1 first appears */
    sr_step(&sr, &c, 0x0F, 0x11, 0, 0);
    sr_step(&sr, &c, 0x11, 0x11, 0, 0);
    uint64_t blank = c + 70224;
    sr_step(&sr, &c, 0x02, 0x11, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_START);
    CHECK(sr.start == blank && sr.attempts == 1);
    /* deaths restart a level without a split */
    sr_step(&sr, &c, 0x01, 0x11, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_NONE);
    c -= 2 * 70224;                                         /* keep the arithmetic simple */
    sr.prev_cycles = c;
    CHECK(sr_play(&sr, &c, 0) == SR_EV_FINISH);
    CHECK(sr.phase == SR_FINISHED && sr.new_pb && sr.completed == 1);
    /* 1-1 lasts 100 frames from the blank frame: 99 more plus the next blank */
    CHECK(sr.times[0] == 101 * 70224);
    int64_t first = sr.times[11];
    CHECK(sr.history[0] == first && sr.pb[11] == first);
    CHECK(speedrun_sum_of_best(&sr) == first);

    /* a slower attempt: behind, no new PB; a reset keeps golds */
    sr_step(&sr, &c, 0x0E, 0x43, 0, 0);
    sr_step(&sr, &c, 0x0F, 0x11, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_START);
    CHECK(sr_play(&sr, &c, 1) == SR_EV_FINISH);
    int64_t d;
    CHECK(!sr.new_pb && speedrun_delta(&sr, 11, &d) && d > 0 && !sr.gold);
    CHECK(sr.pb[11] == first && sr.history[1] == first && sr.completed == 2);
    sr_step(&sr, &c, 0x0F, 0x11, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_START);
    for (int i = 0; i < 50; i++)
        sr_step(&sr, &c, 0x00, 0x11, 0, 0);
    sr_step(&sr, &c, 0x08, 0x12, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x12, 0, 0) == SR_EV_SPLIT && sr.gold);
    CHECK(speedrun_delta(&sr, 0, &d) && d < 0);
    CHECK(sr_step(&sr, &c, 0x39, 0x12, 0, 0) == SR_EV_NONE);   /* game over screen */
    CHECK(sr_step(&sr, &c, 0x0E, 0x12, 0, 0) == SR_EV_RESET && sr.attempts == 3);
    CHECK(sr.best[0] == 52 * 70224 && sr.pb[0] == 101 * 70224);

    /* own file and LiveSplit round trips */
    const char *path = tmp_path("sml_speedrun_test.txt");
    Speedrun b;
    CHECK(speedrun_save(&sr, path) == 0);
    speedrun_init(&b, SR_ANY);
    CHECK(speedrun_load(&b, path) == 0);
    CHECK(!memcmp(b.pb, sr.pb, sizeof b.pb) && !memcmp(b.best, sr.best, sizeof b.best));
    CHECK(!memcmp(b.history, sr.history, sizeof b.history) && b.attempts == 3 && b.completed == 2);
    speedrun_init(&b, SR_NORMAL_HARD);
    CHECK(speedrun_load(&b, path) == -1);                      /* other category */

    const char *lss = tmp_path("sml_speedrun_test.lss");
    CHECK(speedrun_export_lss(&sr, lss) == 0);
    FILE *f = fopen(lss, "rb");
    static char xml[65536];
    size_t n = fread(xml, 1, sizeof xml - 1, f);
    fclose(f);
    xml[n] = 0;
    CHECK(strstr(xml, "<CategoryName>Any%</CategoryName>") && strstr(xml, "<AttemptCount>3</AttemptCount>"));
    CHECK(strstr(xml, "<Name>4-3</Name>") && strstr(xml, "<Attempt id=\"2\">"));
    speedrun_init(&b, SR_ANY);
    char err[200];
    CHECK(speedrun_import_lss(&b, xml, err, sizeof err) == 0 && b.attempts == 3);
    for (int i = 0; i < 12; i++) {
        /* 100 ns ticks lose at most one cycle */
        CHECK(llabs(b.pb[i] - sr.pb[i]) <= 1 && llabs(b.best[i] - sr.best[i]) <= 1);
    }
    speedrun_init(&b, SR_NORMAL_HARD);
    CHECK(speedrun_import_lss(&b, xml, err, sizeof err) == -1 && strstr(err, "24 splits"));

    /* LiveSplit's own shapes: short times, empty splits */
    speedrun_init(&b, SR_ANY);
    char small[4096] = "<Run><AttemptCount>7</AttemptCount><Segments>";
    for (int i = 0; i < 12; i++)
        strcat(small, i == 3 ? "<Segment><Name>x</Name><SplitTimes><SplitTime name=\"Personal Best\" />"
                               "</SplitTimes><BestSegmentTime /></Segment>"
                             : "<Segment><Name>x</Name><SplitTimes><SplitTime name=\"Personal Best\">"
                               "<RealTime>00:01:02.5000000</RealTime></SplitTime></SplitTimes>"
                               "<BestSegmentTime><RealTime>1.00:00:01</RealTime></BestSegmentTime></Segment>");
    strcat(small, "</Segments></Run>");
    CHECK(speedrun_import_lss(&b, small, err, sizeof err) == 0 && b.attempts == 7);
    CHECK(b.pb[0] == 62 * 4194304LL + 2097152 && b.pb[3] == -1 && b.best[3] == -1);
    CHECK(b.best[0] == 86401 * 4194304LL);
    remove(path);
    remove(lss);

    /* Normal + Hard: the ending, the title and its demo keep the run going */
    speedrun_init(&sr, SR_NORMAL_HARD);
    c = 0;
    sr_step(&sr, &c, 0x0F, 0x11, 0, 0);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_START);
    CHECK(sr_play(&sr, &c, 0) == SR_EV_SPLIT && sr.phase == SR_RUNNING && sr.split == 12);
    sr_step(&sr, &c, 0x38, 0x11, 0x60, 0);
    sr_step(&sr, &c, 0x0E, 0x11, 0x60, 0);
    SrFrame title_demo = { c + 70224, 0x00, 0x11, 0x40, 0, 0 };
    c += 70224;
    CHECK(speedrun_frame(&sr, &title_demo) == SR_EV_NONE);
    CHECK(sr_step(&sr, &c, 0x0F, 0x11, 0, 0) == SR_EV_NONE);
    CHECK(sr_step(&sr, &c, 0x00, 0x11, 0, 0) == SR_EV_NONE && sr.split == 12);
    CHECK(sr_play(&sr, &c, 0) == SR_EV_FINISH && sr.split == 24);
    CHECK(!strcmp(speedrun_split_name(&sr, 23), "Hard 4-3"));

    speedrun_format(3091287748LL, 0, t, sizeof t);
    CHECK(!strcmp(t, "12:17.02"));
    speedrun_format(-2223000, 1, t, sizeof t);
    CHECK(!strcmp(t, "-0.53"));
    speedrun_format(4194304LL * 61, 1, t, sizeof t);
    CHECK(!strcmp(t, "+1:01.00"));
    speedrun_format(4194304LL * 3600, 0, t, sizeof t);
    CHECK(!strcmp(t, "1:00:00.00"));
}

int main(void)
{
    test_defaults_round_trip();
    test_parse();
    test_resample();
    test_update();
    test_speedrun();
    if (failures) {
        fprintf(stderr, "frontend_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("frontend_test: all passed\n");
    return 0;
}
