/*
 * shadowtest: conformance test of the timing shadow (src/core/shadow.c).
 *
 *   shadowtest ROM --movie FILE [--model M] [--frames N]
 *
 * 1. Runs the original game on the reference emulator for N frames and
 *    records the stream of events a perfect C translation would report to
 *    the shadow (branch outcomes, jump tables, hardware accesses, shared
 *    variable accesses, HALT), tagged with the interrupt nesting depth and
 *    the exact cycle of each hardware access. Plain RAM writes are recorded
 *    too, so the replay machine's memory follows the original's.
 * 2. Replays the stream into the shadow on a second machine: main-level
 *    events are fed in order, interrupt handlers are fed by a replay handler
 *    that the shadow calls when it dispatches the interrupt.
 * 3. Checks that every hardware access happens at the same cycle as in the
 *    original, and that the shadow accepts every event.
 *
 * Passing means the shadow reproduces the original's timing exactly for
 * everything the run exercised, independently of any translated game code.
 */
#include "../emu/gb.h"
#include "../../src/core/shadow.h"
#include "../../src/game/shared.h"
#include "../../src/util/movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t kind;          /* ShadowEventKind, or K_RAM for a plain RAM write */
    uint8_t depth;
    uint8_t value;
    uint8_t has_cycle;
    uint16_t addr;
    uint64_t cycle;
    uint64_t insn_cycle;   /* cycle at the start of the instruction that made the event */
} Rec;

#define K_RAM 100
#define K_DISPATCH 101
#define K_DONE 102      /* RAM write already applied */

static uint64_t cur_insn_cycle;
static Rec *recs;
static size_t nrecs, cap;
static GB *ref;
static Movie movie;
static long polls;

static Rec *add(int kind, uint16_t addr, uint8_t value, int depth)
{
    if (nrecs == cap) {
        cap = cap ? cap * 2 : 1 << 20;
        recs = realloc(recs, cap * sizeof *recs);
    }
    Rec *r = &recs[nrecs++];
    *r = (Rec){ (uint8_t)kind, (uint8_t)depth, value, 0, addr, 0, cur_insn_cycle };
    return r;
}

/* ---- recording ---- */

static int depth;
static uint16_t frame_sp[16];
static uint16_t prev_pc, prev_expected;
static uint8_t prev_op, prev_cb;
static int prev_valid, prev_branch;
static Rec *pending_write;      /* hardware/shared write waiting for its value */
static Rec *pending_read;       /* hardware read waiting for its cycle */

static int is_hw(uint16_t a)
{
    return (a >= 0x8000 && a < 0xA000) || (a >= 0xFE00 && a < 0xFEA0) ||
           (a >= 0xFF00 && a < 0xFF80) || a == 0xFFFF;
}

static uint8_t classes_of(int bank, uint16_t pc)
{
    for (int i = 0; i < shadow_access_table_size; i++)
        if (shadow_access_table[i].bank == bank && shadow_access_table[i].addr == pc)
            return shadow_access_table[i].classes;
    return ACCESS_RAM;
}

static int insn_len(uint8_t op)
{
    static const uint8_t len3[] = { 0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xC4, 0xCA, 0xCC, 0xCD,
                                    0xD2, 0xD4, 0xDA, 0xDC, 0xEA, 0xFA };
    for (size_t i = 0; i < sizeof len3; i++) if (op == len3[i]) return 3;
    if ((op & 0xC7) == 0x06 || op == 0x18 || (op & 0xE7) == 0x20 || (op & 0xC7) == 0xC6 ||
        op == 0xE0 || op == 0xF0 || op == 0xE8 || op == 0xF8 || op == 0xCB || op == 0x10)
        return 2;
    return 1;
}

/* Instructions after which the C code reports register B (reg_b). */
static int loads_b(uint8_t op, uint8_t cb)
{
    if (op >= 0x41 && op <= 0x47) return 1;
    if (op == 0x03 || op == 0x0B) return 1;
    return op == 0xCB && (cb == 0x10 || cb == 0x18);
}

static int is_cond_branch(uint8_t op)
{
    return (op & 0xE7) == 0x20 || (op & 0xE7) == 0xC2 || (op & 0xE7) == 0xC4 || (op & 0xE7) == 0xC0;
}

