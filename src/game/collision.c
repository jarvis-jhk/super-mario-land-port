/*
 * Mario against the level: solid tiles, blocks, coins, pipes.
 *
 * The level is a background map of 32x32 tiles at 9800-9BFF. Tiles below
 * $60 are scenery Mario passes through; from $60 on they are solid. Every
 * lookup (lookup_tile) takes the pixel position in tile_x_px/tile_y_px
 * (screen position + (8, 16), plus the scroll for x) and leaves the tile's
 * map address in tile_addr_hi/lo.
 *
 * What a block contains is in an "overlay" (see block_contents()).
 *
 * A hit block cannot be updated right away because the background map
 * can only be written during vblank; so the collision code leaves a request
 * in block_event/block_event_hi/lo, and block_events() (called from the
 * vblank handler) carries it out.
 */
#include <string.h>
#include "bcd.h"
#include "player_priv.h"

enum {
    BLOCK_SPRITE_Y = 0xC02C,   /* OAM buffer sprite 11: the bouncing block */
    BLOCK_SPRITE_X = 0xC02D,
};

/* Clear the block overlay, C800-CA3F, from the end (00:165E). */
void clear_block_overlay(void)
{
    uint16_t addr = ADDR_OVERLAY + 0x23F;
    uint16_t n = 0x240;
    do {
        shared_write(addr--, 0);
        n = dec_bc(n);
    } while (taken(n != 0));
}

/*
 * Some solid tiles can be walked through from the side and from below, and
 * only stood upon, like a platform: the trees of world 1, the mountains of
 * world 2. Returns 0 for those and the tile otherwise (00:1A6B). The lists
 * (per world, ended by $FD) are in ROM; world numbers beyond the table read
 * whatever follows it.
 */
uint8_t filter_passthrough_tile(uint8_t tile)
{
    reg_b(tile);
    uint8_t world = (uint8_t)((shared_read(ADDR_WORLD_AND_LEVEL) & 0xF0) >> 4);
    uint8_t index = (uint8_t)((uint8_t)(world - 1) << 1);
    uint16_t table = (uint16_t)(0x1A93 + index);
    uint16_t list = (uint16_t)(rom_byte(0, table) | rom_byte(0, (uint16_t)(table + 1)) << 8);
    for (;;) {
        uint8_t entry = mem_read(list);
        if (taken(entry == 0xFD))
            return tile;                    /* end of the list: no match */
        if (taken(entry == tile))
            return 0;
        list++;
    }
}

/*
 * Count one coin (00:1BFF): 100 points, and a 1UP at 100 coins. Does
 * nothing during the demo. Returns true if the coin counter was redrawn
 * (the original's DisplayCoins leaves the new count in register B).
 */
bool add_coin(void)
{
    if (taken(shared_read(ADDR_DEMO_MODE) != 0))
        return false;
    add_score(0x0100);
    int carry = 0;
    coins_bcd = bcd_add(coins_bcd, 1, &carry);
    if (!taken(coins_bcd != 0))
        shared_write(ADDR_LIVES_CHANGE, 1);
    display_coins();
    return true;
}

/*
 * Carry out the block/coin requests left in block_event by the collision
 * code (00:1B86). Runs in the vblank handler, because only there the
 * background map can be written freely.
 *
 *  - coin tile or breaking block (1, $C0): the tile is erased; a coin
 *    counts, and a coin above a broken block is collected too;
 *  - bounce start (2): same erase, and the event becomes 3 (the block
 *    sprite bounces for a while, the enemy-above test runs);
 *  - bounce over (4): the block is put back into the map (used blocks
 *    turn dark; coin blocks pay a coin).
 *
 * entry_b is the value of register B when the interrupt happened: in the
 * "bounce start" case the original uses B without setting it (it should
 * hold the event code, as in the other cases). In practice B is whatever
 * the interrupted code left, usually 0, and the code is only affected if
 * it equals $C0.
 */
