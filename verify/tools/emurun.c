/*
 * emurun: run the ORIGINAL Super Mario Land ROM in the reference emulator.
 *
 *   emurun ROM [options]
 *     --model M           dmg (default), cgb or agb
 *     --movie FILE        joypad input, one entry per poll (see src/util/movie.h)
 *     --polls N           stop after N joypad polls (default: movie length + 60)
 *     --shot N FILE       save a PNG of the screen at poll N (repeatable)
 *     --shots DIR K       save DIR/poll_NNNNNN.png every K polls
 *     --coverage FILE     write the ROM access map (see gb.h) when done
 *     --log-div           print every read of the DIV register (used as RNG)
 *     --log-timing        print per-poll timing: emulator frames per poll,
 *                         cycle offset of the poll inside the frame
 *     --dump N FILE       dump WRAM+HRAM+VRAM+OAM+IO at poll N (raw)
 *     --watch ADDR        log every CPU write to ADDR (hex; repeatable) with
 *                         poll, LY, cycle in line, PC
 *     --frame-input FILE K  TAS-style input: one hex byte per emulator frame
 *                         (tools/tas_convert.py); frame i of the file is
 *                         applied during emulator frame i+K
 *     --frames N          stop after N emulator frames
 *     --record-polls FILE write the input seen by each poll as a movie
 *     --poll-log FILE     per-poll time + memory hashes (verify/common/polllog.h)
 *     --trace FILE N      instruction trace (pc + registers) until poll N
 *     --callgraph FILE    append control-flow edges (calls, rst, jp hl targets,
 *                         interrupts) with counts; merged across runs
 *     --access-table FILE classify every instruction's memory operand accesses
 *                         (RAM / hardware / shared variable) and merge them
 *                         into the C table FILE (src/core/access_table.c)
 *     --shared-report     list RAM addresses accessed both by interrupt handlers
 *                         and by main code running with interrupts enabled
 *                         (a vblank that does not return to the main loop's
 *                         halt, i.e. a lag frame, counts as interrupting main
 *                         code; its OAM DMA counts as a read of C000-C09F)
 *     --vram-report       count VRAM writes made while the LCD is drawing
 *                         (not in vblank), grouped by PC
 *
 * Polls are counted at the entry of ReadJoypad (bank 3, $47F2); the movie
 * entry for poll i is applied just before poll i reads the joypad.
 */
#include "../emu/gb.h"
#include "../common/png.h"
#include "../../src/util/movie.h"
#include "../common/polllog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_JOYPAD_BANK 3
#define READ_JOYPAD_ADDR 0x47F2

typedef struct { long poll; const char *file; } Shot;

static GB *G;
static FILE *poll_log;
static Movie movie;
static long poll_count;
static FILE *trace_file;
static long trace_until;

/* ---- call graph ---- */
typedef struct { uint8_t kind, from_bank, to_bank; uint16_t from, to; long count; } Edge;
static Edge *edges;
static size_t nedges, edges_cap;
static const char *callgraph_file;
static uint8_t prev_op;
static uint16_t prev_pc;
static uint8_t prev_bank;
static int prev_valid;
static const char *const edge_kinds[] = { "call", "rst", "jphl", "irq", "jp" };

static void add_edge(int kind, int fb, uint16_t from, int tb, uint16_t to)
{
    for (size_t i = 0; i < nedges; i++)
        if (edges[i].kind == kind && edges[i].from == from && edges[i].from_bank == fb &&
            edges[i].to == to && edges[i].to_bank == tb) {
            edges[i].count++;
            return;
        }
    if (nedges == edges_cap) {
        edges_cap = edges_cap ? edges_cap * 2 : 1024;
        edges = realloc(edges, edges_cap * sizeof *edges);
    }
    edges[nedges++] = (Edge){ (uint8_t)kind, (uint8_t)fb, (uint8_t)tb, from, to, 1 };
}

