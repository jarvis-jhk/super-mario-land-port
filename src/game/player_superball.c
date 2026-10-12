/*
 * Superballs: the bouncing balls Mario throws with the flower, and the
 * "bullets" of the autoscroll (submarine / airplane) levels, which use the
 * same code.
 *
 * The ball is sprite 0 of the OAM buffer (y at C000, x at C001) and its
 * state is superball_state (FFA9): bit 0 = going right (else left), bit 2 =
 * going up, bit 3 = going down (set by the bounces). superball_ttl counts
 * down; at 0 the ball disappears. The code loops over three ball states
 * (FFA9-FFAB), but only the first is ever processed (loop counter 1).
 */
#include "player_priv.h"

/*
 * The four fragments of a broken block (objects at C210, 16 bytes apart):
 * each starts at (y, x) and flies off; the upper ones start further left.
 * Shared by Mario breaking a brick and the superball breaking one (00:1A02
 * and 00:20C1).
 */
void start_block_fragments(uint8_t y, uint8_t x)
{
    for (int i = 0; i < 4; i++) {
        uint16_t f = (uint16_t)(0xC210 + 0x10 * i);
        WRAM(f + 0) = 0;
        WRAM(f + 1) = y;
        WRAM(f + 2) = x;
        WRAM(f + 7) = 1;
        WRAM(f + 8) = 7;
    }
    loop_taken(4);
    WRAM(0xC222) -= 4;
    WRAM(0xC242) -= 4;
    WRAM(0xC238) = 0x0B;
    WRAM(0xC248) = 0x0B;
    broken_block_scroll = read_scroll_x();
}

/*
 * Move the four block fragments (03:4AEA), every frame: each drifts one
 * pixel sideways (byte +5: 0 right, else left) and stays put in the level
 * while the screen scrolls; its jump arc (move_along_jump_arc) does the
 * vertical part. When its arc is over (+7 = 0) it is switched off: flag
 * $80, then $FF the next frame (hidden).
 */
void move_block_fragments(void)
{
    uint16_t f = 0xC210;
    uint8_t n = 4;
    do {
        uint8_t flag = WRAM(f);
        if (!taken(flag != 0x80))
            WRAM(f) = 0xFF;
        if (!taken(flag != 0)) {
            if (taken(WRAM(f + 7) == 0)) {
                WRAM(f) = 0x80;
                WRAM(f + 2) = 0xFF;
            } else {
                if (!taken(WRAM(f + 5) != 0))
                    WRAM(f + 2)++;
                else
                    WRAM(f + 2)--;
                uint8_t scrolled = (uint8_t)(read_scroll_x() - broken_block_scroll);
                WRAM(f + 2) = (uint8_t)(WRAM(f + 2) - scrolled);
            }
        }
        f += 0x10;
    } while (taken(--n != 0));
    broken_block_scroll = read_scroll_x();
}

/*
 * In the autoscroll levels a superball also breaks blocks (00:2097): `tile`
 * is the tile ($82 breakable, $80 block with contents) at map address
 * `address`. A block with contents releases it. Elsewhere nothing happens.
 * The fragments start at the position of the ball (tile_y_px, hitbox_bottom).
 */
void break_block_in_autoscroll(uint8_t tile, uint16_t address)
{
    if (taken(game_state() != STATE_AUTOSCROLL))
        return;
    if (taken(pending_block_event() != 0))
        return;
    post_block_event(BLOCK_EVENT_BREAK, address);
    if (!taken(tile != TILE_SOLID_BLOCK_SPRITE)) {
        uint8_t contents = block_contents(address);
        if (!taken(contents == 0))
            spawn_powerup(contents);
    }
    start_block_fragments(tile_y_px, hitbox_bottom);
    add_score(0x0050);
    request_noise(NOISE_BRICK_SHATTER);
}