void block_events(uint8_t entry_b)
{
    uint8_t b = entry_b;
    hud_coins_drawn = 0;
    if (taken(hud_coin_flag != 0)) {
        if (add_coin())
            b = coins_bcd;
    }

    uint8_t event = block_event;
    if (taken(event == BLOCK_EVENT_BREAK)) {
        b = reg_b(event);
        block_event = 0;
    } else if (taken(event == BLOCK_EVENT_BOUNCE)) {
        block_event = BLOCK_EVENT_BOUNCING;
    } else if (taken(event == BLOCK_EVENT_COIN)) {
        b = reg_b(event);
        block_event = 0;
    } else {
        if (taken(event != BLOCK_EVENT_BOUNCE_DONE))
            return;
        /* the bounce is over: put the block back into the map */
        block_event = 0;
        uint16_t tile_addr = (uint16_t)(block_event_hi << 8 | block_event_lo);
        uint8_t block = oam_buffer[BLOCK_SPRITE + 2];
        uint8_t tile = block;
        if (!taken(block == TILE_BREAKABLE_BLOCK)) {
            if (taken(block == TILE_COIN_BLOCK))
                add_coin();
            tile = TILE_USED_BLOCK;
        }
        vram_write(tile_addr, tile);
        return;
    }

    /* erase the tile (a coin or a block) */
    uint16_t tile_addr = (uint16_t)(block_event_hi << 8 | block_event_lo);
    vram_write(tile_addr, TILE_BLANK);
    if (taken(b == BLOCK_EVENT_COIN)) {
        add_coin();
        return;
    }
    /* a coin sitting on top of the block is collected too */
    uint16_t above = (uint16_t)(tile_addr - 0x20);
    if (taken(vram_read(above) != TILE_COIN))
        return;
    vram_write(above, TILE_BLANK);
    WRAM(SOUND_SFX_REQUEST) = SFX_COIN;         /* interrupt context: direct access */
    tile_addr_hi = (uint8_t)(above >> 8);
    tile_addr_lo = (uint8_t)above;
    tile_address_to_pixel();
    floaty_x = (uint8_t)(tile_x_px - reg_b(scroll_x));
    floaty_y = (uint8_t)(tile_y_px + 0x14);
    floaty_control = BLOCK_EVENT_COIN;
    add_coin();
}

/*
 * Mario reaches the end of the level, either by touching the boss switch /
 * fist tile or by walking off the right end (00:1B45). Starts the "level
 * end" state with the victory jingle (unless the boss was just beaten, whose
 * music is already playing) and a 240 frame timer, and resets what a few
 * other routines keep. A small-Mario keeps his size; a big Mario that
 * is not at full size (flower) is reset to small look but keeps super.
 */
void mario_reaches_goal(void)
{
    uint8_t mask = 0xFF;
    if (!taken(super_status == SUPER_BIG)) {
        mask = 0x0F;
        super_status = SUPER_SMALL;
    }
    mario_anim &= mask;
    reg_b(mario_anim);
    if (!taken((mario_anim & 0x0F) >= 0x0A))     /* frames 10+ are the submarine/plane */
        mario_anim &= 0xF0;
    set_game_state(STATE_LEVEL_END);
    if (!taken(boss_defeated != 0)) {
        request_music(MUS_LEVEL_COMPLETE);
        frame_timer = 0xF0;
    }
    clear_object_sprites();
    mario_flags = 0;
    timer_expiring_flag = 0;
    io_write(rTMA, 0);
}

/*
 * Does Mario run into something at his sides? (00:1AAD) Looks at the tile
 * in front of him (the facing direction, 6 pixels from his centre) at
 * knee height, and for big Mario once more 4 pixels higher. Returns 0 when
 * he can move on, $FF when he is blocked (walking stops, walk counter
 * advances), or whatever is left in A by the special cases (coin pickup in
 * progress: the nonzero block_event).
 *
 * Special tiles: coins are collected, a sideways pipe entrance
 * underground starts the "walk into the pipe" state, and the fist tile
 * ends the level.
 */
