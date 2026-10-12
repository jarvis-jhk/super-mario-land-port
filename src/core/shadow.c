/*
 * The timing shadow. See shadow.h for what it does and why.
 *
 * Each instruction is a sequence of M-cycles ("slots"), exactly as the SM83
 * performs them (and as verify/emu/sm83.c models them: the bus access of an
 * M-cycle happens at its start, then the hardware advances 4 T-cycles):
 *
 *   f  fetch an operand byte          r/w  read/write the memory operand
 *   i  internal cycle                 p/q  push high/low byte
 *   o/P  pop low/high byte            ?    branch decision (needs an event):
 *                                          not taken ends the instruction
 *
 * The opcode fetch is the first M-cycle of every instruction. Memory operand
 * accesses that touch hardware (or shared variables, or anything through a
 * computed pointer that may be hardware) wait for the C code's matching
 * event and are performed at their exact cycle. Plain RAM accesses are left
 * to the C code. Stack accesses for return addresses are real, so `ret`
 * goes where the original goes.
 */
#include "shadow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Instruction table                                                   */

enum { M_NONE, M_HL, M_BC, M_DE, M_ABS, M_HIGH, M_C };
enum {
    F_NONE, F_JR, F_JP, F_CALL, F_RET, F_RETI, F_RST, F_JPHL, F_HALT,
    F_LDSP, F_EI, F_DI, F_UNSUPPORTED,
};

typedef struct {
    const char *slots;   /* after the opcode fetch */
    uint8_t mode;        /* memory operand addressing */
    uint8_t flow;
} OpInfo;

static OpInfo ops[256], cb_ops[256];

static void op(int code, const char *slots, int mode, int flow)
{
    ops[code] = (OpInfo){ slots, (uint8_t)mode, (uint8_t)flow };
}

