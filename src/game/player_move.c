/*
 * Mario walking: speed, direction changes, scrolling the screen with him.
 *
 * Mario's walking state (see ram.h):
 *   mario_walk_dir    $10 right, $20 left, 1 turning around, 0 standing
 *   mario_momentum    0-6, how fast he is: grows by 1 per frame a direction
 *                     is held, shrinks when released
 *   mario_speed_mode  0 standing, 2 walking
 *   mario_step_toggle flips every step: the speed table has fractional steps
 */
#include "player_priv.h"

/* How far Mario moves this frame (00:1EB4): 0, 1 or 2 pixels depending on
   speed_mode + the alternating step bit. The table at 1ECE is 0 1 1 1 1 2. */
uint8_t walk_step(void)
{
    uint8_t mode = mario_speed_mode;
    uint8_t toggle = mario_step_toggle ^ 1;
    mario_step_toggle = toggle;
    return rom_byte(0, (uint16_t)(0x1ECE + (uint8_t)(mode + toggle)));
}

/* Move the sprites 12-19 (Mario's companions in the OAM buffer: the
   superballs, block fragments...) left by `pixels` while the screen scrolls
   (00:1EA4). */
void shift_object_sprites(uint8_t pixels)
{
    uint16_t x = ADDR_OAM_BUFFER + 0x0C * 4 + 1;
    uint8_t n = 8;
    do {
        shared_write(x, (uint8_t)(shared_read(x) - pixels));
        x += 4;
    } while (taken(--n != 0));
}

/* Mario turns around: momentum 8 makes him skid (00:1E48) */
static void turn_around(void)
{
    mario_walk_dir = 1;
    mario_momentum = 8;
    if (taken(mario_jump_state != 0))
        return;                                 /* no skid animation in the air */
    mario_anim = (uint8_t)((mario_anim & 0xF0) | 0x05);
    mario_walk_counter = 1;
}

/* Standing still on the ground: first animation frame (00:1D71) */
static void stand_still(void)
{
    if (taken(mario_jump_state != 0))
        return;
    mario_anim &= 0xF0;
    mario_walk_counter = 1;
    mario_speed_mode = 0;
}

/* Keep the walk counter going and finish the frame (00:1E1C) */
static void count_walk_frame(void)
{
    mario_walk_counter++;
}

/* Up to `momentum` 6: one more step of acceleration in this direction;
   a crouching Mario starts walking (stands up) (00:1DC1 / 00:1E7A). */
static void accelerate(uint8_t button, uint8_t direction)
{
    if (taken((joy_held & button) == 0))
        return;
    if (!taken(mario_anim != 0x18))
        mario_anim = (uint8_t)((mario_anim & 0xF0) | 0x01);
    if (taken(mario_momentum == 6))
        return;
    mario_momentum++;
    mario_walk_dir = direction;
}

/* Right button held (00:1DAC). Mario moves right; when he is past the middle
   of the screen the screen scrolls instead, taking the enemies and the
   sprites along. At the end of a level (level_end_counter) the screen
   stops and Mario walks off to the right. */
static void walk_right(void)
{
    if (!taken(mario_walk_dir != 0x20)) {
        turn_around();                          /* was walking left */
        return;
    }
    mario_facing = 0;
    if (taken(mario_side_collision() != 0))
        return;
    accelerate(0x10, 0x10);

    bool scrolls = false;
    if (taken(underground != 0)) {
        scrolls = false;
    } else {
        bool free_scroll;
        if (!taken(level_end_counter < 7))
            free_scroll = !taken((read_scroll_x() & 0x0C) == 0);
        else
            free_scroll = true;
        if (free_scroll && !taken(0x50 >= mario_x))
            scrolls = true;
    }

    if (scrolls) {
        uint8_t step = reg_b(walk_step());
        shared_write(ADDR_SCROLL_X, (uint8_t)(shared_read(ADDR_SCROLL_X) + step));
        shift_object_sprites(step);
        scroll_enemies(step);
        uint16_t x = ADDR_OAM_BUFFER + 1;       /* the three superball sprites */
        uint8_t n = 3;
        do {
            shared_write(x, (uint8_t)(shared_read(x) - step));
            x += 4;
        } while (taken(--n != 0));
        count_walk_frame();
        return;
    }

    mario_x = (uint8_t)(mario_x + walk_step());
    if (taken(game_state() == STATE_AUTOSCROLL)) {
        count_walk_frame();
        return;
    }
    if (taken(level_end_counter == 0)) {
        count_walk_frame();
        return;
    }
    shared_write(ADDR_SCROLL_X, shared_read(ADDR_SCROLL_X) & 0xFC);
    if (taken(mario_x < 0xA0)) {
        count_walk_frame();
        return;
    }
    mario_reaches_goal();                       /* walked off the right edge */
}

/* Left button held (00:1E40). Mario cannot walk off the left edge (x < 15) */
static void walk_left(void)
{
    if (!taken(mario_walk_dir != 0x10)) {
        turn_around();                          /* was walking right */
        return;
    }
    mario_facing = 0x20;
    if (taken(mario_side_collision() != 0))
        return;
    if (!taken(mario_x < 0x0F)) {
        accelerate(0x20, 0x20);
        mario_x = (uint8_t)(mario_x - walk_step());
    }
    mario_walk_counter--;
}

