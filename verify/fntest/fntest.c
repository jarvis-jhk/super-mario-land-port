/*
 * fntest: differential tests of translated functions (see fntest.h).
 *
 *   fntest ROM --movie FILE [--model M] [--samples N] [--polls N]
 *          [--cases FILE] [--coverage FILE] [--require-coverage FILE] [NAME...]
 *
 * Without names all registered functions are tested. N (default 300) caps
 * the number of snapshots per function; snapshots are spread over the run
 * (the first 50 entries, then every 7th).
 */
#include "fntest.h"
#include "../emu/gb.h"
#include "../../src/game/rt.h"
#include "../../src/game/shared.h"
#include "../../src/core/shadow.h"
#include "../../src/util/movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 200000

typedef enum { EV_BRANCH, EV_TABLE, EV_IO_R, EV_IO_W, EV_VRAM_R, EV_VRAM_W, EV_OAM_W, EV_BANK, EV_SHARED_R, EV_SHARED_W,
               EV_BUS_R, EV_BUS_W, EV_JUMP, EV_REG_B } EvKind;
typedef struct { uint8_t kind; uint16_t addr; uint8_t value; uint8_t blocked; uint8_t bank; } Event;  /* bank: of a branch site */
typedef struct { Event *ev; int n; } EventLog;

static const char *const ev_names[] = { "branch", "jumptable", "io-read", "io-write",
                                        "vram-read", "vram-write", "oam-write", "rom-bank",
                                        "shared-read", "shared-write", "bus-read", "bus-write",
                                        "computed-jump", "register-B" };

static void log_add(EventLog *l, int kind, uint16_t addr, uint8_t value)
{
    if (l->n < MAX_EVENTS)
        l->ev[l->n++] = (Event){ (uint8_t)kind, addr, value, 0, 0 };
}

/* ------------------------------------------------------------------ */
/* Original run                                                        */

static EventLog asm_log;
static uint16_t prev_pc;
static uint8_t prev_op, prev_cb, prev_a;
static int prev_valid;

/* Instructions after which the C code reports register B (reg_b): loads
   from another register or memory, inc/dec bc, rl/rr b. */
static int loads_b(uint8_t op, uint8_t cb)
{
    if (op >= 0x41 && op <= 0x47) return 1;
    if (op == 0x03 || op == 0x0B) return 1;
    return op == 0xCB && (cb == 0x10 || cb == 0x18);
}

static int branch_len(uint8_t op)
{
    if ((op & 0xE7) == 0x20) return 2;                        /* jr cc */
    if ((op & 0xE7) == 0xC2 || (op & 0xE7) == 0xC4) return 3; /* jp/call cc */
    if ((op & 0xE7) == 0xC0) return 1;                        /* ret cc */
    return 0;
}

/* RAM bytes the original routine read (inputs worth fuzzing) */
static uint16_t read_addrs[4096];
static int nreads;

static void note_read(uint16_t a)
{
    if (!((a >= 0xC000 && a < 0xE000) || (a >= 0xFF80 && a < 0xFFFF)))
        return;
    for (int i = 0; i < nreads; i++)
        if (read_addrs[i] == a) return;
    if (nreads < 4096) read_addrs[nreads++] = a;
}

/* Effective address of an instruction's memory operand (-1 if none), and
   whether it reads and/or writes it. */
static int mem_operand(const Sm83 *cpu, uint8_t op, uint16_t pc, int *rd, int *wr)
{
    const Sm83Regs *r = &cpu->r;
    uint16_t hl = (uint16_t)(r->h << 8 | r->l), bc = (uint16_t)(r->b << 8 | r->c), de = (uint16_t)(r->d << 8 | r->e);
    uint8_t n8 = machine_peek(cpu->m, (uint16_t)(pc + 1));
    uint16_t n16 = (uint16_t)(n8 | machine_peek(cpu->m, (uint16_t)(pc + 2)) << 8);
    *rd = 1, *wr = 0;
    switch (op) {
    case 0x02: *rd = 0, *wr = 1; return bc;
    case 0x12: *rd = 0, *wr = 1; return de;
    case 0x22: case 0x32: case 0x36: *rd = 0, *wr = 1; return hl;
    case 0x0A: return bc;
    case 0x1A: return de;
    case 0x2A: case 0x3A: return hl;
    case 0x34: case 0x35: *wr = 1; return hl;
    case 0xE0: *rd = 0, *wr = 1; return 0xFF00 | n8;
    case 0xF0: return 0xFF00 | n8;
    case 0xE2: *rd = 0, *wr = 1; return 0xFF00 | r->c;
    case 0xF2: return 0xFF00 | r->c;
    case 0xEA: *rd = 0, *wr = 1; return n16;
    case 0xFA: return n16;
    case 0xCB:
        if ((n8 & 7) != 6) return -1;
        *wr = (n8 >> 6) != 1;
        return hl;
    }
    if (op >= 0x70 && op < 0x78 && op != 0x76) { *rd = 0, *wr = 1; return hl; }
    if (op >= 0x40 && op < 0xC0 && (op & 7) == 6 && op != 0x76) return hl;
    return -1;
}

