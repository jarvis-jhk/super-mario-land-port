/*
 * Player and collision functions under test, with adapters from the
 * original's register interface to the C signatures. Keep sorted by address.
 */
#include "fntest.h"
#include "../../src/game/game.h"
#include "../../src/game/ram.h"

/* hWorldAndLevel always names a world from 1 to 5: the list lookup of 1A6B
   indexes a table with the world, and a fuzzed world 0 makes it scan from a
   pointer taken from code bytes (possibly into hardware or shared HRAM). */
static int world_valid(const Machine *m)
{
    int world = m->hram[0xFFB4 - 0xFF80] >> 4;
    return world >= 1 && world <= 5;
}

/* A pending block event always names a tile of the background map (the
   handler writes to it); a fuzzed address would write to ROM/MBC space. */
static int block_event_valid(const Machine *m)
{
    int event = m->hram[0xFFEE - 0xFF80];
    unsigned address = (unsigned)(m->hram[0xFFEF - 0xFF80] << 8 | m->hram[0xFFF0 - 0xFF80]);
    if (event == 1 || event == 2 || event == 4 || event == 0xC0)
        return address >= 0x9800 && address <= 0x9BFF;
    return 1;
}

/* The five objects drawn with Mario (C200-C24F) have a sprite number that
   exists in the layout table (43 entries) when they are to be drawn; a
   fuzzed number would send the drawer through random ROM data. */
static int sprites_valid(const Machine *m)
{
    for (int i = 0; i < 5; i++) {
        int flags = m->wram[0x200 + 0x10 * i];
        int sprite = m->wram[0x203 + 0x10 * i];
        if ((flags == 0 || flags == 0x80) && sprite >= 43)
            return 0;
    }
    return 1;
}

/* DrawObjects is entered with the object count (1736 sets 5) in FF8F */
static int draw_objects_valid(const Machine *m)
{
    int count = m->hram[0xFF8F - 0xFF80];
    /* FF8D/FF8E: the write position in the OAM buffer, C000-C09F (1736 sets C00C) */
    int hi = m->hram[0xFF8D - 0xFF80], lo = m->hram[0xFF8E - 0xFF80];
    return count >= 1 && count <= 5 && hi == 0xC0 && lo <= 0x40 && sprites_valid(m);
}

static int world_and_sprites_valid(const Machine *m) { return world_valid(m) && sprites_valid(m); }

#define HL(r) ((uint16_t)((r)->h << 8 | (r)->l))

static void t_mario_object_collisions(Sm83Regs *r) { (void)r; mario_object_collisions(); }
static void t_injure_mario(Sm83Regs *r) { (void)r; injure_mario(); }
static void t_kill_mario(Sm83Regs *r) { (void)r; kill_mario(); }
static void t_invincibility_tick(Sm83Regs *r) { (void)r; invincibility_tick(); }
/* The demo plays one of three input lists (C0DC = 0-2), each read two
   bytes at a time up to its $FF end; other values would read "input" from
   anywhere (VRAM, I/O). */
static int demo_input_valid(const Machine *m)
{
    unsigned part = m->wram[0xC0DC - 0xC000], pos = m->wram[0xC0D9 - 0xC000];
    if (part > 2)
        return 0;
    const uint8_t *lists = m->rom + 3 * 0x4000 + (0x4AE4 - 0x4000);
    unsigned list = (unsigned)(lists[part * 2] | lists[part * 2 + 1] << 8);
    if (list < 0x4000 || list > 0x7F00)
        return 0;
    for (unsigned i = 0; i < pos; i += 2)
        if (m->rom[3 * 0x4000 + (list + i - 0x4000)] == 0xFF)
            return 0;
    return (pos & 1) == 0;
}

