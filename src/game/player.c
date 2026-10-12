/*
 * Mario: getting hurt, dying, picking up power-ups, and the tests of Mario
 * against the other objects (enemies, platforms, power-ups).
 *
 * Objects are the ten enemy slots at D100-D19F (see enemies.h); slot 9 at
 * D190 is where blocks release their power-up.
 */
#include "player_priv.h"

enum { DEMO_INPUT_LISTS = 0x4AE4 };   /* bank 3: pointers to the three demo input lists */

/* Mario gets hurt: he starts shrinking (80 frames) and loses the flower
   (00:09E0). */
void injure_mario(void)
{
    super_status = SUPER_SHRINKING;
    superball_mario = 0;
    frame_timer = 0x50;
    request_sfx(SFX_INJURY);
}

/* Mario dies (00:09F1): the "pre dying" state starts, the music changes to
   the death jingle. Once Tatanga is beaten Mario can't die any more. */
void kill_mario(void)
{
    if (taken(boss_defeated != 0))
        return;
    set_game_state(STATE_PRE_DYING);
    superball_mario = 0;
    io_write(rTMA, 0);
    request_music(MUS_DEATH);
    mario_flags = 0x80;
    death_y = mario_y;
}

/*
 * Star power (00:1F03), every fourth frame: count the invincibility timer
 * down and blink Mario. It ends when the timer reaches 1 or the star song
 * is over; then the level music starts again.
 */
void invincibility_tick(void)
{
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 3) != 0))
        return;
    uint8_t timer = invincibility_timer;
    if (taken(timer == 0))
        return;
    if (!taken(timer == 1)) {
        invincibility_timer = (uint8_t)(timer - 1);
        mario_flags ^= 0x80;
        if (taken(shared_read(SOUND_MUSIC_PLAYING) != 0))
            return;
    }
    invincibility_timer = 0;
    mario_flags = 0;
    start_level_music();
}

/*
 * Decide what hitting the enemy whose health byte is in hit_enemy_flags is
 * worth (00:0A10): the top two bits of the byte select the reward code
 * stored in hit_reward (100, 400, 800 points or $50 for bosses/coins).
 */
void set_stomp_reward(void)
{
    hit_reward = rom_byte(0, (uint16_t)(0x0A29 + (hit_enemy_flags >> 6)));
}

/*
 * Does Mario's hit box (hitbox_top/bottom/left/right) overlap the object at
 * `obj` (00:0AAF)? `dimensions` is the object's byte at +A: bits 4-6 give
 * its width in tiles, bits 0-3 its height. y is the top of the bottom tile
 * and x the left edge, and y grows downwards.
 *
 * The original leaves some values in registers that the callers continue
 * with, so they are returned too (see HitboxResult).
 */
HitboxResult hitbox_overlap(uint16_t obj, uint8_t dimensions)
{
    const EnemySlot *o = object_at(obj);
    HitboxResult r = { false, 0, 2 };

    uint8_t bottom = reg_b((uint8_t)(o->y + 8));
    r.b = bottom;
    if (taken(hitbox_top >= bottom))        /* Mario's top is below the object's bottom */
        return r;

    /* top of the object: y minus one tile for each tile of height beyond the first */
    uint8_t height = reg_b(dimensions & 0x0F);
    uint8_t top = o->y;
    for (;;) {
        height--;
        if (taken(height == 0))
            break;
        top -= 8;
    }
    r.b = reg_b(top);
    if (taken(hitbox_bottom < top))         /* Mario's bottom is above the object's top */
        return r;

    r.hl_offset = 3;
    r.b = reg_b(o->x);
    if (taken(hitbox_right < o->x))         /* Mario's right edge is left of the object */
        return r;

    uint8_t width = reg_b((dimensions & 0x70) >> 4);
    uint8_t right = o->x;
    do {
        right += 8;
    } while (taken(--width != 0));
    r.b = reg_b(right);
    if (taken(hitbox_left >= right))        /* Mario's left edge is right of the object */
        return r;
    r.hit = true;
    return r;
}

