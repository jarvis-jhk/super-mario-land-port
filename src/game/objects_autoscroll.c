/*
 * The autoscrolling shooter levels (the submarine and the plane), bank 1,
 * 4FB2-5170: the screen scrolls by itself, Mario flies with the d-pad, and
 * his shots fly to the right.
 *
 * (These routines live in bank 1 although they are used in every world:
 * the game switches to bank 1 for them.)
 */
#include "enemies.h"
#include "game.h"
#include "player_priv.h"

enum {
    /* offsets (in pixels) from Mario's position to the tiles that block him */
    AUTOSCROLL_PROBE_AHEAD = 8,
    AUTOSCROLL_PROBE_BEHIND = 0xFA,   /* -6 */
    SCROLL_LIMIT_END_OF_LEVEL = 7,    /* level_end_counter value from which the scroll stops */
    TILE_MUSHROOM_BLOCK = 0x83,       /* touching this tile also ends the level (the community
                                         disassembly calls it a mushroom) */
};

/* Mario picks up a coin tile: the block code collects it in the vblank
   handler (tile address from the last tile lookup). */
static void collect_coin_tile(void)
{
    post_block_event(BLOCK_EVENT_COIN, looked_up_tile_address());
    request_sfx(SFX_COIN);
}

/*
 * Test the tiles right/left of Mario (01:50CC; dx is 8 for right, -6 for
 * left). Big Mario is tested at two heights, small Mario at one. Coins are
 * collected, the boss switch and a mushroom tile end the level. Returns true
 * if a solid tile blocks him: the original then does not return to its
 * caller but to the caller's caller (POP HL; RET), which the callers here
 * mirror by returning at once.
 */
static bool autoscroll_blocked_sideways(uint8_t dx)
{
    uint8_t y_offset = 5;
    uint8_t heights = 1;
    if (taken(super_status == SUPER_BIG))
        heights = 2;
    for (;;) {
        tile_y_px = (uint8_t)(mario_y + y_offset);
        uint8_t x = reg_b((uint8_t)(dx + reg_b(mario_x)));
        tile_x_px = (uint8_t)(read_scroll_x() + x);
        uint8_t tile = lookup_tile();
        if (taken(tile < TILE_FIRST_SOLID)) {
            y_offset = 0xFD;                      /* the second height is 3 higher */
            if (!taken(--heights != 0))
                return false;
            continue;
        }
        if (taken(tile == TILE_COIN)) {
            collect_coin_tile();
            return false;
        }
        if (taken(tile == TILE_BOSS_SWITCH)) {
            mario_reaches_goal();
            return false;
        }
        if (taken(tile == TILE_MUSHROOM_BLOCK)) {
            mario_reaches_goal();
            return false;
        }
        return true;
    }
}

/*
 * One step of the automatic scrolling (01:4FB2), every other frame: the
 * screen moves one pixel, Mario's screen position one back (he's carried
 * by the world), and objects and enemies shift with the screen. If a wall
 * pushes Mario back, he stays pushed (the routine ends early). At the end of
 * the level the scrolling stops at a multiple of 4.
 */
void autoscroll_step(void)
{
    if (taken(shared_read(ADDR_FRAME_COUNTER) & 1))
        return;
    if (!taken(level_end_counter < SCROLL_LIMIT_END_OF_LEVEL)) {
        if (!taken((read_scroll_x() & 0x0C) != 0)) {
            shared_write(ADDR_SCROLL_X, read_scroll_x() & 0xFC);
            return;
        }
    }
    shared_write(ADDR_SCROLL_X, (uint8_t)(read_scroll_x() + 1));
    shift_object_sprites(1);
    scroll_enemies(1);
    mario_x--;
    if (!taken(mario_x != 0))
        mario_x = 0xF0;
    if (autoscroll_blocked_sideways(AUTOSCROLL_PROBE_AHEAD))
        return;
    mario_x++;
}

/*
 * Is there a solid tile above Mario's head (01:5046)? Looks at two points,
 * 6 pixels apart. Returns $FF if so, 5 if it was a coin (collected), else
 * the tile value left in A.
 */
uint8_t autoscroll_check_above(void)
{
    uint8_t y_offset = 0xFD;                      /* small Mario: 3 above */
    if (!taken(super_status == 0))
        y_offset = 0xFC;
    tile_y_px = (uint8_t)(mario_y + y_offset);
    uint8_t scroll = read_scroll_x();
    tile_x_px = (uint8_t)(scroll + reg_b(mario_x) + 2);
    uint8_t tile = lookup_tile();
    if (!taken(tile >= TILE_FIRST_SOLID)) {
        tile_x_px = (uint8_t)(tile_x_px + 0xFA);
        tile = lookup_tile();
        if (taken(tile < TILE_FIRST_SOLID))
            return tile;
    }
    if (taken(tile == TILE_COIN)) {
        collect_coin_tile();
        return SFX_COIN;
    }
    return 0xFF;
}