static void t_play_demo_input(Sm83Regs *r) { (void)r; play_demo_input(); }
static void t_move_block_fragments(Sm83Regs *r) { (void)r; move_block_fragments(); }
static void t_bounce_block_sprite(Sm83Regs *r) { (void)r; bounce_block_sprite(); }
static void t_mario_falls_in_pit(Sm83Regs *r) { (void)r; mario_falls_in_pit(); }
static void t_mario_growing(Sm83Regs *r) { (void)r; mario_growing(); }
static void t_mario_hurt_blinking(Sm83Regs *r) { (void)r; mario_hurt_blinking(); }
static void t_apply_pending_jump_step(Sm83Regs *r) { (void)r; apply_pending_jump_step(); }
static void t_mario_jump_and_fire(Sm83Regs *r) { (void)r; mario_jump_and_fire(); }
static void t_move_along_jump_arc(Sm83Regs *r)
{
    move_along_jump_arc((uint16_t)((r->b << 8 | r->c) - 8), (uint16_t)(r->h << 8 | r->l));
}
static void t_set_stomp_reward(Sm83Regs *r) { (void)r; set_stomp_reward(); r->a = hit_reward; }
static void t_bounce_block_hits_enemies(Sm83Regs *r) { (void)r; bounce_block_hits_enemies(); }

/* A = overlap, B and HL as the original leaves them (the callers continue with them) */
static void t_hitbox_overlap(Sm83Regs *r)
{
    uint16_t obj = HL(r);
    HitboxResult res = hitbox_overlap(obj, r->c);
    uint16_t hl = (uint16_t)((obj & 0xFF00) | ((obj + res.hl_offset) & 0xFF));
    r->a = res.hit;
    r->b = res.b;
    r->h = hl >> 8;
    r->l = hl & 0xFF;
}

static void t_mario_rides_platforms(Sm83Regs *r) { (void)r; mario_rides_platforms(); }
static void t_clear_block_overlay(Sm83Regs *r) { (void)r; clear_block_overlay(); r->a = 0; }
static void t_mario_floor_collision(Sm83Regs *r) { (void)r; mario_floor_collision(); }
static void t_filter_passthrough_tile(Sm83Regs *r) { r->a = filter_passthrough_tile(r->a); }
static void t_mario_side_collision(Sm83Regs *r) { r->a = mario_side_collision(); }
static void t_mario_head_collision(Sm83Regs *r) { (void)r; mario_head_collision(); }
static void t_block_events(Sm83Regs *r) { block_events(r->b); }
static void t_add_coin(Sm83Regs *r) { if (add_coin()) r->b = coins_bcd; }
static void t_clear_object_sprites(Sm83Regs *r) { (void)r; clear_object_sprites(); r->a = 0x80; }
static void t_mario_walk(Sm83Regs *r) { (void)r; mario_walk(); }
static void t_shift_object_sprites(Sm83Regs *r) { shift_object_sprites(r->b); }
static void t_walk_step(Sm83Regs *r) { r->a = walk_step(); }
static void t_update_superballs(Sm83Regs *r) { (void)r; update_superballs(); }
/* A = the tile (or the small number the original leaves), carry = no collision; B = the x argument; DE and HL are preserved */
static void t_find_neighboring_tile(Sm83Regs *r)
{
    uint8_t x = r->a;
    r->a = find_neighboring_tile(x);
    r->f = (uint8_t)((r->f & ~0x10) | (r->a < 0x60 ? 0x10 : 0));
}
static void t_superball_hits_enemies(Sm83Regs *r) { superball_hits_enemies((uint16_t)(r->d << 8 | r->e)); }
static void t_break_block_in_autoscroll(Sm83Regs *r) { break_block_in_autoscroll(r->a, HL(r)); }
static void t_draw_mario(Sm83Regs *r) { draw_mario(); r->a = saved_rom_bank; }
static void t_draw_objects(Sm83Regs *r) { draw_objects(HL(r)); }
static void t_mario_animate_and_walk(Sm83Regs *r) { (void)r; mario_animate_and_walk(); }
static void t_feed_demo_joypad(Sm83Regs *r) { (void)r; feed_demo_joypad(); }

