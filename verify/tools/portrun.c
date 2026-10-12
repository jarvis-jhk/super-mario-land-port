/*
 * portrun: run the PORT headless on an input movie and write a per-poll log
 * (verify/common/polllog.h) for comparison with the original
 * (build/emurun ... --poll-log, tools/compare_polls.py).
 *
 *   portrun ROM --movie FILE --poll-log FILE [--model M] [--frames N] [--dump N FILE]
 *
 * In hybrid builds, SML_HYBRID_ONLY=Name1,Name2 limits which translated
 * routines replace the original ones (to bisect a divergence).
 * SML_C_MAIN=1 also runs initialization and the endless frame loop in C.
 */
#include "../../src/core/core.h"
#include "../../src/util/movie.h"
#include "../common/polllog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Movie movie;
static FILE *log_file;
static long frames, max_frames;
static long dump_poll = -1;
static const char *dump_file;

static void on_frame(void *user)
{
    (void)user;
    core_machine()->apu.out_len = 0;
    /* stop like emurun does: after the frame in which poll movie+60 happened */
    if (++frames >= max_frames || core_poll_count() > (long)movie.count + 60)
        core_quit();
}

static uint8_t on_poll(void *user)
{
    (void)user;
    Machine *m = core_machine();
    long poll = core_poll_count();
    if (poll == dump_poll) {
        FILE *file = fopen(dump_file, "wb");
        if (!file) { perror(dump_file); exit(2); }
        fwrite(m->wram, 1, sizeof m->wram, file);
        fwrite(m->hram, 1, sizeof m->hram, file);
        fwrite(m->vram, 1, sizeof m->vram, file);
        fwrite(m->oam, 1, sizeof m->oam, file);
        fwrite(m->io, 1, sizeof m->io, file);
        fclose(file);
    }
    if (log_file)
        polllog_write_apu(log_file, poll, m->cycles, m->wram, m->hram, m->vram, m->oam, &m->apu);
    return movie_get(&movie, (size_t)poll);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: portrun ROM --movie FILE --poll-log FILE [--model M] [--frames N] [--dump N FILE]\n");
        return 2;
    }
    MachineModel model = MODEL_DMG;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie_load(&movie, argv[++i]);
        else if (!strcmp(argv[i], "--poll-log") && i + 1 < argc) log_file = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) model = (MachineModel)machine_model_from_name(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 2 < argc) {
            dump_poll = atol(argv[++i]); dump_file = argv[++i];
        }
    }
    if (!max_frames)
        max_frames = (long)movie.count + 600;
    static uint8_t rom[0x10000];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CoreCallbacks cb = { NULL, on_frame, on_poll };
    core_run(model, rom, n, &cb);
    if (log_file)
        fclose(log_file);
    fprintf(stderr, "frames=%ld polls=%ld\n", frames, core_poll_count());
    return 0;
}
