/* The four-floor ladder bonus game (00:3D97-3EE5, bank 2:5832-5F14). */
#include "game.h"
#include "ram.h"
#include "bcd.h"

/* Clear the room and print the current lives (00:3D97). The original
   clears 3*255 map bytes, leaving the final three bytes untouched. */
void prepare_bonus_game(void)
{
    shared_write(0xDFE8, 9);
    io_write(rLCDC, 0); shared_write(0xFFA4, 0);
    uint16_t dst = 0xC000;
    uint8_t count = 0xA0;
    do { shared_write(dst++, 0); } while (taken(--count != 0));
    dst = 0x9800;
    uint8_t groups = 3;
    do {
        count = 0xFF;
        do { vram_write(dst++, TILE_BLANK); } while (taken(--count != 0));
    } while (taken(--groups != 0));
    uint8_t lives = reg_b(lives_bcd);
    vram_write(0x988B, lives & 15); vram_write(0x988A, lives >> 4);
    io_write(rLCDC, 0x83); shared_write(0xFFB3, 0x13);
}

/* Draw the border, floors and randomly rotated prize list (00:3DD7). */
void draw_bonus_room(void)
{
    io_write(rLCDC, 0);
    uint16_t map = 0x9800;
    vram_write(map++, 0xF5);
    uint8_t count = 18;
    do { vram_write(map++, 0x9F); } while (taken(--count != 0));
    vram_write(map, 0xFC);
    map = 0x9820;
    uint8_t sides = 2;
    do {
        count = 16;
        do { vram_write(map, 0xF8); map += 0x20; } while (taken(--count != 0));
        map = (uint16_t)((map & 0xFF00) - 0x0200 + 0x33);
    } while (taken(--sides != 0));
    map = 0x9A20; vram_write(map++, 0xFF); count = 18;
    do { vram_write(map++, 0x9F); } while (taken(--count != 0));
    vram_write(map, 0xE9);
    const uint8_t title[] = { 0x0B,0x18,0x17,0x1E,0x1C };
    for (unsigned i = 0; i < sizeof title; i++) vram_write(0x9845 + i, title[i]);
    vram_write(0x984B, 0x10); vram_write(0x984C, 0x0A); vram_write(0x984D, 0x16); vram_write(0x984E, 0x0E);
    vram_write(0x9887, 0xE4); vram_write(0x9889, 0x2B);
    /* Each floor has eighteen tiles and a prize marker one row above. */
    for (unsigned floor = 0; floor < 4; floor++) {
        map = 0x98E1 + floor * 0x60; count = 18;
        do { vram_write(map++, 0x2D); } while (taken(--count != 0));
        vram_write(0x98D1 + floor * 0x60, 0x2B);
    }
    reg_b(0xE5);        /* the prize table runs as code: ld bc,$E502 / inc bc */
    uint8_t choice = (io_read(rDIV) & 3) + 1;
    loop_taken(choice);
    uint16_t src = 0x3E7B + choice;
    map = 0x98D2;
    do { vram_write(map, mem_read(src++)); map += 0x60; } while (taken((uint8_t)map != 0x52));
    io_write(rLCDC, 0x83); shared_write(0xFFB3, 0x14);
}

/* Draw the four ladder tiles repeatedly to fill the available VRAM time
   (00:3EA7). The map location stays fixed between passes. */
void draw_bonus_ladder(void)
{
    for (;;) {
        uint8_t hi = ladder_map_hi;
        if (taken(hi == 0x9A)) { ladder_phase = 0; break; }
        uint16_t map = hi << 8 | ladder_map_lo, src = 0xDA23;
        do {
            vram_write(map, mem_read(src++)); map += 0x20;
            ladder_tiles_left--;
        } while (taken(ladder_tiles_left != 0));
        ladder_tiles_left = 4;
        ladder_passes--;
        if (!taken(ladder_passes != 0)) { ladder_passes = 3; break; }
    }
    shared_write(0xFFB3, 0x15);
}

/* Randomly choose Mario's starting floor and seed his four tiles (02:5832). */
void prepare_bonus_mario(void)
{
    uint8_t choice = reg_b((io_read(rDIV) & 3) + 1);
    loop_taken(choice);
    uint8_t y = reg_b(0x20 + 0x18 * choice), tile = 0;
    shared_write(0xC030, y); shared_write(0xC031, 0x10);
    if (!taken(super_status != SUPER_BIG)) tile = 0x20;
    const uint8_t positions[] = { y,0x10,tile,0, y,0x18,(uint8_t)(tile+1),0,
        (uint8_t)(y+8),0x10,(uint8_t)(tile+16),0, (uint8_t)(y+8),0x18,(uint8_t)(tile+17),0 };
    for (unsigned i = 2; i < sizeof positions; i++) {
        if (i == 8) reg_b(positions[i]);            /* the lower row's y */
        if ((i & 3) != 3) shared_write(0xC030 + i, positions[i]);
    }
    shared_write(0xFFB3, 0x15);
}