uint8_t mario_side_collision(void)
{
    if (taken(game_state() >= STATE_MENU))
        return 0;
    uint8_t y_offset = 7;
    uint8_t checks = 1;
    if (!taken(super_status != SUPER_BIG)) {
        if (!taken(mario_anim == 0x18)) {
            checks = 2;            /* big and not crouching: two tiles high */
        }
    }
    for (;;) {
        tile_y_px = (uint8_t)(mario_y + y_offset);
        uint8_t x = reg_b(mario_x);
        uint8_t dx = 6;
        if (taken(mario_facing != 0))
            dx = (uint8_t)-6;
        x = reg_b((uint8_t)(x + dx));
        tile_x_px = (uint8_t)(read_scroll_x() + x);

        uint8_t tile = filter_passthrough_tile(lookup_tile());
        uint16_t address = looked_up_tile_address();
        if (!taken(tile == 0) && !taken(tile < TILE_FIRST_SOLID)) {
            if (taken(tile == TILE_COIN)) {
                uint8_t pending = pending_block_event();
                if (taken(pending != 0))
                    return pending;
                post_block_event(BLOCK_EVENT_COIN, address);
                request_sfx(SFX_COIN);
                return 0;
            }
            if (taken(tile == TILE_PIPE_SIDE)) {
                if (!taken(underground == 0)) {
                    /* underground: walk into the pipe */
                    set_game_state(STATE_PIPE_SIDEWAYS);
                    mario_scripted = 0x80;
                    pipe_goal = (uint8_t)(mario_x + 0x18);
                    mario_y = (uint8_t)((mario_y & 0xF8) + 6);
                    clear_object_sprites();
                    return 0xFF;
                }
            } else if (taken(tile == TILE_FIST)) {
                mario_reaches_goal();
                return 0;
            }
            /* solid: Mario is stopped */
            mario_walk_counter++;
            mario_speed_mode = 2;
            return 0xFF;
        }
        y_offset = (uint8_t)-4;
        if (!taken(--checks != 0))
            return 0;
    }
}

/* ---- standing on the floor (17BC) ---- */

/* Mario stands firmly: snap his feet to the tile grid, end the jump (00:185D) */
static void land_on_ground(void)
{
    mario_y = (uint8_t)((((uint8_t)(mario_y - 2)) & 0xFC) | 0x06);
    mario_jump_state = 0;
    mario_jump_step = 0;
    mario_c209 = 0;
    mario_on_ground = 1;
    if (taken(mario_momentum < 7))
        return;
    mario_momentum = 6;
}

/* Nothing solid below: Mario falls (00:1801). While already descending the
   jump code moves him; otherwise he drops 3 pixels a frame. */
static void fall_without_jump(void)
{
    if (taken(mario_jump_state == 2))
        return;
    mario_y += 3;
    mario_on_ground = 0;
    if (taken(mario_speed_mode != 0))
        return;
    mario_speed_mode = 2;
}

/* A coin tile where Mario stands/walks is collected through the vblank handler */
static void touch_coin_tile(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    post_block_event(BLOCK_EVENT_COIN, address);
    request_sfx(SFX_COIN);
}

/* A solid tile (or, for the boss switch quirk, the game state) under
   Mario's feet (00:181E). Spikes hurt, coins are collected. */
static void stand_on_solid(uint8_t tile)
{
    if (!taken(tile != TILE_SPIKES)) {
        if (!taken(invincibility_timer != 0)) {
            uint8_t status = super_status;
            if (taken(status == SUPER_SMALL)) {
                kill_mario();
                land_on_ground();
                return;
            }
            if (!taken(status == SUPER_HURT)) {
                if (!taken(status != SUPER_BIG)) {
                    injure_mario();
                    land_on_ground();
                    return;
                }
            }
        }
    }
    if (taken(tile != TILE_COIN)) {
        land_on_ground();
        return;
    }
    touch_coin_tile(looked_up_tile_address());
    fall_without_jump();
}