/*
 * Look at the tile at the ball's new position (00:1FD2). tile_y_px is
 * preset to the y coordinate, `x` is the sprite x. Returns A as the original
 * leaves it; the carry flag ("no collision") is `result < $60`.
 *
 * Coins are collected by the ball (not in autoscroll levels, and only if no
 * other block event is pending) - the original leaves a small number in A
 * then, so the coin does not stop the ball, but a coin tile that cannot be
 * collected is solid! In the autoscroll levels it breaks blocks.
 */
uint8_t find_neighboring_tile(uint8_t x)
{
    reg_b(x);
    tile_x_px = (uint8_t)(read_scroll_x() + x);
    uint8_t tile = lookup_tile();
    uint16_t address = looked_up_tile_address();
    bool check_blocks = true;
    if (!taken(tile != TILE_COIN)) {
        uint8_t state = game_state();
        if (taken(state == STATE_AUTOSCROLL)) {
            tile = state;                       /* A holds the game state: "not solid" */
            check_blocks = false;
        } else if (taken((tile = pending_block_event()) != 0)) {
            check_blocks = false;               /* A holds the pending event code */
        } else {
            post_block_event(BLOCK_EVENT_COIN, address);
            request_sfx(SFX_COIN);
            tile = SFX_COIN;                    /* A = 5 */
        }
    }
    if (check_blocks) {
        if (taken(tile == TILE_BREAKABLE_BLOCK))
            break_block_in_autoscroll(tile, address);
        if (taken(tile == TILE_SOLID_BLOCK_SPRITE))
            break_block_in_autoscroll(tile, address);
    }
    return tile;
}

/*
 * The ball against the enemies (00:200A). `ball_x` is the address of the
 * ball's x in the OAM buffer. The ball's box is 4 wide and 3 high. An enemy
 * that is hit changes (superball_hits_enemy / bullet_hits_enemy); the ball
 * vanishes, and a killed enemy gives points (floaty).
 */
void superball_hits_enemies(uint16_t ball_x)
{
    uint16_t slot = ENEMY_SLOT_BASE;
    uint8_t remaining = ENEMY_SLOTS;
    for (;;) {
        if (taken(object_at(slot)->id != ENEMY_FREE)) {
            const EnemySlot *o = object_at(slot);
            do {
                if (taken(o->dimensions & 0x80))
                    break;
                uint8_t dimensions = o->dimensions;
                hit_enemy_flags = o->health;
                uint8_t x = shared_read(ball_x);
                hitbox_left = x;
                hitbox_right = (uint8_t)(x + 4);
                uint16_t ball_y = (uint16_t)((ball_x & 0xFF00) | (uint8_t)(ball_x - 1));
                hitbox_top = shared_read(ball_y);
                hitbox_bottom = (uint8_t)(shared_read(ball_y) + 3);   /* read again, as the original */

                if (taken(!hitbox_overlap(slot, dimensions).hit))
                    break;
                set_stomp_reward();
                uint8_t result;
                if (taken(game_state() != STATE_AUTOSCROLL))
                    result = superball_hits_enemy(slot);
                else
                    result = bullet_hits_enemy(slot);
                if (taken(result == 0))
                    break;
                floaty_y = (uint8_t)(shared_read(ball_y) - 8);
                floaty_x = shared_read((uint16_t)(ball_y + 1));
                if (!taken(result != 0xFF)) {
                    request_sfx(SFX_STOMP);         /* the enemy dies */
                    floaty_control = hit_reward;
                }
                /* the ball is gone */
                shared_write((uint16_t)(ball_y + 1), 0);
                shared_write(ball_y, 0);
                uint16_t state = 0xFFAB;
                if (!taken((ball_y & 0x08) != 0)) {
                    state--;
                    if (!taken((ball_y & 0x04) != 0))
                        state--;
                }
                ram_write(state, 0);
            } while (0);
        }
        slot += 0x10;
        if (!taken(--remaining != 0))
            return;
    }
}