static int log_shared;      /* log accesses to shared variables (main-program code) */
static int cur_mixed;       /* the current instruction is a mixed RAM/hardware pointer access */
static int pending_bus_read = -1;

/* Access classes of an instruction (src/core/access_table.c). */
static uint8_t classes_of(int bank, uint16_t pc)
{
    int lo = 0, hi = shadow_access_table_size - 1;
    uint32_t key = (uint32_t)bank << 16 | pc;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const AccessEntry *e = &shadow_access_table[mid];
        uint32_t k = (uint32_t)e->bank << 16 | e->addr;
        if (k == key) return e->classes;
        if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    return ACCESS_RAM;
}

static int indirect_opcode(uint8_t op, uint8_t next)
{
    if (op == 0xE0 || op == 0xF0 || op == 0xEA || op == 0xFA || op == 0x08) return 0;
    if (op == 0xCB) return (next & 7) == 6;
    return 1;
}

/* VRAM/OAM writes are logged as attempts when the instruction starts (the
   LCD may block them); the write hook marks them as having happened. */
static int pending_write = -1;

static void asm_mem_write(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)m; (void)u;
    if (pending_write >= 0 && asm_log.ev[pending_write].addr == addr) {
        asm_log.ev[pending_write].blocked = 0;
        asm_log.ev[pending_write].value = v;
        pending_write = -1;
    }
}

/* Value a writing instruction stores (for blocked writes). */
static uint8_t store_value(const Sm83 *cpu, uint8_t op, uint16_t pc)
{
    const Sm83Regs *r = &cpu->r;
    const uint8_t regs[8] = { r->b, r->c, r->d, r->e, r->h, r->l, 0, r->a };
    if (op >= 0x70 && op < 0x78) return regs[op & 7];
    if (op == 0x36) return machine_peek(cpu->m, (uint16_t)(pc + 1));
    return r->a;
}

/* Finish the events of the previous instruction (now that it ran). */
static void asm_finish_previous(Sm83 *cpu)
{
    /* complete the value of a shared write logged at the previous instruction */
    if (asm_log.n && asm_log.ev[asm_log.n - 1].kind == EV_SHARED_W)
        asm_log.ev[asm_log.n - 1].value = machine_peek(cpu->m, asm_log.ev[asm_log.n - 1].addr);
    if (prev_valid) {
        int len = branch_len(prev_op);
        if (len)
        {
            log_add(&asm_log, EV_BRANCH, prev_pc, cpu->r.pc != (uint16_t)(prev_pc + len));
            asm_log.ev[asm_log.n - 1].bank = prev_pc >= 0x4000 ? (uint8_t)cpu->m->rom_bank : 0;
        }
        if (prev_op == 0xEF)
            log_add(&asm_log, EV_TABLE, prev_pc, prev_a);
        if (loads_b(prev_op, prev_cb) ||
            (prev_op == 0xC1 && shadow_pop_loads_b(prev_pc >= 0x4000 ? cpu->m->rom_bank : 0, prev_pc)))
            log_add(&asm_log, EV_REG_B, prev_pc, cpu->r.b);
    }
}

static void asm_trace(Sm83 *cpu, void *user)
{
    (void)user;
    asm_finish_previous(cpu);
    {
        uint8_t op = machine_peek(cpu->m, cpu->r.pc);
        int rd, wr;
        int a = mem_operand(cpu, op, cpu->r.pc, &rd, &wr);
        cur_mixed = 0;
        if (op == 0xE9 && cpu->r.pc != 0x0033)
            log_add(&asm_log, EV_JUMP, (uint16_t)(cpu->r.h << 8 | cpu->r.l), 0);
        if (a >= 0 && indirect_opcode(op, machine_peek(cpu->m, (uint16_t)(cpu->r.pc + 1)))) {
            int bank = cpu->r.pc >= 0x4000 && cpu->r.pc < 0x8000 ? cpu->m->rom_bank : 0;
            uint8_t cls = classes_of(bank, cpu->r.pc);
            int shared_only_in_isr = !(cls & ACCESS_HW) && !log_shared;
            if ((cls & ACCESS_RAM) && (cls & (ACCESS_HW | ACCESS_SHARED)) && !shared_only_in_isr)
                cur_mixed = 1;
        }
        if (a >= 0 && rd)
            note_read((uint16_t)a);
        if (cur_mixed) {
            if (rd) {
                log_add(&asm_log, EV_BUS_R, (uint16_t)a, machine_peek(cpu->m, (uint16_t)a));
                pending_bus_read = asm_log.n - 1;
            }
            if (wr) {
                log_add(&asm_log, EV_BUS_W, (uint16_t)a, store_value(cpu, op, cpu->r.pc));
                if ((a >= 0x8000 && a < 0xA000) || (a >= 0xFE00 && a < 0xFEA0)) {
                    asm_log.ev[asm_log.n - 1].blocked = 1;
                    pending_write = asm_log.n - 1;
                }
            }
        } else if (a >= 0 && wr && a < 0x8000) {
            /* write into ROM space: MBC register (bank switch) or a no-op */
            log_add(&asm_log, EV_BANK, (uint16_t)a, store_value(cpu, op, cpu->r.pc));
        } else if (a >= 0 && wr && ((a >= 0x8000 && a < 0xA000) || (a >= 0xFE00 && a < 0xFEA0))) {
            log_add(&asm_log, a < 0xA000 ? EV_VRAM_W : EV_OAM_W, (uint16_t)a, store_value(cpu, op, cpu->r.pc));
            asm_log.ev[asm_log.n - 1].blocked = 1;
            pending_write = asm_log.n - 1;
        }
        if (!cur_mixed && a >= 0 && log_shared && is_shared_address((uint16_t)a)) {
            if (rd) log_add(&asm_log, EV_SHARED_R, (uint16_t)a, machine_peek(cpu->m, (uint16_t)a));
            if (wr) log_add(&asm_log, EV_SHARED_W, (uint16_t)a, 0);   /* value filled in below */
        }
    }
    prev_pc = cpu->r.pc;
    prev_op = machine_peek(cpu->m, cpu->r.pc);
    prev_cb = machine_peek(cpu->m, (uint16_t)(cpu->r.pc + 1));
    prev_a = cpu->r.a;
    prev_valid = 1;
}

