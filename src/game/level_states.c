/* Level transitions, death and the boss gate (bank 0). */
#include "game.h"
#include "ram.h"
#include "banks.h"
#include "sound.h"
#include "bcd.h"

/* Change the world graphics and prepare the next checkpoint (00:0D6D).
   World 1 reloads all tiles; the other worlds replace enemies/backdrops. */
void load_world_tiles(uint8_t world_level)
{
    uint8_t world = world_level >> 4, bank = 2;
    if (taken(world == 1)) {
        set_rom_bank(bank);
        shared_write(ADDR_ACTIVE_ROM_BANK, bank);
        io_write(rLCDC, 0);
        load_level_graphics();
    } else {
        bank = 1;
        if (!taken(world == 2)) {
            bank = 3;
            if (!taken(world == 3)) bank = 1;
        }
        reg_b(world);
        set_rom_bank(bank);
        shared_write(ADDR_ACTIVE_ROM_BANK, bank);
        io_write(rLCDC, 0);
        uint8_t offset = (uint8_t)((world - 2) * 2);
        uint16_t table = 0x0DED + offset;
        uint16_t src = mem_read(table) | mem_read(table + 1) << 8;
        uint16_t dst = 0x8A00;
        do {
            vram_write(dst++, mem_read(src++));
        } while (taken(dst < 0x8DD0));
        table = 0x0DF3 + offset;
        src = mem_read(table) | mem_read(table + 1) << 8;
        uint16_t seed = src + 0x02C1;
        dst = 0x9310;
        do {
            vram_write(dst++, mem_read(src++));
        } while (taken((dst >> 8) != 0x97));
        uint8_t rows = 8;
        dst = 0xC600;
        do {
            ram_write(dst++, mem_read(seed));
            seed += 2;
        } while (taken(--rows != 0));
    }
    io_write(rIF, 0);
    io_write(rLCDC, 0xC3);
    screen_index = 3;
    level_end_counter = 0;
    underground = 0;
    shared_write(0xFFB3, 2);
    reset_level_enemies();
}

/* Advance the level after music and the transition timer finish (00:0D49). */
void advance_level(void)
{
    if (taken(frame_timer != 0)) return;
    if (taken(shared_read(0xDFF9) != 0)) return;
    uint8_t next = level_index + 1;
    if (!taken(next != 12)) next = 0;
    level_index = next;
    uint8_t world_level = reg_b(shared_read(ADDR_WORLD_AND_LEVEL) + 1);
    if (!taken((world_level & 15) != 4)) world_level += 13;
    shared_write(ADDR_WORLD_AND_LEVEL, world_level);
    load_world_tiles(world_level);
}

/* Remove enemies and charge one life after the death delay (00:06BC). */
void finish_death(void)
{
    if (taken(frame_timer != 0)) return;
    uint16_t slot = 0xD100;
    uint8_t count = 10;
    do {
        ram_write(slot, ENEMY_FREE);
        slot += 16;
    } while (taken(--count != 0));
    super_status = SUPER_SMALL;
    shared_write(ADDR_LIVES_CHANGE, 0xFF);
    shared_write(0xFFB3, 2);
}

/* Build the four dying-Mario sprites before their ballistic fall (00:0B8D). */
void prepare_death_animation(void)
{
    uint8_t x = mario_x - 8, top = death_y - 8;
    const uint8_t sprite[16] = {
        top, x, 0x0F, 0, death_y, x, 0x1F, 0,
        top, (uint8_t)(x + 8), 0x0F, 0x20,
        death_y, (uint8_t)(x + 8), 0x1F, 0x20
    };
    for (unsigned i = 0; i < sizeof sprite; i++) {
        if (i == 1 || i == 9) reg_b(sprite[i]);     /* the left and right x */
        shared_write(0xC00C + i, sprite[i]);
    }
    shared_write(0xFFB3, 4);
    death_step = 0;
    super_status = SUPER_SMALL;
    pipe_data = 0;
    clear_object_sprites();
}

