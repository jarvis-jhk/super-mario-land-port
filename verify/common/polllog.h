/*
 * Per-poll state log, written by every runner (reference emulator, Gambatte
 * driver, port) so their runs can be compared poll by poll with
 * tools/compare_polls.py. One text line per joypad poll:
 *
 *   <poll> <time> <wram> <hram> <vram> <oam> [<apu>]
 *
 * time = T-cycles since the game started (boot ROM handed over), the four
 * memory fields are 64-bit FNV-1a hashes in hex. The stack area (CE00-CFFF,
 * the stack starts at CFFF) is left out of the WRAM hash: in the port the
 * timing model only keeps return addresses there, not saved registers.
 */
#ifndef VERIFY_POLLLOG_H
#define VERIFY_POLLLOG_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stddef.h>

static inline uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
    return h;
}

static inline void polllog_write_noeol(FILE *f, long poll, uint64_t time,
                                       const uint8_t *wram, const uint8_t *hram,
                                       const uint8_t *vram, const uint8_t *oam);
static inline void polllog_write(FILE *f, long poll, uint64_t time,
                                 const uint8_t *wram, const uint8_t *hram,
                                 const uint8_t *vram, const uint8_t *oam)
{
    uint8_t w[0x2000];
    for (int i = 0; i < 0x2000; i++)
        w[i] = (i >= 0x0E00 && i < 0x1000) ? 0 : wram[i];
    fprintf(f, "%ld %llu %016llx %016llx %016llx %016llx\n", poll, (unsigned long long)time,
            (unsigned long long)fnv1a(w, 0x2000), (unsigned long long)fnv1a(hram, 0x7F),
            (unsigned long long)fnv1a(vram, 0x2000), (unsigned long long)fnv1a(oam, 0xA0));
}

static inline void polllog_write_noeol(FILE *f, long poll, uint64_t time,
                                       const uint8_t *wram, const uint8_t *hram,
                                       const uint8_t *vram, const uint8_t *oam)
{
    uint8_t w[0x2000];
    for (int i = 0; i < 0x2000; i++)
        w[i] = (i >= 0x0E00 && i < 0x1000) ? 0 : wram[i];
    fprintf(f, "%ld %llu %016llx %016llx %016llx %016llx", poll, (unsigned long long)time,
            (unsigned long long)fnv1a(w, 0x2000), (unsigned long long)fnv1a(hram, 0x7F),
            (unsigned long long)fnv1a(vram, 0x2000), (unsigned long long)fnv1a(oam, 0xA0));
}
/* Variant with the sound chip state (registers and channel state, i.e. the
   Apu struct up to its output accumulators) as an extra column. */
#define polllog_write_apu(f, poll, time, wram, hram, vram, oam, apu)                    \
    do {                                                                                  \
        polllog_write_noeol((f), (poll), (time), (wram), (hram), (vram), (oam));          \
        fprintf((f), " %016llx\n", (unsigned long long)fnv1a((const uint8_t *)(apu),      \
                                                     offsetof(Apu, acc_l)));               \
    } while (0)

#endif