static int mem_operand(const Sm83 *cpu, uint8_t op, int *rd, int *wr, int *known)
{
    const Sm83Regs *r = &cpu->r;
    uint16_t pc = r->pc;
    uint16_t hl = (uint16_t)(r->h << 8 | r->l), bc = (uint16_t)(r->b << 8 | r->c), de = (uint16_t)(r->d << 8 | r->e);
    uint8_t n8 = machine_peek(cpu->m, (uint16_t)(pc + 1));
    uint16_t n16 = (uint16_t)(n8 | machine_peek(cpu->m, (uint16_t)(pc + 2)) << 8);
    *rd = 1, *wr = 0, *known = 0;
    switch (op) {
    case 0x02: *rd = 0, *wr = 1; return bc;
    case 0x12: *rd = 0, *wr = 1; return de;
    case 0x22: case 0x32: case 0x36: *rd = 0, *wr = 1; return hl;
    case 0x0A: return bc;
    case 0x1A: return de;
    case 0x2A: case 0x3A: return hl;
    case 0x34: case 0x35: *wr = 1; return hl;
    case 0xE0: *rd = 0, *wr = 1, *known = 1; return 0xFF00 | n8;
    case 0xF0: *known = 1; return 0xFF00 | n8;
    case 0xE2: *rd = 0, *wr = 1; return 0xFF00 | r->c;
    case 0xF2: return 0xFF00 | r->c;
    case 0xEA: *rd = 0, *wr = 1, *known = 1; return n16;
    case 0xFA: *known = 1; return n16;
    case 0xCB:
        if ((n8 & 7) != 6) return -1;
        *wr = (n8 >> 6) != 1;
        return hl;
    }
    if (op >= 0x70 && op < 0x78 && op != 0x76) { *rd = 0, *wr = 1; return hl; }
    if (op >= 0x40 && op < 0xC0 && (op & 7) == 6 && op != 0x76) return hl;
    return -1;
}

/* Event kind for an access, or -1 for none (plain RAM). */
static int access_kind(uint16_t a, int write, int known, uint8_t classes)
{
    int mixed = !known && (classes & ACCESS_RAM) && (classes & (ACCESS_HW | ACCESS_SHARED));
    if (!known && !(classes & (ACCESS_HW | ACCESS_SHARED)))
        return -1;
    if (!known && !(classes & ACCESS_HW) && depth > 0)
        return -1;                        /* shared variable inside a handler */
    if (mixed && !(depth > 0 && !(classes & ACCESS_HW)))
        return write ? SEV_BUS_WRITE : SEV_BUS_READ;
    if (a < 0x8000) return write ? SEV_BANK : -1;
    if (a >= 0x8000 && a < 0xA000) return write ? SEV_VRAM_WRITE : SEV_VRAM_READ;
    if (a >= 0xFE00 && a < 0xFEA0) return write ? SEV_OAM_WRITE : -1;
    if (is_hw(a)) return write ? SEV_IO_WRITE : SEV_IO_READ;
    if (depth == 0 && is_shared_address(a)) return write ? SEV_SHARED_WRITE : SEV_SHARED_READ;
    return -1;
}

static uint64_t prev_insn_cycle;
static int rehalt_pc = -1;
static const int rehalt_depth_main = 0;

