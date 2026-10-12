/* The level: background animation, scrolling, drawing new columns. */
#include "game.h"
#include "ram.h"
#include "banks.h"
#include "level.h"
#include "constants.h"
#include "sound.h"

enum {
    ANIMATED_TILE         = 0x95D1,  /* the one animated background tile (odd bytes = its first bit plane) */
    ANIMATION_FRAME_RAM   = 0xC600,  /* first frame: copied to RAM when the level was loaded */
    ANIMATION_FRAMES_ROM  = 0x3FC4,  /* second frame, one per world (8 bytes each) */
};

/*
 * Animate the background (vblank handler, 00:2401): in levels that have it,
 * every 8 frames one background tile switches between two pictures (water,
 * clouds ...). Only the first bit plane of the tile changes.
 */
void animate_background(void)
{
    if (taken(background_animated == 0))
        return;
    if (taken(game_state_isr >= STATE_AUTOSCROLL))
        return;
    if (taken((frame_counter & 7) != 0))
        return;
    uint16_t src;
    if (!taken((frame_counter & 0x08) == 0)) {
        src = ANIMATION_FRAME_RAM;
    } else {
        /* 8 * (world - 1), computed with a rotate as in the original */
        uint8_t a = (uint8_t)((world_and_level & 0xF0) - 0x10);
        a = (uint8_t)((a >> 1) | (a << 7));
        src = (uint16_t)(ANIMATION_FRAMES_ROM + a);
    }
    uint16_t dst = ANIMATED_TILE;
    uint8_t count = 8;
    do {
        vram_write(dst, mem_read(src++));
        dst += 2;
    } while (taken(--count != 0));
}

/* ------------------------------------------------------------------ */
/* Level columns                                                       */
/*
 * The level is drawn one 16-tile column at a time as the screen scrolls.
 * The main program decodes the next column from the level data into
 * column_buffer (load_next_column); the vblank handler copies it into the
 * background map (draw_column). Next to the background map, an "overlay"
 * map at C800-CBFF (the background map address + $3000) marks special
 * tiles: warp pipes and blocks that contain items.
 */

enum {
    TILE_ITEM_BLOCK     = 0x80,     /* a block that may hold an item */
    LEVEL_DATA_END      = 0xFF,
    COLUMN_END          = 0xFE,
    COLUMN_REPEAT       = 0xFD,
    COLUMNS_PER_SCREEN  = 20,
    LEVEL_POINTERS      = 0x4000,   /* in the level's bank: per level a pointer to its screen list */
    WARP_PIPE_TABLE     = 0x651C,   /* bank 3: per level a list of (screen, column, 4 bytes pipe data) */
    ITEM_BLOCK_TABLE    = 0x6536,   /* bank 3: per level a list of (screen, column, item) */
};

/* Increment only the low byte of a pointer, as `inc e` does. */
static uint16_t inc_low(uint16_t p) { return (uint16_t)((p & 0xFF00) | ((p + 1) & 0xFF)); }

/* The overlay map entry for a background map address. */
static uint16_t overlay_of(uint16_t map) { return (uint16_t)((((map >> 8) + 0x30) & 0xFF) << 8 | (map & 0xFF)); }

static uint16_t rom_word(uint16_t addr) { return (uint16_t)(mem_read(addr) | mem_read((uint16_t)(addr + 1)) << 8); }

/*
 * Once per frame (00:2198): when the screen has scrolled by 8 more pixels,
 * load the next level column (every second one also advances level_progress,
 * which makes enemies appear). While a column is pending, wait until the
 * vblank handler has drawn it and the screen has moved on (00:2188).
 */
void update_level_columns(void)
{
    if (taken(shared_read(ADDR_COLUMN_STATE) != COLUMN_IDLE)) {
        shared_write(ADDR_COLUMN_STATE, COLUMN_WAIT);
        uint8_t scroll = reg_b(shared_read(0xFFA4));
        if (taken(drawn_scroll_x == scroll))
            return;
        shared_write(ADDR_COLUMN_STATE, COLUMN_IDLE);
        return;
    }
    if (taken((shared_read(0xFFA4) & 0x08) != column_phase))
        return;
    column_phase ^= 0x08;
    if (!taken(column_phase != 0))
        level_progress++;
    load_next_column();
}