static void asm_io_read(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)m; (void)u;
    if (cur_mixed) {
        if (pending_bus_read >= 0) asm_log.ev[pending_bus_read].value = v;
        return;
    }
    log_add(&asm_log, EV_IO_R, addr, v);
}

static void asm_io_write(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)m; (void)u;
    if (cur_mixed)
        return;
    if (addr < 0xFF80 || addr == 0xFFFF)
        log_add(&asm_log, EV_IO_W, addr, v);
}


#ifdef MACHINE_HAS_MEM_READ_HOOK
static void asm_mem_read(Machine *m, uint16_t addr, uint8_t v, void *u)
{
    (void)m; (void)u;
    if (addr >= 0x8000 && addr < 0xA000)
        log_add(&asm_log, EV_VRAM_R, addr, v);
}
#endif

/* ------------------------------------------------------------------ */
/* C run: the runtime interface backed by the recording                */

Machine *g_hw;
static EventLog c_log;
static int replay_pos;              /* next asm event to answer reads from */
static int replay_error;
static char replay_msg[256];

static uint8_t replay_read(int kind, uint16_t addr)
{
    while (replay_pos < asm_log.n && asm_log.ev[replay_pos].kind != kind)
        replay_pos++;
    if (replay_pos >= asm_log.n || asm_log.ev[replay_pos].addr != addr) {
        if (!replay_error)
            snprintf(replay_msg, sizeof replay_msg, "C read %s %04X that the original did not make here",
                     ev_names[kind], addr);
        replay_error = 1;
        return 0xFF;
    }
    return asm_log.ev[replay_pos++].value;
}

bool taken(bool cond)
{
    log_add(&c_log, EV_BRANCH, 0, cond ? 1 : 0);
    return cond;
}

void loop_taken(unsigned iterations)
{
    for (unsigned i = 1; i < iterations; i++)
        taken(true);
    taken(false);
}

void jump_table(uint8_t index) { log_add(&c_log, EV_TABLE, 0, index); }
uint8_t reg_b(uint8_t value) { log_add(&c_log, EV_REG_B, 0, value); return value; }
uint8_t interrupted_b(void)
{
    snprintf(replay_msg, sizeof replay_msg, "interrupted_b() is for interrupt wrappers, not translated routines");
    replay_error = 1;
    return 0;
}
void computed_jump(uint16_t target) { log_add(&c_log, EV_JUMP, target, 0); }

uint8_t io_read(uint16_t addr)
{
    uint8_t v = replay_read(EV_IO_R, addr);
    log_add(&c_log, EV_IO_R, addr, v);
    return v;
}

void io_write(uint16_t addr, uint8_t value)
{
    log_add(&c_log, EV_IO_W, addr, value);
    machine_write(g_hw, addr, value);
    /* The game's DMA helper waits forty iterations before returning. The
       C replay does not tick hardware, so apply that completed transfer
       here; its source buffer is unchanged throughout the wait. */
    if (addr == rDMA) {
        for (unsigned i = 0; i < 0xA0; i++)
            g_hw->oam[i] = machine_peek(g_hw, (uint16_t)(value << 8 | i));
    }
}

uint8_t vram_read(uint16_t addr)
{
#ifdef MACHINE_HAS_MEM_READ_HOOK
    uint8_t v = replay_read(EV_VRAM_R, addr);
#else
    uint8_t v = g_hw->vram[addr - 0x8000];
#endif
    log_add(&c_log, EV_VRAM_R, addr, v);
    return v;
}

/* Whether the original's corresponding write got through (the LCD blocks
   VRAM during drawing and OAM during OAM scan + drawing). */
static int write_pos;
static int write_blocked(int kind, uint16_t addr)
{
    while (write_pos < asm_log.n && asm_log.ev[write_pos].kind != kind)
        write_pos++;
    if (write_pos >= asm_log.n || asm_log.ev[write_pos].addr != addr)
        return 0;     /* mismatch; reported by the event comparison */
    return asm_log.ev[write_pos++].blocked;
}

void vram_write(uint16_t addr, uint8_t value)
{
    int blocked = write_blocked(EV_VRAM_W, addr);
    log_add(&c_log, EV_VRAM_W, addr, value);
    c_log.ev[c_log.n - 1].blocked = (uint8_t)blocked;
    if (!blocked)
        g_hw->vram[addr - 0x8000] = value;
}