/* Add the stomp points to the floaty and move the stomp chain along: each
   stomp within 50 frames of the previous one doubles the reward, up to
   three times (00:090D, the part of Call_84E after an enemy is hit). */
static void enemy_defeated(void)
{
    request_sfx(SFX_STOMP);
    floaty_x = (uint8_t)(mario_x - 4);
    floaty_y = (uint8_t)(mario_y - 0x10);
    floaty_control = hit_reward;

    bool reset_chain = false;
    if (taken(stomp_chain_timer == 0)) {
        reset_chain = true;
    } else {
        uint8_t chain = stomp_chain;
        if (!taken(chain == 3)) {
            chain++;
            stomp_chain = chain;
        }
        reg_b(chain);
        if (taken(floaty_control == 0x50)) {   /* coins and 1UPs are not doubled */
            reset_chain = true;
        } else {
            /* BCD reward doubled once per chain step (as a plain shift, which
               makes 800 -> 1000 correct by luck) */
            uint8_t value = floaty_control;
            uint8_t count = chain;
            do {
                value = (uint8_t)(value << 1);
            } while (taken(--count != 0));
            floaty_control = value;
        }
    }
    if (reset_chain)
        stomp_chain = 0;
    stomp_chain_timer = 0x32;
}

/* The power-up in slot 9 touches Mario (00:097C) */
static void pick_up_powerup(uint16_t slot, uint8_t id)
{
    enum { MUSHROOM = 0x29, STAR = 0x34, ONE_UP = 0x2B, FLOWER = 0x2E };
    bool become_super = false;
    bool announce = true;       /* 1000 points, floaty */

    if (taken(id == MUSHROOM)) {
        if (taken(super_status == SUPER_BIG))
            announce = true;
        else
            become_super = true;
    } else if (taken(id == STAR)) {
        invincibility_timer = 0xF8;
        request_music(MUS_STAR);
    } else if (taken(id == ONE_UP)) {
        floaty_control = 0xFF;
        request_sfx(SFX_ONE_UP);
        shared_write(ADDR_LIVES_CHANGE, 1);
        announce = false;
    } else {
        if (taken(id != FLOWER))
            return;
        if (taken(super_status != SUPER_BIG)) {
            become_super = true;        /* lost the size before: acts like a mushroom */
        } else {
            superball_mario = SUPER_BIG;
            request_sfx(SFX_GROW);
        }
    }
    if (become_super) {
        super_status = SUPER_GROWING;
        frame_timer = 0x50;
        request_sfx(SFX_GROW);
    }
    if (announce)
        floaty_control = 0x10;
    floaty_x = (uint8_t)(mario_x - 4);
    floaty_y = (uint8_t)(mario_y - 0x10);
    object_at(slot)->id = ENEMY_FREE;
}

/*
 * Mario against the objects (00:084E): called once per frame in normal
 * gameplay. Goes through the ten slots from the power-up slot down. For an
 * object that overlaps Mario:
 *  - power-ups are picked up;
 *  - from above (Mario's feet above the object's top) the object is
 *    stomped and Mario bounces;
 *  - from the side Mario is hurt, or killed if small;
 *  - with star power (or in autoscroll levels) the enemy dies.
 * After a stomp, a kill or a pick-up the loop ends: one such event a frame.
 */