static void on_trace(Sm83 *cpu, void *user)
{
    (void)user;
    uint16_t pc = cpu->r.pc;
    cur_insn_cycle = cpu->m->cycles;
    if (pending_write) pending_write = NULL;
    /* interrupt dispatch between the previous instruction and this one? */
    int dispatched = prev_valid && (pc == 0x40 || pc == 0x48 || pc == 0x50 || pc == 0x58 || pc == 0x60) &&
                     pc != prev_expected &&
                     !(prev_op == 0xC3 && (machine_peek(cpu->m, (uint16_t)(prev_pc + 1)) |
                                           machine_peek(cpu->m, (uint16_t)(prev_pc + 2)) << 8) == pc);
    /* (the vblank vector at $40 jumps to $60, which is also the joypad vector) */
    uint16_t actual_next = pc;
    if (dispatched)
        actual_next = (uint16_t)(machine_peek(cpu->m, cpu->r.sp) | machine_peek(cpu->m, (uint16_t)(cpu->r.sp + 1)) << 8);
    if (prev_valid && prev_branch) {
        int len = insn_len(prev_op);
        uint64_t save = cur_insn_cycle;
        cur_insn_cycle = prev_insn_cycle;
        add(SEV_BRANCH, prev_pc, actual_next != (uint16_t)(prev_pc + len), depth);
        cur_insn_cycle = save;
    }
    if (prev_valid && (loads_b(prev_op, prev_cb) ||
                       (prev_op == 0xC1 && shadow_pop_loads_b(prev_pc >= 0x4000 ? cpu->m->rom_bank : 0, prev_pc)))) {
        uint64_t save = cur_insn_cycle;
        cur_insn_cycle = prev_insn_cycle;
        add(SEV_REG_B, prev_pc, cpu->r.b, depth);
        cur_insn_cycle = save;
    }
    /* returning from a handler pops its frame (SP before a new dispatch's push) */
    uint16_t sp_before = dispatched ? (uint16_t)(cpu->r.sp + 2) : cpu->r.sp;
    while (depth > 0 && sp_before > frame_sp[depth - 1])
        depth--;
    if (dispatched && prev_op == 0x76 && actual_next == prev_pc)
        rehalt_pc = prev_pc;      /* HALT with an interrupt pending: it runs again */
    if (dispatched) {
        frame_sp[depth++] = cpu->r.sp;
        Rec *d = add(K_DISPATCH, pc, cpu->r.b, depth);   /* B of the interrupted code */
        d->cycle = cpu->m->cycles;
        d->has_cycle = 1;
    }

    uint8_t op = machine_peek(cpu->m, pc);
    int bank = pc >= 0x4000 && pc < 0x8000 ? cpu->m->rom_bank : 0;
    if (op == 0xEF)
        add(SEV_TABLE, pc, cpu->r.a, depth);
    else if (op == 0xE9 && pc != 0x0033)
        add(SEV_JUMP, (uint16_t)(cpu->r.h << 8 | cpu->r.l), 0, depth);
    else if (op == 0x76) {
        /* the C code calls halt() once, also when the original runs the
           HALT again after an interrupt that was already pending */
        if (rehalt_pc == pc && depth == rehalt_depth_main)
            rehalt_pc = -1;
        else
            add(SEV_HALT, pc, 0, depth);
    }
    else {
        int rd, wr, known;
        int a = mem_operand(cpu, op, &rd, &wr, &known);
        if (a >= 0) {
            uint8_t cls = known ? 0 : classes_of(bank, pc);
            if (rd) {
                int k = access_kind((uint16_t)a, 0, known, cls);
                if (k >= 0) {
                    Rec *r = add(k, (uint16_t)a, 0, depth);
                    if (k == SEV_IO_READ) pending_read = r;
                }
            }
            if (wr) {
                int k = access_kind((uint16_t)a, 1, known, cls);
                if (k >= 0) {
                    Rec *r = add(k, (uint16_t)a, cpu->r.a, depth);
                    pending_write = r;
                }
            }
        }
    }
    prev_pc = pc;
    prev_insn_cycle = cur_insn_cycle;
    prev_op = op;
    prev_cb = machine_peek(cpu->m, (uint16_t)(pc + 1));
    prev_expected = (uint16_t)(pc + insn_len(op));
    prev_branch = is_cond_branch(op);
    prev_valid = 1;

    if (pc == 0x47F2 && bank == 3) {
        cpu->m->buttons = movie_get(&movie, (size_t)polls);
        polls++;
    }
}

static void on_write(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)u;
    if (pending_write && pending_write->addr == addr) {
        pending_write->value = v;
        pending_write->cycle = m->cycles;
        pending_write->has_cycle = 1;
        pending_write = NULL;
        return;
    }
    /* The stack (SP starts at $CFFF) is the shadow's own business: the C
       code never writes it, so neither does the replay. */
    if (addr >= 0xCE00 && addr < 0xD000)
        return;
    if ((addr >= 0xC000 && addr < 0xE000) || (addr >= 0xFF80 && addr < 0xFFFF))
        add(K_RAM, addr, v, depth);
}