const FnTest fn_tests_player[] = {
    { "MarioObjectCollisions", 0, 0x084E, t_mario_object_collisions, 0, 0 },
    { "InjureMario",         0, 0x09E0, t_injure_mario, 0, 0 },
    { "KillMario",           0, 0x09F1, t_kill_mario, 0, 0 },
    { "SetStompReward",      0, 0x0A10, t_set_stomp_reward, OUT_A, 0 },
    { "BounceBlockHitsEnemies", 0, 0x0A2D, t_bounce_block_hits_enemies, 0, 0 },
    { "HitboxOverlap",       0, 0x0AAF, t_hitbox_overlap, OUT_A | OUT_B | OUT_HL, 0 },
    { "MarioRidesPlatforms", 0, 0x0AEA, t_mario_rides_platforms, 0, 0 },
    { "ClearBlockOverlay",   0, 0x165E, t_clear_block_overlay, OUT_A, 0 },
    { "MarioFloorCollision", 0, 0x17BC, t_mario_floor_collision, 0, 0 },
    { "MarioHeadCollision",  0, 0x198C, t_mario_head_collision, 0, 0, world_valid },
    { "FilterPassthroughTile", 0, 0x1A6B, t_filter_passthrough_tile, OUT_A, 0, world_valid },
    { "MarioSideCollision",  0, 0x1AAD, t_mario_side_collision, OUT_A, 0, world_valid },
    { "BlockEvents",         0, 0x1B86, t_block_events, 0, 1, block_event_valid },
    { "AddCoin",             0, 0x1BFF, t_add_coin, OUT_B, 0 },
    { "ClearObjectSprites",  0, 0x1ED4, t_clear_object_sprites, OUT_A, 0 },
    { "InvincibilityTick",   0, 0x1F03, t_invincibility_tick, 0, 0 },
    { "ApplyPendingJumpStep", 3, 0x48FC, t_apply_pending_jump_step, 0, 0 },
    { "MoveAlongJumpArc",    3, 0x490D, t_move_along_jump_arc, 0, 0 },
    { "MarioJumpAndFire",     3, 0x498B, t_mario_jump_and_fire, 0, 0 },
    { "PlayDemoInput",       3, 0x4A94, t_play_demo_input, 0, 0, demo_input_valid },
    { "MoveBlockFragments",  3, 0x4AEA, t_move_block_fragments, 0, 0 },
    { "BounceBlockSprite",   3, 0x4B3C, t_bounce_block_sprite, 0, 0 },
    { "MarioFallsInPit",     3, 0x4B6F, t_mario_falls_in_pit, 0, 0 },
    { "MarioGrowing",        3, 0x4B8A, t_mario_growing, 0, 0 },
    { "MarioHurtBlinking",   3, 0x4BB5, t_mario_hurt_blinking, 0, 0 },
    { "MarioAnimateAndWalk", 0, 0x16F5, t_mario_animate_and_walk, 0, 0, world_and_sprites_valid },
    { "DrawMario",           0, 0x1736, t_draw_mario, OUT_A, 0, sprites_valid },
    { "MarioWalk",           0, 0x1D26, t_mario_walk, 0, 0, world_valid },
    { "ShiftObjectSprites",  0, 0x1EA4, t_shift_object_sprites, 0, 0 },
    { "WalkStep",            0, 0x1EB4, t_walk_step, OUT_A, 0 },
    { "UpdateSuperballs",    0, 0x1F2D, t_update_superballs, 0, 0, world_valid },
    { "FindNeighboringTile", 0, 0x1FD2, t_find_neighboring_tile, OUT_A | OUT_CF, 0 },
    { "SuperballHitsEnemies", 0, 0x200A, t_superball_hits_enemies, 0, 0 },
    { "BreakBlockInAutoscroll", 0, 0x2097, t_break_block_in_autoscroll, 0, 0 },
    { "DemoInput",           0, 0x2113, t_feed_demo_joypad, 0, 0 },
    { "DrawObjects",         3, 0x4823, t_draw_objects, 0, 0, draw_objects_valid },
    { 0 },
};