/*
 * Decode the next column of the level into column_buffer (00:21B1).
 *
 * Level data: a list of screens per level; a screen is a list of columns;
 * a column is a list of commands ended by $FE. A command byte has the
 * start row in its high nibble and a tile count in the low one (0 = 16),
 * followed by the tiles, or by $FD and one tile to repeat. Rows not covered
 * stay blank. $FF instead of a screen pointer ends the level.
 */
void load_next_column(void)
{
    uint16_t dst = ADDR_COLUMN_BUFFER;
    uint8_t n = 16;
    do {
        shared_write(dst++, TILE_BLANK);
    } while (taken(--n != 0));

    uint16_t src;
    if (!taken(column_index == 0)) {
        src = (uint16_t)(column_ptr_hi << 8 | column_ptr_lo);
    } else {
        uint16_t screens = rom_word((uint16_t)(LEVEL_POINTERS + (uint8_t)(level_index * 2)));
        uint16_t entry = (uint16_t)(screens + (uint8_t)(screen_index * 2));
        uint8_t lo = mem_read(entry);
        if (taken(lo == LEVEL_DATA_END)) {
            level_end_counter++;
            return;
        }
        src = (uint16_t)(lo | mem_read((uint16_t)(entry + 1)) << 8);
    }

    for (;;) {
        uint8_t cmd = mem_read(src++);
        if (taken(cmd == COLUMN_END))
            break;
        reg_b(cmd);
        dst = (uint16_t)(0xC000 | (uint8_t)(0xB0 + (cmd >> 4)));
        uint8_t count = cmd & 0x0F;
        if (!taken(count != 0))
            count = 16;
        reg_b(count);
        for (;;) {
            uint8_t tile = mem_read(src++);
            if (taken(tile == COLUMN_REPEAT)) {
                tile = mem_read(src);
                do {
                    shared_write(dst, tile);
                    dst = inc_low(dst);
                } while (taken(--count != 0));
                src++;
                break;
            }
            shared_write(dst, tile);
            if (!taken(tile != TILE_PIPE_TOP))
                check_pipe_for_warp();
            else if (!taken(tile != TILE_ITEM_BLOCK))
                check_block_for_item();
            else if (!taken(tile != TILE_HIDDEN_BLOCK))
                check_block_for_item();
            else if (taken(tile == TILE_COIN_BLOCK))
                check_block_for_item();
            dst = inc_low(dst);
            if (!taken(--count != 0))
                break;
        }
    }

    column_ptr_hi = src >> 8;
    column_ptr_lo = src & 0xFF;
    uint8_t col = (uint8_t)(column_index + 1);
    if (!taken(col != COLUMNS_PER_SCREEN)) {
        screen_index++;
        col = 0;
    }
    column_index = col;
    drawn_scroll_x = shared_read(0xFFA4);
    shared_write(ADDR_COLUMN_STATE, COLUMN_READY);
}

/*
 * Copy the loaded column into the background map (00:2258) and record warp
 * pipes and item blocks in the overlay map. The map column advances by one
 * each time, wrapping within the 32 columns. Runs in the vblank handler, and
 * in the main program while a level's first screen is drawn (00:0846).
 */
void draw_column(void)
{
    if (taken(shared_read(ADDR_COLUMN_STATE) != COLUMN_READY))
        return;
    uint8_t lo = shared_read(ADDR_DRAW_COLUMN_LO);
    uint8_t next = (uint8_t)(lo + 1);
    if (!taken(next != 0x60))
        next = 0x40;
    shared_write(ADDR_DRAW_COLUMN_LO, next);

    uint16_t map = (uint16_t)(0x9800 | lo);
    uint16_t src = ADDR_COLUMN_BUFFER;
    uint8_t rows = 16;
    do {
        shared_write(overlay_of(map), 0);
        uint8_t tile = shared_read(src);
        vram_write(map, tile);
        if (!taken(tile != TILE_PIPE_TOP))
            put_pipe_warp(map);
        else if (!taken(tile != TILE_ITEM_BLOCK))
            put_block_item(map);
        else if (!taken(tile != TILE_HIDDEN_BLOCK))
            put_block_item(map);
        else if (taken(tile == TILE_COIN_BLOCK))
            put_block_item(map);
        src = inc_low(src);
        map = (uint16_t)(map + 0x20);
    } while (taken(--rows != 0));
    shared_write(ADDR_COLUMN_STATE, COLUMN_DRAWN);
}

