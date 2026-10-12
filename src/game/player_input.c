/* Jump height, running and firing, from Mario's per-frame joypad input. */
#include "player_priv.h"

enum {
    BUTTON_A = 0x01, BUTTON_B = 0x02, BUTTON_DOWN = 0x80,
    SPEED_WALK = 2, SPEED_RUN = 4,
};

/* Create a projectile in the first free slot (03:4A0C). Normal gameplay
   has one superball; the submarine and plane have three shots. */
static void fire_projectile(void)
{
    uint8_t slots = 3;
    if (!taken(game_state() == STATE_AUTOSCROLL)) {
        if (taken(superball_mario == 0))
            return;
        slots = 1;
    }

    uint16_t status = 0xFFA9, sprite = ADDR_OAM_BUFFER;
    for (;;) {
        if (taken(ram_read(status++) == 0))
            break;
        sprite += 4;
        if (!taken(--slots != 0))
            return;
    }

    uint8_t facing = reg_b(mario_facing);
    shared_write(sprite++, (uint8_t)(mario_y - 2));
    uint8_t offset = 2;
    if (!taken((facing & 0x20) == 0))
        offset = 0xF8;
    shared_write(sprite++, (uint8_t)(mario_x + offset));

    uint8_t tile = 0x60;
    if (!taken(game_state() != STATE_AUTOSCROLL)) {
        tile = 0x7A;                    /* submarine torpedo */
        if (!taken(level_index != 0x0B))
            tile = 0x6E;                /* plane shot */
    }
    shared_write(sprite++, tile);
    shared_write(sprite, 0);

    uint8_t direction = 0x0A;
    if (!taken((facing & 0x20) != 0))
        direction = 0x09;
    ram_write((uint16_t)(status - 1), direction);
    request_sfx(SFX_SUPERBALL);
    shot_cooldown = 0x0C;
    superball_ttl = 0xFF;
}

/* Start a jump (03:49BF). A crouching big Mario keeps his crouching pose
   and existing arc step; running also keeps the existing arc step. */
static void start_jump(uint8_t pressed)
{
    if (taken(mario_jump_state != 0))
        return;
    if (taken(mario_on_ground == 0))
        return;
    if (taken((pressed & BUTTON_A) == 0))
        return;
    mario_on_ground = 0;
    if (!taken(mario_anim == 0x18)) {
        mario_anim = (uint8_t)((mario_anim & 0xF0) | 4);
        if (!taken(mario_speed_mode == SPEED_RUN)) {
            mario_speed_mode = SPEED_WALK;
            mario_jump_step = 2;
        }
        mario_momentum = 0x30;
    }
    request_sfx(SFX_JUMP);
    mario_jump_state = 1;
}

/* Mario's jump/run/fire control (03:498B, including the internal 4966
   and 4975 paths). Releasing A early skips to the end of the rising arc,
   shortening the jump. The shooter stages reuse this entry for firing. */
void mario_jump_and_fire(void)
{
    if (taken(game_state() == STATE_AUTOSCROLL)) {
        if (!taken((joy_pressed & (BUTTON_A | BUTTON_B)) != 0)) {
            if (taken((joy_held & BUTTON_A) == 0))
                return;
            if (!taken(shot_cooldown == 0)) {
                shot_cooldown--;
                return;
            }
        }
        fire_projectile();
        return;
    }

    uint8_t pressed = reg_b(joy_pressed), held = joy_held;
    if (taken((held & BUTTON_B) != 0)) {
        if (!taken(mario_jump_state != 0)) {
            uint8_t speed = SPEED_WALK;
            if (!taken(mario_momentum < 3))
                speed = SPEED_RUN;
            mario_speed_mode = speed;
        }
    } else {
        if (!taken(mario_speed_mode != SPEED_RUN))
            mario_speed_mode = SPEED_WALK;
    }

    if (taken((held & BUTTON_A) != 0)) {
        start_jump(pressed);
    } else {
        if (taken(mario_jump_state == 1)) {
            uint8_t step = mario_jump_step;
            if (!taken(step >= 0x0F)) {
                mario_c209 = (uint8_t)(step - 1);
                mario_jump_step = 0x0F;
            }
        }
    }

    if (taken((pressed & BUTTON_DOWN) != 0))
        mario_momentum = 0x20;
    if (!taken((pressed & BUTTON_B) != 0))
        return;
    if (!taken(mario_momentum != 6)) {
        if (!taken(shared_read(ADDR_DEMO_MODE) != 0))
            mario_momentum = 0;
    }
    fire_projectile();
}