void oam_write(uint16_t addr, uint8_t value)
{
    int blocked = write_blocked(EV_OAM_W, addr);
    log_add(&c_log, EV_OAM_W, addr, value);
    c_log.ev[c_log.n - 1].blocked = (uint8_t)blocked;
    if (!blocked)
        g_hw->oam[addr - 0xFE00] = value;
}

void set_rom_bank(uint8_t bank)
{
    log_add(&c_log, EV_BANK, 0x2000, bank);
    machine_write(g_hw, 0x2000, bank);
}

void halt(void)
{
    snprintf(replay_msg, sizeof replay_msg, "C code executed halt() (not testable here)");
    replay_error = 1;
}

uint8_t shared_read(uint16_t addr)
{
    uint8_t v = machine_peek(g_hw, addr);
    if (!is_shared_address(addr)) {
        snprintf(replay_msg, sizeof replay_msg, "shared_read(%04X): not a shared variable", addr);
        replay_error = 1;
    }
    if (log_shared)
        log_add(&c_log, EV_SHARED_R, addr, v);
    return v;
}

void shared_write(uint16_t addr, uint8_t value)
{
    if (!is_shared_address(addr)) {
        snprintf(replay_msg, sizeof replay_msg, "shared_write(%04X): not a shared variable", addr);
        replay_error = 1;
    }
    if (log_shared)
        log_add(&c_log, EV_SHARED_W, addr, value);
    machine_write(g_hw, addr, value);
}

/* bus_*: the C code says "the original accesses this through a pointer
   that may hit hardware". Logged as bus events; the comparison accepts them
   for any access of the original at the same address. */
uint8_t bus_read(uint16_t addr);
uint8_t shared_or_ram_read(uint16_t addr)
{
    return log_shared ? bus_read(addr) : machine_peek(g_hw, addr);
}

uint8_t bus_read(uint16_t addr)
{
    uint8_t v;
    if ((addr >= 0xFF00 && addr < 0xFF80) || addr == 0xFFFF) {
        /* answer from the original's read (bus or io kind) */
        v = machine_peek(g_hw, addr);
        for (int i = replay_pos; i < asm_log.n; i++)
            if ((asm_log.ev[i].kind == EV_IO_R || asm_log.ev[i].kind == EV_BUS_R) && asm_log.ev[i].addr == addr) {
                v = asm_log.ev[i].value;
                replay_pos = i + 1;
                break;
            }
    } else {
        v = machine_peek(g_hw, addr);
    }
    log_add(&c_log, EV_BUS_R, addr, v);
    return v;
}

void bus_write(uint16_t addr, uint8_t value)
{
    int blocked = 0;
    if ((addr >= 0x8000 && addr < 0xA000) || (addr >= 0xFE00 && addr < 0xFEA0)) {
        while (write_pos < asm_log.n && asm_log.ev[write_pos].kind != EV_VRAM_W &&
               asm_log.ev[write_pos].kind != EV_OAM_W && asm_log.ev[write_pos].kind != EV_BUS_W)
            write_pos++;
        if (write_pos < asm_log.n && asm_log.ev[write_pos].addr == addr)
            blocked = asm_log.ev[write_pos++].blocked;
    }
    log_add(&c_log, EV_BUS_W, addr, value);
    c_log.ev[c_log.n - 1].blocked = (uint8_t)blocked;
    if (blocked)
        return;
    if (addr >= 0x8000 && addr < 0xA000) g_hw->vram[addr - 0x8000] = value;
    else if (addr >= 0xFE00 && addr < 0xFEA0) g_hw->oam[addr - 0xFE00] = value;
    else machine_write(g_hw, addr, value);
}

/* ------------------------------------------------------------------ */
/* Comparison                                                          */

/* Events that the comparison looks at (reads are inputs, already checked
   by the replay). Writes into ROM space (MBC registers) are events too. */
static int compared(int kind)
{
    return kind != EV_IO_R && kind != EV_VRAM_R && kind != EV_BUS_R;
}

/* A bus write of the C code stands for any write the original makes to the
   same address. */
static int write_kind(int k)
{
    return k == EV_IO_W || k == EV_VRAM_W || k == EV_OAM_W || k == EV_SHARED_W || k == EV_BUS_W || k == EV_BANK;
}

static int compare_logs(char *msg, size_t len)
{
    int i = 0, j = 0;
    for (;;) {
        while (i < asm_log.n && !compared(asm_log.ev[i].kind)) i++;
        while (j < c_log.n && !compared(c_log.ev[j].kind)) j++;
        if (i >= asm_log.n || j >= c_log.n)
            break;
        Event *a = &asm_log.ev[i], *c = &c_log.ev[j];
        int kinds_match = a->kind == c->kind ||
                          ((a->kind == EV_BUS_W || c->kind == EV_BUS_W) && write_kind(a->kind) && write_kind(c->kind));
        int same = kinds_match && a->value == c->value &&
                   (a->kind == EV_BRANCH || a->kind == EV_TABLE || a->kind == EV_REG_B || a->addr == c->addr);
        if (!same) {
            snprintf(msg, len, "event %d differs: original %s %04X=%02X, C %s %04X=%02X",
                     i, ev_names[a->kind], a->addr, a->value, ev_names[c->kind], c->addr, c->value);
            return 1;
        }
        i++, j++;
    }
    if (i < asm_log.n) {
        Event *a = &asm_log.ev[i];
        snprintf(msg, len, "C stopped early; original continues with %s %04X=%02X", ev_names[a->kind], a->addr, a->value);
        return 1;
    }
    if (j < c_log.n) {
        Event *c = &c_log.ev[j];
        snprintf(msg, len, "C has extra events, first %s %04X=%02X", ev_names[c->kind], c->addr, c->value);
        return 1;
    }
    return 0;
}

