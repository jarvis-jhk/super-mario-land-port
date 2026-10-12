/* SM83 (Game Boy CPU) interpreter. See sm83.h. */
#include "sm83.h"
#include <stdlib.h>
#include <string.h>

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

/* Every bus access takes one M-cycle (4 T-cycles); the access happens at
   the START of it, then time passes. Internal delays are plain ticks. */
static void tick(Sm83 *cpu) { machine_tick(cpu->m, 4); }
static uint8_t rd(Sm83 *cpu, uint16_t addr)
{
    if (addr < 0x8000 && cpu->rom_access && !cpu->fetching)
        cpu->rom_access[machine_rom_offset(cpu->m, addr)] |= ROM_ACCESS_DATA;
    uint8_t v = machine_read(cpu->m, addr);
    tick(cpu);
    return v;
}
static void wr(Sm83 *cpu, uint16_t addr, uint8_t v) { machine_write(cpu->m, addr, v); tick(cpu); }
static uint8_t fetch(Sm83 *cpu)
{
    if (cpu->rom_access && cpu->r.pc < 0x8000)
        cpu->rom_access[machine_rom_offset(cpu->m, cpu->r.pc)] |= ROM_ACCESS_OPERAND;
    cpu->fetching = 1;
    uint8_t v = rd(cpu, cpu->r.pc++);
    cpu->fetching = 0;
    return v;
}
static uint16_t fetch16(Sm83 *cpu) { uint8_t lo = fetch(cpu); return (uint16_t)(lo | (fetch(cpu) << 8)); }
static void push16(Sm83 *cpu, uint16_t v)
{
    wr(cpu, --cpu->r.sp, v >> 8);
    wr(cpu, --cpu->r.sp, v & 0xFF);
}
static uint16_t pop16(Sm83 *cpu)
{
    uint8_t lo = rd(cpu, cpu->r.sp++);
    return (uint16_t)(lo | (rd(cpu, cpu->r.sp++) << 8));
}

#define A  cpu->r.a
#define F  cpu->r.f
#define BC ((cpu->r.b << 8) | cpu->r.c)
#define DE ((cpu->r.d << 8) | cpu->r.e)
#define HL ((cpu->r.h << 8) | cpu->r.l)
#define SET_BC(v) do { uint16_t _v = (v); cpu->r.b = _v >> 8; cpu->r.c = _v & 0xFF; } while (0)
#define SET_DE(v) do { uint16_t _v = (v); cpu->r.d = _v >> 8; cpu->r.e = _v & 0xFF; } while (0)
#define SET_HL(v) do { uint16_t _v = (v); cpu->r.h = _v >> 8; cpu->r.l = _v & 0xFF; } while (0)

static uint8_t get_r8(Sm83 *cpu, int i)
{
    switch (i) {
    case 0: return cpu->r.b; case 1: return cpu->r.c;
    case 2: return cpu->r.d; case 3: return cpu->r.e;
    case 4: return cpu->r.h; case 5: return cpu->r.l;
    case 6: return rd(cpu, HL);
    default: return cpu->r.a;
    }
}

static void set_r8(Sm83 *cpu, int i, uint8_t v)
{
    switch (i) {
    case 0: cpu->r.b = v; break; case 1: cpu->r.c = v; break;
    case 2: cpu->r.d = v; break; case 3: cpu->r.e = v; break;
    case 4: cpu->r.h = v; break; case 5: cpu->r.l = v; break;
    case 6: wr(cpu, HL, v); break;
    default: cpu->r.a = v; break;
    }
}

static uint16_t get_r16(Sm83 *cpu, int i)   /* BC DE HL SP */
{
    switch (i) {
    case 0: return BC; case 1: return DE; case 2: return HL;
    default: return cpu->r.sp;
    }
}

static void set_r16(Sm83 *cpu, int i, uint16_t v)
{
    switch (i) {
    case 0: SET_BC(v); break; case 1: SET_DE(v); break;
    case 2: SET_HL(v); break; default: cpu->r.sp = v; break;
    }
}

