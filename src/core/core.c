/*
 * Core runtime: the translated game on the timing shadow.
 *
 * The C game (src/game) owns the program: run_game() is the original's
 * initialization and endless frame loop. The shadow walks the original
 * instruction stream alongside it, so the hardware model advances exactly
 * as on the console, and it dispatches interrupts to the C handlers at the
 * exact instruction boundary. The platform is served from shadow hooks:
 * joypad polls when the original reaches ReadJoypad, frames at every LCD
 * frame boundary (also while the game waits in HALT).
 */
#include "core.h"
#include "shadow.h"
#include "../game/game.h"
#include "../game/rt.h"
#include <setjmp.h>

/* Where the original game reads the joypad: ReadJoypad, bank 3 $47F2. */
#define READ_JOYPAD_BANK 3
#define READ_JOYPAD_ADDR 0x47F2

static Machine machine;
static const CoreCallbacks *cb;
static jmp_buf quit_jump;
static long polls;

Machine *core_machine(void) { return &machine; }
long core_poll_count(void) { return polls; }
int core_has_access_table(void) { return shadow_access_table_size >= 0; }

void core_quit(void)
{
    longjmp(quit_jump, 1);
}

static void on_instruction(uint16_t pc, uint8_t bank, int ime)
{
    (void)ime;
    if (pc == READ_JOYPAD_ADDR && bank == READ_JOYPAD_BANK) {
        machine.buttons = cb->poll_input ? cb->poll_input(cb->user) : 0;
        polls++;
    }
}

static void on_frame(void)
{
    machine.frame_done = 0;
    machine.frame_count++;
    if (cb->frame) cb->frame(cb->user);
}

/* The vblank handler sees register B of the code it interrupted. */
static void vblank_isr(void) { vblank_interrupt(interrupted_b()); }

void core_run(MachineModel model, const uint8_t *rom, size_t rom_size,
              const CoreCallbacks *callbacks)
{
    int16_t *audio = machine.apu.out;
    size_t audio_cap = machine.apu.out_cap;
    machine_init(&machine, model, rom, rom_size);
    machine.apu.out = audio;
    machine.apu.out_cap = audio_cap;
    g_hw = &machine;
    shadow_init(&machine);
    shadow_set_isr(0x40, vblank_isr);
    shadow_set_isr(0x48, stat_interrupt);
    shadow_set_isr(0x50, timer_interrupt);
    cb = callbacks;
    polls = 0;
    shadow_trace = on_instruction;
    shadow_frame_boundary = on_frame;
    if (setjmp(quit_jump) == 0)
        run_game();
    shadow_trace = NULL;
    shadow_frame_boundary = NULL;
}
