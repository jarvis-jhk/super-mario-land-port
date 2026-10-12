/*
 * Enemy and object functions under test, with adapters from the original's
 * register interface to the C signatures. Keep sorted by address.
 */
#include "fntest.h"
#include "../../src/game/game.h"

#define HL(r) ((uint16_t)((r)->h << 8 | (r)->l))
#define DE(r) ((uint16_t)((r)->d << 8 | (r)->e))

/* A = result (HL differs: slot, or slot+$C after InitEnemy; callers reload it) */
static uint8_t peek_ram(const Machine *m, unsigned addr)
{
    return addr >= 0xFF80 ? m->hram[addr - 0xFF80] : m->wram[addr - 0xC000];
}

/* The level index is 0-11: it indexes a 12-entry table of enemy list
   pointers, and a fuzzed value would read a "pointer" out of code or data
   bytes that can lead into hardware registers. */
static int level_index_valid(const Machine *m)
{
    return peek_ram(m, 0xFFE4) < 12;
}

/* A level's enemy list always has well-formed entries (ids below $63) in
   ascending column order and ends at an entry that is not yet due; a fuzzed
   list pointer or column counter would make the original scan thousands of
   entries of unrelated ROM bytes. Check that the list is walked sanely. */
static int enemy_list_valid(const Machine *m)
{
    if (!level_index_valid(m))
        return 0;
    unsigned p = (unsigned)(peek_ram(m, 0xD011) << 8 | peek_ram(m, 0xD010));
    if (p < 0x4000 || p >= 0x8000)
        return 0;
    unsigned progress = peek_ram(m, 0xC0AB);
    for (int n = 0; n < 100; n++, p += 3) {
        if (p + 2 >= 0x8000)
            return 0;                  /* the list runs off the end of the bank */
        const uint8_t *e = m->rom + (size_t)m->rom_bank * 0x4000 + (p - 0x4000);
        if (e[0] > progress)
            return 1;                      /* found the first entry not due */
        if ((e[2] & 0x7F) >= 0x63)
            return 0;
    }
    return 0;
}

/* Mario's side collision (called when an enemy carries him) indexes a table
   with the world, 1-5 (see registry_player.c). */
static int world_valid(const Machine *m)
{
    int world = peek_ram(m, 0xFFB4) >> 4;
    return world >= 1 && world <= 5;
}

/* An enemy id indexes the 99-entry script table at 349E (2 bytes each, ids
   $00-$62; enemies.c script_of), and $FF marks an unused slot (ENEMY_FREE). A
   fuzzed id of $63 or more points the script pointer into cart RAM, where the
   ROM reads open bus and the script loops forever. */
static int enemy_ids_valid(const Machine *m)
{
    for (unsigned slot = 0; slot < 10; slot++) {
        unsigned id = peek_ram(m, 0xD100 + 16 * slot);
        if (id != 0xFF && id >= 0x63)
            return 0;
    }
    return 1;
}

/* Sprite numbers index a table of 105 tile list pointers; a fuzzed larger
   number would read a "pointer" from the tile lists behind the table. */
static int sprites_valid(const Machine *m)
{
    for (unsigned slot = 0; slot < 10; slot++)
        if (peek_ram(m, 0xD100 + 16 * slot) != 0xFF && peek_ram(m, 0xD106 + 16 * slot) >= 0x69)
            return 0;
    return 1;
}

static int update_enemies_valid(const Machine *m)
{
    return enemy_list_valid(m) && world_valid(m) && enemy_ids_valid(m) && sprites_valid(m);
}
static int update_enemy_scripts_valid(const Machine *m) { return world_valid(m) && enemy_ids_valid(m); }

static int buffer_sprite_valid(const Machine *m)
{
    return peek_ram(m, 0xFFC6) < 0x69;
}