static int cond(Sm83 *cpu, int c)
{
    switch (c) {
    case 0: return !(F & FLAG_Z);
    case 1: return (F & FLAG_Z) != 0;
    case 2: return !(F & FLAG_C);
    default: return (F & FLAG_C) != 0;
    }
}

static void alu(Sm83 *cpu, int op, uint8_t v)
{
    uint8_t a = A;
    int r, c = (F & FLAG_C) ? 1 : 0;
    switch (op) {
    case 0: /* ADD */
        r = a + v;
        F = ((r & 0xFF) ? 0 : FLAG_Z) | (((a & 0xF) + (v & 0xF)) > 0xF ? FLAG_H : 0) | (r > 0xFF ? FLAG_C : 0);
        A = r; break;
    case 1: /* ADC */
        r = a + v + c;
        F = ((r & 0xFF) ? 0 : FLAG_Z) | (((a & 0xF) + (v & 0xF) + c) > 0xF ? FLAG_H : 0) | (r > 0xFF ? FLAG_C : 0);
        A = r; break;
    case 2: /* SUB */
        r = a - v;
        F = FLAG_N | ((r & 0xFF) ? 0 : FLAG_Z) | ((a & 0xF) < (v & 0xF) ? FLAG_H : 0) | (r < 0 ? FLAG_C : 0);
        A = r; break;
    case 3: /* SBC */
        r = a - v - c;
        F = FLAG_N | ((r & 0xFF) ? 0 : FLAG_Z) | (((a & 0xF) - (v & 0xF) - c) < 0 ? FLAG_H : 0) | (r < 0 ? FLAG_C : 0);
        A = r; break;
    case 4: A = a & v; F = (A ? 0 : FLAG_Z) | FLAG_H; break;
    case 5: A = a ^ v; F = A ? 0 : FLAG_Z; break;
    case 6: A = a | v; F = A ? 0 : FLAG_Z; break;
    case 7: /* CP */
        r = a - v;
        F = FLAG_N | ((r & 0xFF) ? 0 : FLAG_Z) | ((a & 0xF) < (v & 0xF) ? FLAG_H : 0) | (r < 0 ? FLAG_C : 0);
        break;
    }
}

static void cb_op(Sm83 *cpu)
{
    uint8_t op = fetch(cpu);
    int reg = op & 7, y = (op >> 3) & 7;
    uint8_t v = get_r8(cpu, reg), r = 0;
    int c;
    switch (op >> 6) {
    case 0:
        switch (y) {
        case 0: c = v >> 7; r = (v << 1) | c; break;                 /* RLC */
        case 1: c = v & 1; r = (v >> 1) | (c << 7); break;            /* RRC */
        case 2: c = v >> 7; r = (v << 1) | ((F & FLAG_C) ? 1 : 0); break; /* RL */
        case 3: c = v & 1; r = (v >> 1) | ((F & FLAG_C) ? 0x80 : 0); break; /* RR */
        case 4: c = v >> 7; r = v << 1; break;                        /* SLA */
        case 5: c = v & 1; r = (v >> 1) | (v & 0x80); break;          /* SRA */
        case 6: c = 0; r = (v >> 4) | (v << 4); break;                /* SWAP */
        default: c = v & 1; r = v >> 1; break;                        /* SRL */
        }
        F = (r ? 0 : FLAG_Z) | (c ? FLAG_C : 0);
        set_r8(cpu, reg, r);
        break;
    case 1: /* BIT */
        F = (F & FLAG_C) | FLAG_H | ((v >> y) & 1 ? 0 : FLAG_Z);
        break;
    case 2: set_r8(cpu, reg, v & ~(1 << y)); break;  /* RES */
    case 3: set_r8(cpu, reg, v | (1 << y)); break;   /* SET */
    }
}