static void callgraph_step(Sm83 *cpu)
{
    uint16_t pc = cpu->r.pc;
    int bank = pc >= 0x4000 && pc < 0x8000 ? cpu->m->rom_bank : 0;
    if (prev_valid) {
        uint8_t op = prev_op;
        uint16_t next_seq = prev_pc + 1;
        int len = (op == 0xCD || (op & 0xE7) == 0xC4 || op == 0xC3 || (op & 0xE7) == 0xC2) ? 3 :
                  (op == 0x18 || (op & 0xE7) == 0x20) ? 2 : 1;
        next_seq = (uint16_t)(prev_pc + len);
        if ((op == 0xCD || (op & 0xE7) == 0xC4) && pc != next_seq)
            add_edge(0, prev_bank, prev_pc, bank, pc);
        else if ((op & 0xC7) == 0xC7)
            add_edge(1, prev_bank, prev_pc, bank, pc);
        else if (op == 0xE9)
            add_edge(2, prev_bank, prev_pc, bank, pc);
        else if ((op == 0xC3 || (op & 0xE7) == 0xC2) && pc != next_seq)
            add_edge(4, prev_bank, prev_pc, bank, pc);
        else if (pc == 0x40 || pc == 0x48 || pc == 0x50 || pc == 0x58 || pc == 0x60) {
            /* an interrupt was dispatched between the previous instruction and this one */
            int seq = (op == 0x76) ? 1 : 0;
            (void)seq;
            add_edge(3, prev_bank, prev_pc, 0, pc);
        }
    }
    prev_pc = pc;
    prev_bank = (uint8_t)bank;
    prev_op = machine_peek(cpu->m, pc);
    prev_valid = 1;
}

/* ---- shared variable analysis ---- */
static int shared_report;
/* contexts: 0 main IME=1, 1 main IME=0, 2 vblank isr, 3 stat isr, 4 timer isr,
   5 vblank isr in a lag frame (interrupting main code, not the halt at 0296) */
enum { NCTX = 6 };
static uint32_t ctx_access[0x10000][NCTX];
static int isr_stack[8], isr_depth;

static int mem_operand(const Sm83 *cpu, uint8_t op, int *is_write)
{
    const Sm83Regs *r = &cpu->r;
    uint16_t pc = r->pc;
    uint16_t hl = (uint16_t)(r->h << 8 | r->l), bc = (uint16_t)(r->b << 8 | r->c), de = (uint16_t)(r->d << 8 | r->e);
    uint8_t n8 = machine_peek(cpu->m, (uint16_t)(pc + 1));
    uint16_t n16 = (uint16_t)(n8 | machine_peek(cpu->m, (uint16_t)(pc + 2)) << 8);
    *is_write = 0;
    switch (op) {
    case 0x02: *is_write = 1; return bc;
    case 0x12: *is_write = 1; return de;
    case 0x22: case 0x32: case 0x36: *is_write = 1; return hl;
    case 0x0A: return bc;
    case 0x1A: return de;
    case 0x2A: case 0x3A: return hl;
    case 0x34: case 0x35: *is_write = 1; return hl;
    case 0xE0: *is_write = 1; return 0xFF00 | n8;
    case 0xF0: return 0xFF00 | n8;
    case 0xE2: *is_write = 1; return 0xFF00 | r->c;
    case 0xF2: return 0xFF00 | r->c;
    case 0xEA: *is_write = 1; return n16;
    case 0xFA: return n16;
    case 0xCB: if ((n8 & 7) == 6) { *is_write = (n8 >> 6) != 1; return hl; } return -1;
    }
    if (op >= 0x70 && op < 0x78 && op != 0x76) { *is_write = 1; return hl; }
    if (op >= 0x40 && op < 0xC0 && (op & 7) == 6 && op != 0x76) return hl;
    return -1;
}

/* ---- access classification ---- */
static const char *access_file;
static uint8_t access_classes[4][0x8000];       /* [bank][pc & 0x7FFF] */
static int is_shared_addr(uint16_t a);

static void access_step(Sm83 *cpu)
{
    uint16_t pc = cpu->r.pc;
    if (pc >= 0x8000) return;
    uint8_t op = machine_peek(cpu->m, pc);
    int w;
    int a = mem_operand(cpu, op, &w);
    if (a < 0) return;
    int bank = pc < 0x4000 ? 0 : cpu->m->rom_bank;
    uint8_t cls;
    if (a < 0x8000 || (a >= 0x8000 && a < 0xA000) || (a >= 0xFE00 && a < 0xFEA0) ||
        (a >= 0xFF00 && a < 0xFF80) || a == 0xFFFF)
        cls = 2;                                   /* hardware */
    else if (is_shared_addr((uint16_t)a))
        cls = 4;                                   /* shared variable */
    else
        cls = 1;                                   /* plain RAM */
    if (a < 0x8000 && !w)
        cls = 1;                                   /* ROM data read */
    access_classes[bank][pc & 0x7FFF] |= cls;
}

#include "../../src/game/shared.h"
static int is_shared_addr(uint16_t a) { return is_shared_address(a); }

