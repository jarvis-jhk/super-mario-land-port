/* Main game-state dispatch and pause control. */
#include "game.h"
#include "ram.h"
#include "banks.h"

/* The main loop's jump-table dispatcher (00:02A3, RST 28 at 00:0028).
   The two unused entries return immediately, exactly like their ROM RETs. */
void dispatch_game_state(void)
{
    uint8_t state = shared_read(0xFFB3);
    jump_table(state);
    switch (state) {
    case 0x00: update_normal_gameplay(); break;
    case 0x01: finish_death(); break;
    case 0x02: restart_checkpoint(); break;
    case 0x03: prepare_death_animation(); break;
    case 0x04: update_death_animation(); break;
    case 0x05: count_down_level_score(); break;
    case 0x06: finish_level(); break;
    case 0x07: update_level_exit(); break;
    case 0x08: advance_level(); break;
    case 0x09: enter_pipe_down(); break;
    case 0x0A: enter_underground(); break;
    case 0x0B: exit_pipe_sideways(); break;
    case 0x0C: exit_pipe_up(); break;
    case 0x0D: update_autoscroll_gameplay(); break;
    case 0x0E: prepare_menu(); break;
    case 0x0F: update_menu(); break;
    case 0x10: break;
    case 0x11: start_level(); break;
    case 0x12: prepare_bonus_game(); break;
    case 0x13: draw_bonus_room(); break;
    case 0x14: prepare_bonus_mario(); break;
    case 0x15: update_bonus_selection(); break;
    case 0x16: draw_bonus_ladder(); break;
    case 0x17: walk_bonus_mario(); break;
    case 0x18: descend_bonus_ladder(); break;
    case 0x19: ascend_bonus_ladder(); break;
    case 0x1A: award_bonus_prize(); break;
    case 0x1B: leave_bonus_game(); break;
    case 0x1C: prepare_boss_gate(); break;
    case 0x1D: locate_boss_gate(); break;
    case 0x1E: open_boss_gate(); break;
    case 0x1F: finish_boss_gate(); break;
    case 0x20: case 0x28: update_exit_walk(); break;
    case 0x21: update_daisy_meeting(); break;
    case 0x22: scroll_to_daisy(); break;
    case 0x23: approach_daisy(); break;
    case 0x24: update_daisy_dialogue(); break;
    case 0x25: update_daisy_morph(); break;
    case 0x26: update_daisy_escape(); break;
    case 0x27: update_tatanga_defeat(); break;
    case 0x29: prepare_daisy_reunion(); break;
    case 0x2A: update_reunion_dialogue(); break;
    case 0x2B: update_daisy_run(); break;
    case 0x2C: update_daisy_kiss(); break;
    case 0x2D: update_quest_complete(); break;
    case 0x2E: board_plane(); break;
    case 0x2F: prepare_plane_takeoff(); break;
    case 0x30: update_plane_takeoff(); break;
    case 0x31: update_plane_cruise(); break;
    case 0x32: leave_hangar(); break;
    case 0x33: prepare_credit_lines(); break;
    case 0x34: scroll_credits_in(); break;
    case 0x35: hold_credits(); break;
    case 0x36: scroll_credits_out(); break;
    case 0x37: prepare_ending_letters(); break;
    case 0x38: update_ending_letters(); break;
    case 0x39: prepare_game_over(); break;
    case 0x3A: update_game_over(); break;
    case 0x3B: prepare_time_up(); break;
    case 0x3C: finish_time_up(); break;
    case 0x3D: break;
    }
}

/* Toggle pause and its window/music cue; return true for the reset chord
   so the owning loop can restart initialization without a recursive loop
   (00:07DA). Reset does not return in the original. */
bool pause_or_reset(void)
{
    if (!taken((joy_held & 15) != 15)) return true;
    if (taken((joy_pressed & 8) == 0)) return false;
    if (taken(shared_read(0xFFB3) >= STATE_MENU)) return false;
    game_paused ^= 1;
    if (taken(game_paused == 0)) {
        io_write(rLCDC, io_read(rLCDC) & ~0x20);
        shared_write(0xFFDF, 2);
    } else {
        io_write(rLCDC, io_read(rLCDC) | 0x20);
        shared_write(0xFFDF, 1);
    }
    return false;
}