static void t_stomp_enemy(Sm83Regs *r) { r->a = stomp_enemy(HL(r)); }
static void t_hit_enemy_from_below(Sm83Regs *r) { r->a = hit_enemy_from_below(HL(r)); }
static void t_enemy_touches_mario(Sm83Regs *r) { r->a = enemy_touches_mario(HL(r)); }
static void t_superball_hits_enemy(Sm83Regs *r) { r->a = superball_hits_enemy(HL(r)); }
static void t_bullet_hits_enemy(Sm83Regs *r) { r->a = bullet_hits_enemy(HL(r)); }
static void t_star_mario_hits_enemy(Sm83Regs *r) { r->a = star_mario_hits_enemy(HL(r)); }
#include <stdbool.h>
static void set_carry(Sm83Regs *r, bool c) { r->f = (uint8_t)((r->f & ~0x10) | (c ? 0x10 : 0)); }
static void t_explode_all_enemies(Sm83Regs *r) { explode_all_enemies(); }
static void t_probe_left(Sm83Regs *r) { set_carry(r, enemy_probe_left()); }
static void t_probe_right(Sm83Regs *r) { set_carry(r, enemy_probe_right()); }
static void t_probe_below(Sm83Regs *r) { set_carry(r, enemy_probe_below()); }
static void t_probe_below_left(Sm83Regs *r) { set_carry(r, enemy_probe_below_left()); }
static void t_probe_below_right(Sm83Regs *r) { set_carry(r, enemy_probe_below_right()); }
static void t_probe_above(Sm83Regs *r) { set_carry(r, enemy_probe_above()); }
static void t_copy_slot_to_buffer(Sm83Regs *r) { copy_slot_to_buffer(r->a); }
static void t_copy_slot_to_buffer_at(Sm83Regs *r) { copy_slot_to_buffer_at(HL(r)); }
static void t_copy_buffer_to_slot(Sm83Regs *r) { copy_buffer_to_slot(r->a); }
static void t_copy_buffer_to_slot_at(Sm83Regs *r) { copy_buffer_to_slot_at(HL(r)); }
static void t_reset_level_enemies(Sm83Regs *r) { reset_level_enemies(); }
static void t_init_enemy_slots(Sm83Regs *r) { init_enemy_slots(); }
static void t_update_enemies(Sm83Regs *r) { update_enemies(); }
static void t_spawn_enemies(Sm83Regs *r) { spawn_enemies(); }
static void t_launch_enemy_projectile(Sm83Regs *r) { launch_enemy_projectile(); }
static void t_spawn_enemy_from_level_data(Sm83Regs *r) { spawn_enemy_from_level_data(HL(r)); }
static void t_spawn_powerup(Sm83Regs *r) { spawn_powerup(r->a); }
static void t_draw_enemies(Sm83Regs *r) { draw_enemies(); }
static void t_draw_enemy(Sm83Regs *r) { draw_enemy(); }
static void t_update_enemy_scripts(Sm83Regs *r) { update_enemy_scripts(); }
static void t_update_enemy(Sm83Regs *r) { update_enemy(HL(r)); }
static void t_autoscroll_step(Sm83Regs *r) { autoscroll_step(); }
static void t_autoscroll_steer(Sm83Regs *r) { autoscroll_steer(); }
static void t_autoscroll_check_above(Sm83Regs *r) { r->a = autoscroll_check_above(); }
static void t_autoscroll_check_below(Sm83Regs *r) { r->a = autoscroll_check_below(); }
static void t_autoscroll_update_shots(Sm83Regs *r) { autoscroll_update_shots(); }
static void t_autoscroll_crush_check(Sm83Regs *r) { autoscroll_crush_check(); }
static void t_scroll_enemies(Sm83Regs *r) { scroll_enemies(r->b); }
static void t_init_enemy(Sm83Regs *r) { init_enemy(HL(r)); }