/* Follow the death arc, then coast down until Mario leaves view (00:0BD6). */
void update_death_animation(void)
{
    uint8_t speed = reg_b(mem_read(0x0C19 + death_step++));
    if (!taken(speed != 0x7F)) { death_step--; speed = 2; }
    uint16_t sprite = 0xC00C;
    uint8_t count = 4, y;
    do {
        y = speed + shared_read(sprite);
        shared_write(sprite, y);
        sprite += 4;
    } while (taken(--count != 0));
    if (taken(y < 0xB4)) return;
    if (taken(timer_expiring_flag != 0xFF)) {
        frame_timer = 0x90;
        shared_write(0xFFB3, STATE_DEAD);
    } else shared_write(0xFFB3, 0x3B);
}

/* The exit fanfare holds Mario still before counting the remaining time (00:0C40). */
void update_level_exit(void)
{
    if (!taken(frame_timer == 0)) { draw_mario(); return; }
    if (!taken(boss_defeated != 0)) frame_timer = 0x40;
    shared_write(0xFFB3, 5);
    timer_expiring_flag = 0;
    io_write(rTMA, 0);
    if (taken((shared_read(ADDR_WORLD_AND_LEVEL) & 15) != 3)) return;
    explode_all_enemies();
    if (taken(shared_read(ADDR_WORLD_AND_LEVEL) != 0x43)) return;
    shared_write(0xFFB3, 6);
}

/* Convert each remaining time unit into ten points (00:0C73). */
void count_down_level_score(void)
{
    if (!taken((shared_read(ADDR_WORLD_AND_LEVEL) & 15) != 3)) {
        level_progress = 0;
        update_enemies();
    }
    if (taken(frame_timer != 0)) return;
    uint8_t time_lo = shared_read(ADDR_GAME_TIMER + 1);
    uint8_t time_hi = reg_b(shared_read(ADDR_GAME_TIMER + 2));
    if (taken((time_lo | time_hi) == 0)) {
        shared_write(0xFFB3, 6);
        frame_timer = 0x26;
        return;
    }
    shared_write(ADDR_GAME_TIMER, 1);
    switch_rom_bank_saving(2);
    update_timer_and_floaties();
    restore_rom_bank();
    add_score(0x0010);
    frame_timer = 1;
    timer_expiring_flag = 0;
    if (taken((shared_read(ADDR_GAME_TIMER + 1) & 1) != 0)) return;
    shared_write(0xDFE0, 0x0A);
}

/* Choose the bonus game, next level, boss gate or Tatanga ending (00:0CCB). */
void finish_level(void)
{
    if (taken(frame_timer != 0)) return;
    timer_expiring_flag = 0;
    io_write(rTMA, 0);
    if (!taken((shared_read(ADDR_WORLD_AND_LEVEL) & 15) == 3)) {
        if (taken(mario_y < 0x60) || taken(mario_y >= 0xA0)) {
            shared_write(ADDR_ACTIVE_ROM_BANK, 2);
            set_rom_bank(2);
            shared_write(0xFFB3, 0x12);
        } else shared_write(0xFFB3, 8);
        return;
    }
    shared_write(0xFFB3, 0x1C);
    set_rom_bank(3);
    shared_write(ADDR_ACTIVE_ROM_BANK, 3);
    shared_write(0xFFFB, level_index);
    level_index = 12;
    screen_index = column_index = column_phase = 0;
    ram_write(0xFFE0, shared_read(ADDR_DRAW_COLUMN_LO));
    frame_timer = 6;
    if (taken((shared_read(ADDR_WORLD_AND_LEVEL) & 0xF0) != 0x40)) return;
    shared_write(0xFFFB, 0);
    shared_write(0xC0DE, 1);
    enemy_slot_lo = 0xBF;
    frame_timer = 0xFF;
    shared_write(0xFFB3, 0x27);
    init_sound();
}

