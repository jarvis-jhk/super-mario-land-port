/*
 * gbref: run the original ROM on Gambatte, the independent reference.
 *
 * Gambatte (GPLv2, fetched into reference/gambatte, linked only into this
 * verification tool) is the emulator the console-verified Super Mario Land
 * TAS resyncs were made with, through BizHawk. This driver reproduces
 * BizHawk's "vblank driven" frame loop: every frame calls runFor() for at
 * most 35112 stereo samples (70224 CPU cycles) and stops early when a video
 * frame completes; the joypad state of movie frame i is what the game reads
 * during frame i.
 *
 *   gbref ROM --model dmg|cgb|agb [options]
 *     --frame-input FILE K   per-frame TAS input (tools/tas_convert.py);
 *                            movie frame i is used for our frame i-K
 *     --movie FILE           per-poll input (src/util/movie.h)
 *     --frames N             stop after N frames
 *     --record-polls FILE    write the input each joypad poll saw
 *     --shots DIR K          PNG every K polls
 *     --shot N FILE          PNG at poll N
 *     --dump N FILE          raw WRAM+HRAM+VRAM+OAM at poll N
 *     --trace-polls          print one line per poll (frame, cycle)
 *     --poll-log FILE        per-poll time + memory hashes (polllog.h)
 *     --trace FILE N         instruction trace (pc + registers) until poll N
 *     --coverage FILE        ROM access map, one byte per ROM byte: bit 0 opcode
 *                            executed, bit 1 operand, bit 2 read as data
 *                            (OR-ed into FILE if it exists)
 */
#include "gambatte.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "../common/png.h"
#include "../../src/util/movie.h"
}
#include "../common/polllog.h"

static const unsigned kReadJoypadBank = 3, kReadJoypadAddr = 0x47F2;

static gambatte::GB *g;
static unsigned current_buttons;
static std::vector<unsigned char> frame_input;
static long frame_input_offset;
static Movie poll_movie;
static bool use_poll_movie;
static Movie recorded;
static const char *record_file;
static long polls, frames;
static const char *shots_dir;
static long shots_every;
static std::vector<std::pair<long, std::string> > shots;
static long dump_poll = -1;
static const char *dump_file;
static bool trace_polls;
static uint32_t video[160 * 144];
static FILE *poll_log;
static uint64_t samples_done;
static FILE *trace_file;
static std::vector<unsigned char> coverage;

static void on_cdl(int32_t addr, int32_t type, int32_t flags)
{
    if (type == gambatte::eCDLog_AddrType_ROM && addr >= 0 && (size_t)addr < coverage.size())
        coverage[addr] |= (unsigned char)flags;
}
static long trace_until;