static int compare_memory(const Machine *a, const Machine *c, uint16_t sp0, char *msg, size_t len)
{
    for (int i = 0; i < 0x2000; i++) {
        uint16_t addr = (uint16_t)(0xC000 + i);
        if (addr >= sp0 - 0x100 && addr < sp0)
            continue;                       /* stack scratch below the entry frame */
        if (a->wram[i] != c->wram[i]) {
            snprintf(msg, len, "WRAM %04X: original %02X, C %02X", addr, a->wram[i], c->wram[i]);
            return 1;
        }
    }
    for (int i = 0; i < 0x7F; i++)
        if (a->hram[i] != c->hram[i]) {
            snprintf(msg, len, "HRAM %04X: original %02X, C %02X", 0xFF80 + i, a->hram[i], c->hram[i]);
            return 1;
        }
    for (int i = 0; i < 0x2000; i++)
        if (a->vram[i] != c->vram[i]) {
            snprintf(msg, len, "VRAM %04X: original %02X, C %02X", 0x8000 + i, a->vram[i], c->vram[i]);
            return 1;
        }
    for (int i = 0; i < 0xA0; i++)
        if (a->oam[i] != c->oam[i]) {
            snprintf(msg, len, "OAM %04X: original %02X, C %02X", 0xFE00 + i, a->oam[i], c->oam[i]);
            return 1;
        }
    if (a->rom_bank != c->rom_bank) {
        snprintf(msg, len, "ROM bank: original %d, C %d", a->rom_bank, c->rom_bank);
        return 1;
    }
    return 0;
}

