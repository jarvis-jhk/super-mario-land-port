/*
 * Reference emulator = hardware model (src/hw/machine.c) + SM83 interpreter.
 * Runs the original ROM; used only for verification.
 */
#ifndef GB_H
#define GB_H

#include "sm83.h"

typedef struct {
    Machine m;
    Sm83 cpu;
} GB;

GB *gb_create(MachineModel model, const uint8_t *rom, size_t rom_size);
void gb_destroy(GB *gb);
/* Run until the next frame boundary (start of vblank, or 70224 cycles
   while the LCD is off). Calls m.hooks.on_frame afterwards. */
void gb_run_frame(GB *gb);

#endif