void mario_object_collisions(void)
{
    if (!taken(stomp_chain_timer == 0))
        stomp_chain_timer--;

    uint16_t slot = ENEMY_SLOT_BASE + 16 * (ENEMY_SLOTS - 1);
    uint8_t remaining = ENEMY_SLOTS;
    for (;;) {
        uint8_t id = object_at(slot)->id;
        if (taken(id != ENEMY_FREE)) {
            const EnemySlot *o = object_at(slot);
            shared_write(ADDR_SLOT_ID_SCRATCH, id);
            enemy_slot_lo = (uint8_t)slot;
            uint8_t dimensions = o->dimensions;
            hit_enemy_flags = o->health;

            uint8_t top = reg_b(mario_y);
            if (!taken(super_status != SUPER_BIG)) {
                if (!taken(mario_anim == 0x18))        /* big and not crouching: taller box */
                    top = reg_b((uint8_t)(top - 2));
            }
            hitbox_top = top;
            hitbox_bottom = (uint8_t)(mario_y + 6);
            hitbox_left = (uint8_t)(reg_b(mario_x) - 3);
            hitbox_right = (uint8_t)(mario_x + 2);

            HitboxResult hit = hitbox_overlap(slot, dimensions);
            if (!taken(!hit.hit)) {
                if (taken(enemy_slot_lo == 0x90)) {
                    pick_up_powerup(slot, shared_read(ADDR_SLOT_ID_SCRATCH));
                    return;
                }
                shared_read(ADDR_SLOT_ID_SCRATCH);     /* the original reads it again, unused */
                bool side_hit;
                if (taken(game_state() == STATE_AUTOSCROLL)) {
                    side_hit = true;
                } else if (!taken(invincibility_timer == 0)) {
                    side_hit = true;
                } else {
                    side_hit = false;
                    /* Is Mario above the object, coming from the top? */
                    uint8_t b = hit.b;
                    if (taken((uint8_t)(mario_x + 6) < o->x)) side_hit = true;
                    else if (taken((uint8_t)(mario_x - 6) >= b)) side_hit = true;
                    else if (taken(mario_y >= (uint8_t)(reg_b(o->y) - 3))) side_hit = true;
                }
                if (!side_hit) {
                    /* stomp */
                    if (taken(o->dimensions & 0x80))   /* can't be killed */
                        return;
                    set_stomp_reward();
                    if (taken(stomp_enemy(slot) == 0))
                        return;
                    mario_on_ground = 0;
                    mario_jump_step = 0x0D;
                    mario_jump_state = 1;
                    mario_anim = (uint8_t)((mario_anim & 0xF0) | 0x04);    /* rising */
                    enemy_defeated();
                    return;
                }
                /* touched from the side */
                if (!taken(invincibility_timer != 0)) {
                    if (taken(super_status >= SUPER_SHRINKING))
                        return;                          /* already hurt: invulnerable */
                    if (taken(enemy_touches_mario(slot) == 0))
                        return;
                    if (taken(super_status != SUPER_SMALL))
                        injure_mario();
                    else
                        kill_mario();
                    return;
                }
                if (taken(star_mario_hits_enemy(slot) == 0))
                    return;
                enemy_defeated();
                return;
            }
        }
        slot -= 0x10;
        if (!taken(--remaining != 0))
            return;
    }
}

/*
 * A bouncing block hits the enemy standing on it (00:0A2D). Runs every
 * frame; only does something while a block is bouncing ($FFEE is nonzero
 * and not the coin-collect code). Every enemy that is 13-19 pixels above
 * Mario's feet row (that is, standing on the block) and overlaps Mario's
 * horizontal extent is hit from below.
 */
