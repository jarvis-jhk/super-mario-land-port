/*
 * The timing shadow: what makes the C port cycle-exact.
 *
 * The original game's timing is the timing of its instructions. The shadow
 * walks the original instruction stream (decoded from the ROM) alongside the
 * C code and charges every instruction's exact M-cycles to the hardware model
 * (machine.c), so the LCD, timer and sound chip advance exactly as on the
 * console. It dispatches interrupts at the exact instruction boundary and
 * runs the C interrupt handlers there.
 *
 * It does not execute the original's computations; the C code does. The
 * shadow only needs to know what it cannot work out from the instruction
 * stream itself, which the C code reports as events (see src/game/rt.h):
 * conditional branch outcomes, jump table indices, computed jump targets,
 * hardware accesses (performed by the shadow at their exact cycle), accesses
 * to variables shared with interrupt handlers, and HALT. Calls, returns,
 * unconditional jumps and straight-line code are followed automatically.
 *
 * Each event must match the next instruction of the original that needs one;
 * if it does not, the C code has diverged from the original's structure and
 * the shadow stops with a diagnostic (shadow_fail). That makes it a
 * permanent self-check of the translation.
 */
#ifndef SML_SHADOW_H
#define SML_SHADOW_H

#include <stdbool.h>
#include <stdint.h>
#include "../hw/machine.h"

typedef enum {
    SEV_BRANCH,        /* value = taken */
    SEV_TABLE,         /* rst $28 jump table; value = index */
    SEV_JUMP,          /* jp hl; addr = target */
    SEV_IO_READ, SEV_IO_WRITE,
    SEV_VRAM_READ, SEV_VRAM_WRITE,
    SEV_OAM_WRITE,
    SEV_BANK,          /* write to the MBC bank register; value = bank */
    SEV_SHARED_READ, SEV_SHARED_WRITE,
    SEV_BUS_READ, SEV_BUS_WRITE,
    SEV_HALT,
    SEV_REG_B,         /* the original loads register B; value = new B */
} ShadowEventKind;

typedef struct {
    ShadowEventKind kind;
    uint16_t addr;
    uint8_t value;     /* in: value written / branch outcome; out: value read */
} ShadowEvent;

/* Interrupt handler written in C, run at the vector's address. */
typedef void (*ShadowIsr)(void);

/* Classification of instructions with a register-indirect memory operand
   (ld a,[hl] etc.), which the shadow cannot resolve by itself. Produced from
   the original's behaviour by `emurun --access-table`. */
enum {
    ACCESS_RAM = 1,      /* plain RAM: no event */
    ACCESS_HW = 2,       /* hardware (IO/VRAM/OAM): io_/vram_/oam_ event */
    ACCESS_SHARED = 4,   /* a variable shared with interrupt handlers */
};
typedef struct { uint8_t bank; uint16_t addr; uint8_t classes; } AccessEntry;
extern const AccessEntry shadow_access_table[];
extern const int shadow_access_table_size;

typedef struct Shadow Shadow;

void shadow_init(Machine *m);
/* Interrupt vector (0x40, 0x48, 0x50, 0x58, 0x60) handled by fn. */
void shadow_set_isr(uint16_t vector, ShadowIsr fn);
/* Process one event from the C code (blocks until the original reaches it). */
void shadow_event(ShadowEvent *ev);
/* Called when a C interrupt handler returns: run to its reti. */
void shadow_finish_isr(void);

/* Position of the shadow (for diagnostics and hybrid mode). */
uint16_t shadow_pc(void);
uint16_t shadow_sp(void);
bool shadow_ime(void);
/* Register B of the original. The vblank handler's bouncing-block path
   reads the value an interrupted routine left in B, so the shadow keeps
   it: loads from constants, inc/dec, shifts and push/pop bc are followed
   automatically; the C code reports loads from other registers or memory
   with reg_b() (src/game/rt.h). */
uint8_t shadow_b(void);
void shadow_set_b(uint8_t b);
/* `pop bc` instructions that do not restore a `push bc` (PrintVictoryMessage
   pushes AF and pops it into BC): B comes from the C code as well. */
static inline bool shadow_pop_loads_b(uint8_t bank, uint16_t pc)
{
    return bank == 0 && pc == 0x0FDC;
}
int shadow_ime_pending(void);
/* The code being walked runs inside an interrupt handler. */
bool shadow_in_handler(void);
/* Start walking at pc with the given stack pointer and interrupt state;
   isr_depth > 0 means the code runs inside an interrupt handler. */
void shadow_set_position(uint16_t pc, uint16_t sp, int ime, int ime_pending, int isr_depth);
/* After an interrupt handler ran elsewhere (on the interpreter): continue
   at pc/sp with this interrupt state, keeping the nesting bookkeeping. */
void shadow_resume(uint16_t pc, uint16_t sp, int ime, int ime_pending);
/* Walk the original until the routine whose return address is at sp0
   has returned (no events may be needed on the way). */
void shadow_finish_call(uint16_t sp0);

/* The shadow's state can be saved and restored around nested use (hybrid
   mode: original code running on the interpreter calls translated code
   that is interrupted by original code ...). */
typedef struct { unsigned char opaque[192]; } ShadowSaved;
void shadow_save(ShadowSaved *out);
void shadow_restore(const ShadowSaved *in);

/* Cycle count at the start of the instruction the shadow is on, and at
   the last hardware access it performed. */
uint64_t shadow_insn_start_cycle(void);
uint64_t shadow_last_access_cycle(void);

/* Debug hook: called at the start of every instruction the shadow walks. */
extern void (*shadow_trace)(uint16_t pc, uint8_t bank, int ime);
/* Platform frame boundaries, after an instruction or a halted M-cycle. */
extern void (*shadow_frame_boundary)(void);

/* Failure hook: default prints and aborts. */
extern void (*shadow_fail)(const char *message);

#endif
