/*
 * Drawing objects with a sprite description: Mario, his block fragments
 * and floating scores (bank 3). Each object's sprite number selects an
 * entry in a table of sprite layouts (a list of tiles with offsets), which
 * is turned into OAM-buffer entries.
 *
 * The work is done in a handful of scratch bytes in high RAM, FF86-FF97,
 * whose values the original leaves behind (and the tests check):
 *   FF86-FF8C  copy of the object's bytes 0-6
 *   FF8D/FF8E  where the next OAM entry goes (updated as sprites are added)
 *   FF8F       number of objects left
 *   FF90/FF91  y/x offset of the layout
 *   FF92/FF93  x/y of the sprite being added
 *   FF94       its attribute bits (the layout's, optionally flipped)
 *   FF95       $80 if the object is to be drawn off screen (hidden: blinking)
 *   FF96/FF97  address of the object
 */
#include "player_priv.h"

#define obj_flags_copy      HRAM(0xFF86)
#define obj_y_copy          HRAM(0xFF87)
#define obj_x_copy          HRAM(0xFF88)
#define obj_sprite_copy     HRAM(0xFF89)    /* the sprite layout number, later the tile being added */
#define obj_attr_copy       HRAM(0xFF8A)
#define obj_facing_copy     HRAM(0xFF8B)    /* bit 5: mirror in x, bit 6: mirror in y */
#define obj_attr2_copy      HRAM(0xFF8C)
#define objects_left        HRAM(0xFF8F)
#define layout_y_offset     HRAM(0xFF90)
#define layout_x_offset     HRAM(0xFF91)
#define sprite_x            HRAM(0xFF92)
#define sprite_y            HRAM(0xFF93)
#define sprite_attr         HRAM(0xFF94)
#define hide_flag           HRAM(0xFF95)
#define object_addr_hi      HRAM(0xFF96)
#define object_addr_lo      HRAM(0xFF97)

enum { ROM_SPRITE_LAYOUTS = 0x4C37, DRAW_BANK = 3 };

static inline uint8_t rom3(uint16_t addr) { return rom_byte(DRAW_BANK, addr); }
static inline uint16_t rom3_word(uint16_t addr) { return (uint16_t)(rom3(addr) | rom3((uint16_t)(addr + 1)) << 8); }

/* position + offset (+ carry of the first add): the original's `add b; adc c`.
   Note that the carry of the first addition goes into the second one. */
static uint8_t position_forward(uint8_t layout_offset, uint8_t base, uint8_t tile_offset)
{
    unsigned sum = (unsigned)layout_offset + base;
    return (uint8_t)(sum + tile_offset + (sum > 0xFF));
}

/* mirrored: base - layout - tile_offset - 8, with borrows chained like the
   original's `sub; sbc; sbc $08` */
static uint8_t position_mirrored(uint8_t layout_offset, uint8_t base, uint8_t tile_offset)
{
    int diff = (int)base - layout_offset;
    int borrow = diff < 0;
    int second = (int)(uint8_t)diff - tile_offset - borrow;
    borrow = second < 0;
    return (uint8_t)((uint8_t)second - 8 - borrow);
}

/*
 * Draw `objects_left` objects (16 bytes apart, from `obj`) into the OAM
 * buffer at draw_oam_hi/lo (03:4823). An object whose first byte is 0 is
 * drawn, $80 is drawn hidden (y = $FF), anything else is skipped.
 */
void draw_objects(uint16_t obj)
{
    for (;;) {
        object_addr_hi = (uint8_t)(obj >> 8);
        object_addr_lo = (uint8_t)obj;
        uint8_t flags = ram_read(obj);
        bool draw = true;
        if (taken(flags == 0)) {
            /* drawn normally */
        } else if (taken(flags == 0x80)) {
            hide_flag = flags;
        } else {
            draw = false;
        }

        if (draw) {
            for (int i = 0; i < 7; i++)
                HRAM(0xFF86 + i) = ram_read((uint16_t)(obj + i));
            loop_taken(7);

            /* sprite layout: a table of pointers indexed by the sprite
               number, rotated left (not multiplied: sprite numbers
               >= $80 wrap) */
            uint8_t n = obj_sprite_copy;
            uint8_t index = (uint8_t)((n << 1) | (n >> 7));
            uint16_t entry = rom3_word((uint16_t)(ROM_SPRITE_LAYOUTS + index));
            uint16_t tiles = rom3_word(entry);
            layout_y_offset = rom3((uint16_t)(entry + 2));
            layout_x_offset = rom3((uint16_t)(entry + 3));
            uint16_t offsets = rom3_word(tiles);       /* pairs (y, x) per sprite */
            uint16_t list = (uint16_t)(tiles + 1);

            for (;;) {
                list++;
                sprite_attr = obj_attr2_copy;
                uint8_t tile = rom3(list);
                if (taken(tile == 0xFF)) {              /* end of the layout */
                    hide_flag = 0;
                    break;
                }
                if (!taken(tile != 0xFD)) {             /* the following sprites have the other palette */
                    sprite_attr = obj_attr2_copy ^ 0x10;
                    continue;
                }
                if (taken(tile == 0xFE)) {              /* skip a sprite */
                    offsets = (uint16_t)(offsets + 2);
                    continue;
                }
                obj_sprite_copy = tile;
                reg_b(obj_y_copy);
                uint8_t y_off = rom3(offsets);
                if (!taken((obj_facing_copy & 0x40) != 0))
                    sprite_y = position_forward(layout_y_offset, obj_y_copy, y_off);
                else
                    sprite_y = position_mirrored(reg_b(layout_y_offset), obj_y_copy, y_off);
                offsets = (uint16_t)(offsets + 1);
                reg_b(obj_x_copy);
                uint8_t x_off = rom3(offsets);
                offsets = (uint16_t)(offsets + 1);
                if (!taken((obj_facing_copy & 0x20) != 0))
                    sprite_x = position_forward(layout_x_offset, obj_x_copy, x_off);
                else
                    sprite_x = position_mirrored(reg_b(layout_x_offset), obj_x_copy, x_off);

                uint16_t out = (uint16_t)(draw_oam_hi << 8 | draw_oam_lo);
                shared_write(out++, taken(hide_flag == 0) ? sprite_y : 0xFF);
                shared_write(out++, sprite_x);
                shared_write(out++, obj_sprite_copy);
                uint8_t attr = reg_b((uint8_t)(reg_b(sprite_attr) | obj_facing_copy));
                shared_write(out++, (uint8_t)(attr | obj_attr_copy));
                draw_oam_hi = (uint8_t)(out >> 8);
                draw_oam_lo = (uint8_t)out;
            }
        }

        /* next object */
        obj = (uint16_t)(((object_addr_hi << 8) | object_addr_lo) + 0x10);
        if (taken(--objects_left == 0))
            return;
    }
}

/*
 * Draw Mario (00:1736): his object and the four after it (the block
 * fragments), into the OAM buffer from sprite 3 on (C00C). Switches to
 * bank 3, where the drawing code lives.
 */
void draw_mario(void)
{
    draw_oam_lo = 0x0C;
    draw_oam_hi = 0xC0;
    objects_left = 5;
    uint8_t bank = shared_read(ADDR_ACTIVE_ROM_BANK);
    saved_rom_bank = bank;
    shared_write(ADDR_ACTIVE_ROM_BANK, DRAW_BANK);
    set_rom_bank(DRAW_BANK);
    draw_objects(0xC200);
    shared_write(ADDR_ACTIVE_ROM_BANK, saved_rom_bank);
    set_rom_bank(saved_rom_bank);
}