void bounce_block_hits_enemies(void)
{
    uint8_t event = pending_block_event();
    if (taken(event == 0))
        return;
    if (taken(event == BLOCK_EVENT_COIN))
        return;

    uint16_t slot = ENEMY_SLOT_BASE;
    uint8_t remaining = ENEMY_SLOTS;
    for (;;) {
        if (taken(object_at(slot)->id != ENEMY_FREE)) {
            const EnemySlot *o = object_at(slot);
            do {
                if (taken(o->dimensions & 0x80))        /* can't be hurt */
                    break;
                hit_enemy_flags = o->health;
                uint8_t dy = (uint8_t)(mario_y - reg_b(o->y));
                if (taken(mario_y < o->y))              /* enemy must be above Mario */
                    break;
                reg_b(dy);
                if (taken(0x14 < dy))                   /* but not too far above */
                    break;
                if (taken((uint8_t)(0x14 - dy) >= 7))   /* at least 13 pixels */
                    break;
                uint8_t width = (o->dimensions & 0x70) >> 4;
                uint8_t right = o->x;
                do {
                    right += 8;
                } while (taken(--width != 0));
                uint8_t left = reg_b(o->x);
                if (taken((uint8_t)(mario_x - 6) >= right))
                    break;
                if (taken((uint8_t)(mario_x + 6) < left))
                    break;
                set_stomp_reward();
                if (taken(hit_enemy_from_below(slot) == 0))
                    break;
                floaty_x = (uint8_t)(mario_x - 4);
                floaty_y = (uint8_t)(mario_y - 0x10);
                floaty_control = hit_reward;
            } while (0);
        }
        slot += 0x10;
        if (!taken(--remaining != 0))
            return;
    }
}

/*
 * Mario standing on a solid object such as a lift (00:0AEA). For every
 * object that cannot be killed (bit 7 of its dimensions: platforms) whose
 * top is at most 7 pixels below Mario's feet and under Mario's horizontal
 * extent, Mario is put on top of it, the object is told it carries Mario
 * (stomp response, then +B = 1) and Mario lands. Does nothing while Mario
 * is rising.
 */
void mario_rides_platforms(void)
{
    if (taken(mario_jump_state == 1))
        return;

    uint16_t slot = ENEMY_SLOT_BASE;
    uint8_t remaining = ENEMY_SLOTS;
    for (;;) {
        if (taken(object_at(slot)->id != ENEMY_FREE)) {
            EnemySlot *o = object_at(slot);
            do {
                if (taken((o->dimensions & 0x80) == 0))
                    break;
                /* the top of the object: y minus a tile per tile of height beyond the first */
                uint8_t height = o->dimensions & 0x0F;
                hitbox_top = height;                    /* the original parks it here */
                uint8_t b = reg_b(hitbox_top);
                uint8_t top = o->y;
                for (;;) {
                    b--;
                    if (taken(b == 0))
                        break;
                    top -= 8;
                }
                hitbox_top = top;
                uint8_t mario_bottom = reg_b((uint8_t)(mario_y + 6));
                if (taken((uint8_t)(top - mario_bottom) >= 7))   /* Mario must be above, within 7 */
                    break;

                /* Horizontal overlap, allowing up to 2 pixels of overhang */
                if (!taken(o->x < reg_b(mario_x))) {
                    if (taken((uint8_t)(o->x - mario_x) >= 3))
                        break;
                }
                uint8_t width = reg_b((o->dimensions & 0x70) >> 4);
                uint8_t right = o->x;
                do {
                    right += 8;
                } while (taken(--width != 0));
                if (!taken(mario_x < reg_b(right))) {
                    if (taken((uint8_t)(mario_x - right) >= 3))
                        break;
                }

                /* Mario lands on the object */
                mario_y = (uint8_t)(hitbox_top - 0x0A);
                stomp_enemy(slot);
                o->carries_mario = 1;
                mario_jump_state = 0;
                mario_jump_step = 0;
                mario_c209 = 0;
                mario_on_ground = 1;
                if (!taken(mario_momentum < 7))
                    mario_momentum = 6;
                return;
            } while (0);
        }
        slot += 0x10;
        if (!taken(--remaining != 0))
            return;
    }
}

/*
 * Clear the sprites that are not Mario, enemies or platforms (00:1ED4):
 * the projectiles, block fragments, floating scores; and stop the
 * superballs. The four block fragment objects at C210 are set to $80
 * (not drawn).
 */
