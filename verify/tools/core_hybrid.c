/*
 * Development core (verification only): the same interface as the port's
 * core (src/core/core.c), but the original program runs on the SM83
 * interpreter and every translated routine replaces its original through
 * the hybrid bridge (src/core/hybrid.c). Used by portrun_hybrid to bisect a
 * divergence to single routines (SML_HYBRID_ONLY=Name1,Name2).
 * SML_C_MAIN=1 runs initialization and the frame loop in C as well, with
 * interrupt handlers still entered through the interpreter.
 */
#include "../../src/core/core.h"
#include "../emu/sm83.h"
#include "hybrid.h"
#include "../../src/core/shadow.h"
#include "../../src/game/game.h"
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/* Where the original game reads the joypad: ReadJoypad, bank 3 $47F2. */
#define READ_JOYPAD_BANK 3
#define READ_JOYPAD_ADDR 0x47F2

static Machine machine;
static Sm83 cpu;
static const CoreCallbacks *cb;
static jmp_buf quit_jump;
static long polls;

Machine *core_machine(void) { return &machine; }
long core_poll_count(void) { return polls; }

void core_quit(void)
{
    longjmp(quit_jump, 1);
}

static int on_exec(Sm83 *c, uint16_t pc, void *user)
{
    (void)c; (void)user;
    if (pc == READ_JOYPAD_ADDR && machine.rom_bank == READ_JOYPAD_BANK) {
        machine.buttons = cb->poll_input ? cb->poll_input(cb->user) : 0;
        polls++;
    }
    return 0;
}

static void on_shadow_trace(uint16_t pc, uint8_t bank, int ime)
{
    (void)ime;
    if (pc == READ_JOYPAD_ADDR && bank == READ_JOYPAD_BANK) {
        machine.buttons = cb->poll_input ? cb->poll_input(cb->user) : 0;
        polls++;
    }
}

static void on_shadow_frame(void)
{
    machine.frame_done = 0;
    machine.frame_count++;
    if (cb->frame) cb->frame(cb->user);
}

void core_run(MachineModel model, const uint8_t *rom, size_t rom_size,
              const CoreCallbacks *callbacks)
{
    int16_t *audio = machine.apu.out;
    size_t audio_cap = machine.apu.out_cap;
    machine.apu.out = audio;
    machine_init(&machine, model, rom, rom_size);
    machine.apu.out = audio;
    machine.apu.out_cap = audio_cap;
    sm83_init(&cpu, &machine);
    sm83_set_exec_hook(&cpu, READ_JOYPAD_BANK, READ_JOYPAD_ADDR, 1);
    hybrid_next_exec = on_exec;
    hybrid_init(&cpu, &machine, getenv("SML_HYBRID_ONLY"));
    cb = callbacks;
    polls = 0;

    if (setjmp(quit_jump) == 0) {
        if (getenv("SML_C_MAIN")) {
            shadow_trace = on_shadow_trace;
            shadow_frame_boundary = on_shadow_frame;
            run_game();
        }
        for (;;) {
            machine.frame_done = 0;
            while (!machine.frame_done)
                sm83_step(&cpu);
            machine.frame_count++;
            if (cb->frame)
                cb->frame(cb->user);
        }
    }
    shadow_trace = NULL;
    shadow_frame_boundary = NULL;
    free(cpu.exec_hook);
    cpu.exec_hook = NULL;
}