static int compare_regs(const Sm83Regs *a, const Sm83Regs *c, unsigned outs, char *msg, size_t len)
{
    struct { unsigned bit; const char *name; uint8_t x, y; } r[] = {
        { OUT_A, "A", a->a, c->a }, { OUT_B, "B", a->b, c->b }, { OUT_C, "C", a->c, c->c },
        { OUT_D, "D", a->d, c->d }, { OUT_E, "E", a->e, c->e }, { OUT_H, "H", a->h, c->h },
        { OUT_L, "L", a->l, c->l },
        { OUT_ZF, "Z flag", a->f & 0x80, c->f & 0x80 }, { OUT_CF, "C flag", a->f & 0x10, c->f & 0x10 },
    };
    for (size_t i = 0; i < sizeof r / sizeof *r; i++)
        if ((outs & r[i].bit) && r[i].x != r[i].y) {
            snprintf(msg, len, "output %s: original %02X, C %02X", r[i].name, r[i].x, r[i].y);
            return 1;
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */

typedef struct { uint16_t pc; uint8_t bank, seen; } BranchCov;   /* seen: bit0 not taken, bit1 taken */
typedef struct {
    long entries, tested, passed, skipped, fuzzed;
    int reported;
    BranchCov cov[512];
    int ncov;
} Stats;

static void record_coverage(Stats *st)
{
    for (int i = 0; i < asm_log.n; i++) {
        Event *e = &asm_log.ev[i];
        if (e->kind != EV_BRANCH)
            continue;
        int k = 0;
        while (k < st->ncov && (st->cov[k].pc != e->addr || st->cov[k].bank != e->bank)) k++;
        if (k == st->ncov) {
            if (st->ncov == 512) continue;
            st->cov[st->ncov++] = (BranchCov){ e->addr, e->bank, 0 };
        }
        st->cov[k].seen |= (uint8_t)(1 << (e->value ? 1 : 0));
    }
}

static GB *ref;
static Movie movie;
static long poll_count;
static int samples_max = 300;
static const FnTest *active[256];
static Stats stats[256];
static int nactive;
static int failures;
static int verbose;

static void dump_logs(void)
{
    int n = asm_log.n > c_log.n ? asm_log.n : c_log.n;
    for (int i = 0; i < n && i < 400; i++) {
        char a[64] = "", c[64] = "";
        if (i < asm_log.n) snprintf(a, sizeof a, "%-12s %04X %02X%s", ev_names[asm_log.ev[i].kind],
                                     asm_log.ev[i].addr, asm_log.ev[i].value, asm_log.ev[i].blocked ? " blk" : "");
        if (i < c_log.n) snprintf(c, sizeof c, "%-12s %04X %02X%s", ev_names[c_log.ev[i].kind],
                                   c_log.ev[i].addr, c_log.ev[i].value, c_log.ev[i].blocked ? " blk" : "");
        printf("  %4d  %-30s | %s\n", i, a, c);
    }
}

static Machine scratch_a, scratch_c;

static Machine start_state;
static char fuzz_desc[256];
static int fuzz_rounds = 8;
static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

/* Returns 1 if tested, 0 if skipped. */
static int run_case(const FnTest *t, Stats *st, const Machine *start, Sm83Regs r0, int fuzzed)
{
    uint16_t sp0 = r0.sp;
    uint16_t ret_addr = (uint16_t)(machine_peek(start, sp0) | machine_peek(start, (uint16_t)(sp0 + 1)) << 8);

    /* original */
    scratch_a = *start;
    scratch_a.apu.out = NULL;
    scratch_a.apu.out_cap = 0;
    scratch_a.ie = 0;
    memset(&scratch_a.hooks, 0, sizeof scratch_a.hooks);
    scratch_a.hooks.on_io_read = asm_io_read;
    scratch_a.hooks.on_io_write = asm_io_write;
    scratch_a.hooks.on_mem_write = asm_mem_write;
#ifdef MACHINE_HAS_MEM_READ_HOOK
    scratch_a.hooks.on_mem_read = asm_mem_read;
#endif
    Sm83 cpu;
    sm83_init(&cpu, &scratch_a);
    cpu.r = r0;
    cpu.r.ime = 0;
    cpu.on_trace = asm_trace;
    asm_log.n = 0;
    prev_valid = 0;
    pending_write = -1;
    log_shared = !t->in_interrupt;
    if (!fuzzed)
        nreads = 0;
    long steps = 0;
    while (!(cpu.r.pc == ret_addr && cpu.r.sp == (uint16_t)(sp0 + 2))) {
        /* fuzzed states can send the original on long excursions: give up
           sooner on those (they are skipped, not failed) */
        if (cpu.r.sp > (uint16_t)(sp0 + 2) || ++steps > (fuzzed ? 200000 : 3000000) ||
            cpu.r.halted || cpu.locked) {
            if (!fuzzed)
                st->skipped++;
            return 0;    /* does not return normally: not unit-testable */
        }
        sm83_step(&cpu);
    }
    asm_finish_previous(&cpu);   /* the last branch (the caller's next instruction is not logged) */
    record_coverage(st);

    /* translation */
    scratch_c = *start;
    scratch_c.apu.out = NULL;
    scratch_c.apu.out_cap = 0;
    scratch_c.ie = 0;
    memset(&scratch_c.hooks, 0, sizeof scratch_c.hooks);
    g_hw = &scratch_c;
    c_log.n = 0;
    replay_pos = 0;
    write_pos = 0;
    replay_error = 0;
    Sm83Regs rc = r0;
    t->adapter(&rc);

    char msg[256] = "";
    int bad = replay_error ? (snprintf(msg, sizeof msg, "%s", replay_msg), 1) : 0;
    if (!bad) bad = compare_logs(msg, sizeof msg);
    if (!bad) bad = compare_memory(&scratch_a, &scratch_c, sp0, msg, sizeof msg);
    if (!bad) bad = compare_regs(&cpu.r, &rc, t->outputs, msg, sizeof msg);
    if (fuzzed) st->fuzzed++; else st->tested++;
    if (bad) {
        failures++;
        if (st->reported++ < 5)
            printf("FAIL %s (poll %ld, entry %ld%s%s): %s\n", t->name, poll_count, st->entries,
                   fuzzed ? ", fuzzed:" : "", fuzzed ? fuzz_desc : "", msg);
        if (verbose && st->reported == 1)
            dump_logs();
    } else if (!fuzzed) {
        st->passed++;
    }
    return 1;
}

static void run_one(const FnTest *t, Stats *st)
{
    start_state = ref->m;
    Sm83Regs r0 = ref->cpu.r;
    if (!run_case(t, st, &start_state, r0, 0))
        return;
    /* Fuzzing: perturb some of the RAM bytes the original read and run
       both versions again on the modified state. */
    static uint16_t inputs[4096];
    int ninputs = nreads;
    memcpy(inputs, read_addrs, sizeof inputs);
    for (int round = 0; round < fuzz_rounds && ninputs > 0; round++) {
        static Machine fz;
        fz = start_state;
        int changes = 1 + (int)(rnd() % 3);
        fuzz_desc[0] = 0;
        for (int k = 0; k < changes; k++) {
            uint16_t a = inputs[rnd() % (uint32_t)ninputs];
            uint8_t *p = a >= 0xFF80 ? &fz.hram[a - 0xFF80] : &fz.wram[a - 0xC000];
            static const uint8_t specials[] = { 0x00, 0x01, 0x02, 0x7F, 0x80, 0x99, 0xFE, 0xFF };
            switch (rnd() % 4) {
            case 0: *p = specials[rnd() % 8]; break;
            case 1: *p = (uint8_t)(*p + 1); break;
            case 2: *p = (uint8_t)(*p - 1); break;
            default: *p = (uint8_t)rnd(); break;
            }
            if (strlen(fuzz_desc) < 200)
                sprintf(fuzz_desc + strlen(fuzz_desc), " %04X=%02X", a, *p);
        }
        if (t->valid_state && !t->valid_state(&fz))
            continue;              /* a state the game cannot be in */
        run_case(t, st, &fz, r0, 1);
    }
}

static int on_exec(Sm83 *cpu, uint16_t pc, void *user)
{
    (void)user;
    int bank = pc < 0x4000 ? 0 : cpu->m->rom_bank;
    if (pc == 0x47F2 && bank == 3) {
        cpu->m->buttons = movie_get(&movie, (size_t)poll_count);
        poll_count++;
    }
    for (int i = 0; i < nactive; i++) {
        const FnTest *t = active[i];
        if (t->addr != pc || t->bank != bank)
            continue;
        Stats *st = &stats[i];
        long e = st->entries++;
        if (st->tested + st->skipped < samples_max && (e < 50 || e % 7 == 0))
            run_one(t, st);
    }
    return 0;
}

/* Seed rare, valid transitions from a movie snapshot. Each line names a
   registered routine followed by hexadecimal RAM/VRAM address=value pairs
   (or bank=value, hl=value, de=value, bc=value, a=value). Register pairs
   accept 16-bit values; other inputs are bytes. The original and C still run
   from identical snapshots; existing fuzz predicates apply unchanged.
   Each case starts independently. */
static int run_cases(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 1; }
    static Machine base;
    base = ref->m;
    Sm83Regs regs = ref->cpu.r;
    char line[1024];
    unsigned lineno = 0;
    while (fgets(line, sizeof line, file)) {
        lineno++;
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        char *name = strtok(line, " \t\r\n");
        if (!name) continue;
        int index = -1;
        for (int i = 0; i < nactive; i++)
            if (!strcmp(active[i]->name, name)) index = i;
        if (index < 0) {
            fprintf(stderr, "%s:%u: unregistered or unselected function %s\n", path, lineno, name);
            fclose(file); return 1;
        }
        ref->m = base;
        ref->cpu.r = regs;
        const FnTest *t = active[index];
        if (t->bank) ref->m.rom_bank = t->bank;
        for (char *pair = strtok(NULL, " \t\r\n"); pair; pair = strtok(NULL, " \t\r\n")) {
            char *eq = strchr(pair, '=');
            if (!eq) goto bad_case;
            *eq++ = 0;
            char *end;
            unsigned long value = strtoul(eq, &end, 16);
            if (end == eq || *end || value > 0xFFFF) goto bad_case;
            if (!strcmp(pair, "hl")) {
                ref->cpu.r.h = value >> 8; ref->cpu.r.l = value;
                continue;
            }
            if (!strcmp(pair, "de")) {
                ref->cpu.r.d = value >> 8; ref->cpu.r.e = value;
                continue;
            }
            if (!strcmp(pair, "bc")) {
                ref->cpu.r.b = value >> 8; ref->cpu.r.c = value;
                continue;
            }
            if (value > 255) goto bad_case;
            if (!strcmp(pair, "a")) {
                ref->cpu.r.a = value;
                continue;
            }
            if (!strcmp(pair, "bank")) {
                if (value < 1 || value > 3) goto bad_case;
                ref->m.rom_bank = value;
                continue;
            }
            unsigned long addr = strtoul(pair, &end, 16);
            if (end == pair || *end) goto bad_case;
            if (addr >= 0xC000 && addr < 0xE000) ref->m.wram[addr - 0xC000] = value;
            else if (addr >= 0xFF80 && addr < 0xFFFF) ref->m.hram[addr - 0xFF80] = value;
            else if (addr >= 0x8000 && addr < 0xA000) ref->m.vram[addr - 0x8000] = value;
            else goto bad_case;
        }
        if (t->valid_state && !t->valid_state(&ref->m)) goto bad_case;
        ref->cpu.r.pc = t->addr;
        ref->cpu.r.sp = 0xCFF0;
        ref->cpu.r.halted = 0;
        ref->m.wram[0xFF0] = 0; ref->m.wram[0xFF1] = 1;
        long tested = stats[index].tested;
        stats[index].entries++;
        run_one(t, &stats[index]);
        if (stats[index].tested == tested) {
            fprintf(stderr, "%s:%u: case did not return\n", path, lineno);
            fclose(file); return 1;
        }
        continue;
bad_case:
        fprintf(stderr, "%s:%u: invalid function case\n", path, lineno);
        fclose(file); return 1;
    }
    if (ferror(file)) { perror(path); fclose(file); return 1; }
    fclose(file);
    ref->m = base; ref->cpu.r = regs;
    return 0;
}

/* Coverage contracts fail even when all differential comparisons pass.
   NAME tested requires a successful sample; NAME BANK:PC requires both
   outcomes of that branch in this entry's original-ROM execution. */
static int require_coverage(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 1; }
    char line[256];
    unsigned lineno = 0;
    int missing = 0;
    while (fgets(line, sizeof line, file)) {
        lineno++;
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        char *name = strtok(line, " \t\r\n");
        if (!name) continue;
        char *site = strtok(NULL, " \t\r\n");
        int index = -1;
        for (int i = 0; i < nactive; i++)
            if (!strcmp(active[i]->name, name)) index = i;
        if (index < 0 || !site || strtok(NULL, " \t\r\n")) goto invalid;
        Stats *st = &stats[index];
        if (!strcmp(site, "tested")) {
            if (st->passed) continue;
        } else {
            char *end;
            unsigned long bank = strtoul(site, &end, 16);
            if (end == site || *end != ':' || bank > 3) goto invalid;
            char *pc_text = end + 1;
            unsigned long pc = strtoul(pc_text, &end, 16);
            if (end == pc_text || *end || pc > 0x7FFF ||
                (pc < 0x4000 ? bank != 0 : bank == 0)) goto invalid;
            int covered = 0;
            for (int k = 0; k < st->ncov; k++)
                if (st->cov[k].bank == bank && st->cov[k].pc == pc && st->cov[k].seen == 3)
                    covered = 1;
            if (covered) continue;
        }
        fprintf(stderr, "%s:%u: missing coverage: %s %s\n", path, lineno, name, site);
        missing = 1;
        continue;
invalid:
        fprintf(stderr, "%s:%u: invalid coverage requirement\n", path, lineno);
        fclose(file); return 1;
    }
    if (ferror(file)) { perror(path); missing = 1; }
    fclose(file);
    return missing;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fntest ROM --movie FILE [--model M] [--samples N] [--polls N] [--cases FILE] [--coverage FILE] [--require-coverage FILE] [NAME...]\n");
        return 2;
    }
    MachineModel model = MODEL_DMG;
    long max_polls = -1;
    const char *cases = NULL, *coverage_path = NULL, *coverage_requirements = NULL;
    const char *names[256];
    int nnames = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) {
            if (movie_load(&movie, argv[++i])) { fprintf(stderr, "bad movie\n"); return 2; }
        } else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            model = (MachineModel)machine_model_from_name(argv[++i]);
        } else if (!strcmp(argv[i], "--samples") && i + 1 < argc) {
            samples_max = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-v")) {
            verbose = 1;
        } else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) {
            fuzz_rounds = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--polls") && i + 1 < argc) {
            max_polls = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--cases") && i + 1 < argc) {
            cases = argv[++i];
        } else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) {
            coverage_path = argv[++i];
        } else if (!strcmp(argv[i], "--require-coverage") && i + 1 < argc) {
            coverage_requirements = argv[++i];
        } else if (nnames < 256) {
            names[nnames++] = argv[i];
        }
    }
