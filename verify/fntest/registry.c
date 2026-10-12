/*
 * Core functions under test (shared helpers), with adapters from the
 * original's register interface to the C signatures. Keep sorted by address.
 * Subsystems have their own registry_<name>.c files (see fntest.h).
 */
#include "fntest.h"
#include "../../src/game/game.h"
#include "../../src/game/ram.h"

static void t_vblank(Sm83Regs *r) { vblank_interrupt(r->b); }
static void t_stat(Sm83Regs *r) { (void)r; stat_interrupt(); }
static void t_timer(Sm83Regs *r) { (void)r; timer_interrupt(); }
static int timer_valid(const Machine *m)
{
    for (const FnTest *t = fn_tests_sound; t && t->name; t++)
        if (t->bank == 3 && t->addr == 0x7FF0)
            return !t->valid_state || t->valid_state(m);
    return 0;
}
static int vblank_valid(const Machine *m)
{
    unsigned lo = m->hram[0x69];
    if (m->hram[0x6A] == 1 && (lo < 0x40 || lo >= 0x60)) return 0;
    for (const FnTest *t = fn_tests_player; t && t->name; t++)
        if (t->bank == 0 && t->addr == 0x1B86)
            return !t->valid_state || t->valid_state(m);
    return 0;
}
static void t_update_lives(Sm83Regs *r) { (void)r; update_lives(); }
static void t_display_lives(Sm83Regs *r) { (void)r; display_lives(); }
static void t_display_coins(Sm83Regs *r) { (void)r; display_coins(); }
static void t_print_bcd6(Sm83Regs *r) { print_bcd6((uint16_t)(r->d << 8 | r->e), (uint16_t)(r->h << 8 | r->l)); }
static void t_display_score(Sm83Regs *r) { (void)r; display_score(); }
static void t_copy_data(Sm83Regs *r) { copy_data((uint16_t)(r->h << 8 | r->l), (uint16_t)(r->d << 8 | r->e), (uint16_t)(r->b << 8 | r->c)); }
static void t_clear_bg_maps(Sm83Regs *r) { (void)r; clear_bg_maps(); }
static void t_tile_address_at(Sm83Regs *r) { uint16_t a = tile_address_at(); r->h = a >> 8; r->l = a & 0xFF; }
static void t_lookup_tile(Sm83Regs *r)
{
    r->a = lookup_tile();
    r->h = tile_addr_hi;     /* callers use HL (the tile's map address) too */
    r->l = tile_addr_lo;
}
static void t_tile_address_to_pixel(Sm83Regs *r) { (void)r; tile_address_to_pixel(); }
static void t_add_score(Sm83Regs *r) { add_score((uint16_t)(r->d << 8 | r->e)); }
static void t_read_joypad(Sm83Regs *r) { (void)r; read_joypad(); }
static void t_load_level_graphics(Sm83Regs *r) { (void)r; load_level_graphics(); }
static void t_prepare_hud(Sm83Regs *r) { (void)r; prepare_hud(); }

const FnTest fn_tests_core[] = {
    { "VBlankInterrupt", 0, 0x0040, t_vblank, 0, 1, vblank_valid },
    { "StatInterrupt", 0, 0x0048, t_stat, 0, 1 },
    { "TimerInterrupt", 0, 0x0050, t_timer, 0, 1, timer_valid },
    { "LookupTile",          0, 0x0153, t_lookup_tile, OUT_A | OUT_HL, 0 },
    { "AddScore",            0, 0x0166, t_add_score, 0, 0 },
    { "ClearBgMaps",         0, 0x05CF, t_clear_bg_maps, 0, 0 },
    { "CopyData",            0, 0x05DE, t_copy_data, 0, 0 },
    { "LoadLevelGraphics",   0, 0x05E7, t_load_level_graphics, 0, 0 },
    { "PrepareHUD",          0, 0x060F, t_prepare_hud, 0, 0 },
    { "DisplayCoins",        0, 0x1C1B, t_display_coins, 0, 0 },
    { "UpdateLives",         0, 0x1C33, t_update_lives, 0, 1 },
    { "DisplayLives",        0, 0x1C56, t_display_lives, 0, 0 },
    { "TileAddressAt",       0, 0x3EE6, t_tile_address_at, OUT_HL, 0 },
    { "TileAddressToPixel",  0, 0x3F13, t_tile_address_to_pixel, 0, 0 },
    { "DisplayScore",        0, 0x3F39, t_display_score, 0, 1 },
    { "PrintBcd6",           0, 0x3F4D, t_print_bcd6, 0, 0 },
    { "ReadJoypad",          3, 0x47F2, t_read_joypad, 0, 0 },
    { 0 },
};