static void on_io_read(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)u; (void)v;
    if (pending_read && pending_read->addr == addr) {
        pending_read->cycle = m->cycles;
        pending_read->has_cycle = 1;
        pending_read = NULL;
    }
}

/* ---- replay ---- */

static Machine rm;
static size_t qpos;
static int replay_depth;
static size_t failures;
static size_t checked;
static const char *kind_name(int k)
{
    static const char *const n[] = { "branch", "table", "jump", "io-read", "io-write", "vram-read",
                                     "vram-write", "oam-write", "bank", "shared-read", "shared-write",
                                     "bus-read", "bus-write", "halt", "register-b" };
    return k == K_RAM ? "ram-write" : k == K_DISPATCH ? "dispatch" : k == K_DONE ? "ram-write(done)" : n[k];
}

static void replay_level(int level);
static void dump_ring(void);

static size_t in_flight = (size_t)-1;   /* record being fed right now */

static void replay_isr(void)
{
    int saved_depth = replay_depth;
    while (qpos < nrecs && (qpos == in_flight || recs[qpos].kind == K_DONE))
        qpos++;              /* the in-flight event / RAM writes already applied */
    /* A handler that interrupts another one while that one is finishing
       (after its C code returned) is still nested in it in the original. */
    replay_depth++;
    if (qpos < nrecs && recs[qpos].kind == K_DISPATCH && recs[qpos].depth > replay_depth)
        replay_depth = recs[qpos].depth;
    if (qpos >= nrecs) {
        printf("replayed all %zu events, %zu hardware access/dispatch cycles checked, %zu mismatches\n",
               nrecs, checked, failures);
        exit(failures ? 1 : 0);
    }
    if (recs[qpos].kind != K_DISPATCH || recs[qpos].depth != replay_depth) {
        printf("the shadow dispatched an interrupt at pc %04X cycle %llu (depth %d) that the original did not take here;"
               " next record: %s depth %d cycle %llu\n", shadow_pc(), (unsigned long long)rm.cycles, replay_depth,
               qpos < nrecs ? kind_name(recs[qpos].kind) : "end", qpos < nrecs ? recs[qpos].depth : -1,
               qpos < nrecs ? (unsigned long long)recs[qpos].cycle : 0ull);
        for (size_t x = qpos; x < qpos + 5000 && x < nrecs; x++)
            if (recs[x].kind == K_DISPATCH) {
                printf("  the original's next dispatch: record %zu vector %04X cycle %llu (%+lld vs shadow)\n", x,
                       recs[x].addr, (unsigned long long)recs[x].cycle, (long long)(recs[x].cycle - rm.cycles));
                break;
            }
        dump_ring();
        for (size_t x = qpos > 30 ? qpos - 30 : 0; x < qpos + 6 && x < nrecs; x++)
            printf("  rec %zu depth %d %s %04X=%02X insn_cycle %llu cycle %llu\n", x, recs[x].depth,
                   kind_name(recs[x].kind), recs[x].addr, recs[x].value,
                   (unsigned long long)recs[x].insn_cycle, (unsigned long long)recs[x].cycle);
        {
            /* compare the hardware state with the original at this cycle */
            GB *g2 = gb_create(rm.model, rm.rom, rm.rom_size);
            long p2 = 0;
            while (g2->m.cycles < rm.cycles) {
                if (g2->cpu.r.pc == 0x47F2 && g2->m.rom_bank == 3 && !g2->cpu.r.halted)
                    g2->m.buttons = movie_get(&movie, (size_t)p2++);
                sm83_step(&g2->cpu);
            }
            printf("at cycle %llu/%llu: original LY=%d line_cycle=%d STAT=%02X LYC=%02X IF=%02X IE=%02X LCDC=%02X\n",
                   (unsigned long long)g2->m.cycles, (unsigned long long)rm.cycles, g2->m.ly, g2->m.line_cycle,
                   g2->m.io[0x41], g2->m.io[0x45], g2->m.io[0x0F], g2->m.ie, g2->m.io[0x40]);
            printf("                     shadow   LY=%d line_cycle=%d STAT=%02X LYC=%02X IF=%02X IE=%02X LCDC=%02X\n",
                   rm.ly, rm.line_cycle, rm.io[0x41], rm.io[0x45], rm.io[0x0F], rm.ie, rm.io[0x40]);
        }
        exit(1);
    }
    if (recs[qpos].value != shadow_b()) {
        if (failures++ < 10)
            printf("register B differs at dispatch record %zu (vector %04X): original %02X, shadow %02X\n",
                   qpos, recs[qpos].addr, recs[qpos].value, shadow_b());
        if (failures == 1) {
            dump_ring();
            for (size_t x = qpos > 40 ? qpos - 40 : 0; x <= qpos; x++)
                if (recs[x].kind != K_RAM && recs[x].kind != K_DONE)
                    printf("  rec %zu depth %d %s %04X=%02X\n", x, recs[x].depth, kind_name(recs[x].kind), recs[x].addr, recs[x].value);
        }
    }
    if (recs[qpos].addr != shadow_pc() || recs[qpos].cycle != rm.cycles) {
        if (failures++ < 10)
            printf("dispatch mismatch at record %zu: original vector %04X cycle %llu, shadow vector %04X cycle %llu\n",
                   qpos, recs[qpos].addr, (unsigned long long)recs[qpos].cycle, shadow_pc(),
                   (unsigned long long)rm.cycles);
    }
    checked++;
    qpos++;
    replay_level(replay_depth);
    replay_depth = saved_depth;
}

