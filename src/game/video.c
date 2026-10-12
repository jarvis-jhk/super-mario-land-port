/* Background map helpers. */
#include "game.h"
#include "ram.h"

enum { TILE_SPACE = 0x2C };

/* Load the current world's background and sprite tiles (00:05E7).
   Keep one byte from each row of the animated tile as its rotation seed. */
void load_level_graphics(void)
{
    copy_data(0x5032, 0x9000, 0x0800);
    copy_data(0x4032, 0x8000, 0x1000);
    uint16_t src = 0x5603, dst = 0xC600;
    uint8_t rows = 8;
    do {
        ram_write(dst++, mem_read(src));
        src += 2;
    } while (taken(--rows != 0));
}

/* Copy count bytes (0 means 65536) from src to dst (00:05DE). */
void copy_data(uint16_t src, uint16_t dst, uint16_t count)
{
    do {
        bus_write(dst++, mem_read(src++));      /* reads ROM/RAM, writes VRAM */
        count = dec_bc(count);
    } while (taken(count != 0));
}

/* Fill both background maps (9800-9BFF) with blanks, top down from the end
   (00:05CF). Only used with the LCD off. */
void clear_bg_maps(void)
{
    erase_tile_map(0x9BFF, 0x0400);
}

/* Erase backwards, including the ending's unusual 9C00 start (00:05D5). */
void erase_tile_map(uint16_t addr, uint16_t count)
{
    do {
        vram_write(addr--, TILE_SPACE);
        count = dec_bc(count);
    } while (taken(count != 0));
}

/* Background map address of the tile under pixel (tile_x_px - 8,
   tile_y_px - 16), also stored in tile_addr_lo/hi (00:3EE6). */
uint16_t tile_address_at(void)
{
    uint8_t row = (uint8_t)(tile_y_px - 0x10) >> 3;
    loop_taken(32);                  /* the original multiplies by adding 32 times */
    uint16_t addr = (uint16_t)(0x9800 + row * 32);
    uint8_t col = (uint8_t)(tile_x_px - 0x08) >> 3;
    addr = (uint16_t)(addr + col);
    tile_addr_hi = addr >> 8;
    tile_addr_lo = addr & 0xFF;
    return addr;
}

/*
 * The background tile under (tile_x_px, tile_y_px) (00:0153). Video RAM can
 * only be read outside the LCD's drawing phase, so this waits for the
 * horizontal blank. A timer interrupt between the wait and the read could
 * still make the read fail (it returns $FF), so the tile is read twice and
 * the two results ANDed.
 */
uint8_t lookup_tile(void)
{
    uint16_t addr = tile_address_at();
    while (taken((io_read(rSTAT) & 3) != 0))
        ;
    uint8_t first = reg_b(vram_read(addr));
    while (taken((io_read(rSTAT) & 3) != 0))
        ;
    return vram_read(addr) & first;
}

/* Inverse of tile_address_at: set tile_y_px/tile_x_px to the top-left pixel
   of the tile at tile_addr_hi/lo (00:3F13). */
void tile_address_to_pixel(void)
{
    uint8_t row_bits = (uint8_t)((tile_addr_hi << 4) | (tile_addr_lo >> 4));
    loop_taken(4);                   /* 16-bit shift right by 4, one bit per loop */
    uint8_t y = (uint8_t)((row_bits - 0x84) & 0xFE);
    y = (uint8_t)((y << 2) | (y >> 6));   /* rotate left twice */
    tile_y_px = (uint8_t)(y + 8);
    tile_x_px = (uint8_t)(((tile_addr_lo & 0x1F) << 3) + 8);
}