static void execute(Sm83 *cpu, uint8_t op)
{
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    if (x == 1) {
        if (op == 0x76) { /* HALT */
            if (machine_pending_interrupts(cpu->m)) {
                /* An interrupt is already pending: the CPU does not sleep.
                   The next opcode has been fetched but PC was not advanced
                   (the "HALT bug": that byte is executed twice); if the
                   interrupt is dispatched, it returns to the HALT. */
                cpu->prefetched = 1;
                cpu->opcode = machine_read(cpu->m, cpu->r.pc);
            } else {
                /* Entering sleep takes one more M-cycle on a Color and two
                   on the original; the DMA engine stops after the first. */
                cpu->r.halted = 1;
                tick(cpu);
                cpu->m->cpu_halted = 1;
                if (cpu->m->model == MODEL_DMG)
                    tick(cpu);
            }
            return;
        }
        set_r8(cpu, y, get_r8(cpu, z));
        return;
    }
    if (x == 2) { alu(cpu, y, get_r8(cpu, z)); return; }

    uint16_t t16; uint8_t t8; int8_t e;
    switch (op) {
    case 0x00: break;                                           /* NOP */
    case 0x01: case 0x11: case 0x21: case 0x31:                 /* LD rr,nn */
        set_r16(cpu, op >> 4, fetch16(cpu)); break;
    case 0x02: wr(cpu, BC, A); break;
    case 0x12: wr(cpu, DE, A); break;
    case 0x22: wr(cpu, HL, A); SET_HL(HL + 1); break;
    case 0x32: wr(cpu, HL, A); SET_HL(HL - 1); break;
    case 0x0A: A = rd(cpu, BC); break;
    case 0x1A: A = rd(cpu, DE); break;
    case 0x2A: A = rd(cpu, HL); SET_HL(HL + 1); break;
    case 0x3A: A = rd(cpu, HL); SET_HL(HL - 1); break;
    case 0x03: case 0x13: case 0x23: case 0x33:                 /* INC rr */
        set_r16(cpu, op >> 4, get_r16(cpu, op >> 4) + 1); tick(cpu); break;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B:                 /* DEC rr */
        set_r16(cpu, op >> 4, get_r16(cpu, op >> 4) - 1); tick(cpu); break;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
        t8 = get_r8(cpu, y) + 1;
        F = (F & FLAG_C) | (t8 ? 0 : FLAG_Z) | ((t8 & 0xF) == 0 ? FLAG_H : 0);
        set_r8(cpu, y, t8); break;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:
        t8 = get_r8(cpu, y) - 1;
        F = (F & FLAG_C) | FLAG_N | (t8 ? 0 : FLAG_Z) | ((t8 & 0xF) == 0xF ? FLAG_H : 0);
        set_r8(cpu, y, t8); break;
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
        t8 = fetch(cpu); set_r8(cpu, y, t8); break;
    case 0x07: t8 = A >> 7; A = (A << 1) | t8; F = t8 ? FLAG_C : 0; break;     /* RLCA */
    case 0x0F: t8 = A & 1; A = (A >> 1) | (t8 << 7); F = t8 ? FLAG_C : 0; break; /* RRCA */
    case 0x17: t8 = A >> 7; A = (A << 1) | ((F & FLAG_C) ? 1 : 0); F = t8 ? FLAG_C : 0; break;
    case 0x1F: t8 = A & 1; A = (A >> 1) | ((F & FLAG_C) ? 0x80 : 0); F = t8 ? FLAG_C : 0; break;
    case 0x08: t16 = fetch16(cpu); wr(cpu, t16, cpu->r.sp & 0xFF); wr(cpu, t16 + 1, cpu->r.sp >> 8); break;
    case 0x09: case 0x19: case 0x29: case 0x39: {               /* ADD HL,rr */
        uint16_t hl = HL, v = get_r16(cpu, op >> 4);
        uint32_t r = hl + v;
        F = (F & FLAG_Z) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FLAG_H : 0) | (r > 0xFFFF ? FLAG_C : 0);
        SET_HL(r); tick(cpu); break;
    }
    case 0x10: fetch(cpu); break;                                /* STOP (unused) */
    case 0x18: e = (int8_t)fetch(cpu); cpu->r.pc += e; tick(cpu); break;
    case 0x20: case 0x28: case 0x30: case 0x38:
        e = (int8_t)fetch(cpu);
        if (cond(cpu, y - 4)) { cpu->r.pc += e; tick(cpu); }
        break;
    case 0x27: { /* DAA */
        int a = A;
        if (!(F & FLAG_N)) {
            if ((F & FLAG_C) || a > 0x99) { a += 0x60; F |= FLAG_C; }
            if ((F & FLAG_H) || (a & 0x0F) > 0x09) a += 0x06;
        } else {
            if (F & FLAG_C) a -= 0x60;
            if (F & FLAG_H) a -= 0x06;
        }
        A = a;
        F = (F & (FLAG_N | FLAG_C)) | (A ? 0 : FLAG_Z);
        break;
    }
    case 0x2F: A = ~A; F |= FLAG_N | FLAG_H; break;             /* CPL */
    case 0x37: F = (F & FLAG_Z) | FLAG_C; break;                /* SCF */
    case 0x3F: F = (F & FLAG_Z) | ((F & FLAG_C) ? 0 : FLAG_C); break; /* CCF */

    case 0xC0: case 0xC8: case 0xD0: case 0xD8:                 /* RET cc */
        tick(cpu);
        if (cond(cpu, y)) { cpu->r.pc = pop16(cpu); tick(cpu); }
        break;
    case 0xC9: cpu->r.pc = pop16(cpu); tick(cpu); break;
    case 0xD9: cpu->r.pc = pop16(cpu); tick(cpu); cpu->r.ime = 1; break;  /* RETI */
    case 0xC1: case 0xD1: case 0xE1: set_r16(cpu, (op >> 4) - 0xC, pop16(cpu)); break;
    case 0xF1: t16 = pop16(cpu); A = t16 >> 8; F = t16 & 0xF0; break;
    case 0xC5: case 0xD5: case 0xE5: tick(cpu); push16(cpu, get_r16(cpu, (op >> 4) - 0xC)); break;
    case 0xF5: tick(cpu); push16(cpu, (A << 8) | F); break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA:                 /* JP cc */
        t16 = fetch16(cpu);
        if (cond(cpu, y)) { cpu->r.pc = t16; tick(cpu); }
        break;
    case 0xC3: t16 = fetch16(cpu); cpu->r.pc = t16; tick(cpu); break;
    case 0xE9: cpu->r.pc = HL; break;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC:                 /* CALL cc */
        t16 = fetch16(cpu);
        if (cond(cpu, y)) { tick(cpu); push16(cpu, cpu->r.pc); cpu->r.pc = t16; }
        break;
    case 0xCD: t16 = fetch16(cpu); tick(cpu); push16(cpu, cpu->r.pc); cpu->r.pc = t16; break;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        tick(cpu); push16(cpu, cpu->r.pc); cpu->r.pc = y * 8; break;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(cpu, y, fetch(cpu)); break;
    case 0xCB: cb_op(cpu); break;
    case 0xE0: t8 = fetch(cpu); wr(cpu, 0xFF00 | t8, A); break;
    case 0xF0: t8 = fetch(cpu); A = rd(cpu, 0xFF00 | t8); break;
    case 0xE2: wr(cpu, 0xFF00 | cpu->r.c, A); break;
    case 0xF2: A = rd(cpu, 0xFF00 | cpu->r.c); break;
    case 0xEA: t16 = fetch16(cpu); wr(cpu, t16, A); break;
    case 0xFA: t16 = fetch16(cpu); A = rd(cpu, t16); break;
    case 0xE8: case 0xF8: {                                     /* ADD SP,e / LD HL,SP+e */
        e = (int8_t)fetch(cpu);
        uint16_t sp = cpu->r.sp, r = sp + e;
        F = (((sp & 0xF) + (e & 0xF)) > 0xF ? FLAG_H : 0) | (((sp & 0xFF) + (uint8_t)e) > 0xFF ? FLAG_C : 0);
        if (op == 0xE8) { cpu->r.sp = r; tick(cpu); tick(cpu); }
        else { SET_HL(r); tick(cpu); }
        break;
    }
    case 0xF9: cpu->r.sp = HL; tick(cpu); break;
    case 0xF3: cpu->r.ime = 0; cpu->ime_pending = 0; break;       /* DI */
    case 0xFB: cpu->ime_pending = 2; break;                      /* EI */
    default:
        /* Illegal opcode: the real CPU locks up. */
        cpu->r.halted = 1; cpu->locked = 1;
        cpu->m->ie = 0; cpu->m->cpu_halted = 1;
        break;
    }
}