static void build_tables(void)
{
    static int built;
    if (built) return;
    built = 1;
    for (int i = 0; i < 256; i++) {
        int x = i >> 6, y = (i >> 3) & 7, z = i & 7;
        ops[i] = (OpInfo){ "", M_NONE, F_UNSUPPORTED };
        if (x == 1) {                                       /* LD r,r' */
            if (i == 0x76) op(i, "", M_NONE, F_HALT);
            else if (y == 6) op(i, "w", M_HL, F_NONE);
            else if (z == 6) op(i, "r", M_HL, F_NONE);
            else op(i, "", M_NONE, F_NONE);
        } else if (x == 2) {                                /* ALU A,r */
            op(i, z == 6 ? "r" : "", z == 6 ? M_HL : M_NONE, F_NONE);
        }
        /* CB-prefixed */
        if (y == 6 || z == 6 || 1) {
            int cz = i & 7, cx = i >> 6;
            if (cz == 6)
                cb_ops[i] = (OpInfo){ cx == 1 ? "r" : "rw", M_HL, F_NONE };
            else
                cb_ops[i] = (OpInfo){ "", M_NONE, F_NONE };
        }
    }
    op(0x00, "", M_NONE, F_NONE);
    for (int r = 0; r < 4; r++) {
        op(0x01 + r * 16, "ff", M_NONE, r == 3 ? F_LDSP : F_NONE);   /* LD rr,nn */
        op(0x03 + r * 16, "i", M_NONE, F_NONE);                      /* INC rr */
        op(0x0B + r * 16, "i", M_NONE, F_NONE);                      /* DEC rr */
        op(0x09 + r * 16, "i", M_NONE, F_NONE);                      /* ADD HL,rr */
    }
    if (1) ops[0x31].flow = F_LDSP;
    op(0x02, "w", M_BC, F_NONE); op(0x12, "w", M_DE, F_NONE);
    op(0x22, "w", M_HL, F_NONE); op(0x32, "w", M_HL, F_NONE);
    op(0x0A, "r", M_BC, F_NONE); op(0x1A, "r", M_DE, F_NONE);
    op(0x2A, "r", M_HL, F_NONE); op(0x3A, "r", M_HL, F_NONE);
    for (int y = 0; y < 8; y++) {
        int inc = 0x04 + y * 8, dec = 0x05 + y * 8, ld = 0x06 + y * 8;
        op(inc, y == 6 ? "rw" : "", y == 6 ? M_HL : M_NONE, F_NONE);
        op(dec, y == 6 ? "rw" : "", y == 6 ? M_HL : M_NONE, F_NONE);
        op(ld, y == 6 ? "fw" : "f", y == 6 ? M_HL : M_NONE, F_NONE);
    }
    op(0x07, "", M_NONE, F_NONE); op(0x0F, "", M_NONE, F_NONE);
    op(0x17, "", M_NONE, F_NONE); op(0x1F, "", M_NONE, F_NONE);
    op(0x27, "", M_NONE, F_NONE); op(0x2F, "", M_NONE, F_NONE);
    op(0x37, "", M_NONE, F_NONE); op(0x3F, "", M_NONE, F_NONE);
    op(0x08, "ffww", M_ABS, F_UNSUPPORTED);                          /* LD (nn),SP */
    op(0x10, "f", M_NONE, F_UNSUPPORTED);                            /* STOP */
    op(0x18, "fi", M_NONE, F_JR);
    op(0x20, "f?i", M_NONE, F_JR); op(0x28, "f?i", M_NONE, F_JR);
    op(0x30, "f?i", M_NONE, F_JR); op(0x38, "f?i", M_NONE, F_JR);
    for (int c = 0; c < 4; c++) {
        op(0xC0 + c * 8, "i?oPi", M_NONE, F_RET);                    /* RET cc */
        op(0xC2 + c * 8, "ff?i", M_NONE, F_JP);                      /* JP cc */
        op(0xC4 + c * 8, "ff?ipq", M_NONE, F_CALL);                  /* CALL cc */
    }
    op(0xC9, "oPi", M_NONE, F_RET);
    op(0xD9, "oPi", M_NONE, F_RETI);
    op(0xC3, "ffi", M_NONE, F_JP);
    op(0xCD, "ffipq", M_NONE, F_CALL);
    op(0xE9, "", M_NONE, F_JPHL);
    for (int r = 0; r < 4; r++) {
        op(0xC1 + r * 16, "oP", M_NONE, F_NONE);                     /* POP */
        op(0xC5 + r * 16, "ipq", M_NONE, F_NONE);                    /* PUSH */
    }
    for (int y = 0; y < 8; y++) {
        op(0xC6 + y * 8, "f", M_NONE, F_NONE);                       /* ALU n */
        op(0xC7 + y * 8, "ipq", M_NONE, F_RST);                      /* RST */
    }
    op(0xCB, "f", M_NONE, F_NONE);                                   /* prefix */
    op(0xE0, "fw", M_HIGH, F_NONE); op(0xF0, "fr", M_HIGH, F_NONE);
    op(0xE2, "w", M_C, F_NONE); op(0xF2, "r", M_C, F_NONE);
    op(0xEA, "ffw", M_ABS, F_NONE); op(0xFA, "ffr", M_ABS, F_NONE);
    op(0xE8, "fii", M_NONE, F_UNSUPPORTED);                          /* ADD SP,e */
    op(0xF8, "fi", M_NONE, F_UNSUPPORTED);                           /* LD HL,SP+e */
    op(0xF9, "i", M_NONE, F_UNSUPPORTED);                            /* LD SP,HL */
    op(0xF3, "", M_NONE, F_DI);
    op(0xFB, "", M_NONE, F_EI);
    /* Register B is visible to the vblank handler (see shadow.h): loads of
       B from another register or memory, inc/dec bc (B depends on C) and
       rotations through carry need the value from the C code ('b'). */
    for (int z = 1; z < 8; z++)
        ops[0x40 + z].slots = z == 6 ? "rb" : "b";
    ops[0x03].slots = "ib";
    ops[0x0B].slots = "ib";
    cb_ops[0x10].slots = "b";
    cb_ops[0x18].slots = "b";
}

/* ------------------------------------------------------------------ */
/* State                                                               */

static struct {
    Machine *m;
    uint16_t pc, sp;
    int ime, ime_pending, halted;
    ShadowIsr isr[5];

