/* Gameplay's per-frame updates, in the original call order. */
#include "game.h"
#include "ram.h"
#include "banks.h"

/* Update the level, Mario, enemies and rewards (00:0627 GameState_00).
   The order matters: collisions use positions from different points in
   the frame, and the bank changes are visible to the timer interrupt. */
void update_normal_gameplay(void)
{
    update_level_columns();
    mario_object_collisions();

    switch_rom_bank_saving(3);
    apply_pending_jump_step();
    for (uint16_t object = 0xC200; object <= 0xC240; object += 0x10)
        move_along_jump_arc(object, 0x216D);
    play_demo_input();
    mario_jump_and_fire();
    move_block_fragments();
    bounce_block_sprite();
    mario_falls_in_pit();
    mario_growing();
    mario_hurt_blinking();
    restore_rom_bank();

    update_superballs();
    update_enemies();
    switch_rom_bank_saving(2);
    update_timer_and_floaties();
    restore_rom_bank();

    mario_head_collision();
    mario_animate_and_walk();
    mario_floor_collision();
    mario_rides_platforms();
    bounce_block_hits_enemies();
    invincibility_tick();

    if (taken(coin_block_timer == 0))
        return;
    coin_block_timer--;
    feed_demo_joypad();
}

/* Update the submarine/plane stages (00:2376 GameState_0D). The shooter
   movement and shots run in bank 1; only the four reward objects follow
   jump arcs here, since Mario is steered directly with the d-pad. */
void update_autoscroll_gameplay(void)
{
    if (taken(game_paused != 0))
        return;

    update_level_columns();
    autoscroll_step();
    if (taken(boss_defeated != 0))
        mario_reaches_goal();
    mario_object_collisions();
    autoscroll_steer();
    autoscroll_update_shots();

    switch_rom_bank_saving(3);
    mario_jump_and_fire();
    for (uint16_t object = 0xC210; object <= 0xC240; object += 0x10)
        move_along_jump_arc(object, 0x216D);
    move_block_fragments();
    mario_growing();
    mario_hurt_blinking();
    restore_rom_bank();

    update_enemies();
    switch_rom_bank_saving(2);
    update_timer_and_floaties();
    restore_rom_bank();
    draw_mario();
    autoscroll_crush_check();
    invincibility_tick();

    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0))
        return;
    mario_anim ^= 1;
}
