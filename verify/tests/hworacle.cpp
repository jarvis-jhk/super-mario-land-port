/*
 * hworacle: run Gambatte's hardware test ROMs on Gambatte itself and print
 * what each shows on screen, as the expected values for hwtest --oracle.
 *
 *   hworacle dmg|cgb|agb ROM... > oracle_MODEL.txt
 *
 * Output lines are "name digits". The ROMs are loaded with the CGB flag of
 * the cartridge header cleared (so a Color/Advance runs them in DMG
 * compatibility mode, the situation of a DMG game), which lets the CGB-only
 * (.gbc) test ROMs serve as additional differential tests for our model.
 * The number of digits read comes from the file name.
 *
 * Build (after "make oracle" has built build/libgambatte.a):
 *   g++ -O2 -std=c++11 -w -DHAVE_STDINT_H -Ireference/gambatte/libgambatte/include \
 *       -o build/hworacle verify/tests/hworacle.cpp build/libgambatte.a
 */
#include "gambatte.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "hwtest_glyphs.h"
}

static const unsigned W = 160, H = 144;

static int digit_count(const std::string &name)
{
    const char *keys[] = { "cgb04c_out", "dmg08_out", "_out" };
    for (const char *k : keys) {
        size_t p = name.rfind(k);
        if (p == std::string::npos)
            continue;
        const char *s = name.c_str() + p + strlen(k);
        int n = 0;
        while (n < 15 && ((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'A' && s[n] <= 'F')))
            n++;
        return n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hworacle dmg|cgb|agb ROM...\n");
        return 2;
    }
    std::string model = argv[1];
    unsigned flags = gambatte::GB::NO_BIOS | gambatte::GB::READONLY_SAV;
    if (model == "cgb") flags |= gambatte::GB::CGB_MODE;
    else if (model == "agb") flags |= gambatte::GB::CGB_MODE | gambatte::GB::GBA_FLAG;
    else if (model != "dmg") { fprintf(stderr, "unknown model\n"); return 2; }

    static uint32_t video[W * H];
    static uint32_t audio[(35112 + 2064) * 2];
    for (int i = 2; i < argc; i++) {
        std::string path = argv[i];
        std::string base = path.substr(path.rfind('/') + 1);
        int n = digit_count(base);
        if (!n)
            continue;
        /* load with the CGB flag (header byte 0143) cleared */
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) { perror(argv[i]); continue; }
        std::vector<char> rom(0x80000);
        rom.resize(fread(rom.data(), 1, rom.size(), f));
        fclose(f);
        if (rom.size() > 0x143)
            rom[0x143] = 0;

        gambatte::GB gb;
        if (gb.load(rom.data(), rom.size(), flags)) {
            fprintf(stderr, "cannot load %s\n", argv[i]);
            continue;
        }
        for (int k = 0; k < 12; k++)
            gb.setDmgPaletteColor(k / 4, k % 4, (3 - (k & 3)) * 85 * 0x010101ul);
        memset(video, 0, sizeof video);
        long left = 35112L * 15;
        while (left >= 0) {
            size_t samples = 35112;
            gb.runFor(video, W, audio, samples);
            left -= samples;
        }
        char out[16];
        for (int d = 0; d < n; d++) {
            out[d] = '?';
            for (int g = 0; g < 16; g++) {
                bool ok = true;
                for (int y = 0; y < 8 && ok; y++)
                    for (int x = 0; x < 8 && ok; x++) {
                        uint32_t p = video[y * W + d * 8 + x];
                        int lum = ((p >> 16 & 0xFF) * 3 + (p >> 8 & 0xFF) * 6 + (p & 0xFF)) / 10;
                        int black = (hwtest_glyphs[g][y] >> (7 - x)) & 1;
                        if (black ? lum > 0x60 : lum < 0xC0)
                            ok = false;
                    }
                if (ok) { out[d] = "0123456789ABCDEF"[g]; break; }
            }
        }
        out[n] = 0;
        if (strchr(out, '?'))
            continue;   /* nothing readable: the test needs CGB mode */
        printf("%s %s\n", base.c_str(), out);
    }
    return 0;
}