/*
 * The column being loaded has a pipe top: if the level's warp pipe table
 * has a pipe at this screen and column, copy its four bytes (destination
 * and exit position) to pipe_data, for put_pipe_warp (00:22A9).
 */
void check_pipe_for_warp(void)
{
    if (taken(underground != 0))
        return;
    switch_rom_bank_saving(3);
    uint16_t p = rom_word((uint16_t)(WARP_PIPE_TABLE + (uint8_t)(level_index * 2)));
    for (;;) {
        if (taken(screen_index == mem_read(p))) {
            p++;
            if (!taken(column_index != mem_read(p))) {
                p++;
                for (int i = 0; i < 4; i++)
                    (&pipe_data)[i] = mem_read((uint16_t)(p + i));
                break;
            }
        } else {
            if (taken(mem_read(p) == LEVEL_DATA_END))
                break;
            p++;
        }
        p = (uint16_t)(p + 5);
    }
    restore_rom_bank();
}

/* Record the pending warp pipe in the overlay map: the destination in the
   pipe's own cell, the three other bytes in the three cells above it
   (00:22FD). */
void put_pipe_warp(uint16_t map)
{
    uint8_t dest = (&pipe_data)[0];
    if (taken(dest == 0))
        return;
    uint16_t o = overlay_of(map);
    shared_write(o, dest);
    for (int i = 1; i < 4; i++) {
        o = (uint16_t)(o - 0x20);
        shared_write(o, (&pipe_data)[i]);
    }
    (&pipe_data)[0] = 0;
    (&pipe_data)[1] = 0;
}

/* The column being loaded has a block: look up whether the level's item
   table has an item at this screen and column (00:2321). */
void check_block_for_item(void)
{
    switch_rom_bank_saving(3);
    uint16_t p = rom_word((uint16_t)(ITEM_BLOCK_TABLE + (uint8_t)(level_index * 2)));
    for (;;) {
        if (taken(screen_index == mem_read(p))) {
            p++;
            if (!taken(column_index != mem_read(p))) {
                p++;
                block_item = mem_read(p);
                break;
            }
        } else {
            if (taken(mem_read(p) == LEVEL_DATA_END))
                break;
            p++;
        }
        p = (uint16_t)(p + 2);
    }
    restore_rom_bank();
}

/* Put the pending item into the overlay map at this block (00:2363). */
void put_block_item(uint16_t map)
{
    uint8_t item = block_item;
    if (taken(item == 0))
        return;
    shared_write(overlay_of(map), item);
    block_item = 0;
}

/* ------------------------------------------------------------------ */
/* Music                                                               */

enum { MUSIC_BY_LEVEL = 0x07CE };   /* 12 bytes: the song of each level, 1-1 to 4-3 */

/*
 * Start the level's music (00:07A3): after a level starts, after the star
 * runs out, on leaving an underground bonus area. Nothing happens while
 * Mario is invincible (the star music keeps playing). The sound engine is
 * reset first; the original maps bank 3 for it without saving the bank
 * (interrupts are disabled whenever this runs).
 */
void start_level_music(void)
{
    if (taken(invincibility_timer != 0))
        return;
    set_rom_bank(3);
    init_sound();
    set_rom_bank(shared_read(ADDR_ACTIVE_ROM_BANK));
    if (taken(pipe_data != 0)) {      /* entered a pipe to an underground area */
        shared_write(SOUND_MUSIC_REQUEST, MUS_UNDERGROUND);
        return;
    }
    shared_write(SOUND_MUSIC_REQUEST, mem_read((uint16_t)(MUSIC_BY_LEVEL + level_index)));
}