static uint16_t ring_pc[64];
static uint8_t ring_ime[64];
static unsigned ring_n;
static void trace_hook(uint16_t pc, uint8_t bank, int ime)
{
    (void)bank;
    ring_pc[ring_n % 64] = pc;
    ring_ime[ring_n % 64] = (uint8_t)ime;
    ring_n++;
}
static void dump_ring(void);
static void dump_ring_impl(void)
{
    printf("last instructions walked by the shadow (pc/ime):");
    for (unsigned i = ring_n > 40 ? ring_n - 40 : 0; i < ring_n; i++)
        printf(" %04X/%d", ring_pc[i % 64], ring_ime[i % 64]);
    printf("\n");
}

static void dump_ring(void) { dump_ring_impl(); }

static void apply_ram(size_t i)
{
    Rec *r = &recs[i];
    if (r->addr >= 0xFF80) rm.hram[r->addr - 0xFF80] = r->value;
    else rm.wram[r->addr - 0xC000] = r->value;
}

/* Feed event k to the shadow; records [first, k) belong to interrupt
   handlers the shadow must dispatch on the way (they are consumed by the
   nested replay_isr calls). */
static void feed(size_t k, size_t first, size_t expect)
{
    Rec *r = &recs[k];
    ShadowEvent ev = { (ShadowEventKind)r->kind, r->addr, r->value };
    qpos = first;
    size_t saved_flight = in_flight;
    in_flight = k;
    shadow_event(&ev);
    in_flight = saved_flight;
    if (qpos < expect) {
        printf("the original took an interrupt before event %zu (%s %04X, depth %d), the shadow did not "
               "(records %zu..%zu unconsumed)\n", k, kind_name(r->kind), r->addr, r->depth, qpos, k - 1);
        for (size_t x = first > 12 ? first - 12 : 0; x <= k + 1 && x < nrecs; x++)
            printf("  %zu depth %d %s %04X=%02X\n", x, recs[x].depth, kind_name(recs[x].kind), recs[x].addr, recs[x].value);
        printf("shadow pc %04X sp %04X ime %d cycles %llu\n", shadow_pc(), shadow_sp(), shadow_ime(), (unsigned long long)rm.cycles);
        dump_ring();
        exit(1);
    }
    int nested = qpos > k + 1;   /* an interrupt ran during the event itself */
    if (qpos < k + 1)
        qpos = k + 1;
    if (r->has_cycle && shadow_last_access_cycle() != r->cycle) {
        if (failures++ < 10)
            printf("cycle mismatch at event %zu (%s %04X depth %d): original %llu, shadow %llu (%+lld)\n",
                   k, kind_name(r->kind), r->addr, r->depth, (unsigned long long)r->cycle,
                   (unsigned long long)shadow_last_access_cycle(),
                   (long long)(shadow_last_access_cycle() - r->cycle));
    }
    checked += r->has_cycle;
    if (r->kind != K_DISPATCH && r->kind != SEV_HALT && !nested && shadow_insn_start_cycle() != r->insn_cycle) {
        if (failures++ < 10)
            printf("instruction timing differs at event %zu (%s %04X depth %d): original started it at %llu, shadow at %llu (%+lld)\n",
                   k, kind_name(r->kind), r->addr, r->depth, (unsigned long long)r->insn_cycle,
                   (unsigned long long)shadow_insn_start_cycle(),
                   (long long)(shadow_insn_start_cycle() - r->insn_cycle));
        if (failures >= 10) { dump_ring(); exit(1); }
    }
}