static void on_trace(void *p)
{
    if (polls >= trace_until)
        return;
    unsigned *r = (unsigned *)p;
    /* r: offset pc sp a b c d e f h l */
    fprintf(trace_file, "%llu %04X %04X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            (unsigned long long)((samples_done + r[0]) * 2), r[1], r[2], r[3], r[8], r[4], r[5], r[6], r[7], r[9], r[10]);
}   /* stereo samples (2 T-cycles each) of finished runFor calls */

static unsigned input_getter(void *) { return current_buttons; }

static void save_png(const char *path)
{
    /* Gambatte gives 0xRRGGBB pixels. Map them to shades by brightness so
       DMG and CGB-in-DMG-mode screenshots look alike. */
    static uint8_t shades[160 * 144];
    for (int i = 0; i < 160 * 144; i++) {
        uint32_t p = video[i];
        int lum = ((p >> 16 & 0xFF) * 3 + (p >> 8 & 0xFF) * 6 + (p & 0xFF)) / 10;
        shades[i] = lum > 0xC0 ? 0 : lum > 0x80 ? 1 : lum > 0x40 ? 2 : 3;
    }
    png_write_shades(path, shades, 160, 144, 1);
}

static void dump_state(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    unsigned char *data;
    int len;
    /* areas: 0 vram, 2 wram, 4 oam, 5 hram (see Memory::getMemoryArea) */
    /* CGB models have 32K WRAM / 16K VRAM; the game only uses the DMG part */
    const int areas[] = { 2, 5, 0, 4 };
    const int sizes[] = { 0x2000, 0x7F, 0x2000, 0xA0 };
    for (int i = 0; i < 4; i++)
        if (g->getMemoryArea(areas[i], &data, &len))
            fwrite(data, 1, len < sizes[i] ? len : sizes[i], f);
    fclose(f);
}

static void on_exec(int32_t addr, int64_t offset)
{
    if ((unsigned)addr != kReadJoypadAddr || g->getAddrBank(addr) != kReadJoypadBank)
        return;
    if (use_poll_movie)
        current_buttons = movie_get(&poll_movie, polls);
    if (trace_polls)
        printf("poll %ld frame %ld buttons %02X\n", polls, frames, current_buttons);
    if (record_file)
        movie_append(&recorded, (uint8_t)current_buttons);
    for (auto &s : shots)
        if (s.first == polls)
            save_png(s.second.c_str());
    if (shots_dir && polls % shots_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/poll_%06ld.png", shots_dir, polls);
        save_png(path);
    }
    if (polls == dump_poll)
        dump_state(dump_file);
    if (poll_log) {
        unsigned char *w, *h, *v, *o;
        int l;
        g->getMemoryArea(2, &w, &l);
        g->getMemoryArea(5, &h, &l);
        g->getMemoryArea(0, &v, &l);
        g->getMemoryArea(4, &o, &l);
        polllog_write(poll_log, polls, (samples_done + (uint64_t)offset) * 2, w, h, v, o);
    }
    polls++;
}

static void load_frame_input(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f))
        if (line[0] != '#' && line[0] != '\n')
            frame_input.push_back((unsigned char)strtol(line, NULL, 16));
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gbref ROM --model dmg|cgb|agb [options]\n");
        return 2;
    }
    std::string model = "dmg";
    const char *coverage_file = NULL;
    long max_frames = -1;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--model" && i + 1 < argc) model = argv[++i];
        else if (a == "--frame-input" && i + 2 < argc) {
            load_frame_input(argv[++i]);
            frame_input_offset = atol(argv[++i]);
        } else if (a == "--movie" && i + 1 < argc) {
            if (movie_load(&poll_movie, argv[++i])) { fprintf(stderr, "bad movie\n"); return 1; }
            use_poll_movie = true;
        } else if (a == "--frames" && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (a == "--record-polls" && i + 1 < argc) record_file = argv[++i];
        else if (a == "--shots" && i + 2 < argc) { shots_dir = argv[++i]; shots_every = atol(argv[++i]); }
        else if (a == "--shot" && i + 2 < argc) { long n = atol(argv[i + 1]); shots.push_back(std::make_pair(n, std::string(argv[i + 2]))); i += 2; }
        else if (a == "--dump" && i + 2 < argc) { dump_poll = atol(argv[++i]); dump_file = argv[++i]; }
        else if (a == "--trace-polls") trace_polls = true;
        else if (a == "--poll-log" && i + 1 < argc) poll_log = fopen(argv[++i], "w");
        else if (a == "--coverage" && i + 1 < argc) coverage_file = argv[++i];
        else if (a == "--trace" && i + 2 < argc) { trace_file = fopen(argv[++i], "w"); trace_until = atol(argv[++i]); }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (max_frames < 0)
        max_frames = use_poll_movie ? (long)poll_movie.count + 600
                                    : (long)frame_input.size() + frame_input_offset + 600;

    unsigned flags = gambatte::GB::NO_BIOS | gambatte::GB::READONLY_SAV;
    if (model == "cgb") flags |= gambatte::GB::CGB_MODE;
    else if (model == "agb") flags |= gambatte::GB::CGB_MODE | gambatte::GB::GBA_FLAG;
    else if (model != "dmg") { fprintf(stderr, "unknown model\n"); return 2; }

    g = new gambatte::GB();
    if (g->load(argv[1], flags) != 0) { fprintf(stderr, "cannot load ROM\n"); return 1; }
    g->setInputGetter(input_getter, NULL);
    g->setExecCallback(on_exec);
    if (trace_file)
        g->setTraceCallback(on_trace);
    if (coverage_file) {
        coverage.assign(0x10000, 0);
        if (FILE *c = fopen(coverage_file, "rb")) {
            size_t got = fread(coverage.data(), 1, coverage.size(), c);
            (void)got;
            fclose(c);
        }
        g->setCDCallback(on_cdl);
    }

    static uint32_t audio[(35112 + 2064) * 2];
    for (frames = 0; frames < max_frames; frames++) {
        if (!use_poll_movie) {
            long i = frames + frame_input_offset;
            current_buttons = (i >= 0 && i < (long)frame_input.size()) ? frame_input[i] : 0;
        }
        std::size_t samples = 35112;
        g->runFor(video, 160, audio, samples);
        samples_done += samples;
    }
    if (poll_log)
        fclose(poll_log);
    if (record_file)
        movie_save(&recorded, record_file);
    if (coverage_file) {
        FILE *c = fopen(coverage_file, "wb");
        fwrite(coverage.data(), 1, coverage.size(), c);
        fclose(c);
    }
    fprintf(stderr, "frames=%ld polls=%ld world=%02X state=%02X score=%02X%02X%02X\n", frames, polls,
            g->externalRead(0xFFB4), g->externalRead(0xFFB3), g->externalRead(0xC0A2),
            g->externalRead(0xC0A1), g->externalRead(0xC0A0));
    delete g;
    return 0;
}