/*
 * Move the superball and check its surroundings (00:1F2D), every frame in
 * levels where Mario can throw one (not in autoscroll levels... the bullets
 * of those use the same code).
 *
 * The ball moves 2 pixels a frame horizontally and vertically in its
 * current directions; before each move it looks at the tile in the
 * direction of travel: a solid one turns the ball around (right/left,
 * up/down). It disappears when it leaves the screen or its time to live runs
 * out.
 */
void update_superballs(void)
{
    uint8_t e = 1;                              /* low byte of the OAM address (C001 = x) */

    if (taken(HRAM(0xFFA9) != 0)) {
        uint16_t state = 0xFFA9;
        bool removed = false;
        if (taken(superball_ttl == 0)) {
            removed = true;
        } else {
            superball_ttl--;
            if (!taken((HRAM(state) & 1) == 0)) {
                /* flying right */
                uint8_t x = (uint8_t)(shared_read(ADDR_OAM_BUFFER + e) + 2);
                shared_write(ADDR_OAM_BUFFER + e, x);
                if (!taken(x < 0xA2)) {
                    removed = true;
                } else {
                    e--;
                    tile_y_px = shared_read(ADDR_OAM_BUFFER + e);
                    if (!taken(find_neighboring_tile((uint8_t)(x + 3)) < TILE_FIRST_SOLID))
                        HRAM(state) = (uint8_t)((HRAM(state) & 0xFC) | 0x02);   /* bounce: go left */
                }
            } else {
                /* flying left */
                uint8_t x = (uint8_t)(shared_read(ADDR_OAM_BUFFER + e) - 2);
                shared_write(ADDR_OAM_BUFFER + e, x);
                if (taken(x < 4)) {
                    removed = true;
                } else {
                    e--;
                    tile_y_px = shared_read(ADDR_OAM_BUFFER + e);
                    if (!taken(find_neighboring_tile((uint8_t)(x - 2)) < TILE_FIRST_SOLID))
                        HRAM(state) = (uint8_t)((HRAM(state) & 0xFC) | 0x01);   /* bounce: go right */
                }
            }
            if (!removed) {
                /* vertical motion */
                if (!taken((HRAM(state) & 4) == 0)) {
                    uint8_t y = (uint8_t)(shared_read(ADDR_OAM_BUFFER + e) - 2);
                    shared_write(ADDR_OAM_BUFFER + e, y);
                    if (taken(y < 0x10)) {
                        removed = true;
                    } else {
                        tile_y_px = (uint8_t)(y - 1);
                        e++;
                        if (!taken(find_neighboring_tile(shared_read(ADDR_OAM_BUFFER + e)) < TILE_FIRST_SOLID))
                            HRAM(state) = (uint8_t)((HRAM(state) & 0xF3) | 0x08);  /* bounce: go down */
                    }
                } else {
                    uint8_t y = (uint8_t)(shared_read(ADDR_OAM_BUFFER + e) + 2);
                    shared_write(ADDR_OAM_BUFFER + e, y);
                    if (taken(y >= 0xA8)) {
                        removed = true;
                    } else {
                        tile_y_px = (uint8_t)(y + 4);
                        e++;
                        if (!taken(find_neighboring_tile(shared_read(ADDR_OAM_BUFFER + e)) < TILE_FIRST_SOLID))
                            HRAM(state) = (uint8_t)((HRAM(state) & 0xF3) | 0x04);  /* bounce: go up */
                    }
                }
            }
        }
        if (removed) {
            e &= 0xFE;
            shared_write(ADDR_OAM_BUFFER + e, 0);                  /* y = 0: off the screen (x stays) */
            HRAM(state) = 0;
        }
        /* the registers are restored first: the enemy test always gets the
           ball's x address, even when the ball was just removed */
        superball_hits_enemies(0xC001);
    }
    taken(false);                               /* the loop over the balls ends: only one is processed */
}