#define FNTEST_ADD(sub)                                                          \
    for (const FnTest *t = fn_tests_##sub; t && t->name && nactive < 256; t++) {  \
        int want = nnames == 0;                                                    \
        for (int k = 0; k < nnames; k++)                                           \
            if (!strcmp(names[k], t->name)) want = 1;                              \
        if (want)                                                                  \
            active[nactive++] = t;                                                 \
    }
    FNTEST_TABLES(FNTEST_ADD)
    if (max_polls < 0)
        max_polls = (long)movie.count + 60;

    static uint8_t rom[0x10000];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    asm_log.ev = calloc(MAX_EVENTS, sizeof(Event));
    c_log.ev = calloc(MAX_EVENTS, sizeof(Event));

    ref = gb_create(model, rom, n);
    ref->cpu.on_exec = on_exec;
    sm83_set_exec_hook(&ref->cpu, 3, 0x47F2, 1);
    for (int i = 0; i < nactive; i++)
        sm83_set_exec_hook(&ref->cpu, active[i]->bank, active[i]->addr, 1);
    while (poll_count < max_polls)
        gb_run_frame(ref);
    if (cases && run_cases(cases)) return 2;
    if (coverage_requirements && require_coverage(coverage_requirements)) failures++;

    /* Every branch site per entry (tools/merge_coverage.py merges runs):
       "NAME BANK:PC SEEN", SEEN bit 0 = not taken, bit 1 = taken. */
    if (coverage_path) {
        FILE *cf = fopen(coverage_path, "w");
        if (!cf) { perror(coverage_path); return 2; }
        for (int i = 0; i < nactive; i++)
            for (int k = 0; k < stats[i].ncov; k++)
                fprintf(cf, "%s %d:%04X %d\n", active[i]->name, stats[i].cov[k].bank,
                        stats[i].cov[k].pc, stats[i].cov[k].seen);
        fclose(cf);
    }

    int untested = 0;
    for (int i = 0; i < nactive; i++) {
        Stats *st = &stats[i];
        int uncovered = 0;
        char list[512] = "";
        for (int k = 0; k < st->ncov; k++)
            if (st->cov[k].seen != 3) {
                uncovered++;
                if (strlen(list) < 400)
                    sprintf(list + strlen(list), " %04X:%s", st->cov[k].pc,
                            st->cov[k].seen == 1 ? "never-taken" : "always-taken");
            }
        printf("%-24s entries %7ld tested %4ld passed %4ld fuzzed %5ld skipped %3ld branches %d/%d%s\n",
               active[i]->name, st->entries, st->tested, st->passed, st->fuzzed, st->skipped,
               st->ncov - uncovered, st->ncov, st->tested == 0 ? "  (NOT TESTED)" : "");
        if (uncovered)
            printf("    one-sided branches:%s\n", list);
        if (st->tested == 0) untested++;
    }
    printf("%s: %d failures, %d functions without tests\n", failures ? "FAILED" : "ok", failures, untested);
    return failures ? 1 : 0;
}