static void shared_step(Sm83 *cpu)
{
    uint16_t pc = cpu->r.pc;
    if (pc == 0x40 || pc == 0x48 || pc == 0x50) {
        uint16_t ret = (uint16_t)(machine_peek(cpu->m, cpu->r.sp) | machine_peek(cpu->m, (uint16_t)(cpu->r.sp + 1)) << 8);
        int ctx = pc == 0x48 ? 3 : pc == 0x50 ? 4 : isr_depth == 0 && ret != 0x0297 ? 5 : 2;
        if (isr_depth < 8) isr_stack[isr_depth++] = ctx;
    }
    if (pc == 0x006D && isr_depth)                /* call hDMARoutine: OAM DMA from C000 */
        for (int a = 0xC000; a < 0xC0A0; a++)
            ctx_access[a][isr_stack[isr_depth - 1]] |= 1u;
    uint8_t op = machine_peek(cpu->m, pc);
    int w;
    int a = mem_operand(cpu, op, &w);
    if (a >= 0xC000 && a < 0xFFFF && !(a >= 0xE000 && a < 0xFF80)) {
        int ctx = isr_depth ? isr_stack[isr_depth - 1] : (cpu->r.ime ? 0 : 1);
        ctx_access[a][ctx] |= w ? 2u : 1u;
    }
    if (op == 0xD9 && isr_depth)
        isr_depth--;
}

