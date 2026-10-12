/*
 * Hybrid execution (development stage of the port).
 *
 * While the translation is incomplete, the original code runs on the SM83
 * interpreter, and every routine that already has a C translation runs as C
 * instead, timed by the shadow (shadow.c), on the same hardware. The
 * translations and their register adapters are the ones registered for the
 * function tests (verify/fntest/registry*.c).
 *
 * Original code calls a translated routine: the interpreter's exec hook at
 * the routine's address hands over to C; when the C function returns the
 * shadow walks the original to the routine's `ret`, and the interpreter
 * continues there with the adapter's output registers.
 *
 * An interrupt while C code runs: the shadow dispatches it; if the handler
 * has no translation, the interpreter runs it (and may call translated
 * routines again, nesting), then the shadow continues.
 *
 * The port itself does not use this (src/core/core.c runs the C game on the
 * shadow alone); it remains a verification tool for bisecting divergences.
 */
#include "hybrid.h"
#include "../../src/core/shadow.h"
#include "../fntest/fntest.h"
#include "../../src/game/rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FNTEST_TABLES(FNTEST_DECLARE)

static Sm83 *cpu;
static int isr_depth;               /* interpreter-side interrupt nesting */
static uint16_t frame_sp[32];
static uint16_t prev_pc, prev_expected;
static int prev_valid;
static const FnTest *by_entry[4][0x8000];
static long c_calls;

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

/* Forget the handlers that have returned: the stack is above their frame. */
static void drop_returned_isrs(uint16_t sp)
{
    while (isr_depth > 0 && sp > frame_sp[isr_depth - 1])
        isr_depth--;
}

/* Track the interpreter's interrupt nesting (needed to tell the shadow
   whether code runs in a handler). */
static void on_trace(Sm83 *c, void *user)
{
    (void)user;
    uint16_t pc = c->r.pc;
    int dispatched = prev_valid && (pc == 0x40 || pc == 0x48 || pc == 0x50 || pc == 0x58 || pc == 0x60) &&
                     pc != prev_expected && !(prev_pc == 0x40 && pc == 0x60);
    drop_returned_isrs(dispatched ? (uint16_t)(c->r.sp + 2) : c->r.sp);
    if (dispatched && isr_depth < 32)
        frame_sp[isr_depth++] = c->r.sp;
    prev_pc = pc;
    prev_expected = (uint16_t)(pc + insn_len(machine_peek(c->m, pc)));
    prev_valid = 1;
}

/* Interrupt dispatched by the shadow while C code runs: run the original
   handler on the interpreter. */
static void interpreted_isr(void)
{
    ShadowSaved saved;
    shadow_save(&saved);
    cpu->r.pc = shadow_pc();
    cpu->r.sp = shadow_sp();
    cpu->r.b = shadow_b();
    cpu->r.ime = 0;
    cpu->ime_pending = 0;
    cpu->r.halted = 0;
    uint16_t frame = cpu->r.sp;
    if (isr_depth < 32)
        frame_sp[isr_depth++] = frame;
    prev_valid = 0;
    while (cpu->r.sp <= frame)
        sm83_step(cpu);
    while (isr_depth > 0 && cpu->r.sp > frame_sp[isr_depth - 1])
        isr_depth--;
    shadow_restore(&saved);
    /* the handler has returned: continue the shadow there */
    shadow_resume(cpu->r.pc, cpu->r.sp, cpu->r.ime, cpu->ime_pending);
    shadow_set_b(cpu->r.b);
}

static int on_exec(Sm83 *c, uint16_t pc, void *user)
{
    (void)user;
    int bank = pc < 0x4000 ? 0 : c->m->rom_bank;
    const FnTest *t = by_entry[bank][pc & 0x7FFF];
    if (!t)
        return 0;
    /* The reset chord never returns to this call frame. Hybrid ownership
       stays with the interpreter through bootstrap; the C main loop owns
       this transition when SML_C_MAIN is enabled. */
    if (bank == 0 && pc == 0x07DA && (c->m->hram[0] & 15) == 15)
        return 0;
    /* Vector hooks run before the interpreter's trace hook. Record the
       dispatch now so shared accesses in the C handler are interrupt-local. */
    if (bank == 0 && (pc == 0x40 || pc == 0x48 || pc == 0x50))
        on_trace(c, user);
    /* A handler can return (reti) straight to a translated routine's entry,
       e.g. when the interrupt came right after the `call`: the exec hook then
       runs before on_trace has seen that the handler is done. */
    drop_returned_isrs(c->r.sp);
    ShadowSaved saved;
    shadow_save(&saved);
    uint16_t sp0 = c->r.sp;
    shadow_set_position(pc, sp0, c->r.ime, c->ime_pending, isr_depth);
    shadow_set_b(c->r.b);
    Sm83Regs r = c->r;
    t->adapter(&r);
    shadow_finish_call(sp0);
    r.pc = shadow_pc();
    r.sp = shadow_sp();
    /* B as the shadow followed it: a wrong reg_b() shows up as a lockstep
       divergence of the interpreted code that uses B afterwards */
    r.b = shadow_b();
    r.ime = (uint8_t)shadow_ime();
    r.halted = 0;
    c->r = r;
    c->ime_pending = (uint8_t)shadow_ime_pending();
    shadow_restore(&saved);
    /* the interpreter continues at the return address; an interrupt taken
       right there must still be recognised as one */
    prev_pc = 0;
    prev_expected = c->r.pc;
    prev_valid = 1;
    c_calls++;
    return 1;
}

void hybrid_init(Sm83 *interpreter, Machine *m, const char *only)
{
    cpu = interpreter;
    g_hw = m;
    shadow_init(m);
    for (uint16_t v = 0x40; v <= 0x60; v += 8)
        shadow_set_isr(v, interpreted_isr);
    memset(by_entry, 0, sizeof by_entry);
    int n = 0;
#define HYBRID_ADD(sub)                                                        \
    for (const FnTest *t = fn_tests_##sub; t && t->name; t++) {                \
        if (only && !strstr(only, t->name)) continue;                          \
        by_entry[t->bank][t->addr & 0x7FFF] = t;                               \
        sm83_set_exec_hook(cpu, t->bank, t->addr, 1);                          \
        n++;                                                                   \
    }
    FNTEST_TABLES(HYBRID_ADD)
    cpu->on_exec = hybrid_chain_exec;
    cpu->on_trace = on_trace;
    fprintf(stderr, "hybrid: %d translated routines active\n", n);
}

/* The core also hooks ReadJoypad; chain to it. */
int (*hybrid_next_exec)(Sm83 *c, uint16_t pc, void *user);

int hybrid_chain_exec(Sm83 *c, uint16_t pc, void *user)
{
    if (hybrid_next_exec && hybrid_next_exec(c, pc, user))
        return 1;
    return on_exec(c, pc, user);
}

long hybrid_c_calls(void) { return c_calls; }