/* Cycle Mario's floor and the ladder until A freezes the choice (02:5835). */
void update_bonus_selection(void)
{
    if (!taken((ladder_phase & 1) == 0)) {
        if (taken((joy_held & 1) != 0)) {
            bonus_spare = ladder_phase = bonus_frame = 0;
            shared_write(0xFFB3, 0x17); return;
        }
    }
    bonus_frame++;
    if (taken(bonus_frame != 3)) return;
    bonus_frame = 0;
    if (!taken((ladder_phase & 1) == 0)) {
        uint16_t sprite = 0xC030;
        uint8_t count = 4;
        if (!taken(shared_read(sprite) == 0x80)) {
            do { shared_write(sprite, shared_read(sprite) + 0x18); sprite += 4; } while (taken(--count != 0));
        } else {
            count = 2;
            do { shared_write(sprite, 0x38); sprite += 4; } while (taken(--count != 0));
            count = 2;
            do { shared_write(sprite, 0x40); sprite += 4; } while (taken(--count != 0));
        }
    }
    uint16_t map = 0x98EA;
    uint8_t phase = ++ladder_phase;
    if (!taken(phase < 3)) {
        map += 0x60;
        if (!taken(phase < 5)) {
            map += 0x60;
            if (!taken(phase < 7)) { map = 0x98EA; phase = ladder_phase = 1; }
        }
    }
    ladder_map_hi = map >> 8; ladder_map_lo = map;
    if (!taken((phase & 1) == 0)) {
        ladder_tiles[0] = 0x2E; ladder_tiles[1] = ladder_tiles[2] = 0x2F; ladder_tiles[3] = 0x30;
    } else {
        ladder_tiles[0] = ladder_tiles[3] = 0x2D; ladder_tiles[1] = ladder_tiles[2] = TILE_BLANK;
    }
    shared_write(0xFFB3, 0x16);
}

/* Animate Mario while moving one pixel along the bonus route. Each caller
   has the same branch sequence, including the original pointer-add loop. */
static void move_bonus_mario(int direction)
{
    uint16_t sprite = direction == 0 ? 0xC031 : 0xC030;
    uint16_t src = 0x5C9D;
    uint8_t offset = bonus_anim;
    if (!taken(offset == 0)) {
        do { src++; } while (taken(--offset != 0));
    }
    uint8_t count = 4;
    do {
        shared_write(sprite, shared_read(sprite) + (direction < 0 ? -1 : 1));
        sprite += direction == 0 ? 1 : 2;
        uint8_t tile = mem_read(src);
        if (!taken(tile != 0xFF)) { src = 0x5C9D; bonus_anim = 0; tile = mem_read(src); }
        if (!taken(super_status != SUPER_BIG)) tile += 0x20;
        shared_write(sprite, tile); src++;
        sprite += direction == 0 ? 3 : 2;
    } while (taken(--count != 0));
    bonus_anim += 4;
}

/* Walk across a floor, probing for a ladder after each cooldown (02:5838). */
void walk_bonus_mario(void)
{
    if (!taken(bonus_walking != 0)) { bonus_walking++; shared_write(0xDFE8, 0x0A); }
    move_bonus_mario(0);
    uint8_t x = shared_read(0xC031);
    if (taken(x >= 0x80)) { bonus_walking = 0; shared_write(0xFFB3, 0x1A); return; }
    tile_x_px = x + 4; tile_y_px = shared_read(0xC030) + 16;
    if (taken(--bonus_probe_timer != 0)) return;
    bonus_probe_timer = 1;
    lookup_tile();
    /* The original reads [HL] again after LookupTile, even if its two reads
       were blocked. Keep this third hardware read. */
    uint8_t tile = vram_read(tile_addr_hi << 8 | tile_addr_lo);
    if (taken(tile == 0x2E)) { shared_write(0xFFB3, 0x18); return; }
    if (taken(tile == 0x30)) shared_write(0xFFB3, 0x19);
}

/* Descend to the next floor (02:583B). */
void descend_bonus_ladder(void)
{
    move_bonus_mario(1);
    uint8_t y = shared_read(0xC030);
    if (taken(y == 0x50) || taken(y == 0x68) || taken(y == 0x80)) {
        bonus_probe_timer = 8; shared_write(0xFFB3, 0x17);
    }
}

/* Climb to the next floor (02:583E). */
void ascend_bonus_ladder(void)
{
    move_bonus_mario(-1);
    uint8_t y = shared_read(0xC030);
    if (taken(y == 0x38) || taken(y == 0x50) || taken(y == 0x68)) {
        bonus_probe_timer = 8; shared_write(0xFFB3, 0x17);
    }
}

/* Remove unselected prizes, grow Mario for a flower, or jump once per
   awarded life before returning to the level (02:5841). */