/* Push PC and jump to the vector of the highest-priority pending interrupt. */
static void dispatch_interrupt(Sm83 *cpu)
{
    Machine *m = cpu->m;
    if (cpu->prefetched) {         /* undo the prefetch */
        cpu->r.pc--;
        cpu->prefetched = 0;
    }
    cpu->r.ime = 0;
    cpu->ime_pending = 0;
    tick(cpu); tick(cpu); tick(cpu);
    wr(cpu, --cpu->r.sp, cpu->r.pc >> 8);
    /* the vector is chosen after the high byte is pushed (the push may
       overwrite IE), but IF is cleared only after the low byte */
    uint8_t pending = machine_pending_interrupts(m);
    uint8_t n = pending & (uint8_t)-pending;
    uint16_t vector = n == 0 ? 0x0000 : 0x40 + (uint16_t)__builtin_ctz(n) * 8;
    machine_write(m, --cpu->r.sp, cpu->r.pc & 0xFF);
    if (n)
        machine_ack_interrupt(m, n);
    tick(cpu);
    cpu->r.pc = vector;
}

/* Leave HALT: the wake-up takes one more M-cycle on a Color; on the
   original only when the request arrived in the last two cycles. */
static void wake(Sm83 *cpu)
{
    Machine *m = cpu->m;
    if (m->model != MODEL_DMG || (int64_t)m->cycles - m->min_int_time < 2)
        tick(cpu);
    cpu->r.halted = 0;
    m->cpu_halted = 0;
}

