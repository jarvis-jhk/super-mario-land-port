/*
 * Game Boy LCD scanline renderer.
 *
 * Super Mario Land draws its picture the way every Game Boy game does: it
 * fills tile graphics and tile maps in video RAM, puts sprites into OAM and
 * sets a handful of LCD registers. This module turns that description into
 * pixels, one scanline at a time. The port and the reference emulator share
 * it, so identical video RAM + registers always give identical pixels.
 */
#ifndef GB_LCD_H
#define GB_LCD_H

#include <stdint.h>

#define LCD_W 160
#define LCD_H 144

/* LCDC bits */
enum {
    LCDC_BG_ON        = 0x01,
    LCDC_OBJ_ON       = 0x02,
    LCDC_OBJ_TALL     = 0x04,  /* 8x16 sprites */
    LCDC_BG_MAP_9C00  = 0x08,
    LCDC_TILES_8000   = 0x10,
    LCDC_WIN_ON       = 0x20,
    LCDC_WIN_MAP_9C00 = 0x40,
    LCDC_LCD_ON       = 0x80,
};

/* The registers that affect one scanline. */
typedef struct {
    uint8_t lcdc, scy, scx, wy, wx, bgp, obp0, obp1;
} LcdRegs;

/* State carried from line to line within a frame. */
typedef struct {
    int window_line;       /* internal window line counter */
    int window_triggered;  /* WY matched LY at some point this frame */
} LcdState;

/* Call at the start of every frame (line 0). */
void lcd_begin_frame(LcdState *st);

/* Render scanline ly into out[0..159] as shades 0 (white) .. 3 (black).
   vram is the 8 KiB at 8000-9FFF, oam the 160 bytes at FE00-FE9F. */
void lcd_render_line(LcdState *st, const LcdRegs *r, int ly,
                     const uint8_t *vram, const uint8_t *oam, uint8_t *out);

/* Number of sprites the hardware would select on line ly (max 10); used by
   the reference emulator to time mode 3. */
int lcd_sprites_on_line(const LcdRegs *r, int ly, const uint8_t *oam,
                        uint8_t xs[10]);

#endif
