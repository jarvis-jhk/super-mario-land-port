/* Widescreen view, see widescreen.h. */
#include "widescreen.h"

#include <string.h>

enum {
    STATE_ADDR = 0xFFB3,        /* hGameState */
    LEVEL_INDEX = 0xFFE4,
    SCREEN_INDEX = 0xFFE5,
    COLUMN_INDEX = 0xFFE6,
    DRAW_COLUMN_LO = 0xFFE9,    /* next background map column: 9840-985F */
    COLUMN_STATE = 0xFFEA,      /* 1 = a decoded column waits to be drawn */
    UNDERGROUND = 0xFFF9,
    ACTIVE_BANK = 0xFFFD,       /* the level's bank while a level runs */
    LEVEL_POINTERS = 0x4000,
    LEVEL_ROW0 = 2,             /* level rows are map rows 2..17 */
    LEVEL_ROWS = 16,
    BLANK_TILE = 0x2C,
    MAX_DECODED = 96,
};

static uint8_t hram(const Machine *m, uint16_t a) { return m->hram[a - 0xFF80]; }

static int rom_byte(const uint8_t *rom, size_t size, int bank, uint16_t addr, int *ok)
{
    size_t off = addr < 0x4000 ? addr : (size_t)bank * 0x4000 + (addr - 0x4000);
    if (off >= size) {
        *ok = 0;
        return 0xFF;
    }
    return rom[off];
}

/* Decodes level column n (screen n / 20, column n % 20) of the level as
   load_next_column (00:21B1) does. Returns 0 past the level's end. */
static int decode_column(const uint8_t *rom, size_t size, int bank, int level, int n,
                         uint8_t tiles[LEVEL_ROWS])
{
    int ok = 1;
    memset(tiles, BLANK_TILE, LEVEL_ROWS);
    if (n < 0)
        return 0;
    int screen = n / 20, column = n % 20;
    uint16_t list = (uint16_t)(rom_byte(rom, size, bank, (uint16_t)(LEVEL_POINTERS + (uint8_t)(level * 2)), &ok) |
                               rom_byte(rom, size, bank, (uint16_t)(LEVEL_POINTERS + (uint8_t)(level * 2) + 1), &ok) << 8);
    /* The screen list is indexed with an 8-bit offset, like the original. */
    uint16_t entry = (uint16_t)(list + (uint8_t)(screen * 2));
    for (int s = 0; s <= screen && ok; s++)
        if (rom_byte(rom, size, bank, (uint16_t)(list + (uint8_t)(s * 2)), &ok) == 0xFF)
            return 0;
    uint16_t src = (uint16_t)(rom_byte(rom, size, bank, entry, &ok) |
                              rom_byte(rom, size, bank, (uint16_t)(entry + 1), &ok) << 8);
    for (int c = 0; c <= column && ok; c++) {
        for (int guard = 0; guard < 64; guard++) {
            int cmd = rom_byte(rom, size, bank, src++, &ok);
            if (cmd == 0xFE || !ok)
                break;
            int row = cmd >> 4, count = cmd & 0x0F;
            if (!count)
                count = 16;
            for (;;) {
                int tile = rom_byte(rom, size, bank, src++, &ok);
                if (tile == 0xFD) {
                    tile = rom_byte(rom, size, bank, src++, &ok);
                    for (; count > 0; count--, row++)
                        if (c == column && row < LEVEL_ROWS)
                            tiles[row] = (uint8_t)tile;
                    break;
                }
                if (c == column && row < LEVEL_ROWS)
                    tiles[row] = (uint8_t)tile;
                row++;
                if (--count == 0 || !ok)
                    break;
            }
        }
    }
    return ok;
}

static int tile_pixel(const uint8_t *vram, uint8_t lcdc, uint8_t t, int x, int y)
{
    const uint8_t *p = (lcdc & LCDC_TILES_8000) ? vram + t * 16 : vram + 0x1000 + (int8_t)t * 16;
    uint8_t lo = p[y * 2], hi = p[y * 2 + 1];
    int bit = 7 - x;
    return ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
}

static int shade(uint8_t palette, int colour) { return (palette >> (colour * 2)) & 3; }

/* The level is on screen and scrolls through the background map. */
static int level_on_screen(const Machine *m)
{
    if (!m->lcd_on || !(m->line_regs[LCD_H - 1].lcdc & LCDC_LCD_ON) || !(m->line_regs[LCD_H - 1].lcdc & LCDC_BG_ON))
        return 0;
    if (hram(m, UNDERGROUND))
        return 0;
    uint8_t lo = hram(m, DRAW_COLUMN_LO);
    if (lo < 0x40 || lo > 0x5F)
        return 0;
    switch (hram(m, STATE_ADDR)) {
    case 0x00: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x09: case 0x0D: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        return 1;
    default:
        return 0;
    }
}

/* The level column that goes into map column next_map, and how many map
   columns lie between the screen's left edge and it. */
static int next_level_column(const Machine *m)
{
    return hram(m, SCREEN_INDEX) * 20 + hram(m, COLUMN_INDEX) - (hram(m, COLUMN_STATE) == 1);
}

static int columns_ahead(const Machine *m)
{
    return (hram(m, DRAW_COLUMN_LO) - 0x40 - (m->line_regs[LCD_H - 1].scx >> 3)) & 31;
}