void sm83_step(Sm83 *cpu)
{
    Machine *m = cpu->m;
    if (cpu->r.halted) {
        if (!machine_pending_interrupts(m)) {
            tick(cpu);
            return;
        }
        wake(cpu);
    }
    if (cpu->r.ime && machine_pending_interrupts(m)) {
        dispatch_interrupt(cpu);
        return;
    }
    uint16_t pc = cpu->r.pc;
    if (cpu->exec_hook && pc < 0x8000) {
        int bank = pc < 0x4000 ? 0 : cpu->m->rom_bank;
        if (cpu->exec_hook[bank * 0x4000 + (pc & 0x3FFF)] && cpu->on_exec &&
            cpu->on_exec(cpu, pc, cpu->user))
            return;   /* the hook took over this instruction */
    }
    if (cpu->rom_access && pc < 0x8000)
        cpu->rom_access[machine_rom_offset(cpu->m, pc)] |= ROM_ACCESS_OPCODE;
    if (cpu->on_trace)
        cpu->on_trace(cpu, cpu->user);
    uint8_t op;
    if (cpu->prefetched) {
        op = cpu->opcode;
        cpu->prefetched = 0;
        tick(cpu);
    } else {
        op = fetch(cpu);
    }
    execute(cpu, op);
    /* EI enables interrupts only after the instruction that follows it */
    if (cpu->ime_pending == 2)
        cpu->ime_pending = 1;
    else if (cpu->ime_pending == 1) {
        cpu->r.ime = 1;
        cpu->ime_pending = 0;
    }
}

void sm83_init(Sm83 *cpu, Machine *m)
{
    memset(cpu, 0, sizeof *cpu);
    cpu->m = m;
    /* Registers as left by the boot ROM at $0100 (A=$11 tells games they run
       on a Color, B=1 on an Advance) */
    cpu->r.a = m->model == MODEL_DMG ? 0x01 : 0x11;
    cpu->r.f = 0xB0;
    cpu->r.b = m->model == MODEL_AGB ? 0x01 : 0x00;
    cpu->r.c = 0x13;
    cpu->r.d = 0x00; cpu->r.e = 0xD8; cpu->r.h = 0x01; cpu->r.l = 0x4D;
    cpu->r.sp = 0xFFFE; cpu->r.pc = 0x0100;
}

void sm83_set_exec_hook(Sm83 *cpu, int bank, uint16_t addr, int on)
{
    if (!cpu->exec_hook)
        cpu->exec_hook = calloc(4, 0x4000);
    cpu->exec_hook[bank * 0x4000 + (addr & 0x3FFF)] = (uint8_t)on;
}
