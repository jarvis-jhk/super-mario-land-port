/* The plane flight, scrolling credits and final letters (00:1254-1556). */
#include "game.h"
#include "ram.h"

static void next_scene(void)
{
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Alternate the plane exhaust every four frames (00:12E6). */
void animate_plane_exhaust(uint16_t x_addr)
{
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0)) return;
    ram_write(x_addr + 1, ram_read(x_addr + 1) ^ 1);
}

/* Draw the plane and animate its exhaust (00:130E). */
void animate_plane(void)
{
    animate_plane_exhaust(0xC202); draw_mario();
}

/* Drift the three clouds left; DIV chooses a new height on wrapping (00:13C4). */
void animate_clouds(void)
{
    uint16_t obj = 0xC212;
    uint8_t count = 3;
    do {
        uint8_t x = ram_read(obj) - 1; ram_write(obj, x);
        if (taken(x != 1)) {
            if (!taken(x != 0xE0)) {
                uint8_t y = (io_read(rDIV) + ram_read(obj - 1)) & 0x7F;
                if (!taken(y >= 0x68)) y &= 0x3F;
                ram_write(obj - 1, y); ram_write(obj - 2, 0);
            }
        } else ram_write(obj, 0xFE);
        obj += 16;
    } while (taken(--count != 0));
}

/* Draw the plane and drift its clouds (00:1550). */
void animate_plane_and_clouds(void) { animate_plane(); animate_clouds(); }

/* Walk Mario and Daisy into the waiting plane (00:1254). */
void board_plane(void)
{
    if (!taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0))
        ram_write(0xC213, ram_read(0xC213) ^ 1);
    if (!taken(ram_read(0xC240) != 0)) {
        uint8_t x = ram_read(0xC242) - 1; ram_write(0xC242, x);
        if (!taken(x != 0x50)) mario_flags = 0x80;
        else if (!taken(x != 0x40)) {
            ram_write(0xC210, 0x80); frame_timer = 0x40; next_scene();
        }
    }
    walk_right_at_exit(); update_level_columns();
    if (taken(screen_index != 3)) return;
    if (taken(column_index != 0)) return;
    ram_write(0xC240, 0); ram_write(0xC242, 0xC0);
}

/* Transfer the plane into Mario's object slot for the flight (00:12A1). */
void prepare_plane_takeoff(void)
{
    if (taken(frame_timer != 0)) return;
    uint16_t src = 0xC240, dst = 0xC200;
    uint8_t count = 6;
    do { ram_write(dst++, mem_read(src++)); } while (taken(--count != 0));
    mario_anim = 0x26; ram_write(0xC241, 0xF0); next_scene();
}

/* Climb one pixel every other frame until the cruising height (00:12C2). */
void update_plane_takeoff(void)
{
    draw_mario();
    if (taken((reg_b(shared_read(ADDR_FRAME_COUNTER)) & 1) != 0)) return;
    ram_write(0xC240, 0xFF);
    mario_y--;
    if (taken(mario_y == 0x58)) { next_scene(); shared_write(0xFFFB, 4); }
    else animate_plane_exhaust(0xC202);
}

/* Feed blank columns behind the plane, including the two HUD tiles (00:134E). */
void clear_flight_column(void)
{
    uint16_t dst = ADDR_COLUMN_BUFFER;
    uint8_t count = 16;
    do { shared_write(dst++, TILE_BLANK); } while (taken(--count != 0));
    shared_write(ADDR_COLUMN_STATE, 1);
    uint16_t map = 0x9800 | (uint8_t)(shared_read(ADDR_DRAW_COLUMN_LO) - 0x20);
    count = 2;
    do {
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        vram_write(map, TILE_BLANK);
        map = (map & 0xFF00) | (uint8_t)(map - 0x20);
    } while (taken(--count != 0));
}

/* Copy the five visible cloud fields (00:1376). */
void replace_cloud(uint16_t obj, uint16_t src)
{
    uint8_t count = 5;
    do { ram_write(obj++, mem_read(src++)); } while (taken(--count != 0));
}

/* End the hangar flight after four complete horizontal scroll wraps (00:1318). */
void count_plane_scroll_wrap(void)
{
    uint8_t remaining = shared_read(0xFFFB) - 1; shared_write(0xFFFB, remaining);
    if (taken(remaining != 0)) return;
    io_write(rLYC, 0); shared_write(0xFFFB, 0x21); shared_write(ADDR_DRAW_COLUMN_LO, 0x54);
    clear_flight_column();
    replace_cloud(0xC210, 0x137F); replace_cloud(0xC220, 0x1384); replace_cloud(0xC230, 0x1389);
    next_scene();
}

/* Cruise through the hangar and start the ending song (00:12F1). */
void update_plane_cruise(void)
{
    animate_plane(); update_level_columns();
    uint8_t scroll = shared_read(0xFFA4) + 1;
    if (taken(scroll == 0)) count_plane_scroll_wrap();
    scroll++;
    if (taken(scroll == 0)) count_plane_scroll_wrap();
    shared_write(0xFFA4, scroll);
    if (taken(shared_read(0xDFE9) != 0)) return;
    shared_write(0xDFE8, 0x11);
}