/* Initialize the cartridge's RAM, tiles, DMA helper and sound (00:0185).
   Keep the original out-of-range clears: cartridge RAM, unusable OAM,
   one I/O byte below HRAM, and two extra bytes after the DMA routine. */
void initialize_game(void)
{
    io_write(rIF, 3); io_write(rIE, 3); io_write(rSTAT, 0x40);
    io_write(rSCY, 0); io_write(rSCX, 0); shared_write(0xFFA4, 0);
    io_write(rLCDC, 0x80);
    while (taken(io_read(rLY) != 0x94)) ;
    io_write(rLCDC, 3); io_write(rBGP, 0xE4); io_write(rOBP0, 0xE4); io_write(rOBP1, 0x54);
    io_write(rNR52, 0x80); io_write(rNR51, 0xFF); io_write(rNR50, 0x77);
    uint16_t dst = 0xDFFF;
    uint8_t groups = 0x40, count;
    do {
        count = 0;
        do { bus_write(dst--, 0); } while (taken(--count != 0));
    } while (taken(--groups != 0));
    dst = 0x9FFF; groups = 0x20;
    do {
        count = 0;
        do { vram_write(dst--, 0); } while (taken(--count != 0));
    } while (taken(--groups != 0));
    dst = 0xFEFF; count = 0;
    do { bus_write(dst--, 0); } while (taken(--count != 0));
    dst = 0xFFFE; count = 0x80;
    do { bus_write(dst--, 0); } while (taken(--count != 0));
    uint16_t src = 0x3F92; dst = 0xFFB6; count = 12;
    do { ram_write(dst++, mem_read(src++)); } while (taken(--count != 0));
    level_index = 0; shared_write(ADDR_WORLD_AND_LEVEL, 0x11); continue_world_level = 0x11;
    demo_part = 2; shared_write(0xFFB3, STATE_MENU);
    set_rom_bank(3); shared_write(ADDR_GAME_OVER_STATE_COPY, 3);
    ram_write(0xC0E1, 0); win_count = 0; init_sound();
    set_rom_bank(2); shared_write(ADDR_ACTIVE_ROM_BANK, 2);
}

/* The original's endless per-frame loop (00:0226). Platform callbacks are
   serviced by the timing runtime, including while this loop waits in HALT. */
void run_game(void)
{
    for (;;) {
        initialize_game();
        for (;;) {
            if (!taken(timer_expiring_flag != 3)) {
                timer_expiring_flag = 0xFF; kill_mario(); draw_mario();
            }
            switch_rom_bank_saving(3); read_joypad(); restore_rom_bank();
            bool wait_only = false;
            if (!taken(shared_read(ADDR_DEMO_MODE) != 0)) {
                if (pause_or_reset()) break;
                wait_only = taken(game_paused != 0);
            }
            if (!wait_only) {
                uint16_t timer = 0xFFA6; uint8_t count = 2;
                do {
                    uint8_t value = ram_read(timer);
                    if (!taken(value == 0)) ram_write(timer, value - 1);
                    timer++;
                } while (taken(--count != 0));
                if (!taken(shared_read(ADDR_DEMO_MODE) == 0)) {
                    bool stop_demo = taken((joy_held & 8) != 0);
                    if (!stop_demo && !taken((shared_read(ADDR_FRAME_COUNTER) & 15) != 0)) {
                        if (taken(demo_timer == 0)) stop_demo = true;
                        else demo_timer--;
                    }
                    if (stop_demo && !taken(shared_read(0xFFB3) != 0)) {
                        set_rom_bank(2); shared_write(ADDR_ACTIVE_ROM_BANK, 2);
                        shared_write(0xFFB3, STATE_MENU);
                    }
                }
                dispatch_game_state();
            }
            do { halt(); } while (taken(shared_read(0xFF85) == 0));
            shared_write(0xFF85, 0);
        }
    }
}