const FnTest fn_tests_enemies[] = {
    { "StompEnemy",          0, 0x2A01, t_stomp_enemy, OUT_A, 0 },
    { "HitEnemyFromBelow",   0, 0x2A23, t_hit_enemy_from_below, OUT_A, 0 },
    { "EnemyTouchesMario",   0, 0x2A44, t_enemy_touches_mario, OUT_A, 0 },
    { "SuperballHitsEnemy",  0, 0x2A68, t_superball_hits_enemy, OUT_A, 0 },
    { "BulletHitsEnemy",     0, 0x2AAD, t_bullet_hits_enemy, OUT_A, 0 },
    { "StarMarioHitsEnemy",  0, 0x2B06, t_star_mario_hits_enemy, OUT_A, 0 },
    { "ResetLevelEnemies",   0, 0x2442, t_reset_level_enemies, 0, 0, level_index_valid },
    { "InitEnemySlots",      0, 0x245C, t_init_enemy_slots, 0, 0, level_index_valid },
    { "SpawnEnemies",        0, 0x249B, t_spawn_enemies, 0, 0, enemy_list_valid },
    { "LaunchEnemyProjectile", 0, 0x24D6, t_launch_enemy_projectile, 0, 0 },
    { "SpawnEnemyFromLevelData", 0, 0x24EF, t_spawn_enemy_from_level_data, 0, 0 },
    { "SpawnPowerup",        0, 0x254D, t_spawn_powerup, 0, 0 },
    { "DrawEnemies",         0, 0x2568, t_draw_enemies, 0, 0, sprites_valid },
    { "DrawEnemy",           0, 0x25C0, t_draw_enemy, 0, 0, buffer_sprite_valid },
    { "UpdateEnemies",       0, 0x2491, t_update_enemies, 0, 0, update_enemies_valid },
    { "UpdateEnemyScripts",  0, 0x2648, t_update_enemy_scripts, 0, 0, update_enemy_scripts_valid },
    { "UpdateEnemy",         0, 0x2676, t_update_enemy, 0, 0, world_valid },
    { "ExplodeAllEnemies",   0, 0x2B2A, t_explode_all_enemies, 0, 0 },
    { "EnemyProbeLeft",      0, 0x2B84, t_probe_left, OUT_CF, 0 },
    { "EnemyProbeRight",     0, 0x2B9A, t_probe_right, OUT_CF, 0 },
    { "EnemyProbeBelow",     0, 0x2BBB, t_probe_below, OUT_CF, 0 },
    { "EnemyProbeBelowLeft", 0, 0x2BE4, t_probe_below_left, OUT_CF, 0 },
    { "EnemyProbeBelowRight",0, 0x2BFE, t_probe_below_right, OUT_CF, 0 },
    { "EnemyProbeAbove",     0, 0x2C21, t_probe_above, OUT_CF, 0 },
    { "ScrollEnemies",       0, 0x2C9F, t_scroll_enemies, OUT_HL | OUT_DE, 0 },
    { "CopyEnemySlotToBuffer", 0, 0x2CE5, t_copy_slot_to_buffer, 0, 0 },
    { "CopySlotToBufferAt",  0, 0x2CEB, t_copy_slot_to_buffer_at, 0, 0 },
    { "CopyBufferToEnemySlot", 0, 0x2CF7, t_copy_buffer_to_slot, 0, 0 },
    { "CopyBufferToSlotAt",  0, 0x2CFD, t_copy_buffer_to_slot_at, 0, 0 },
    { "InitEnemy",           0, 0x2CBB, t_init_enemy, 0, 0 },
    /* bank 1: the shooter levels (4FB2-515E). 50CC is part of 4FB2/4FEC: when a wall
       blocks Mario it returns to its caller's caller, so it can't be run alone. */
    { "AutoscrollStep",      1, 0x4FB2, t_autoscroll_step, 0, 0 },
    { "AutoscrollSteer",     1, 0x4FEC, t_autoscroll_steer, 0, 0 },
    { "AutoscrollCheckAbove", 1, 0x5046, t_autoscroll_check_above, OUT_A, 0 },
    { "AutoscrollCheckBelow", 1, 0x5089, t_autoscroll_check_below, OUT_A, 0 },
    { "AutoscrollUpdateShots", 1, 0x5118, t_autoscroll_update_shots, 0, 0 },
    { "AutoscrollCrushCheck", 1, 0x515E, t_autoscroll_crush_check, 0, 0 },
    { 0 },
};