void widescreen_track(WideState *ws, const Machine *m)
{
    if (!level_on_screen(m) || columns_ahead(m) < 20) {
        ws->on = 0;
        return;
    }
    int next = next_level_column(m);
    if (!ws->on || ws->level_index != hram(m, LEVEL_INDEX) || next < ws->last_next)
        ws->first_column = next - columns_ahead(m);
    ws->on = 1;
    ws->level_index = hram(m, LEVEL_INDEX);
    ws->last_next = next;
}

int widescreen_render(const WideState *ws, const Machine *m, const uint8_t *rom, size_t rom_size,
                      int w, uint8_t *out)
{
    if (w < LCD_W)
        w = LCD_W;
    int ext = (w - LCD_W) / 2;
    int level = level_on_screen(m) && ws->on;
    int bank = hram(m, ACTIVE_BANK), level_index = hram(m, LEVEL_INDEX);
    int next_level = next_level_column(m);

    /* Columns relative to the screen's leftmost map column: the map holds
       the 32 columns up to next_map; others are decoded from the level. */
    uint8_t decoded[MAX_DECODED][LEVEL_ROWS];
    int have[MAX_DECODED];
    memset(have, 0, sizeof have);
    int rel_next = columns_ahead(m);

    for (int y = 0; y < LCD_H; y++) {
        const LcdRegs *r = &m->line_regs[y];
        uint8_t *row = out + y * w;
        memcpy(row + ext, m->framebuffer + y * LCD_W, LCD_W);
        int bg_y = (y + r->scy) & 0xFF, map_row = bg_y >> 3;
        int on = (r->lcdc & LCDC_LCD_ON) && (r->lcdc & LCDC_BG_ON);
        int win_line = (r->lcdc & LCDC_WIN_ON) && y >= r->wy && r->wx <= 166 ? y - r->wy : -1;
        const uint8_t *bg_map = m->vram + ((r->lcdc & LCDC_BG_MAP_9C00) ? 0x1C00 : 0x1800);
        uint8_t bg_colour[WIDE_MAX_W];
        int nb = 0;
        for (int i = 0; i < 2 * ext; i++) {
            int x = i < ext ? i - ext : LCD_W + (i - ext);   /* screen x */
            int colour = 0;
            if (!on || !level) {
                colour = 0;
            } else if (win_line >= 0 && (x >= 0 || r->wx <= 7)) {
                colour = 0;     /* next to a window (pause, game over): blank */
            } else if (map_row >= LEVEL_ROW0 && map_row < LEVEL_ROW0 + LEVEL_ROWS) {
                int lin = x + (r->scx & 7);
                int k = lin >= 0 ? lin >> 3 : -((7 - lin) >> 3);   /* floor(lin / 8) */
                int px = lin & 7;
                int ahead = k - rel_next;
                uint8_t t;
                if (ahead < 0 && ahead > -32 && next_level + ahead >= ws->first_column) {
                    t = bg_map[map_row * 32 + (((r->scx >> 3) + k) & 31)];
                } else {
                    /* Columns before the first one drawn may be an underground
                       room kept in the same screen list: blank. */
                    int slot = ahead >= 0 ? ahead : MAX_DECODED / 2 + (-ahead - 32);
                    if (slot >= MAX_DECODED)
                        slot = MAX_DECODED - 1;
                    if (!have[slot]) {
                        decode_column(rom, rom_size, bank, level_index,
                                      next_level + ahead < ws->first_column ? -1 : next_level + ahead,
                                      decoded[slot]);
                        have[slot] = 1;
                    }
                    t = decoded[slot][map_row - LEVEL_ROW0];
                }
                colour = tile_pixel(m->vram, r->lcdc, t, px, bg_y & 7);
            } else {
                colour = 0;     /* the status bar: its background colour */
            }
            bg_colour[nb++] = (uint8_t)colour;
            row[i < ext ? i : i + LCD_W] = (uint8_t)shade(r->bgp, colour);
        }
        if (!on || !level || !(r->lcdc & LCDC_OBJ_ON))
            continue;

        /* The parts of sprites that cross the screen's edges. */
        int height = (r->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
        int sel[10], n = 0;
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = m->oam[i * 4] - 16;
            if (y >= sy && y < sy + height)
                sel[n++] = i;
        }
        for (int i = 1; i < n; i++) {
            int v = sel[i], j = i - 1;
            while (j >= 0 && m->oam[sel[j] * 4 + 1] > m->oam[v * 4 + 1]) {
                sel[j + 1] = sel[j];
                j--;
            }
            sel[j + 1] = v;
        }
        for (int i = 0; i < 2 * ext; i++) {
            int x = i < ext ? i - ext : LCD_W + (i - ext);
            for (int k = 0; k < n; k++) {
                const uint8_t *s = m->oam + sel[k] * 4;
                int sx = s[1] - 8;
                if (x < sx || x >= sx + 8 || sx <= -8 || sx >= LCD_W)
                    continue;
                int srow = y - (s[0] - 16);
                if (s[3] & 0x40)
                    srow = height - 1 - srow;
                uint8_t tile = height == 16 ? s[2] & 0xFE : s[2];
                int col = x - sx;
                if (s[3] & 0x20)
                    col = 7 - col;
                int colour = tile_pixel(m->vram, LCDC_TILES_8000, tile, col, srow);
                if (!colour)
                    continue;
                if (!((s[3] & 0x80) && bg_colour[i] != 0))
                    row[i < ext ? i : i + LCD_W] = (uint8_t)shade((s[3] & 0x10) ? r->obp1 : r->obp0, colour);
                break;
            }
        }
    }
    return level;
}
