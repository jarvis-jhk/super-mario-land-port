/*
 * hwtest: run Gambatte's hardware timing test ROMs on our hardware model.
 *
 *   hwtest dmg|cgb|agb [-v] ROM...
 *
 * The test ROMs (reference/gambatte/test/hwtests, assembled with qdgbas.py)
 * were verified on real DMG-08 and CGB-04C consoles. Each prints a few hex
 * digits in the top-left corner of the screen; the expected digits are in
 * the file name:
 *   ..._dmg08_cgb04c_outXY   same result on both consoles
 *   ..._dmg08_outXY_cgb04c_outZW
 *   ..._outXY                CGB only
 * Like Gambatte's testrunner we start from the post-boot state and look at
 * the screen after 15 frames. AGB uses the CGB expectations (as Gambatte
 * does). Tests expecting audio output ("audio0/1") or PNG images are skipped.
 *
 * Scope: Super Mario Land is a DMG game. A DMG ignores the CGB flag in the
 * cartridge header, so on "dmg" both .gb and .gbc ROMs run. On "cgb"/"agb"
 * only the .gb ROMs (no CGB flag, i.e. DMG compatibility mode, exactly the
 * game's situation) are meaningful, so .gbc ROMs are skipped. Double speed
 * tests (_ds_) and categories about CGB-only or out-of-scope hardware
 * (speedchange, cgbpal_m3, dma = HDMA, serial, undef_ops, sound) are skipped.
 *
 * Oracle mode: "--oracle FILE" takes the expected digits from FILE (lines
 * "name digits", written by verify/tests/hworacle.cpp, which runs the same
 * ROMs on Gambatte) instead of the file name. With it, .gbc ROMs are run on
 * cgb/agb as well; the oracle runs them with the CGB flag cleared, so both
 * sides see them as DMG-compatibility-mode programs. This turns the whole
 * suite into a differential test of our model against Gambatte.
 */
#include "../emu/gb.h"
#include "hwtest_glyphs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Expected digit string for this model, or NULL if the test does not apply. */
static const char *expected(const char *name, MachineModel model, char *buf)
{
    const char *key = NULL;
    const char *p;
    if ((p = strstr(name, "dmg08_cgb04c_out"))) {
        key = p + strlen("dmg08_cgb04c_out");
    } else if (model == MODEL_DMG) {
        if ((p = strstr(name, "dmg08_out")))
            key = p + strlen("dmg08_out");
    } else {
        if ((p = strstr(name, "cgb04c_out")))
            key = p + strlen("cgb04c_out");
        else if (!strstr(name, "dmg08") && (p = strstr(name, "_out")))
            key = p + strlen("_out");
    }
    if (!key || !strncmp(key, "audio", 5))
        return NULL;
    int n = 0;
    while (n < 15 && ((key[n] >= '0' && key[n] <= '9') || (key[n] >= 'A' && key[n] <= 'F')))
        buf[n] = key[n], n++;
    buf[n] = 0;
    return n ? buf : NULL;
}

/* Read up to n digits from the top-left of the screen; '?' if unreadable. */
static void read_screen(const uint8_t *fb, int n, char *out)
{
    for (int i = 0; i < n; i++) {
        out[i] = '?';
        for (int d = 0; d < 16; d++) {
            int ok = 1;
            for (int y = 0; y < 8 && ok; y++)
                for (int x = 0; x < 8 && ok; x++) {
                    int black = (hwtest_glyphs[d][y] >> (7 - x)) & 1;
                    int shade = fb[y * LCD_W + i * 8 + x];
                    if (black ? shade != 3 : shade != 0)
                        ok = 0;
                }
            if (ok) { out[i] = "0123456789ABCDEF"[d]; break; }
        }
    }
    out[n] = 0;
}

/* Tests that do not apply to a DMG game on any of our models. */
static int out_of_scope(const char *path)
{
    static const char *const skip[] = {
        "/speedchange/", "/cgbpal_m3/", "/dma/", "/serial/", "/undef_ops/", "/sound/",
    };
    for (size_t i = 0; i < sizeof skip / sizeof *skip; i++)
        if (strstr(path, skip[i]))
            return 1;
    /* double speed, and the Color's palette registers (not in DMG mode) */
    return strstr(path, "_ds_") != NULL || strstr(path, "_cgbp") != NULL;
}

/* Look up the oracle's digits for a ROM; returns 0 if not listed. */
static int oracle_lookup(const char *file, const char *base, char *buf)
{
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); exit(2); }
    char name[512], digits[32];
    int found = 0;
    while (fscanf(f, "%511s %31s", name, digits) == 2)
        if (!strcmp(name, base)) {
            strcpy(buf, digits);
            found = 1;
            break;
        }
    fclose(f);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hwtest dmg|cgb|agb [-v] [--oracle FILE] ROM...\n");
        return 2;
    }
    int model = machine_model_from_name(argv[1]);
    if (model < 0) { fprintf(stderr, "unknown model\n"); return 2; }
    int verbose = 0, first = 2;
    const char *oracle = NULL;
    for (; first < argc; first++) {
        if (!strcmp(argv[first], "-v")) verbose = 1;
        else if (!strcmp(argv[first], "--oracle") && first + 1 < argc) oracle = argv[++first];
        else break;
    }
    static uint8_t rom[0x80000];
    int run = 0, passed = 0;
    for (int i = first; i < argc; i++) {
        char want[16], got[16];
        const char *base = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        size_t len = strlen(argv[i]);
        int is_gbc = len > 4 && !strcmp(argv[i] + len - 4, ".gbc");
        if (out_of_scope(argv[i]))
            continue;
        if (oracle) {
            if (!oracle_lookup(oracle, base, want))
                continue;
        } else {
            if (is_gbc && model != MODEL_DMG)
                continue;
            if (!expected(base, (MachineModel)model, want))
                continue;
        }
        FILE *f = fopen(argv[i], "rb");
        if (!f) { perror(argv[i]); continue; }
        size_t n = fread(rom, 1, sizeof rom, f);
        fclose(f);
        GB *gb = gb_create((MachineModel)model, rom, n);
        uint64_t end = 70224ull * 15;
        uint8_t shown[LCD_W * LCD_H];
        memset(shown, 0, sizeof shown);
        while (gb->m.cycles < end) {
            gb_run_frame(gb);
            if (gb->m.lcd_on)
                memcpy(shown, gb->m.framebuffer, sizeof shown);
        }
        read_screen(shown, (int)strlen(want), got);
        run++;
        if (!strcmp(want, got)) {
            passed++;
        } else if (verbose) {
            printf("FAIL %s: want %s got %s\n", argv[i], want, got);
        }
        gb_destroy(gb);
    }
    printf("%s: %d/%d passed\n", argv[1], passed, run);
    return passed == run ? 0 : 1;
}
