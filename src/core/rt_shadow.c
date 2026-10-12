/*
 * The runtime interface of the translated game (src/game/rt.h), backed by
 * the timing shadow: every event is handed to the shadow, which performs
 * hardware accesses at their exact cycle.
 */
#include "shadow.h"
#include "../game/rt.h"

Machine *g_hw;

static uint8_t event(ShadowEventKind kind, uint16_t addr, uint8_t value)
{
    ShadowEvent ev = { kind, addr, value };
    shadow_event(&ev);
    return ev.value;
}

bool taken(bool cond)
{
    event(SEV_BRANCH, 0, cond ? 1 : 0);
    return cond;
}

void loop_taken(unsigned iterations)
{
    for (unsigned i = 1; i < iterations; i++)
        event(SEV_BRANCH, 0, 1);
    event(SEV_BRANCH, 0, 0);
}

void jump_table(uint8_t index) { event(SEV_TABLE, 0, index); }
void computed_jump(uint16_t target) { event(SEV_JUMP, target, 0); }
uint8_t io_read(uint16_t addr) { return event(SEV_IO_READ, addr, 0); }
void io_write(uint16_t addr, uint8_t value) { event(SEV_IO_WRITE, addr, value); }
uint8_t vram_read(uint16_t addr) { return event(SEV_VRAM_READ, addr, 0); }
void vram_write(uint16_t addr, uint8_t value) { event(SEV_VRAM_WRITE, addr, value); }
void oam_write(uint16_t addr, uint8_t value) { event(SEV_OAM_WRITE, addr, value); }
void set_rom_bank(uint8_t bank) { event(SEV_BANK, 0x2000, bank); }
void halt(void) { event(SEV_HALT, 0, 0); }
uint8_t reg_b(uint8_t value) { event(SEV_REG_B, 0, value); return value; }
uint8_t interrupted_b(void) { return shadow_b(); }
/* Inside a handler, nothing can interrupt the access: plain RAM. */
uint8_t shared_read(uint16_t addr)
{
    return shadow_in_handler() ? machine_peek(g_hw, addr) : event(SEV_SHARED_READ, addr, 0);
}
void shared_write(uint16_t addr, uint8_t value)
{
    if (shadow_in_handler()) machine_write(g_hw, addr, value);
    else event(SEV_SHARED_WRITE, addr, value);
}
uint8_t bus_read(uint16_t addr) { return event(SEV_BUS_READ, addr, 0); }
uint8_t shared_or_ram_read(uint16_t addr)
{
    return shadow_in_handler() ? machine_peek(g_hw, addr) : bus_read(addr);
}
void bus_write(uint16_t addr, uint8_t value) { event(SEV_BUS_WRITE, addr, value); }