/*
 * Read the d-pad and walk (00:1D26), every frame in normal gameplay. If no
 * direction is held but Mario still has momentum, he keeps going in his
 * last direction while the momentum runs down; down makes big Mario crouch.
 */
void mario_walk(void)
{
    if (!taken(mario_walk_dir != 1)) {
        /* turning around: wait out the momentum */
        if (taken(mario_momentum != 0)) {
            mario_momentum--;
            return;
        }
        mario_walk_dir = 0;
        stand_still();
        return;
    }
    if (!taken(mario_momentum != 6)) {
        if (!taken(mario_speed_mode != 0))
            mario_speed_mode = 2;                   /* top momentum: at least walking pace */
    }

    uint8_t keys = joy_held;
    if (taken(keys & 0x80)) {                   /* down */
        if (!taken(super_status != SUPER_BIG) && !taken(mario_jump_state != 0)) {
            mario_anim = 0x18;                  /* crouching */
            if (taken((joy_held & 0x30) != 0) || taken(mario_momentum == 0)) {
                mario_momentum = 0;
                return;
            }
        }
    }
    for (;;) {
        if (taken(keys & 0x10)) {
            walk_right();
            return;
        }
        if (taken(keys & 0x20)) {
            walk_left();
            return;
        }
        if (taken(mario_momentum == 0))
            break;
        /* no button: coast on in the walking direction while slowing down */
        mario_speed_mode = 0;
        mario_momentum--;
        keys = mario_walk_dir;
    }
    mario_walk_dir = 0;
    if (taken(mario_jump_state != 0))
        return;
    stand_still();
}

/*
 * Animate Mario and let him walk (00:16F5): draws him, then, while he is on
 * the ground and not in one of the special poses (frame 10 or more:
 * submarine, plane), advances the walking animation every 4th frame of
 * walking, and finally handles the d-pad.
 */
void mario_animate_and_walk(void)
{
    draw_mario();
    if (!taken(mario_on_ground == 0) && !taken((mario_anim & 0x0F) >= 0x0A)) {
        bool advance;
        if (taken(mario_speed_mode == 0x23))     /* never true in practice: speed modes are 0/2/4 */
            advance = !taken((mario_walk_counter & 1) != 0);
        else
            advance = !taken((mario_walk_counter & 3) != 0);
        if (advance && !taken(mario_anim == 0x18)) {
            mario_anim++;
            if (!taken((mario_anim & 0x0F) < 4))     /* three walking frames: back to the first */
                mario_anim = (uint8_t)((mario_anim & 0xF0) | 0x01);
        }
    }
    mario_walk();
}

/* ------------------------------------------------------------------ */
/* Jumping                                                             */

/*
 * (03:48FC) A jump arc step waiting in mario_c209 replaces the current one,
 * but only early in the arc (step below 15); it is consumed either way only
 * then.
 */
void apply_pending_jump_step(void)
{
    uint8_t pending = reg_b(mario_c209);
    if (taken(pending == 0))
        return;
    if (taken(mario_jump_step >= 0x0F))
        return;
    mario_jump_step = pending;
    mario_c209 = 0;
}

/*
 * Move a jumping object (Mario at C200, or one of the objects at C210-C240)
 * one step along its jump arc (03:490D). The arc table (always JUMP_ARC
 * 00:216D) holds the pixels moved per frame, ending with $7F at the top.
 * State byte +7: 0 not jumping, 1 rising (walk the table forwards, moving
 * up), 2 falling (walk it backwards, moving down). Byte +8 is the step;
 * byte +1 the Y position. The fall ends at step $FF.
 */
void move_along_jump_arc(uint16_t obj, uint16_t arc)
{
    uint8_t step = ram_read((uint16_t)(obj + 8));
    uint8_t state = ram_read((uint16_t)(obj + 7));
    uint16_t y = (uint16_t)(obj + 1);
    if (taken(state == 0))
        return;
    uint16_t entry;
    if (!taken(state == 2)) {
        entry = (uint16_t)(arc + step);
        if (!taken(mem_read(entry) == 0x7F)) {
            ram_write(y, (uint8_t)(ram_read(y) - mem_read(entry)));
            ram_write((uint16_t)(obj + 8), (uint8_t)(step + 1));
            return;
        }
        /* top of the arc: start falling from the step before the $7F */
        entry = (uint16_t)(entry - 1);
        step = (uint8_t)(step - 1);
        ram_write((uint16_t)(obj + 7), 2);
    } else {
        if (taken(step == 0xFF)) {
            /* landed back at the start of the arc */
            ram_write((uint16_t)(obj + 7), 0);
            ram_write((uint16_t)(obj + 8), 0);
            return;
        }
        entry = (uint16_t)(arc + step);
        if (taken(mem_read(entry) == 0x7F)) {
            entry = (uint16_t)(entry - 1);
            step = (uint8_t)(step - 1);
        }
    }
    ram_write(y, (uint8_t)(ram_read(y) + mem_read(entry)));
    ram_write((uint16_t)(obj + 8), (uint8_t)(step - 1));
}