void award_bonus_prize(void)
{
    if (!taken(bonus_prize != 0)) {
        uint8_t passes = 2;
        do {
            uint16_t map = 0x98D1;
            uint8_t y = reg_b(shared_read(0xC030));
            for (unsigned floor = 0; floor < 4; floor++, map += 0x60) {
                if (!taken(y == 0x38 + 0x18 * floor)) {
                    vram_write(map, TILE_BLANK); vram_write(map + 1, TILE_BLANK);
                }
            }
        } while (taken(--passes != 0));
        tile_x_px = shared_read(0xC031) + 0x18;
        tile_y_px = shared_read(0xC030) + 8;
        lookup_tile();
        uint8_t prize = vram_read(tile_addr_hi << 8 | tile_addr_lo);
        if (taken(prize == 3)) bonus_prize = 4;
        else if (taken(prize == 0xE5)) {
            if (taken(superball_mario == 0)) bonus_prize = 0x10;
            else { shared_write(0xDFE8, 0x0E); bonus_prize = 1; return; }
        } else if (taken(prize == 2)) bonus_prize = 3;
        else bonus_prize = 2;
        shared_write(0xDFE8, 0x0D); return;
    }
    uint8_t prize = bonus_prize;
    if (!taken(prize >= 0x10)) {
        if (!taken(prize >= 2)) {
            if (taken(--bonus_end_timer != 0)) return;
            bonus_end_timer = 0x40;
            bonus_prize = bonus_anim = bonus_walking = bonus_grow = bonus_jump = 0;
            bonus_probe_timer = 1; bonus_anim_timer = 0x40;
            shared_write(0xFFB3, 0x1B); return;
        }
        if (taken(--bonus_anim_timer != 0)) return;
        bonus_anim_timer = 4;
        if (!taken(bonus_jump != 0)) {
            for (unsigned i = 0; i < 4; i++) {
                if (i == 0 || i == 2) reg_b(i < 2 ? 0x38 : 0x40);
                shared_write(0xC030 + 4*i, i < 2 ? 0x38 : 0x40);
                shared_write(0xC031 + 4*i, (i & 1) ? 0x60 : 0x58);
            }
            bonus_jump = 1; return;
        }
        if (taken(bonus_jump_phase == 2)) {
            uint8_t tile = 0;
            uint16_t sprite = 0xC030;
            shared_write(sprite, shared_read(sprite) + 1);
            reg_b(tile);
            if (!taken(super_status != SUPER_BIG)) tile = reg_b(0x20);
            shared_write(sprite + 2, tile);
            for (unsigned i = 1; i < 4; i++) {
                sprite += 4;
                shared_write(sprite, shared_read(sprite) + 1);
                if (i == 2) reg_b(tile + 16);
                shared_write(sprite + 2, tile + (i & 1) + (i >= 2 ? 16 : 0));
            }
            bonus_jump = bonus_jump_phase = 0; bonus_prize--; return;
        }
        if (!taken(bonus_jump_phase != 0)) {
            uint8_t tile = 8;
            uint16_t sprite = 0xC030;
            shared_write(sprite, shared_read(sprite) - 1);
            reg_b(tile);
            if (!taken(super_status != SUPER_BIG)) tile = reg_b(tile + 0x20);
            shared_write(sprite + 2, tile);
            for (unsigned i = 1; i < 4; i++) {
                sprite += 4;
                shared_write(sprite, shared_read(sprite) - 1);
                if (i == 2) reg_b(tile + 16);
                shared_write(sprite + 2, tile + (i & 1) + (i >= 2 ? 16 : 0));
            }
            if (taken(++bonus_jump != 6)) return;
            shared_write(0xDFE0, 8);
            if (!taken(lives_bcd >= 0x99)) {
                int carry = 0;
                lives_bcd = bcd_add(lives_bcd, 1, &carry);
                uint8_t lives = reg_b(lives_bcd);
                vram_write(0x988B, lives & 15); vram_write(0x988A, lives >> 4);
            }
            bonus_jump = bonus_jump_phase = 1; return;
        }
        for (unsigned i = 0; i < 4; i++) {
            uint16_t sprite = 0xC030 + 4*i;
            shared_write(sprite, shared_read(sprite) + 1);
        }
        if (taken(++bonus_jump != 5)) return;
        bonus_jump_phase = 2; return;
    }
    if (taken(--bonus_anim_timer != 0)) return;
    bonus_anim_timer = 3;
    if (!taken(++bonus_prize == 0x28)) {
        if (!taken(bonus_walking != 0)) {
            bonus_walking = 1; shared_write(0xDFE0, 4);
            if (taken(super_status == SUPER_BIG)) goto flower_done;
        }
        uint8_t count = 4;
        uint16_t sprite = 0xC032;
        if (taken(bonus_grow != 0)) {
            bonus_grow--;
            do { shared_write(sprite, shared_read(sprite) - 0x20); sprite += 4; } while (taken(--count != 0));
        } else {
            bonus_grow++;
            do { shared_write(sprite, shared_read(sprite) + 0x20); sprite += 4; } while (taken(--count != 0));
        }
        return;
    }
flower_done:
    bonus_prize = 1; super_status = superball_mario = SUPER_BIG;
}
