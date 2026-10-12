/* Runs a Blargg-style test ROM on the reference emulator and prints what the
   ROM sends over the serial port. Usage: testrom rom.gb [frames] */
#include "../emu/gb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char out[4096];
static int out_len;

static void on_io_write(Machine *m, uint16_t addr, uint8_t v, void *user)
{
    (void)user;
    if (addr == 0xFF02 && (v & 0x80) && out_len < (int)sizeof out - 1) {
        out[out_len++] = (char)m->io[0x01];
        m->io[0x02] &= 0x7F;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t rom[1 << 20];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    int frames = argc > 2 ? atoi(argv[2]) : 4000;
    GB *gb = gb_create(MODEL_DMG, rom, n);
    gb->m.hooks.on_io_write = on_io_write;
    for (int i = 0; i < frames; i++) {
        gb_run_frame(gb);
        if (strstr(out, "Passed") || strstr(out, "Failed")) break;
    }
    printf("%s\n", out);
    return strstr(out, "Passed") ? 0 : 1;
}
