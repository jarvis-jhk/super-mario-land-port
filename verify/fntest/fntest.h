/*
 * Per-function differential testing of translated code.
 *
 * For every registered function the tester runs the reference emulator over
 * an input movie and, each time the original routine is entered, takes a
 * snapshot of the whole machine and then
 *   1. runs the ORIGINAL routine from the snapshot on the SM83 interpreter
 *      until it returns, recording the conditional branch outcomes, the jump
 *      table indices and every hardware register / VRAM / OAM access;
 *   2. runs the C translation from the same snapshot, with hardware reads
 *      answered from the recording (so timing-dependent values like STAT,
 *      LY or DIV are identical), recording the same events;
 *   3. compares memory, the event sequences and the declared output
 *      registers. Any difference is reported with the poll number, so the
 *      case can be reproduced.
 * Interrupts are disabled in both runs; interrupt interplay and cycle timing
 * are verified separately by the full-game lockstep test.
 */
#ifndef FNTEST_H
#define FNTEST_H

#include "../emu/sm83.h"

/* Output registers a routine produces for its callers (bit set). */
enum {
    OUT_A = 1 << 0, OUT_B = 1 << 1, OUT_C = 1 << 2, OUT_D = 1 << 3, OUT_E = 1 << 4,
    OUT_H = 1 << 5, OUT_L = 1 << 6, OUT_ZF = 1 << 7, OUT_CF = 1 << 8,
    OUT_BC = OUT_B | OUT_C, OUT_DE = OUT_D | OUT_E, OUT_HL = OUT_H | OUT_L,
};

typedef struct {
    const char *name;
    int bank;                  /* 0 for 0000-3FFF */
    uint16_t addr;
    /* Calls the C function with arguments taken from regs and stores its
       results back into regs (only the OUT_* bits declared are compared). */
    void (*adapter)(Sm83Regs *regs);
    unsigned outputs;
    int in_interrupt;          /* runs inside an interrupt handler: shared
                                  variables are accessed directly */
    /* Optional: returns 0 for fuzzed states the game can never be in (e.g. a
       sound number outside its table); those are skipped. Document why. */
    int (*valid_state)(const Machine *m);
} FnTest;

/* Each subsystem registers its functions in its own file
   (verify/fntest/registry_<subsystem>.c) as
       const FnTest fn_tests_<subsystem>[] = { ..., { 0 } };
   terminated by an entry with name == NULL. registry.c lists the tables;
   missing ones (not linked) are skipped. */
#define FNTEST_TABLES(X) X(core) X(sound) X(enemies) X(player) X(level)
#define FNTEST_DECLARE(name) extern const FnTest fn_tests_##name[] __attribute__((weak));
FNTEST_TABLES(FNTEST_DECLARE)

#endif