/* Clear the remaining hangar while clouds enter view (00:138E). */
void leave_hangar(void)
{
    animate_plane_and_clouds();
    uint8_t scroll = shared_read(0xFFA4) + 2; shared_write(0xFFA4, scroll);
    if (taken(column_phase != reg_b(scroll & 8))) return;
    column_phase ^= 8; clear_flight_column();
    uint8_t remaining = shared_read(0xFFFB) - 1; shared_write(0xFFFB, remaining);
    if (taken(remaining != 0)) return;
    shared_write(0xFFA4, 0); io_write(rLYC, 0x60);
    text_cursor_hi = 0x15; text_cursor_lo = 0x57; frame_timer = 0xF0; next_scene();
}

/* Print two credit lines below the visible map for the next scroll (00:13F0). */
void prepare_credit_lines(void)
{
    animate_plane_and_clouds();
    if (taken(frame_timer != 0)) return;
    uint16_t text = text_cursor_hi << 8 | text_cursor_lo, map = 0x9A42;
    for (;;) {
        uint8_t tile = mem_read(text);
        if (taken(tile == 0xFE)) tile = TILE_BLANK;
        else { text++; reg_b(tile); }
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        vram_write(map++, tile);
        if (taken((uint8_t)map == 0x54)) { map = 0x9A87; text++; continue; }
        if (taken((uint8_t)map == 0x93)) break;
    }
    text++;
    if (!taken(mem_read(text) != 0xFF)) shared_write(0xC0DE, 0xFF);
    text_cursor_hi = text >> 8; text_cursor_lo = text; next_scene();
}

/* Scroll the new credits into view (00:1441). */
void scroll_credits_in(void)
{
    animate_plane_and_clouds();
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0)) return;
    uint8_t scroll = shared_read(0xC0DF) + 1; shared_write(0xC0DF, scroll);
    scroll = shared_read(0xC0DF);
    if (taken(scroll != 0x20)) return;
    next_scene(); frame_timer = 0x50;
}

/* Keep the credit lines still for their reading interval (00:145A). */
void hold_credits(void)
{
    animate_plane_and_clouds();
    if (taken(frame_timer != 0)) return;
    next_scene();
}

/* Scroll the credits out and select another pair or the finale (00:1466). */
void scroll_credits_out(void)
{
    animate_plane_and_clouds();
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0)) return;
    uint8_t scroll = shared_read(0xC0DF) + 1; shared_write(0xC0DF, scroll);
    scroll = shared_read(0xC0DF);
    if (taken(scroll != 0x50)) return;
    shared_write(0xC0DF, 0);
    uint8_t state = 0x33;
    if (!taken(shared_read(0xC0DE) != 0xFF)) state = 0x37;
    shared_write(0xFFB3, state);
}

/* Fly the plane out and seed THE END's six letter sprites (00:1488). */
void prepare_ending_letters(void)
{
    animate_plane_and_clouds(); mario_x++;
    if (taken(mario_x != 0xD0)) return;
    mario_y = 0xF0; draw_mario(); mario_flags = 0xFF;
    uint16_t src = 0x14C4, dst = 0xC070;
    uint8_t count = 0x18;
    do { shared_write(dst++, mem_read(src++)); } while (taken(--count != 0));
    count = 0x18;
    do { shared_write(dst++, 0); } while (taken(--count != 0));
    frame_timer = 0x90; win_count++; ram_write(0xC0E1, win_count); next_scene();
}

/* Reset to the menu when the user presses a button after the ending (00:1529). */
void ending_button_press(void)
{
    if (taken(joy_pressed == 0)) return;
    init_sound(); reset_to_menu();
}

/* Bring each THE END letter to rest before accepting input (00:14DC). */
void update_ending_letters(void)
{
    animate_plane_and_clouds();
    if (taken(frame_timer != 0)) return;
    uint16_t sprite = 0xC071;
    if (!taken(shared_read(sprite) == 0x3C)) {
        shared_write(sprite, shared_read(sprite) - 1);
        shared_write(sprite, shared_read(sprite) - 1);
        shared_write(sprite, shared_read(sprite) - 1); return;
    }
    for (unsigned i = 0; i < 5; i++) {
        sprite += 4;
        if (taken(shared_read(sprite) != rom_byte(0, (uint16_t)(0x14F4 + 8 * i)))) {
            shared_write(sprite, shared_read(sprite) - 1);
            shared_write(sprite, shared_read(sprite) - 1);
            shared_write(sprite, shared_read(sprite) - 1); return;
        }
    }
    ending_button_press();
    level_index = super_status = superball_mario = num_continues = 0;
    shared_write(ADDR_WORLD_AND_LEVEL, 0x11);
}
