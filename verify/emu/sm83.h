/*
 * SM83 (Game Boy CPU) interpreter, M-cycle timed.
 *
 * Used by the reference emulator to run the ORIGINAL game code. It is not
 * part of the shipped port. Every bus access advances the shared hardware
 * model (src/hw/machine.c) by one M-cycle before the access happens.
 */
#ifndef SM83_H
#define SM83_H

#include <stdint.h>
#include "../../src/hw/machine.h"

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t ime, halted;
} Sm83Regs;

enum { ROM_ACCESS_OPCODE = 1, ROM_ACCESS_OPERAND = 2, ROM_ACCESS_DATA = 4 };

typedef struct Sm83 Sm83;
struct Sm83 {
    Machine *m;
    Sm83Regs r;
    uint8_t ime_pending;      /* EI takes effect after the next instruction */
    uint8_t prefetched;       /* HALT fetched the next opcode without advancing PC */
    uint8_t opcode;           /* that opcode */
    uint8_t locked;           /* executed an illegal opcode */

    /* Instrumentation */
    void *user;
    /* Called before executing an instruction at a flagged ROM address.
       Return nonzero if the hook handled the instruction itself. */
    int (*on_exec)(Sm83 *cpu, uint16_t pc, void *user);
    uint8_t *exec_hook;       /* 4 banks * 0x4000 flags */
    /* Called before every instruction fetch (instruction tracing). */
    void (*on_trace)(Sm83 *cpu, void *user);
    /* Optional ROM access map (rom_size bytes): ROM_ACCESS_* bits per byte,
       used to tell code from data. */
    uint8_t *rom_access;
    int fetching;
};

void sm83_init(Sm83 *cpu, Machine *m);
/* Execute one instruction, one halted M-cycle, or one interrupt dispatch. */
void sm83_step(Sm83 *cpu);
void sm83_set_exec_hook(Sm83 *cpu, int bank, uint16_t addr, int on);

#endif
