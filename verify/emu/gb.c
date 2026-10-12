#include "gb.h"
#include <stdlib.h>

GB *gb_create(MachineModel model, const uint8_t *rom, size_t rom_size)
{
    GB *gb = calloc(1, sizeof *gb);
    machine_init(&gb->m, model, rom, rom_size);
    sm83_init(&gb->cpu, &gb->m);
    return gb;
}

void gb_destroy(GB *gb)
{
    free(gb->cpu.exec_hook);
    free(gb);
}

void gb_run_frame(GB *gb)
{
    gb->m.frame_done = 0;
    while (!gb->m.frame_done)
        sm83_step(&gb->cpu);
    gb->m.frame_count++;
    if (gb->m.hooks.on_frame)
        gb->m.hooks.on_frame(&gb->m, gb->m.hooks.user);
}