/*
 * Mario stands on a pipe and presses down (00:1765): the pipe's
 * destination (4 bytes from the overlay, which holds the underground
 * area's data) is copied to pipe_data, Mario is centred over the pipe
 * and the "going down a pipe" state starts. A pipe without data (overlay
 * 0) is just a floor.
 */
static void stand_on_pipe(void)
{
    if (taken((joy_held & 0x80) == 0)) {            /* down not pressed */
        land_on_ground();
        return;
    }
    uint16_t address = looked_up_tile_address();
    tile_addr_hi = (uint8_t)(address >> 8);
    tile_addr_lo = (uint8_t)address;
    uint16_t p = (uint16_t)(address + 0x3000);
    uint8_t data = shared_read(p);
    if (taken(data == 0)) {
        land_on_ground();
        return;
    }
    HRAM(0xFFF4) = data;
    for (int i = 1; i < 4; i++) {
        p = (uint16_t)(p - 0x20);
        HRAM(0xFFF4 + i) = shared_read(p);
    }
    tile_address_to_pixel();
    pipe_goal = (uint8_t)(mario_y + 0x10);
    uint8_t scroll = reg_b(read_scroll_x());
    mario_x = (uint8_t)(tile_x_px - scroll + 8);
    mario_scripted = 0x80;
    set_game_state(STATE_PIPE_DOWN);
    if (!taken(invincibility_timer != 0))
        request_music(MUS_UNDERGROUND);                /* keep the star music going otherwise */
    clear_object_sprites();
    land_on_ground();
}

/*
 * What is under Mario's feet (00:17BC), every frame unless he is rising.
 * Looks at the tile below his left foot (2 pixels left of his x) and
 * then 4 pixels (8 when running on the ground) to the right. Solid tile
 * (from $60): he stands on it; nothing solid: he falls.
 */
void mario_floor_collision(void)
{
    if (taken(mario_jump_state == 1))
        return;
    tile_y_px = (uint8_t)(mario_y + 0x0B);
    tile_x_px = (uint8_t)(reg_b(read_scroll_x()) + mario_x + 0xFE);
    uint8_t tile = lookup_tile();
    if (taken(tile == TILE_PIPE_TOP)) {
        stand_on_pipe();
        return;
    }
    if (taken(tile == TILE_BOSS_SWITCH)) {
        /* In normal gameplay it ends the level; during the autoscroll level
           states the original hands the game state to the solid-tile code */
        uint8_t state = game_state();
        if (taken(state >= STATE_MENU))
            stand_on_solid(state);
        else
            mario_reaches_goal();
        return;
    }
    if (taken(tile >= TILE_FIRST_SOLID)) {
        stand_on_solid(tile);
        return;
    }
    uint8_t step = 4;
    if (!taken(mario_speed_mode != 4)) {
        if (!taken(mario_jump_state != 0))
            step = 8;
    }
    tile_x_px += step;
    tile = lookup_tile();
    if (taken(tile >= TILE_FIRST_SOLID)) {
        stand_on_solid(tile);
        return;
    }
    fall_without_jump();
}

/* ---- hitting blocks from below (198C) ---- */

/* The sprite of the block that Mario bumped; the vblank handler's
   BLOCK_EVENT_BOUNCING / BOUNCE_DONE steps animate it (00:1937). `address`
   is the block's map address. */
static void start_block_bounce(uint16_t address)
{
    post_block_event(BLOCK_EVENT_BOUNCE, address);
    tile_addr_hi = (uint8_t)(address >> 8);
    tile_addr_lo = (uint8_t)address;
    tile_address_to_pixel();
    uint8_t y = (uint8_t)(mario_y - 0x0B);
    set_block_sprite(0, y);
    block_sprite_y = y;
    uint8_t scroll = reg_b(read_scroll_x());
    block_sprite_x = tile_x_px;
    uint8_t x = (uint8_t)(tile_x_px - scroll);
    set_block_sprite(1, x);
    set_block_sprite(3, 0);
    floaty_x = x;
}

