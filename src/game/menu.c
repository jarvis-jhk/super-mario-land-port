/* Title screen, attract mode and level selection. */
#include "game.h"
#include "ram.h"

/* Fill the hidden top row of the title map (00:056F). */
void fill_menu_top_row(uint16_t map, uint8_t tile)
{
    uint8_t count = 20;
    do { vram_write(map++, tile); } while (taken(--count != 0));
}

/* Rebuild the title screen, update the high score and rotate demos (00:0322). */
void prepare_menu(void)
{
    io_write(rLCDC, 0);
    shared_write(0xFFA4, 0);
    uint16_t dst = 0xC000;
    uint8_t count = 0x9F;
    do { shared_write(dst++, 0); } while (taken(--count != 0));
    super_status = 0;
    shared_write(0xC0A5, 0); shared_write(0xC0AD, 0);
    demo_wait = demo_pos = demo_input = 0;
    win_count = ram_read(0xC0E1);
    copy_data(0x791A, 0x9300, 0x0500);
    copy_data(0x7E1A, 0x8800, 0x0170);
    uint16_t head = 0x4862;
    if (!taken(win_count < 1)) head = 0x4E72;
    copy_data(head, 0x8AC0, 16);
    copy_data(0x5032, 0x9000, 0x02C0);
    copy_data(0x5032, 0x8000, 0x02A0);
    clear_bg_maps();
    screen_index = 0;
    uint8_t level = level_index;
    level_index = 12;
    draw_first_level_screen();
    level_index = level;
    fill_menu_top_row(0x9800, 0x3C);
    vram_write(0x9804, 0x94);
    vram_write(0x9822, 0x95); vram_write(0x9823, 0x96); vram_write(0x9824, 0x8C);
    vram_write(0x982F, 0x3F); vram_write(0x9830, 0x4C); vram_write(0x9831, 0x4D);
    uint16_t score = 0xC0A2, top = 0xC0C2;
    count = 3;
    for (;;) {
        uint8_t best = ram_read(top), current = shared_read(score);
        if (taken(best < current)) {
            score = 0xC0A2; top = 0xC0C2; count = 3;
            do { ram_write(top--, shared_read(score--)); } while (taken(--count != 0));
            break;
        }
        if (taken(best != current)) break;
        top--; score--;
        if (!taken(--count != 0)) break;
    }
    print_bcd6(0xC0C2, 0x9969);
    uint16_t cursor = 0xC004;
    shared_write(cursor, 0x78);
    if (!taken(num_continues == 0)) {
        if (taken(win_count < 2)) {
            uint16_t text = 0x0446, map = 0x99C6;
            count = 10;
            do { vram_write(map++, mem_read(text++)); } while (taken(--count != 0));
            cursor = 0xC000;
            shared_write(cursor++, 0x80); shared_write(cursor++, 0x88);
            shared_write(cursor++, num_continues); shared_write(cursor++, 0);
            shared_write(cursor, 0x80);
        }
    }
    shared_write(++cursor, 0x28); shared_write(++cursor, 0xAC);
    io_write(rIF, 0); io_write(rLCDC, 0xC3);
    shared_write(0xFFB3, 0x0F);
    underground = 0;
    demo_timer = 0x28;
    shared_write(ADDR_DEMO_MODE, 0x28);
    demo_part++;
    if (taken(demo_part != 3)) return;
    demo_part = 0;
}

/* Start/continue a game, select an expert level or launch a demo (00:04C3). */
void update_menu(void)
{
    uint8_t buttons = reg_b(joy_pressed);
    if (taken((buttons & 8) != 0)) {
        if (taken(shared_read(0xC004) == 0x78)) {
            num_continues = 0;
            if (!taken(win_count >= 2)) {
                shared_write(ADDR_WORLD_AND_LEVEL, 0x11);
                level_index = 0;
            }
        } else {
            num_continues--;
            uint8_t selected = continue_world_level;
            shared_write(ADDR_WORLD_AND_LEVEL, selected);
            uint8_t index = 0;
            for (; index < 11; index++)
                if (taken(selected == rom_byte(0, (uint16_t)(0x0466 + 5 * index)))) break;
            level_index = index;
        }
        shared_write(0xFFB3, 0x11);
        io_write(rIF, 0);
        shared_write(ADDR_DEMO_MODE, 0);
        shared_write(ADDR_GAME_OVER_STATE_COPY, 0);
        shared_write(0xDFE8, 0xFF);
        set_rom_bank(3); init_sound();
        set_rom_bank(shared_read(ADDR_ACTIVE_ROM_BANK));
        shared_write(0xDFE0, 0); shared_write(0xDFF0, 0); shared_write(0xDFF8, 0);
        io_write(rIE, 7);
        return;
    }
    if (taken((buttons & 4) != 0)) {
        if (!taken(num_continues == 0))
            shared_write(0xC004, shared_read(0xC004) ^ 0xF8);
    }
    if (!taken(win_count < 2)) {
        if (!taken((buttons & 1) == 0)) {
            uint8_t world_level = reg_b(shared_read(ADDR_WORLD_AND_LEVEL) + 1);
            if (!taken((world_level & 15) != 4)) world_level += 13;
            shared_write(ADDR_WORLD_AND_LEVEL, world_level);
            uint8_t next = level_index + 1;
            if (!taken(next != 12)) { shared_write(ADDR_WORLD_AND_LEVEL, 0x11); next = 0; }
            level_index = next;
        }
        uint8_t world_level = shared_read(ADDR_WORLD_AND_LEVEL);
        shared_write(0xC008, 0x78); shared_write(0xC009, 0x78); shared_write(0xC00A, world_level >> 4);
        shared_write(0xC00C, 0x78); shared_write(0xC00D, 0x88); shared_write(0xC00E, world_level & 15);
        shared_write(0xC010, 0x78); shared_write(0xC011, 0x80); shared_write(0xC012, 0x29);
    }
    if (taken(demo_timer != 0)) return;
    uint16_t demo = 0x0569 + (uint8_t)(demo_part * 2);
    shared_write(ADDR_WORLD_AND_LEVEL, mem_read(demo));
    level_index = mem_read(demo + 1);
    demo_timer = 0x50;
    shared_write(0xFFB3, 0x11);
    win_count = 0;
}