/*
 * The same below Mario's feet (01:5089), two points 6 pixels apart. The boss
 * switch tile ends the level. Returns like autoscroll_check_above; after
 * the level end (the original jumps to it) A is 0.
 */
uint8_t autoscroll_check_below(void)
{
    tile_y_px = (uint8_t)(mario_y + 0x0A);
    tile_x_px = (uint8_t)(reg_b(read_scroll_x()) + mario_x + 0xFE);
    uint8_t tile = lookup_tile();
    if (!taken(tile >= TILE_FIRST_SOLID)) {
        tile_x_px = (uint8_t)(tile_x_px + 4);
        tile = lookup_tile();
        if (taken(tile == TILE_BOSS_SWITCH)) {
            mario_reaches_goal();
            return 0;
        }
        if (!taken(tile >= TILE_FIRST_SOLID))
            return tile;
    }
    if (taken(tile != TILE_COIN))
        return 0xFF;
    collect_coin_tile();
    return SFX_COIN;
}

/*
 * Fly Mario with the d-pad (01:4FEC). Up and down are limited by the tiles
 * above/below and by the screen (Y between $30 and $94); left and right
 * move him one pixel, two to the left while the level is not ending, and
 * stop at walls and at X = $10 / $A0.
 */
void autoscroll_steer(void)
{
    uint8_t keys = joy_held;
    if (taken(keys & BTN_UP)) {
        uint8_t tile = autoscroll_check_above();
        if (!taken(tile == 0xFF))
            if (!taken(mario_y < 0x30))
                mario_y--;
    } else if (taken(keys & BTN_DOWN)) {
        uint8_t tile = autoscroll_check_below();
        if (!taken(tile == 0xFF))
            if (!taken(mario_y >= 0x94))
                mario_y++;
    }

    keys = joy_held;
    if (taken(keys & BTN_RIGHT)) {
        if (autoscroll_blocked_sideways(AUTOSCROLL_PROBE_AHEAD))
            return;
        if (taken(mario_x >= 0xA0))
            return;
        mario_x++;
        return;
    }
    if (taken(!(keys & BTN_LEFT)))
        return;
    if (autoscroll_blocked_sideways(AUTOSCROLL_PROBE_BEHIND))
        return;
    if (taken(mario_x < 0x10))
        return;
    mario_x--;
    if (taken(level_end_counter >= SCROLL_LIMIT_END_OF_LEVEL))
        return;
    mario_x--;
}

/*
 * Move Mario's three shots (01:5118) in the shooter levels. A shot is a
 * status byte (FFA9-FFAB, nonzero when in flight) and the sprite in the OAM
 * buffer (sprites 0-2). Each moves 2 pixels to the right per frame and
 * disappears at the right edge ($A9) or when it meets a solid tile; then it
 * is tested against the enemies.
 */
void autoscroll_update_shots(void)
{
    uint16_t status = 0xFFA9;
    uint16_t sprite_x = 0xC001;
    uint8_t shots_left = 3;
    for (;;) {
        uint8_t flying = ram_read(status);
        if (taken(flying != 0)) {
            uint16_t x_address = sprite_x;
            uint8_t x = (uint8_t)(shared_read(x_address) + 2);
            shared_write(x_address, x);
            hitbox_bottom = x;                    /* positions for the enemy test */
            enemy_buf.x = x;
            bool remove = true;
            if (taken(x < 0xA9)) {
                uint8_t y = shared_read((uint16_t)(sprite_x - 1));
                enemy_buf.y = y;
                tile_y_px = (uint8_t)(y + 6);
                remove = !taken(find_neighboring_tile((uint8_t)(x + 2)) < TILE_FIRST_SOLID);
            }
            if (remove) {
                shared_write((uint16_t)(sprite_x & ~1), 0);   /* Y = 0: off the screen */
                ram_write(status, 0);
            }
            superball_hits_enemies(x_address);
        }
        status++;
        sprite_x = (uint16_t)((sprite_x & 0xFF00) | (uint8_t)(sprite_x + 4));
        if (!taken(--shots_left != 0))
            return;
    }
}

/*
 * Mario is pushed against the left edge by the scrolling (01:515E): if his
 * X is 1-$EC (not at the very left, 0, or the wrapped-around values at the
 * right) nothing happens; otherwise he is crushed and dies.
 */
void autoscroll_crush_check(void)
{
    uint8_t x = mario_x;
    if (!taken(x < 1)) {
        if (taken(x < 0xED))
            return;
    }
    super_status = SUPER_SMALL;
    superball_mario = 0;
    set_game_state(STATE_DEAD);
    request_music(MUS_DEATH);
    frame_timer = 0x90;
}