/* Bump a block that does not give way: sound unless something else is
   playing, then the bounce (00:1923) */
static void bump_block(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    set_block_sprite(2, TILE_BREAKABLE_BLOCK);
    if (!taken(shared_read(SOUND_SFX_REQUEST) != 0))
        request_sfx(SFX_BUMP);
    start_block_bounce(address);
}

/* A coin block pays a coin through the vblank handler's coin counting (00:18A4) */
static void pay_block_coin(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    request_sfx(SFX_COIN);
    floaty_y = (uint8_t)(mario_y - 0x10);
    floaty_control = BLOCK_EVENT_COIN;
    shared_write(ADDR_HUD_COIN_FLAG, BLOCK_EVENT_COIN);
    if (taken(coin_block_timer != 0)) {
        bump_block(address);
        return;
    }
    set_block_sprite(2, TILE_SOLID_BLOCK_SPRITE);     /* 18C0: the last coin */
    start_block_bounce(address);
}

/* A block with a powerup (or other contents) bounces and releases it (00:18C7) */
static void release_block_contents(uint16_t address, uint8_t contents)
{
    hitbox_top = contents;
    set_block_sprite(2, TILE_SOLID_BLOCK_SPRITE);
    request_sfx(SFX_BUMP);
    if (taken(pending_block_event() != 0))
        return;
    post_block_event(BLOCK_EVENT_BOUNCE, address);
    tile_addr_hi = (uint8_t)(address >> 8);
    tile_addr_lo = (uint8_t)address;
    hitbox_top = block_contents(address);
    tile_address_to_pixel();
    uint8_t y = (uint8_t)(mario_y - 0x0B);
    set_block_sprite(0, y);
    HRAM(0xFFC2) = y;                   /* the enemy work buffer's position, reused as scratch */
    block_sprite_y = y;
    uint8_t scroll = reg_b(read_scroll_x());
    block_sprite_x = tile_x_px;
    uint8_t x = (uint8_t)(tile_x_px - scroll);
    set_block_sprite(1, x);
    HRAM(0xFFC3) = x;
    set_block_sprite(3, 0);

    uint8_t item = hitbox_top;
    if (taken(item == TILE_NOT_A_BLOCK))
        return;
    if (!taken(item != 0x28)) {                 /* a mushroom, or the flower for super Mario */
        if (!taken(super_status != SUPER_BIG))
            item = 0x2D;
    }
    spawn_powerup(item);
}

/* Contents $C0 (coins) or an item or nothing (00:189B) */
static void hit_block_with_contents(uint16_t address, uint8_t contents)
{
    if (taken(contents != 0xC0)) {
        release_block_contents(address, contents);
        return;
    }
    coin_block_timer = 0xFF;
    pay_block_coin(address);
}

/* A breakable brick (00:19E1): small Mario bumps it, super Mario breaks it
   into four fragments (the objects at C210). */
static void hit_brick(uint16_t address)
{
    uint8_t contents = block_contents(address);
    if (taken(contents == 0xC0)) {
        pay_block_coin(address);
        return;
    }
    if (taken(super_status != SUPER_BIG)) {
        bump_block(address);
        return;
    }
    if (taken(pending_block_event() != 0))
        return;
    post_block_event(BLOCK_EVENT_BREAK, address);
    start_block_fragments((uint8_t)(mario_y - 0x0D), (uint8_t)(mario_x + 2));
    request_noise(NOISE_BRICK_SHATTER);
    add_score(0x0050);
    mario_jump_state = 2;
}

/* A solid block with whatever is inside (00:1888) */
static void hit_solid_block(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    uint8_t contents = block_contents(address);
    if (taken(contents == 0)) {
        hit_brick(address);
        return;
    }
    if (taken(contents == TILE_NOT_A_BLOCK)) {
        set_block_sprite(2, TILE_SOLID_BLOCK_SPRITE);
        start_block_bounce(address);
        return;
    }
    hit_block_with_contents(address, contents);
}