/* Feed all events of this nesting level; deeper ones are consumed by the
   handlers the shadow dispatches while processing them. */
static void replay_level(int level)
{
    while (qpos < nrecs && recs[qpos].depth >= level) {
        size_t i = qpos;
        if (recs[i].depth == level && recs[i].kind == K_DISPATCH)
            return;        /* the next handler at this level starts: ours is done */
        if (recs[i].depth == level && recs[i].kind == K_DONE) {
            qpos++;
            continue;
        }
        if (recs[i].depth == level) {
            if (recs[i].kind == K_RAM) {
                apply_ram(i);
                qpos++;
            } else {
                feed(i, i + 1, i + 1);
            }
            continue;
        }
        /* Interrupt handlers run before our next event (possibly several,
           with RAM writes of ours in between, which the C code would have
           made before reaching the event anyway). */
        size_t k = i, last_deeper = i;
        while (k < nrecs) {
            if (recs[k].depth > level) { last_deeper = k; k++; continue; }
            if (recs[k].depth == level && (recs[k].kind == K_RAM || recs[k].kind == K_DONE)) { k++; continue; }
            break;
        }
        if (k >= nrecs || recs[k].depth != level || recs[k].kind == K_DISPATCH)
            return;        /* they fire while our own handler is finishing */
        for (size_t r = i; r < k; r++)
            if (recs[r].depth == level && recs[r].kind == K_RAM) {
                apply_ram(r);
                recs[r].kind = K_DONE;
            }
        feed(k, i, last_deeper + 1);
    }
}

static void fail_handler(const char *msg)
{
    printf("shadow failure after %zu/%zu events: %s\n", qpos, nrecs, msg);
    for (size_t i = qpos > 8 ? qpos - 8 : 0; i < qpos + 4 && i < nrecs; i++)
        printf("  %s%zu depth %d %s %04X=%02X\n", i + 1 == qpos ? ">" : " ", i, recs[i].depth,
               kind_name(recs[i].kind), recs[i].addr, recs[i].value);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: shadowtest ROM --movie FILE [--model M] [--frames N]\n");
        return 2;
    }
    MachineModel model = MODEL_DMG;
    long frames = 3000;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie_load(&movie, argv[++i]);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc) model = (MachineModel)machine_model_from_name(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atol(argv[++i]);
    }
    static uint8_t rom[0x10000];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);

    ref = gb_create(model, rom, n);
    ref->cpu.on_trace = on_trace;
    ref->m.hooks.on_mem_write = on_write;
    ref->m.hooks.on_io_write = on_write;
    ref->m.hooks.on_io_read = on_io_read;
    for (long i = 0; i < frames; i++)
        gb_run_frame(ref);
    printf("recorded %zu events over %ld frames (%ld polls)\n", nrecs, frames, polls);

    machine_init(&rm, model, rom, n);
    shadow_init(&rm);
    shadow_fail = fail_handler;
    shadow_trace = trace_hook;
    for (uint16_t v = 0x40; v <= 0x60; v += 8)
        shadow_set_isr(v, replay_isr);
    replay_depth = 0;
    replay_level(0);
    printf("replayed %zu events, %zu hardware access cycles checked, %zu mismatches\n",
           qpos, checked, failures);
    return failures ? 1 : 0;
}