/* Restore the HUD after leaving the bonus room (00:0DF9). */
void leave_bonus_game(void)
{
    io_write(rLCDC, 0);
    prepare_hud(); display_coins(); display_lives();
    io_write(rIF, 0); io_write(rLCDC, 0xC3);
    shared_write(0xFFB3, 8);
    shared_write(ADDR_SCORE_CHANGED, 8);
}

/* Load the hangar columns while the defeated enemies explode (00:0E15). */
void prepare_boss_gate(void)
{
    if (taken(frame_timer == 0)) {
        frame_timer = 0x40;
        shared_write(0xFFB3, shared_read(0xFFB3) + 1);
        return;
    }
    load_next_column(); level_progress = 0; update_enemies(); draw_mario();
}

/* Locate the bottom gate tile, preserving the map's horizontal wrap (00:0E31). */
void locate_boss_gate(void)
{
    level_progress = 0; update_enemies();
    if (taken(frame_timer != 0)) return;
    uint8_t column = ram_read(0xFFE0) - 2;
    if (!taken(column >= 0x40)) column += 0x20;
    ram_write(0xFFE0, column + 0x20);
    enemy_slot_lo = 5;
    frame_timer = 8;
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Remove one gate segment every eight frames, then stop the music (00:0E5D). */
void open_boss_gate(void)
{
    if (taken(frame_timer != 0)) return;
    uint8_t remaining = enemy_slot_lo - 1;
    if (taken(remaining == 0)) {
        frame_timer = 0x10;
        shared_write(ADDR_ACTIVE_ROM_BANK, 3); set_rom_bank(3); init_sound();
        shared_write(0xFFB3, shared_read(0xFFB3) + 1);
        return;
    }
    enemy_slot_lo = remaining;
    uint16_t map = 0x9900 | ram_read(0xFFE0);
    ram_write(0xFFE0, (uint8_t)map - 0x20);
    while (taken((io_read(rSTAT) & 3) != 0)) ;
    vram_write(map, TILE_BLANK);
    frame_timer = 8;
    shared_write(0xDFE0, 0x0B);
}

/* Start walking through the opened gate (00:0E96). */
void finish_boss_gate(void)
{
    if (taken(frame_timer != 0)) return;
    level_end_counter = mario_jump_state = 0;
    underground = 1;
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Set up the window's game-over text and award continues (00:1C7C). */
void prepare_game_over(void)
{
    uint16_t src = 0x1CD7, dst = 0x9C00;
    uint8_t count = 17;
    do {
        uint8_t tile = mem_read(src++);
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        vram_write(dst++, tile);
    } while (taken(--count != 0));
    shared_write(0xDFE8, 0x10);
    continue_world_level = shared_read(ADDR_WORLD_AND_LEVEL);
    uint8_t continues = num_continues + reg_b(shared_read(ADDR_SCORE + 2) >> 4);
    if (!taken(continues < 10)) continues = 9;
    num_continues = continues;
    count = 0xA0; dst = 0xC000;
    do { shared_write(dst++, 0); } while (taken(--count != 0));
    timer_expiring_flag = 0;
    io_write(rTMA, 0); io_write(rWY, 0x8F); io_write(rWX, 7);
    shared_write(0xFFFB, 0xFF);
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Wait for the STAT handler to finish the game-over window (00:1CE8). */
void update_game_over(void)
{
    if (taken(shared_read(0xC0AD) != 0)) reset_to_menu();
}

/* Display the time-up window before charging a life (00:1CF0). */
void prepare_time_up(void)
{
    uint16_t src = 0x1D14, dst = 0x9C00;
    uint8_t count = 9;
    do {
        uint8_t tile = reg_b(mem_read(src++));
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        vram_write(dst++, tile);
    } while (taken(--count != 0));
    io_write(rLCDC, io_read(rLCDC) | 0x20);
    frame_timer = 0xA0;
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* End the time-up delay (00:1D1D). */
void finish_time_up(void)
{
    if (taken(frame_timer != 0)) return;
    shared_write(0xFFB3, STATE_DEAD);
}
