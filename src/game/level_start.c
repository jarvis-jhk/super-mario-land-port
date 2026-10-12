/* Timer, floating rewards and bonus-game defaults at level start. */
#include "game.h"
#include "ram.h"

/* Reset the reward sprites, start the clock at 400 and seed bonus-game
   state (00:3D1A). This also resets lives to two, as the original does. */
void init_level_counters(void)
{
    uint16_t sprite = 0xC030;
    uint8_t count = 32;
    do {
        shared_write(sprite++, 0);
    } while (taken(--count != 0));

    shared_write(ADDR_GAME_TIMER, 40);
    shared_write(ADDR_GAME_TIMER + 1, 0);
    shared_write(ADDR_GAME_TIMER + 2, 4);
    print_timer();

    for (unsigned i = 0; i < 4; i++) floaty_ttl[i] = 0x20;
    for (unsigned i = 0; i < 4; i++) floaty_coin_tile[i] = 0xF6;
    next_floaty_oam = 0x30;

    uint16_t addr = 0xDA0C;
    count = 9;
    do {
        ram_write(addr++, 0);
    } while (taken(--count != 0));

    lives_bcd = 2;
    ram_write(0xDA16, 1);
    for (addr = 0xDA17; addr <= 0xDA1A; addr++) ram_write(addr, 0);
    ram_write(0xDA1B, 0x40);   /* bonus-game end timer */
    ram_write(0xDA1C, 0);
    timer_expiring_flag = 0;
    ram_write(0xDA1E, 0);      /* bonus-game growth animation */
    ram_write(0xDA1F, 0x40);   /* bonus-game animation timer */

    addr = 0xDA20;
    count = 8;
    do {
        ram_write(addr++, 0);
    } while (taken(--count != 0));
    ram_write(0xDA28, 4);
    ram_write(0xDA29, 0x11);
}

/* Seed Mario and the reward objects, then draw the first screen (00:0807).
   The 81-byte copy deliberately includes one byte beyond the five objects,
   matching the original's off-by-one. Normal levels preload seven columns. */
void draw_first_level_screen(void)
{
    uint16_t src = 0x211D, dst = 0xC200;
    uint8_t count = 0x51;
    do {
        ram_write(dst++, mem_read(src++));
    } while (taken(--count != 0));
    if (!taken(super_status == 0)) mario_anim = 0x10;
    draw_initial_columns();
}

/* Draw the first screen without resetting the objects (00:081F). */
void draw_initial_columns(void)
{
    uint16_t dst;
    uint8_t count;
    dst = 0xFFE6;
    count = 6;
    do {
        bus_write(dst++, 0); /* the loop clears both plain and shared RAM */
    } while (taken(--count != 0));
    column_phase = 0;
    drawn_scroll_x = 0;
    shared_write(ADDR_DRAW_COLUMN_LO, 0x40);
    count = 20;
    if (!taken(shared_read(0xFFB3) == STATE_PIPE_UNDERGROUND)) {
        if (!taken(level_index == 12)) count = 27;
    }
    do {
        load_next_column();
        draw_column();
    } while (taken(--count != 0));
}

/* Return to the title screen with bank 2 mapped (00:1530). */
void reset_to_menu(void)
{
    shared_write(ADDR_ACTIVE_ROM_BANK, 2);
    set_rom_bank(2);
    demo_part = 2;
    shared_write(ADDR_GAME_OVER_STATE_COPY, 2);
    shared_write(ADDR_GAME_TIMER, 0);
    shared_write(0xC0A5, 0);
    shared_write(0xC0AD, 0);
    io_write(rIE, 3);
    shared_write(0xFFB3, STATE_MENU);
}

/* Build the level HUD and initialize gameplay after menu selection (00:0576). */
void start_level(void)
{
    io_write(rLCDC, 0);
    if (!taken(shared_read(ADDR_DEMO_MODE) != 0)) {
        shared_write(ADDR_SCORE, 0); shared_write(ADDR_SCORE + 1, 0); shared_write(ADDR_SCORE + 2, 0);
        coins_bcd = 0;
    }
    load_level_graphics(); clear_bg_maps();
    uint16_t map = 0x9C00;
    uint8_t count = 0x5F;
    do { vram_write(map++, TILE_BLANK); } while (taken(--count != 0));
    prepare_hud();
    io_write(rLYC, 15); io_write(rTAC, 7); io_write(rWY, 0x85); io_write(rWX, 0x60);
    io_write(rTMA, 0); io_write(rIF, 0);
    ram_write(0xFFA7, 0xFF);
    shared_write(ADDR_SCORE_CHANGED, 0xFF); shared_write(ADDR_DRAW_COLUMN_LO, 0x5B);
    reset_level_enemies(); init_level_counters(); display_coins(); display_lives();
    load_world_tiles(shared_read(ADDR_WORLD_AND_LEVEL));
}

/* Rebuild the level at the closest preceding checkpoint after a death (00:06DC). */
void restart_checkpoint(void)
{
    io_write(rLCDC, 0);
    clear_object_sprites(); clear_block_overlay();
    uint8_t screen;
    if (taken(underground == 0)) screen = screen_index;
    else { underground = 0; screen = ram_read(0xFFF5) + 1; }
    if (!taken(screen == 3)) screen--;
    uint8_t checkpoint = 3, progress = 0x0C;
    for (unsigned i = 0; i < 5; i++) {
        if (taken(screen < rom_byte(0, (uint16_t)(0x0701 + 7 * i)))) break;
        checkpoint += 4; progress += 0x28;
    }
    screen_index = checkpoint; column_index = 0; level_progress = progress;
    draw_first_level_screen();
    vram_write(0x982B, TILE_BLANK);
    uint8_t world_level = reg_b(shared_read(ADDR_WORLD_AND_LEVEL));
    vram_write(0x982C, world_level >> 4); vram_write(0x982E, world_level & 15);
    uint16_t src = 0x079A, dst = 0x9C00;
    uint8_t count = 9;
    do { vram_write(dst++, mem_read(src++)); } while (taken(--count != 0));
    shared_write(0xFFB3, 0); invincibility_timer = 0;
    io_write(rLCDC, 0xC3); start_level_music();
    io_write(rIF, 0); shared_write(0xFFA4, 0);
    level_end_counter = 0; shared_write(ADDR_BLOCK_EVENT, 0); timer_expiring_flag = 0;
    io_write(rTMA, 0);
    shared_write(ADDR_GAME_TIMER + 1, 0); shared_write(ADDR_GAME_TIMER + 2, 4); shared_write(ADDR_GAME_TIMER, 0x28);
    shared_write(ADDR_DRAW_COLUMN_LO, 0x5B);
    uint8_t vehicle = 0x0A;
    if (taken(level_index == 5)) {
        shared_write(0xFFB3, STATE_AUTOSCROLL);
        mario_anim = (mario_anim & 0xF0) | vehicle;
    } else {
        vehicle = 0x0C;
        if (!taken(level_index != 11)) {
            shared_write(0xFFB3, STATE_AUTOSCROLL);
            mario_anim = (mario_anim & 0xF0) | vehicle;
        }
    }
    init_enemy_slots();
}