    /* instruction in progress */
    int busy;
    uint16_t insn_pc;
    uint8_t insn_bank;
    uint8_t opcode;
    uint8_t cb;                /* second byte of a CB-prefixed instruction */
    const OpInfo *info;
    const char *slot;
    uint8_t operand[2];
    int noperands;
    uint16_t target;           /* control flow target */
    int taken;                 /* conditional outcome */
    uint8_t classes;           /* access table entry for indirect operands */

    /* rst $28 table dispatch */
    int table_pending;
    uint8_t table_index;
    uint16_t table_base;

    int isr_depth;
    uint64_t insn_start_cycle;
    uint64_t last_access_cycle;
    int prefetched;            /* HALT with an interrupt pending: see run_until */
    uint8_t b;                 /* register B of the original */
} S;

static void default_fail(const char *message)
{
    fprintf(stderr, "timing shadow: %s\n", message);
    abort();
}
void (*shadow_fail)(const char *message) = default_fail;
void (*shadow_trace)(uint16_t pc, uint8_t bank, int ime);
void (*shadow_frame_boundary)(void);

static void failf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void failf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    snprintf(buf + n, sizeof buf - (size_t)n, " [at %02X:%04X, opcode %02X]",
             S.insn_bank, S.insn_pc, S.opcode);
    shadow_fail(buf);
}

static const char *const event_names[] = {
    "branch", "jump table", "computed jump", "io read", "io write", "vram read",
    "vram write", "oam write", "rom bank", "shared read", "shared write",
    "bus read", "bus write", "halt", "register B",
};

uint16_t shadow_pc(void) { return S.pc; }
uint64_t shadow_insn_start_cycle(void) { return S.insn_start_cycle; }
uint64_t shadow_last_access_cycle(void) { return S.last_access_cycle; }
uint16_t shadow_sp(void) { return S.sp; }
uint8_t shadow_b(void) { return S.b; }
void shadow_set_b(uint8_t b) { S.b = b; }
bool shadow_ime(void) { return S.ime; }

int shadow_ime_pending(void) { return S.ime_pending; }
bool shadow_in_handler(void) { return S.isr_depth > 0; }

void shadow_set_position(uint16_t pc, uint16_t sp, int ime, int ime_pending, int isr_depth)
{
    S.pc = pc;
    S.sp = sp;
    S.ime = ime;
    S.ime_pending = ime_pending;
    S.halted = 0;
    S.isr_depth = isr_depth;
    S.busy = 0;
    S.table_pending = 0;
}

void shadow_resume(uint16_t pc, uint16_t sp, int ime, int ime_pending)
{
    S.pc = pc;
    S.sp = sp;
    S.ime = ime;
    S.ime_pending = ime_pending;
    S.halted = 0;
    S.busy = 0;
}

_Static_assert(sizeof S <= sizeof(ShadowSaved), "ShadowSaved too small");
void shadow_save(ShadowSaved *out) { memcpy(out->opaque, &S, sizeof S); }
void shadow_restore(const ShadowSaved *in) { memcpy(&S, in->opaque, sizeof S); }

void shadow_init(Machine *m)
{
    build_tables();
    memset(&S, 0, sizeof S);
    S.m = m;
    S.pc = 0x0100;
    S.sp = 0xFFFE;
    S.b = m->model == MODEL_AGB ? 0x01 : 0x00;     /* left by the boot ROM */
}