/* A hidden block ($5F) only reacts if it has contents (00:187B) */
static void hit_hidden_block(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    if (taken(block_contents(address) == 0))
        return;
    hit_solid_block(address);
}

/* A mystery block ($81 in the map) (00:1966): an empty one gives a coin */
static void hit_mystery_block(uint16_t address)
{
    if (taken(pending_block_event() != 0))
        return;
    uint8_t contents = block_contents(address);
    if (taken(contents != 0)) {
        hit_block_with_contents(address, contents);
        return;
    }
    request_sfx(SFX_COIN);
    set_block_sprite(2, TILE_COIN_BLOCK);
    floaty_y = (uint8_t)(mario_y - 0x10);
    floaty_control = BLOCK_EVENT_COIN;
    start_block_bounce(address);
}

/*
 * Mario's head against the tile above (00:198C), only while he rises. Checks
 * the tile above his right side, then, if that one is free, above his left
 * side. Hidden blocks, bricks, mystery blocks and coins react; any other
 * solid tile stops the jump with a bump.
 */
void mario_head_collision(void)
{
    if (taken(mario_jump_state != 1))
        return;
    tile_y_px = (uint8_t)(mario_y - 3);
    uint8_t scroll = read_scroll_x();
    tile_x_px = (uint8_t)(scroll + reg_b(mario_x) + 2);
    uint8_t tile = lookup_tile();
    if (taken(tile == TILE_HIDDEN_BLOCK)) {
        hit_hidden_block(looked_up_tile_address());
        return;
    }
    if (!taken(tile >= TILE_FIRST_SOLID)) {
        tile_x_px = (uint8_t)(tile_x_px - 4);
        tile = lookup_tile();
        if (taken(tile == TILE_HIDDEN_BLOCK)) {
            hit_hidden_block(looked_up_tile_address());
            return;
        }
        if (taken(tile < TILE_FIRST_SOLID))
            return;
    }
    uint16_t address = looked_up_tile_address();
    tile = filter_passthrough_tile(tile);
    if (taken(tile == 0))
        return;
    if (taken(tile == TILE_BREAKABLE_BLOCK)) {
        hit_brick(address);
    } else if (taken(tile == TILE_COIN)) {
        touch_coin_tile(address);
    } else if (taken(tile == TILE_COIN_BLOCK)) {
        hit_mystery_block(address);
    } else if (taken(tile == TILE_SOLID_BLOCK_SPRITE)) {
        hit_solid_block(address);
    } else {
        mario_jump_state = 2;
        request_sfx(SFX_BUMP);
    }
}

/*
 * The block Mario bumped from below bounces as a sprite (03:4B3C), sprite
 * 11 of the OAM buffer, placed at the block (in level coordinates) and
 * 11 pixels above Mario's head. The bounce ends (block event 4) when Mario
 * stands on the ground again or has fallen below the block; while he is
 * still close under it, he starts falling.
 */
void bounce_block_sprite(void)
{
    if (taken(shared_read(ADDR_BLOCK_EVENT) != 3))
        return;
    uint8_t scroll = reg_b(read_scroll_x());
    shared_write(BLOCK_SPRITE_X, (uint8_t)(block_sprite_x - scroll));
    shared_write(BLOCK_SPRITE_Y, (uint8_t)(mario_y - 0x0B));
    if (!taken(mario_on_ground != 0)) {
        uint8_t block_y = reg_b(block_sprite_y);
        if (taken((uint8_t)(block_y - 4) >= shared_read(BLOCK_SPRITE_Y))) {
            mario_jump_state = 2;
            return;
        }
        if (taken(block_y >= shared_read(BLOCK_SPRITE_Y)))
            return;
    }
    shared_write(BLOCK_SPRITE_Y, 0);
    shared_write(ADDR_BLOCK_EVENT, 4);
}
