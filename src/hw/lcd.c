/* Game Boy LCD scanline renderer. See lcd.h. */
#include "lcd.h"
#include <string.h>

void lcd_begin_frame(LcdState *st)
{
    st->window_line = 0;
    st->window_triggered = 0;
}

/* Colour number (0..3) of pixel (x, y) of the tile whose 16 bytes start at p */
static int tile_pixel(const uint8_t *p, int x, int y)
{
    uint8_t lo = p[y * 2], hi = p[y * 2 + 1];
    int bit = 7 - x;
    return ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
}

/* Address (offset into vram) of background/window tile number t */
static const uint8_t *bg_tile(const uint8_t *vram, uint8_t lcdc, uint8_t t)
{
    if (lcdc & LCDC_TILES_8000)
        return vram + t * 16;
    return vram + 0x1000 + (int8_t)t * 16;
}

static int shade(uint8_t palette, int colour)
{
    return (palette >> (colour * 2)) & 3;
}

int lcd_sprites_on_line(const LcdRegs *r, int ly, const uint8_t *oam,
                        uint8_t xs[10])
{
    int height = (r->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
    int n = 0;
    for (int i = 0; i < 40 && n < 10; i++) {
        int y = oam[i * 4] - 16;
        if (ly >= y && ly < y + height)
            xs[n++] = oam[i * 4 + 1];
    }
    return n;
}

void lcd_render_line(LcdState *st, const LcdRegs *r, int ly,
                     const uint8_t *vram, const uint8_t *oam, uint8_t *out)
{
    uint8_t bg_colour[LCD_W];   /* colour number before palette, for priority */

    /* --- background and window --- */
    if (ly == r->wy)
        st->window_triggered = 1;
    int window_x0 = r->wx - 7;
    int window_visible = (r->lcdc & LCDC_WIN_ON) && (r->lcdc & LCDC_BG_ON) &&
                         st->window_triggered && r->wx <= 166;

    if (r->lcdc & LCDC_BG_ON) {
        const uint8_t *bg_map = vram + ((r->lcdc & LCDC_BG_MAP_9C00) ? 0x1C00 : 0x1800);
        const uint8_t *win_map = vram + ((r->lcdc & LCDC_WIN_MAP_9C00) ? 0x1C00 : 0x1800);
        int bg_y = (ly + r->scy) & 0xFF;
        for (int x = 0; x < LCD_W; x++) {
            int colour;
            if (window_visible && x >= window_x0) {
                int wx = x - window_x0, wy = st->window_line;
                uint8_t t = win_map[(wy / 8) * 32 + wx / 8];
                colour = tile_pixel(bg_tile(vram, r->lcdc, t), wx & 7, wy & 7);
            } else {
                int bx = (x + r->scx) & 0xFF;
                uint8_t t = bg_map[(bg_y / 8) * 32 + bx / 8];
                colour = tile_pixel(bg_tile(vram, r->lcdc, t), bx & 7, bg_y & 7);
            }
            bg_colour[x] = (uint8_t)colour;
            out[x] = (uint8_t)shade(r->bgp, colour);
        }
    } else {
        memset(bg_colour, 0, sizeof bg_colour);
        memset(out, 0, LCD_W);
    }
    if (window_visible && window_x0 < LCD_W)
        st->window_line++;

    /* --- sprites --- */
    if (!(r->lcdc & LCDC_OBJ_ON))
        return;
    int height = (r->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
    int sel[10], n = 0;
    for (int i = 0; i < 40 && n < 10; i++) {
        int y = oam[i * 4] - 16;
        if (ly >= y && ly < y + height)
            sel[n++] = i;
    }
    /* DMG priority: smaller X first, then lower OAM index (selection order
       already sorted by index, so a stable insertion sort on X suffices). */
    for (int i = 1; i < n; i++) {
        int v = sel[i], j = i - 1;
        while (j >= 0 && oam[sel[j] * 4 + 1] > oam[v * 4 + 1]) {
            sel[j + 1] = sel[j];
            j--;
        }
        sel[j + 1] = v;
    }
    for (int x = 0; x < LCD_W; x++) {
        for (int k = 0; k < n; k++) {
            const uint8_t *s = oam + sel[k] * 4;
            int sx = s[1] - 8;
            if (x < sx || x >= sx + 8)
                continue;
            uint8_t attr = s[3];
            int row = ly - (s[0] - 16);
            if (attr & 0x40)
                row = height - 1 - row;
            uint8_t tile = s[2];
            if (height == 16)
                tile &= 0xFE;
            int col = x - sx;
            if (attr & 0x20)
                col = 7 - col;
            int colour = tile_pixel(vram + tile * 16, col, row);
            if (colour == 0)
                continue;           /* transparent: next sprite may show */
            if (!((attr & 0x80) && bg_colour[x] != 0))
                out[x] = (uint8_t)shade((attr & 0x10) ? r->obp1 : r->obp0, colour);
            break;                  /* highest priority opaque pixel decides */
        }
    }
}
