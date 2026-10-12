/* Scripted walking, Daisy's dialogue and her disguise at the level exit. */
#include "game.h"
#include "ram.h"
#include "sound.h"
#include "banks.h"

enum { EXIT_WALK_RIGHT = 0x10, TEXT_NEWLINE = 0xFE, TEXT_END = 0xFF };

/* Move Mario right until he walks or flies off screen (00:0EBB). */
void walk_right_at_exit(void)
{
    joy_held = EXIT_WALK_RIGHT;
    if (taken((mario_anim & 0x0F) < 0x0A)) mario_floor_collision();
    mario_animate_and_walk();
}

/* Wait for Mario to leave the screen, then advance the scene (00:0EA9). */
void update_exit_walk(void)
{
    walk_right_at_exit();
    if (taken(mario_x < 0xC0)) return;
    frame_timer = 0x20;
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Place Mario and the apparent Daisy for their meeting (00:0EE7). */
void prepare_mario_and_daisy(void)
{
    mario_y = 0x7E;
    mario_x = 0xB0;
    mario_anim &= 0xF0;
    uint16_t src = 0x211D, dst = 0xC210;
    uint8_t count = 16;
    do {
        ram_write(dst++, mem_read(src++));
    } while (taken(--count != 0));
    ram_write(0xC211, 0x7E);
    ram_write(0xC212, 0);
    ram_write(0xC213, 0x22);
    ram_write(0xC215, 0x20);   /* face Mario */
}

/* Start the meeting after the exit-walk delay (00:0ECD). */
void update_daisy_meeting(void)
{
    if (taken(frame_timer != 0)) return;
    prepare_mario_and_daisy();
    shared_write(ADDR_COLUMN_STATE, 0);
    column_phase = 0;
    frame_timer = 0xA1;
    shared_write(SOUND_MUSIC_REQUEST, MUS_OH_DAISY);  /* Daisy's music */
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Print one dialogue tile every twelve frames (00:0F8A). FE introduces
   a skip distance and the next tile. Preserve the original low-byte wrap
   and return value: callers test A for FF, even on the timer-only path. */
uint8_t print_victory_message(uint16_t text)
{
    uint8_t result = frame_timer;
    if (taken(result != 0)) return result;
    uint8_t index = shared_read(0xFFFB);
    text = (uint16_t)(text + index);
    uint8_t tile = reg_b(mem_read(text));
    uint16_t cursor;
    if (taken(tile == TEXT_NEWLINE)) {
        uint8_t skip = mem_read(++text);
        tile = mem_read(++text);
        cursor = (uint16_t)((text_cursor_hi << 8 | text_cursor_lo) + skip);
        reg_b(tile);                        /* push af ... pop bc */
        index = (uint8_t)(index + 2);
    } else {
        if (taken(tile == TEXT_END)) return tile;
        cursor = (uint16_t)(text_cursor_hi << 8 | text_cursor_lo);
    }
    while (taken((io_read(rSTAT) & 3) != 0)) ;
    while (taken((io_read(rSTAT) & 3) != 0)) ;
    vram_write(cursor++, tile);
    text_cursor_hi = cursor >> 8;
    uint8_t lo = cursor;
    if (!taken((lo & 0x0F) != 0)) {
        if (!taken((lo & 0x10) != 0)) lo = (uint8_t)(lo - 0x20);
    }
    text_cursor_lo = lo;
    shared_write(0xFFFB, (uint8_t)(index + 1));
    frame_timer = 12;
    return 12;
}

/* Finish fake Daisy's speech and begin her transformation (00:0F6A). */
void update_daisy_dialogue(void)
{
    if (taken(print_victory_message(0x0FE1) != TEXT_END)) return;
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
    ram_write(0xC210, 0x80);
    frame_timer = 8;
    shared_write(0xFFFB, 8);
    shared_write(SOUND_MUSIC_REQUEST, MUS_FAKE_DAISY);
}

/* Replace Daisy's four sprites with a transformation frame (00:1029). */
void write_daisy_sprites(uint16_t src)
{
    uint16_t dst = 0xC01C;
    uint8_t count = 16;
    do {
        shared_write(dst++, mem_read(src++));
    } while (taken(--count != 0));
}

/* Alternate the disguise sprites before revealing the monster (00:0FFD). */
void update_daisy_morph(void)
{
    if (taken(frame_timer != 0)) return;
    uint8_t remaining = (uint8_t)(shared_read(0xFFFB) - 1);
    if (taken(remaining == 0)) {
        ram_write(0xC210, 0);
        shared_write(0xFFB3, shared_read(0xFFB3) + 1);
        return;
    }
    shared_write(0xFFFB, remaining);
    uint16_t frame = 0x1035;
    if (!taken((remaining & 1) != 0)) {
        frame = 0x1045;
        shared_write(SOUND_NOISE_REQUEST, NOISE_DEATH_CRY);
    }
    write_daisy_sprites(frame);
    frame_timer = 8;
}

/* Scroll the hangar into view while Mario and Daisy stand still (00:0F12). */
void scroll_to_daisy(void)
{
    if (taken(frame_timer == 0)) {
        level_index = shared_read(0xFFFB);
        shared_write(0xFFB3, shared_read(0xFFB3) + 1);
        return;
    }
    shared_write(0xFFA4, shared_read(0xFFA4) + 1);
    update_level_columns(); mario_x--; ram_write(0xC212, ram_read(0xC212) - 1); draw_mario();
}

/* Walk Mario to Daisy and position the dialogue above the gate (00:0F33). */
void approach_daisy(void)
{
    joy_held = EXIT_WALK_RIGHT; mario_floor_collision(); mario_animate_and_walk();
    if (taken(mario_x < 0x4C)) return;
    mario_anim &= 0xF0;
    uint8_t cursor = reg_b(ram_read(0xFFE0) - 0x40 + 4);
    if (!taken((cursor & 0xF0) != 0xC0)) cursor -= 0x20;
    text_cursor_lo = cursor; text_cursor_hi = 0x98;
    shared_write(0xFFFB, 0); shared_write(0xFFB3, shared_read(0xFFB3) + 1); draw_mario();
}

/* Make fake Daisy's revealed monster hop off screen (00:1055). */
void update_daisy_escape(void)
{
    if (taken(frame_timer != 0)) return;
    ram_write(0xC213, 0x20); move_along_jump_arc(0xC210, 0x216D);
    if (!taken(ram_read(0xC217) != 0)) {
        ram_write(0xC217, 1); ram_write(0xC213, 0x21); frame_timer = 0x40;
    }
    if (!taken((shared_read(ADDR_FRAME_COUNTER) & 1) != 0)) {
        uint8_t x = ram_read(0xC212) + 1; ram_write(0xC212, x);
        if (taken(x >= 0xD0)) {
            shared_write(0xFFB3, 0x12); shared_write(ADDR_ACTIVE_ROM_BANK, 2); set_rom_bank(2); return;
        }
    }
    draw_mario();
}

/* Shake Tatanga's stage and dissolve the tiles with a rotating mask (00:1099). */
void update_tatanga_defeat(void)
{
    if (!taken(ram_read(0xFFA7) != 0)) { shared_write(0xDFF8, 1); ram_write(0xFFA7, 0x20); }
    level_progress = 0; update_enemies();
    uint8_t timer = frame_timer;
    if (!taken((timer & 3) != 0)) {
        uint8_t shake = shared_read(0xFFFB) ^ 1; shared_write(0xFFFB, shake);
        uint8_t step = 0xFC;
        if (!taken(shake == 0)) step = 4;
        shared_write(0xC0DF, shared_read(0xC0DF) + step);
    }
    if (taken(timer >= 0x80)) return;
    if (taken((timer & 31) != 0)) return;
    uint16_t map = 0x8DD0, count = 34 * 16;
    uint8_t mask = enemy_slot_lo;
    do {
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        uint8_t bits = vram_read(map) & mask;
        while (taken((io_read(rSTAT) & 3) != 0)) ;
        vram_write(map++, bits);
        if (!taken((map >> 8) != 0x8F)) map = 0x9690;
        mask = (mask >> 1) | (mask << 7);
        count = dec_bc(count);
    } while (taken(count != 0));
    mask = (uint8_t)(enemy_slot_lo << 1);
    if (taken(mask == 0)) {
        shared_write(0xC0DF, 0); level_end_counter = 0; underground = 1;
        shared_write(0xFFB3, shared_read(0xFFB3) + 1);
    } else { enemy_slot_lo = (mask << 4) | (mask >> 4); frame_timer = 0x3F; }
}

/* Set up the real Daisy's reunion in the hangar (00:1116). */
void prepare_daisy_reunion(void)
{
    io_write(rLCDC, 0); underground = 0;
    erase_tile_map(0x9C00, 0x0100);
    draw_initial_columns(); prepare_mario_and_daisy();
    mario_x = 0x38; mario_anim = 0x10; ram_write(0xC212, 0x78);
    io_write(rIF, 0); shared_write(0xFFA4, 0); shared_write(0xC0DF, 0); shared_write(0xFFFB, 0);
    uint16_t dst = 0xC000; uint8_t count = 12;
    do { shared_write(dst++, 0); } while (taken(--count != 0));
    draw_mario(); text_cursor_hi = 0x98; text_cursor_lo = 0xA5;
    shared_write(0xDFE8, 0x0F); io_write(rLCDC, 0xC3);
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Daisy answers Mario, then starts running to him (00:1165). */
void update_reunion_dialogue(void)
{
    if (taken(print_victory_message(0x1183) != TEXT_END)) return;
    shared_write(0xFFFB, 0); text_cursor_hi = 0x99; text_cursor_lo = 2;
    ram_write(0xC213, 0x23); shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}

/* Print Daisy's thanks as she runs towards Mario (00:1194). */
void update_daisy_run(void)
{
    print_victory_message(0x11BF);
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0)) return;
    uint8_t x = ram_read(0xC212);
    if (!taken(x < 0x44)) { ram_write(0xC212, x - 1); draw_mario(); return; }
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
    shared_write(0xC030, 0x70); shared_write(0xC031, 0x3A); shared_write(0xC032, 0x84); shared_write(0xC033, 0);
}

/* Float a heart between Mario and Daisy, then erase the dialogue (00:11D0). */
void update_daisy_kiss(void)
{
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 1) != 0)) return;
    uint8_t y = shared_read(0xC030) - 1; shared_write(0xC030, y);
    y = shared_read(0xC030);
    if (taken(y < 0x20)) {
        shared_write(0xC031, 0xF0);
        uint16_t map = 0x98A5; uint8_t count = 0x6D;
        do {
            while (taken((io_read(rSTAT) & 3) != 0)) ;
            while (taken((io_read(rSTAT) & 3) != 0)) ;
            vram_write(map++, TILE_BLANK);
        } while (taken(--count != 0));
        shared_write(0xFFFB, 0); text_cursor_hi = 0x99; text_cursor_lo = 0;
        shared_write(0xFFB3, shared_read(0xFFB3) + 1); return;
    }
    uint8_t direction = shared_read(0xFFFB), x = shared_read(0xC031);
    if (taken(direction != 0)) {
        shared_write(0xC031, shared_read(0xC031) + 1);
        if (taken(x < 0x50)) return;
        shared_write(0xFFFB, 0);
    } else {
        shared_write(0xC031, shared_read(0xC031) - 1);
        if (taken(x >= 0x30)) return;
        shared_write(0xFFFB, x);
    }
}

/* Announce the completed quest and prepare the plane (00:121B). */
void update_quest_complete(void)
{
    if (taken(print_victory_message(0x123F) != TEXT_END)) return;
    ram_write(0xC213, 0x24); ram_write(0xC215, 0);
    ram_write(0xC241, 0x7E); ram_write(0xC243, 0x28); ram_write(0xC245, 0);
    shared_write(0xFFB3, shared_read(0xFFB3) + 1);
}