void shadow_set_isr(uint16_t vector, ShadowIsr fn)
{
    S.isr[(vector - 0x40) / 8] = fn;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */

static void tick(void)
{
    machine_tick(S.m, 4);
    if (S.halted && S.m->frame_done && shadow_frame_boundary)
        shadow_frame_boundary();
}

static uint8_t code_byte(uint16_t addr) { return machine_peek(S.m, addr); }

static uint8_t lookup_classes(uint8_t bank, uint16_t addr)
{
    int lo = 0, hi = shadow_access_table_size - 1;
    uint32_t key = (uint32_t)bank << 16 | addr;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const AccessEntry *e = &shadow_access_table[mid];
        uint32_t k = (uint32_t)e->bank << 16 | e->addr;
        if (k == key) return e->classes;
        if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    return ACCESS_RAM;
}

static void push_byte(uint8_t v) { S.sp--; machine_write(S.m, S.sp, v); tick(); }
static uint8_t pop_byte(void) { uint8_t v = machine_peek(S.m, S.sp); S.sp++; tick(); return v; }

static int is_shared(uint16_t a);

/* What a memory operand access at a known address needs. */
enum { NEED_NOTHING, NEED_IO, NEED_VRAM, NEED_OAM, NEED_BANK, NEED_SHARED, NEED_INDIRECT };
static int need_for_address(uint16_t addr, int write)
{
    if (addr < 0x8000) return write ? NEED_BANK : NEED_NOTHING;
    if (addr < 0xA000) return NEED_VRAM;
    if (addr >= 0xFE00 && addr < 0xFEA0) return NEED_OAM;
    if ((addr >= 0xFF00 && addr < 0xFF80) || addr == 0xFFFF) return NEED_IO;
    if (S.isr_depth == 0 && is_shared(addr)) return NEED_SHARED;
    return NEED_NOTHING;
}

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */

static void run_until(ShadowEvent *ev, int finishing_isr, uint16_t isr_sp);

/* Push PC and jump to the vector of the highest-priority pending interrupt
   (5 M-cycles), then run its C handler and walk to its reti. */
static void dispatch_interrupt(void)
{
    if (S.prefetched) {         /* HALT with an interrupt pending: return to it */
        S.pc--;
        S.prefetched = 0;
    }
    S.ime = 0;
    S.ime_pending = 0;
    tick();
    tick();
    tick();
    S.sp--;
    machine_write(S.m, S.sp, S.pc >> 8);
    tick();
    /* the vector is chosen after the high byte is pushed (the push may
       overwrite IE); IF is cleared right after the low byte */
    uint8_t pending = machine_pending_interrupts(S.m);
    uint8_t n = pending & (uint8_t)-pending;
    S.sp--;
    machine_write(S.m, S.sp, S.pc & 0xFF);
    if (n)
        machine_ack_interrupt(S.m, n);
    tick();
    if (!n)
        failf("interrupt cancelled by an IE write (not supported)");
    int bit = __builtin_ctz(n);
    S.pc = (uint16_t)(0x40 + bit * 8);
    if (!S.isr[bit])
        failf("no C handler for interrupt vector %04X", S.pc);
    uint16_t frame = S.sp;
    S.isr_depth++;
    S.isr[bit]();
    run_until(NULL, 1, frame);
    S.isr_depth--;
}

/* Leaving HALT takes one more M-cycle on a Color/Advance; on the original
   only when the request arrived in the last two cycles. */
static void wake(void)
{
    if (S.m->model != MODEL_DMG || (int64_t)S.m->cycles - S.m->min_int_time < 2)
        tick();
    S.halted = 0;
    S.m->cpu_halted = 0;
}

/* Pending interrupts are serviced before the next instruction starts,
   including one that became pending while a handler ran. */
static void check_interrupts(void)
{
    while (S.ime && machine_pending_interrupts(S.m))
        dispatch_interrupt();
}

/* ------------------------------------------------------------------ */
/* Executing instructions                                              */

static void start_instruction(void)
{
    S.insn_pc = S.pc;
    S.insn_bank = S.pc >= 0x4000 && S.pc < 0x8000 ? S.m->rom_bank : 0;
    S.insn_start_cycle = S.m->cycles;
    if (shadow_trace)
        shadow_trace(S.insn_pc, S.insn_bank, S.ime);
    S.opcode = code_byte(S.pc++);               /* opcode fetch */
    tick();
    S.info = &ops[S.opcode];
    S.noperands = 0;
    S.taken = 1;
    S.classes = 0;
    if (S.opcode == 0xCB) {
        S.cb = code_byte(S.pc++);
        tick();
        S.info = &cb_ops[S.cb];
    }
    S.slot = S.info->slots;
    if (S.opcode == 0xC1 && shadow_pop_loads_b(S.insn_bank, S.insn_pc))
        S.slot = "oPb";
    S.busy = 1;
    if (S.info->mode == M_HL || S.info->mode == M_BC || S.info->mode == M_DE || S.info->mode == M_C)
        S.classes = lookup_classes(S.insn_bank, S.insn_pc);
    switch (S.info->flow) {
    case F_UNSUPPORTED:
        failf("instruction not supported by the timing shadow");
        break;
    default:
        break;
    }
}

static uint16_t operand16(void) { return (uint16_t)(S.operand[0] | S.operand[1] << 8); }

/* Register B after instructions that compute it from known values. */
static void update_b(void)
{
    if (S.opcode == 0x06) S.b = S.operand[0];                 /* ld b,n */
    else if (S.opcode == 0x01) S.b = S.operand[1];            /* ld bc,nn */
    else if (S.opcode == 0x04) S.b++;
    else if (S.opcode == 0x05) S.b--;
    else if (S.opcode == 0xCB && (S.cb & 7) == 0) {
        uint8_t b = S.b, n = (S.cb >> 3) & 7;
        switch (S.cb >> 6) {
        case 0:
            switch (n) {
            case 0: b = (uint8_t)(b << 1 | b >> 7); break;     /* rlc */
            case 1: b = (uint8_t)(b >> 1 | b << 7); break;     /* rrc */
            case 4: b = (uint8_t)(b << 1); break;              /* sla */
            case 5: b = (uint8_t)(b >> 1 | (b & 0x80)); break; /* sra */
            case 6: b = (uint8_t)(b << 4 | b >> 4); break;     /* swap */
            case 7: b = (uint8_t)(b >> 1); break;              /* srl */
            default: break;                                    /* rl/rr: event */
            }
            break;
        case 2: b = (uint8_t)(b & ~(1 << n)); break;           /* res */
        case 3: b = (uint8_t)(b | 1 << n); break;              /* set */
        default: break;                                        /* bit */
        }
        S.b = b;
    }
}

static void finish_instruction(void)
{
    const OpInfo *in = S.info;
    update_b();
    switch (in->flow) {
    case F_JR:
        if (S.taken) S.pc = (uint16_t)(S.pc + (int8_t)S.operand[0]);
        break;
    case F_JP:
        if (S.taken) S.pc = operand16();
        break;
    case F_CALL:
        if (S.taken) S.pc = operand16();
        break;
    case F_RST:
        S.pc = (uint16_t)(S.opcode & 0x38);
        break;
    case F_RET:
    case F_RETI:
        if (S.taken) S.pc = S.target;
        if (in->flow == F_RETI) S.ime = 1;
        break;
    case F_LDSP:
        S.sp = operand16();
        break;
    case F_DI:
        S.ime = 0;
        S.ime_pending = 0;
        break;
    default:
        break;
    }
    S.busy = 0;
    /* EI enables interrupts only after the following instruction */
    if (in->flow == F_EI) {
        S.ime_pending = 2;
    }
    if (S.ime_pending == 2) S.ime_pending = 1;
    else if (S.ime_pending == 1) { S.ime = 1; S.ime_pending = 0; }
    if (S.m->frame_done && shadow_frame_boundary)
        shadow_frame_boundary();
}

/* Does event ev satisfy a memory operand access (write?) with need? */
static int accept_access(ShadowEvent *ev, int need, int write, uint16_t addr_known, int known)
{
    if (!ev) return 0;
    switch (ev->kind) {
    case SEV_IO_READ: case SEV_VRAM_READ: case SEV_SHARED_READ: case SEV_BUS_READ:
        if (write) return 0;
        break;
    case SEV_IO_WRITE: case SEV_VRAM_WRITE: case SEV_OAM_WRITE: case SEV_SHARED_WRITE:
    case SEV_BUS_WRITE: case SEV_BANK:
        if (!write) return 0;
        break;
    default:
        return 0;
    }
    if (known && ev->kind != SEV_BANK && ev->addr != addr_known)
        return 0;
    switch (need) {
    case NEED_IO:     return ev->kind == SEV_IO_READ || ev->kind == SEV_IO_WRITE || ev->kind == SEV_BUS_READ || ev->kind == SEV_BUS_WRITE;
    case NEED_VRAM:   return ev->kind == SEV_VRAM_READ || ev->kind == SEV_VRAM_WRITE || ev->kind == SEV_BUS_READ || ev->kind == SEV_BUS_WRITE;
    case NEED_OAM:    return ev->kind == SEV_OAM_WRITE || ev->kind == SEV_BUS_WRITE;
    case NEED_BANK:   return ev->kind == SEV_BANK || ev->kind == SEV_BUS_WRITE;
    case NEED_SHARED: return ev->kind == SEV_SHARED_READ || ev->kind == SEV_SHARED_WRITE || ev->kind == SEV_BUS_READ || ev->kind == SEV_BUS_WRITE;
    case NEED_INDIRECT: return 1;
    }
    return 0;
}

static void perform_access(ShadowEvent *ev)
{
    switch (ev->kind) {
    case SEV_IO_READ: case SEV_VRAM_READ: case SEV_BUS_READ: case SEV_SHARED_READ:
        ev->value = machine_read(S.m, ev->addr);
        break;
    default:
        machine_write(S.m, ev->addr, ev->value);
        break;
    }
}

/* Process slots of the current instruction. Returns 1 when ev was consumed
   (or ev == NULL and nothing blocks), 0 if the instruction needs an event
   that was not given (it stays in progress). */
static int run_slots(ShadowEvent **evp)
{
    ShadowEvent *ev = *evp;
    while (*S.slot) {
        char c = *S.slot;
        switch (c) {
        case 'f':
            S.operand[S.noperands++] = code_byte(S.pc++);
            tick();
            break;
        case 'i':
            tick();
            break;
        case 'p':
            if (S.info->flow == F_CALL || S.info->flow == F_RST) push_byte(S.pc >> 8);
            else push_byte(S.opcode == 0xC5 ? S.b : 0);
            break;
        case 'q':
            if (S.info->flow == F_CALL || S.info->flow == F_RST) push_byte(S.pc & 0xFF);
            else push_byte(0);
            break;
        case 'o':
            S.target = pop_byte();
            break;
        case 'P':
            S.target = (uint16_t)(S.target | pop_byte() << 8);
            if (S.opcode == 0xC1)
                S.b = (uint8_t)(S.target >> 8);          /* pop bc */
            break;
        case 'b':
            if (!ev) return 0;
            if (ev->kind != SEV_REG_B)
                failf("C code reported %s, but the original loads register B here (reg_b)", event_names[ev->kind]);
            S.b = ev->value;
            *evp = ev = NULL;
            break;
        case '?': {
            if (!ev) return 0;
            if (ev->kind != SEV_BRANCH)
                failf("C code reported %s, but the original has a conditional branch here", event_names[ev->kind]);
            S.taken = ev->value != 0;
            *evp = ev = NULL;
            S.slot++;
            if (!S.taken) {
                S.slot = "";
            }
            continue;
        }
        case 'r':
        case 'w': {
            int write = c == 'w';
            int mode = S.info->mode;
            int known = mode == M_ABS || mode == M_HIGH;
            uint16_t addr = mode == M_ABS ? operand16() : (uint16_t)(0xFF00 | S.operand[0]);
            int need;
            if (known) {
                need = need_for_address(addr, write);
            } else if (S.classes & (ACCESS_HW | ACCESS_SHARED)) {
                need = (S.classes & ACCESS_SHARED) && !(S.classes & ACCESS_HW) && S.isr_depth > 0
                     ? NEED_NOTHING : NEED_INDIRECT;
            } else {
                need = NEED_NOTHING;
            }
            if (need == NEED_NOTHING) {
                tick();                       /* plain RAM: the C code does it */
                break;
            }
            if (!ev) return 0;
            if (!accept_access(ev, need, write, addr, known))
                failf("C code reported %s %04X, but the original %s %s%04X here",
                      event_names[ev->kind], ev->addr, write ? "writes" : "reads",
                      known ? "" : "via a pointer, e.g. ", known ? addr : ev->addr);
            S.last_access_cycle = S.m->cycles;
            perform_access(ev);
            tick();
            *evp = ev = NULL;
            break;
        }
        }
        S.slot++;
    }
    return 1;
}

/* Run the original forward until event ev has been consumed and the
   instruction that consumed it has gone as far as it can, or, when finishing
   an interrupt handler (ev == NULL), until the handler's frame is popped. */
static void run_until(ShadowEvent *ev, int finishing_isr, uint16_t isr_sp)
{
    for (;;) {
        if (!S.busy) {
            if (finishing_isr ? S.sp > isr_sp : ev == NULL)
                return;
            check_interrupts();
            start_instruction();
            /* instructions that take an event as a whole */
            switch (S.info->flow) {
            case F_HALT:
                if (!ev || ev->kind != SEV_HALT)
                    failf("the original halts here, but the C code reported %s",
                          ev ? event_names[ev->kind] : "nothing (end of interrupt handler)");
                ev = NULL;
                finish_instruction();
                for (;;) {
                    if (machine_pending_interrupts(S.m)) {
                        /* An interrupt is already pending: the CPU does not
                           sleep. If it is enabled it is taken and returns to
                           the HALT, which then runs again. */
                        if (!S.ime)
                            failf("HALT bug (HALT with interrupts disabled and one pending) is not supported");
                        S.prefetched = 1;
                        dispatch_interrupt();          /* returns to the HALT */
                        check_interrupts();
                        S.insn_start_cycle = S.m->cycles;
                        S.pc++;                        /* the HALT runs again */
                        tick();
                        if (S.ime_pending == 2) S.ime_pending = 1;
                        else if (S.ime_pending == 1) { S.ime = 1; S.ime_pending = 0; }
                        continue;
                    }
                    S.halted = 1;
                    tick();
                    S.m->cpu_halted = 1;
                    if (S.m->model == MODEL_DMG)
                        tick();
                    while (!machine_pending_interrupts(S.m))
                        tick();
                    wake();
                    check_interrupts();
                    break;
                }
                continue;
            case F_RST:
                if (S.opcode != 0xEF)
                    failf("rst other than $28 is not supported");
                if (!ev || ev->kind != SEV_TABLE)
                    failf("the original dispatches through a jump table (rst $28) here, but the C code reported %s",
                          ev ? event_names[ev->kind] : "nothing");
                S.table_pending = 1;
                S.table_index = ev->value;
                S.table_base = S.pc;
                ev = NULL;
                break;
            case F_JPHL: {
                uint16_t target;
                if (S.table_pending && S.insn_pc == 0x0033) {
                    uint16_t entry = (uint16_t)(S.table_base + 2 * S.table_index);
                    target = (uint16_t)(code_byte(entry) | code_byte((uint16_t)(entry + 1)) << 8);
                    S.table_pending = 0;
                } else {
                    if (!ev || ev->kind != SEV_JUMP)
                        failf("the original jumps to a computed address (jp hl) here, but the C code reported %s",
                              ev ? event_names[ev->kind] : "nothing");
                    target = ev->addr;
                    ev = NULL;
                }
                S.pc = target;
                finish_instruction();
                continue;
            }
            default:
                break;
            }
        }
        if (run_slots(&ev)) {
            finish_instruction();
        } else {
            /* the instruction waits for an event */
            if (ev)
                failf("internal: event %s not consumed", event_names[ev->kind]);
            if (finishing_isr)
                failf("the C interrupt handler returned, but the original still needs an event here");
            return;     /* our event was consumed; continue at the next one */
        }
    }
}

void shadow_event(ShadowEvent *ev)
{
    run_until(ev, 0, 0);
}

void shadow_finish_isr(void)
{
}

void shadow_finish_call(uint16_t sp0)
{
    run_until(NULL, 1, sp0);
}

/* ------------------------------------------------------------------ */

#include "../game/shared.h"
static int is_shared(uint16_t a) { return is_shared_address(a); }