void clear_object_sprites(void)
{
    uint16_t addr = ADDR_OAM_BUFFER + 7 * 4;        /* sprites 7-19 */
    uint8_t n = 13 * 4;
    do {
        shared_write(addr++, 0);
    } while (taken(--n != 0));
    addr = ADDR_OAM_BUFFER;                         /* sprites 0-1 and 3/4 of 2 (an oversight) */
    n = 11;
    do {
        shared_write(addr++, 0);
    } while (taken(--n != 0));
    HRAM(0xFFA9) = 0;                               /* the three superball states */
    HRAM(0xFFAA) = 0;
    HRAM(0xFFAB) = 0;
    for (int i = 0; i < 4; i++)
        WRAM(0xC210 + 0x10 * i) = 0x80;
    loop_taken(4);
}

/* While the attract-mode demo plays the game reads the stored joypad state
   instead of the real one (00:2113). */
void feed_demo_joypad(void)
{
    if (taken(shared_read(ADDR_DEMO_MODE) == 0))
        return;
    joy_held = demo_joypad;
}

/*
 * The attract-mode demo's joypad (03:4A94): plays back one of three input
 * lists (demo_part) of (buttons, frames) pairs, ended by $FF. The real
 * joypad state is kept in demo_joypad. A demo_mode of $FF means the demo
 * is over.
 */
void play_demo_input(void)
{
    uint8_t mode = shared_read(ADDR_DEMO_MODE);
    if (taken(mode == 0))
        return;
    if (taken(mode == 0xFF))
        return;
    if (!taken(demo_wait == 0)) {
        demo_wait--;
    } else {
        uint16_t list = (uint16_t)(DEMO_INPUT_LISTS + (uint8_t)(demo_part << 1));
        uint16_t p = (uint16_t)((mem_read(list) | mem_read((uint16_t)(list + 1)) << 8) + demo_pos);
        uint8_t buttons = mem_read(p);
        if (taken(buttons == 0xFF)) {
            demo_input = 0;
        } else {
            demo_input = buttons;
            demo_wait = mem_read((uint16_t)(p + 1));
            demo_pos = (uint8_t)(demo_pos + 2);
        }
    }
    demo_joypad = joy_held;
    joy_held = demo_input;
    joy_pressed = demo_input;
}

/* Mario falls into a pit (03:4B6F): below the screen (Y B4-BF), he dies
   at once, without the "pre dying" jump. */
void mario_falls_in_pit(void)
{
    uint8_t y = mario_y;
    if (taken(y < 0xB4))
        return;
    if (taken(y >= 0xC0))
        return;
    super_status = SUPER_SMALL;
    superball_mario = 0;
    set_game_state(STATE_DEAD);
    request_music(MUS_DEATH);
    frame_timer = 0x90;
}

/* Mushroom growth (03:4B8A): every 4th frame Mario switches between his
   small and big look until the timer runs out; then he is big. */
void mario_growing(void)
{
    if (taken(super_status != SUPER_GROWING))
        return;
    uint8_t t = frame_timer;
    if (!taken(t == 0)) {
        if (taken((t & 3) != 0))
            return;
        mario_flags = 0;
        mario_anim ^= 0x10;
        return;
    }
    super_status = SUPER_BIG;
    mario_flags = 0;
    mario_anim |= 0x10;
}

/* After a hit (03:4BB5): Mario shrinks (switching looks every 4th frame),
   then blinks while invulnerable for 64 frames. */
void mario_hurt_blinking(void)
{
    uint8_t status = super_status;
    if (taken(status == SUPER_HURT)) {
        uint8_t t = frame_timer;
        if (!taken(t == 0)) {
            if (taken((t & 3) != 0))
                return;
            mario_flags ^= 0x80;
            return;
        }
        super_status = SUPER_SMALL;
        mario_flags = 0;
        mario_anim &= 0x0F;
        return;
    }
    if (taken(status != SUPER_SHRINKING))
        return;
    uint8_t t = frame_timer;
    if (!taken(t == 0)) {
        if (taken((t & 3) != 0))
            return;
        mario_anim ^= 0x10;
        return;
    }
    super_status = SUPER_HURT;
    frame_timer = 0x40;
    mario_anim &= 0x0F;
}