static void on_trace(Sm83 *cpu, void *user)
{
    (void)user;
    if (access_file)
        access_step(cpu);
    if (shared_report)
        shared_step(cpu);
    if ((shared_report || access_file) && !trace_file && !callgraph_file)
        return;
    if (callgraph_file) {
        callgraph_step(cpu);
        if (!trace_file)
            return;
    }
    if (poll_count >= trace_until)
        return;
    Sm83Regs *r = &cpu->r;
    fprintf(trace_file, "%llu %04X %04X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            (unsigned long long)cpu->m->cycles, r->pc, r->sp, r->a, r->f, r->b, r->c, r->d, r->e, r->h, r->l);
}

static long max_polls = -1;
static Shot shots[256];
static int nshots;
static const char *shots_dir;
static long shots_every;
static int log_div, log_timing;
static long dump_poll = -1;
static const char *dump_file;
static uint64_t last_vblank_cycle, last_poll_frame;
static uint8_t watched[0x10000];
static int vram_report;
static uint8_t *frame_input;
static long frame_input_len, frame_input_offset, max_frames = -1;
static const char *record_file;
static Movie recorded;
static long vram_busy_writes[4][0x8000];   /* [bank][pc & 0x7FFF] */

static void log_write(GB *gb, uint16_t addr, uint8_t v)
{
    if (watched[addr])
        printf("w poll=%ld ly=%d lc=%d pc=%02X:%04X addr=%04X val=%02X\n", poll_count, gb->m.ly,
               gb->m.line_cycle, (gb->cpu.r.pc < 0x4000 ? 0 : gb->m.rom_bank), gb->cpu.r.pc, addr, v);
    if (vram_report && addr >= 0x8000 && addr < 0xA000 && gb->m.lcd_on && gb->m.ly < 144)
        vram_busy_writes[(gb->cpu.r.pc < 0x4000 ? 0 : gb->m.rom_bank)][gb->cpu.r.pc & 0x7FFF]++;
}

static void on_mem_write(Machine *mm, uint16_t addr, uint8_t v, void *user)
{
    (void)user; (void)mm;
    GB *gb = G;
    log_write(gb, addr, v);
}

static void dump_state(GB *gb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(gb->m.wram, 1, sizeof gb->m.wram, f);
    fwrite(gb->m.hram, 1, sizeof gb->m.hram, f);
    fwrite(gb->m.vram, 1, sizeof gb->m.vram, f);
    fwrite(gb->m.oam, 1, sizeof gb->m.oam, f);
    fwrite(gb->m.io, 1, sizeof gb->m.io, f);
    fclose(f);
}

static int on_exec(Sm83 *cpu, uint16_t pc, void *user)
{
    (void)user; (void)cpu;
    GB *gb = G;
    if (pc != READ_JOYPAD_ADDR || gb->m.rom_bank != READ_JOYPAD_BANK)
        return 0;
    if (log_timing)
        printf("poll %ld frames_since_last=%llu cycle_in_frame=%llu ly=%d\n", poll_count,
               (unsigned long long)(gb->m.frame_count - last_poll_frame),
               (unsigned long long)(gb->m.cycles - last_vblank_cycle), gb->m.ly);
    last_poll_frame = gb->m.frame_count;
    for (int i = 0; i < nshots; i++)
        if (shots[i].poll == poll_count)
            png_write_shades(shots[i].file, gb->m.framebuffer, LCD_W, LCD_H, 1);
    if (shots_dir && poll_count % shots_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/poll_%06ld.png", shots_dir, poll_count);
        png_write_shades(path, gb->m.framebuffer, LCD_W, LCD_H, 1);
    }
    if (poll_count == dump_poll)
        dump_state(gb, dump_file);
    if (poll_log)
        polllog_write_apu(poll_log, poll_count, gb->m.cycles, gb->m.wram, gb->m.hram, gb->m.vram, gb->m.oam, &gb->m.apu);
    if (!frame_input)
        gb->m.buttons = movie_get(&movie, (size_t)poll_count);
    if (record_file)
        movie_append(&recorded, gb->m.buttons);
    poll_count++;
    return 0;
}

static void on_io_write(Machine *mm, uint16_t addr, uint8_t v, void *user)
{
    (void)user; (void)mm;
    GB *gb = G;
    log_write(gb, addr, v);
}

static void on_io_read(Machine *mm, uint16_t addr, uint8_t v, void *user)
{
    (void)user; (void)mm;
    GB *gb = G;
    if (addr == 0xFF04 && log_div)
        printf("div poll=%ld pc=%02X:%04X value=%02X cycle_in_frame=%llu\n", poll_count,
               (gb->cpu.r.pc < 0x4000 ? 0 : gb->m.rom_bank), gb->cpu.r.pc, v,
               (unsigned long long)(gb->m.cycles - last_vblank_cycle));
}

static void on_frame(Machine *mm, void *user)
{
    (void)user; (void)mm;
    GB *gb = G;
    last_vblank_cycle = gb->m.cycles;
    if (frame_input) {
        long i = (long)gb->m.frame_count - frame_input_offset;
        gb->m.buttons = (i >= 0 && i < frame_input_len) ? frame_input[i] : 0;
    }
}

static void load_frame_input(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256];
    frame_input = malloc(1 << 20);
    while (fgets(line, sizeof line, f))
        if (line[0] != '#' && line[0] != '\n')
            frame_input[frame_input_len++] = (uint8_t)strtol(line, NULL, 16);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: emurun ROM [options] (see source header)\n");
        return 2;
    }
    const char *coverage_file = NULL;
    MachineModel model = MODEL_DMG;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            int mm = machine_model_from_name(argv[++i]);
            if (mm < 0) { fprintf(stderr, "unknown model\n"); return 2; }
            model = (MachineModel)mm;
        } else if (!strcmp(argv[i], "--movie") && i + 1 < argc) {
            if (movie_load(&movie, argv[++i])) { fprintf(stderr, "cannot load movie\n"); return 1; }
        } else if (!strcmp(argv[i], "--polls") && i + 1 < argc) max_polls = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && nshots < 256) {
            shots[nshots].poll = atol(argv[++i]);
            shots[nshots++].file = argv[++i];
        } else if (!strcmp(argv[i], "--shots") && i + 2 < argc) {
            shots_dir = argv[++i];
            shots_every = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) coverage_file = argv[++i];
        else if (!strcmp(argv[i], "--log-div")) log_div = 1;
        else if (!strcmp(argv[i], "--log-timing")) log_timing = 1;
        else if (!strcmp(argv[i], "--dump") && i + 2 < argc) {
            dump_poll = atol(argv[++i]);
            dump_file = argv[++i];
        } else if (!strcmp(argv[i], "--watch") && i + 1 < argc) {
            watched[strtol(argv[++i], NULL, 16) & 0xFFFF] = 1;
        } else if (!strcmp(argv[i], "--frame-input") && i + 2 < argc) {
            load_frame_input(argv[++i]);
            frame_input_offset = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            max_frames = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--record-polls") && i + 1 < argc) {
            record_file = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 2 < argc) {
            trace_file = fopen(argv[++i], "w");
            trace_until = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--access-table") && i + 1 < argc) {
            access_file = argv[++i];
        } else if (!strcmp(argv[i], "--shared-report")) {
            shared_report = 1;
        } else if (!strcmp(argv[i], "--callgraph") && i + 1 < argc) {
            callgraph_file = argv[++i];
        } else if (!strcmp(argv[i], "--poll-log") && i + 1 < argc) {
            poll_log = fopen(argv[++i], "w");
        } else if (!strcmp(argv[i], "--vram-report")) {
            vram_report = 1;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (max_polls < 0)
        max_polls = frame_input ? 1L << 40 : (long)movie.count + 60;
    if (max_frames < 0)
        max_frames = frame_input ? frame_input_len + frame_input_offset + 600 : 1L << 40;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static uint8_t rom[0x10000];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);

    GB *gb = gb_create(model, rom, n);
    G = gb;
    gb->cpu.on_exec = on_exec;
    gb->m.hooks.on_io_read = on_io_read;
    gb->m.hooks.on_frame = on_frame;
    gb->m.hooks.on_mem_write = on_mem_write;
    gb->m.hooks.on_io_write = on_io_write;
    sm83_set_exec_hook(&gb->cpu, READ_JOYPAD_BANK, READ_JOYPAD_ADDR, 1);
    if (trace_file || callgraph_file || shared_report || access_file)
        gb->cpu.on_trace = on_trace;
    if (callgraph_file) {
        /* merge with an existing file */
        FILE *c = fopen(callgraph_file, "r");
        if (c) {
            char kind[16];
            unsigned fb, from, tb, to;
            long count;
            while (fscanf(c, "%15s %x:%x %x:%x %ld", kind, &fb, &from, &tb, &to, &count) == 6)
                for (int k = 0; k < 5; k++)
                    if (!strcmp(kind, edge_kinds[k])) {
                        add_edge(k, (int)fb, (uint16_t)from, (int)tb, (uint16_t)to);
                        edges[nedges - 1].count += count - 1;
                    }
            fclose(c);
        }
    }
    if (coverage_file)
        gb->cpu.rom_access = calloc(1, n);

    while (poll_count <= max_polls && (long)gb->m.frame_count < max_frames)
        gb_run_frame(gb);
    if (record_file)
        movie_save(&recorded, record_file);
    if (poll_log)
        fclose(poll_log);
    if (callgraph_file) {
        FILE *c = fopen(callgraph_file, "w");
        for (size_t i = 0; i < nedges; i++)
            fprintf(c, "%s %02X:%04X %02X:%04X %ld\n", edge_kinds[edges[i].kind], edges[i].from_bank,
                    edges[i].from, edges[i].to_bank, edges[i].to, edges[i].count);
        fclose(c);
    }

    if (coverage_file) {
        FILE *c = fopen(coverage_file, "wb");
        fwrite(gb->cpu.rom_access, 1, n, c);
        fclose(c);
    }
    if (access_file) {
        /* merge with the existing table */
        FILE *in = fopen(access_file, "r");
        if (in) {
            char line[256];
            unsigned b, a, c;
            while (fgets(line, sizeof line, in))
                if (sscanf(line, " { 0x%x, 0x%x, %u }", &b, &a, &c) == 3 && b < 4)
                    access_classes[b][a & 0x7FFF] |= (uint8_t)c;
            fclose(in);
        }
        FILE *out = fopen(access_file, "w");
        int n = 0;
        fprintf(out, "/* Generated by `emurun --access-table` from runs of the original game:\n"
                     "   for every instruction with a memory operand, which kinds of memory it\n"
                     "   was seen to access (1 RAM, 2 hardware, 4 shared variable). Sorted. */\n"
                     "#include \"shadow.h\"\n\nconst AccessEntry shadow_access_table[] = {\n");
        for (int b = 0; b < 4; b++)
            for (int a = 0; a < 0x8000; a++) {
                if (!access_classes[b][a]) continue;
                int addr = a;
                if (b > 0 && a < 0x4000) continue;          /* bank 0 code is listed once */
                fprintf(out, "    { 0x%02X, 0x%04X, %u },\n", b, addr, access_classes[b][a]);
                n++;
            }
        fprintf(out, "};\nconst int shadow_access_table_size = %d;\n", n);
        fclose(out);
    }
    if (shared_report) {
        static const char *const ctx_names[NCTX] = { "main", "main-di", "vblank", "stat", "timer", "lag-vblank" };
        for (int a = 0xC000; a < 0x10000; a++) {
            uint32_t *c = ctx_access[a];
            int concurrent = c[3] | c[4];        /* can interrupt any code with IME=1 */
            if (!(c[0] && (concurrent || c[5])) && !(c[2] && concurrent))
                continue;
            printf("shared %04X", a);
            for (int k = 0; k < NCTX; k++)
                if (c[k])
                    printf(" %s:%s%s", ctx_names[k], c[k] & 1 ? "r" : "", c[k] & 2 ? "w" : "");
            printf("\n");
        }
    }
    if (vram_report)
        for (int b = 0; b < 4; b++)
            for (int pc = 0; pc < 0x8000; pc++)
                if (vram_busy_writes[b][pc])
                    printf("vram-write-while-drawing pc=%02X:%04X count=%ld\n", b, pc, vram_busy_writes[b][pc]);
    fprintf(stderr, "polls=%ld frames=%llu cycles=%llu\n", poll_count,
            (unsigned long long)gb->m.frame_count, (unsigned long long)gb->m.cycles);
    gb_destroy(gb);
    movie_free(&movie);
    return 0;
}
