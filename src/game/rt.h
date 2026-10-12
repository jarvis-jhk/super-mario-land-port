/*
 * Runtime interface for the translated game code.
 *
 * The game logic is ordinary C operating on the Game Boy's memory, laid out
 * exactly like the original (see ram.h), so that save states, achievements,
 * TAS tools and the verification harness all see the same bytes.
 *
 * To be *cycle-exact* the C code tells the runtime the few things that the
 * timing model cannot infer by itself (see docs/TRANSLATION.md):
 *
 *   taken(cond)        The original has a conditional branch here (jr cc,
 *                      jp cc, call cc, ret cc) and it is taken iff cond.
 *                      Returns cond, so it is written inside the if/while:
 *                          if (taken(lives == 0)) return;
 *   jump_table(i)      The original dispatches through `rst $28` with index i.
 *   io_read/io_write   Hardware registers (FF00-FF7F, FFFF).
 *   vram_read/write    Video RAM (8000-9FFF), which the LCD may lock.
 *   oam_write          Sprite memory (FE00-FE9F).
 *   set_rom_bank(b)    MBC1 bank switch (the original writes to $2000).
 *   halt()             The original executes HALT (waits for an interrupt).
 *
 * Plain RAM is accessed directly. Everything else about timing (straight-line
 * code, calls, returns, unconditional jumps) is followed automatically by the
 * runtime from the original instruction stream.
 */
#ifndef SML_RT_H
#define SML_RT_H

#include <stdbool.h>
#include <stdint.h>
#include "../hw/machine.h"

/* The hardware the game runs on (set by the core or by the test harness). */
extern Machine *g_hw;

/* Memory access helpers for the original addresses */
#define WRAM(addr) (g_hw->wram[(addr) - 0xC000])
#define HRAM(addr) (g_hw->hram[(addr) - 0xFF80])
#define WRAM_PTR(addr) (&g_hw->wram[(addr) - 0xC000])

/* ROM data: bank 0 for 0000-3FFF, else the given bank for 4000-7FFF */
static inline const uint8_t *rom_ptr(int bank, uint16_t addr)
{
    return g_hw->rom + (addr < 0x4000 ? addr : (size_t)bank * 0x4000 + (addr - 0x4000));
}
static inline uint8_t rom_byte(int bank, uint16_t addr) { return *rom_ptr(bank, addr); }

/* Read ROM, work RAM or high RAM at a computed address (no hardware). */
static inline uint8_t mem_read(uint16_t addr) { return machine_peek(g_hw, addr); }

/* Read/write work RAM or high RAM at a computed address. */
static inline uint8_t ram_read(uint16_t addr) { return machine_peek(g_hw, addr); }
static inline void ram_write(uint16_t addr, uint8_t value)
{
    if (addr >= 0xFF80) g_hw->hram[(addr - 0xFF80) & 0x7F] = value;
    else g_hw->wram[(addr - 0xC000) & 0x1FFF] = value;
}

/* Timing/hardware interface (implemented by rt_shadow.c in the port and by
   the function tester in verify/fntest). */
bool taken(bool cond);
/* A loop of the original runs `iterations` times (>= 1) with nothing but
   arithmetic in it: reports its loop branch taken iterations-1 times, then
   not taken. Lets the C code compute the result directly. */
void loop_taken(unsigned iterations);
void jump_table(uint8_t index);
/* The original jumps to a computed address (jp hl) */
void computed_jump(uint16_t target);
uint8_t io_read(uint16_t addr);
void io_write(uint16_t addr, uint8_t value);
uint8_t vram_read(uint16_t addr);
void vram_write(uint16_t addr, uint8_t value);
void oam_write(uint16_t addr, uint8_t value);
void set_rom_bank(uint8_t bank);
void halt(void);

/* The original loads register B from another register or from memory
   (ld b,r / ld b,[hl]), changes it with inc/dec bc, or rotates it through
   the carry: value is the new B. Returns value, so it can wrap the
   expression that computes it:  uint8_t count = reg_b(enemy_count);
   B matters beyond the routine because the vblank handler can observe the
   value an interrupted routine left in it (see block_events()). */
uint8_t reg_b(uint8_t value);
/* inc bc / dec bc on a counter or pointer that the original keeps in BC. */
static inline uint16_t inc_bc(uint16_t bc) { bc++; reg_b((uint8_t)(bc >> 8)); return bc; }
static inline uint16_t dec_bc(uint16_t bc) { bc--; reg_b((uint8_t)(bc >> 8)); return bc; }
/* Inside an interrupt handler: register B of the interrupted code. */
uint8_t interrupted_b(void);

/* Variables shared with interrupt handlers (shared.h). Inside a handler
   these are plain accesses, so routines that run both in the main program
   and in a handler (e.g. HUD printing) use them everywhere. */
uint8_t shared_read(uint16_t addr);
void shared_write(uint16_t addr, uint8_t value);

/* Access through a pointer by an instruction that touches hardware (or
   both hardware and RAM) depending on the pointer, e.g. generic copy loops.
   Which instructions these are is in src/core/access_table.c (classes 2+:
   see docs/TRANSLATION.md); every access such an instruction makes goes
   through bus_*, even when it hits RAM. Pointer accesses by instructions
   that only ever touch ROM/RAM use mem_read/ram_write instead. */
uint8_t bus_read(uint16_t addr);
void bus_write(uint16_t addr, uint8_t value);
/* Pointer read by an instruction that touches shared variables and plain
   RAM (class 5): bus_read in main-program code, a plain read inside an
   interrupt handler (like shared_read). */
uint8_t shared_or_ram_read(uint16_t addr);

#endif
