/* The level timer and the "floaties": score numbers and coins floating up. */
#include "game.h"
#include "ram.h"
#include "bcd.h"
#include "level.h"

enum {
    TIMER_FRAMES_PER_UNIT = 0x28,   /* a time unit is 40 frames, not a second */
    FLOATY_FIRST_OAM      = 0x30,   /* four floaties of two sprites each: 30, 38, 40, 48 */
    FLOATY_END_OAM        = 0x50,
    FLOATY_TTL            = 0x20,   /* moves up 32 times, then disappears */
    FLOATY_COIN           = 0xC0,   /* floaty_control / floaty_is_coin: a coin */
    TILE_COIN_FIRST       = 0xF6,   /* the coin spins through F6, F7, F8 */
    TILE_COIN_END         = 0xF9,
};

static uint16_t oam(uint8_t offset) { return (uint16_t)(ADDR_OAM_BUFFER + offset); }

/*
 * Count the level timer down (02:584B), one unit every 40 frames. At 100 and
 * at 50 units left the music gets faster (the timer interrupt that drives it
 * is sped up through TMA); at 0 timer_expiring_flag becomes 3 and the timer
 * stops. The hundreds are only decremented when the lower digits wrap to 99,
 * and the checks run every time unit, as in the original.
 */
void update_game_timer(void)
{
    if (taken(timer_expiring_flag == 3))
        return;
    uint8_t frames = (uint8_t)(shared_read(ADDR_GAME_TIMER) - 1);
    shared_write(ADDR_GAME_TIMER, frames);
    if (taken(frames != 0))
        return;
    shared_write(ADDR_GAME_TIMER, TIMER_FRAMES_PER_UNIT);
    uint8_t low = shared_read(ADDR_GAME_TIMER + 1);
    uint8_t hundreds = shared_read(ADDR_GAME_TIMER + 2);
    int borrow;
    low = bcd_sub(low, 1, &borrow);
    shared_write(ADDR_GAME_TIMER + 1, low);
    if (!taken(low != 0x99)) {
        shared_write(ADDR_GAME_TIMER + 2, (uint8_t)(hundreds - 1));
        return;
    }
    if (taken(low == 0x50)) {
        if (taken(hundreds != 0))
            return;
        timer_expiring_flag = 2;
        io_write(rTMA, 0x50);
        return;
    }
    if (taken(low != 0))
        return;
    if (taken(hundreds != 0)) {
        if (taken(hundreds != 1))
            return;
        timer_expiring_flag = 1;
        io_write(rTMA, 0x30);
        return;
    }
    timer_expiring_flag = 3;
}

/* Instruction addresses only: comparison operands and sprite/score values
   are read from the player's ROM, rather than copied into generated tables. */
static const uint16_t floaty_tile_tests[] = {
    0x5913, 0x5918, 0x591D, 0x5922, 0x5927, 0x592E,
    0x5933, 0x5938, 0x593D, 0x5942, 0x5949,
};
static const uint16_t floaty_score_tests[] = {
    0x5968, 0x596D, 0x5973, 0x5978, 0x597E,
    0x5984, 0x598A, 0x5990, 0x5996, 0x599C,
};

/* Start a new floaty in the next of the four slots (02:5899): a score
   number (and its points) or a coin at floaty_x/floaty_y. */
static void spawn_floaty(uint8_t control)
{
    uint8_t slot_oam = next_floaty_oam;
    next_floaty_oam = (uint8_t)(slot_oam + 8);
    if (!taken(next_floaty_oam != FLOATY_END_OAM))
        next_floaty_oam = FLOATY_FIRST_OAM;

    int slot;
    if (!taken(slot_oam != 0x30)) slot = 0;
    else if (!taken(slot_oam != 0x38)) slot = 1;
    else if (!taken(slot_oam != 0x40)) slot = 2;
    else slot = 3;
    floaty_ttl[slot] = FLOATY_TTL;
    floaty_coin_tile[slot] = TILE_COIN_FIRST;
    if (!taken(control != FLOATY_COIN))
        floaty_is_coin[slot] = FLOATY_COIN;

    shared_write(oam(slot_oam), floaty_y);
    shared_write(oam(slot_oam + 1), floaty_x);
    uint8_t left = rom_byte(2, 0x594F), right = rom_byte(2, 0x594E);
    for (unsigned i = 0; i < sizeof floaty_tile_tests / sizeof floaty_tile_tests[0]; i++) {
        if (taken(control == rom_byte(2, floaty_tile_tests[i] + 1))) {
            if (i < 5) {
                left = (uint8_t)(rom_byte(2, 0x5912) + i);
                right = rom_byte(2, 0x5911);
            } else if (i < 10) {
                left = (uint8_t)(rom_byte(2, 0x592C) + i - 5);
                right = (uint8_t)(rom_byte(2, 0x5911) - 1);
            } else {
                left = (uint8_t)(rom_byte(2, 0x592C) + 5);
                right = rom_byte(2, 0x5948);
            }
            break;
        }
    }
    /* left sprite: Y, X, tile (attributes untouched); right sprite 8 pixels on */
    shared_write(oam(slot_oam + 2), left);
    shared_write(oam(slot_oam + 4), floaty_y);
    shared_write(oam(slot_oam + 5), (uint8_t)(floaty_x + 8));
    shared_write(oam(slot_oam + 6), right);
    floaty_control = 0;
    floaty_y = 0;
    floaty_x = 0;

    for (unsigned i = 0; i < sizeof floaty_score_tests / sizeof floaty_score_tests[0]; i++) {
        if (taken(control == rom_byte(2, floaty_score_tests[i] + 1))) {
            uint8_t high = i < 4 ? (uint8_t)(rom_byte(2, 0x5967) + i + (i >= 2))
                                 : rom_byte(2, floaty_score_tests[i] - 1);
            add_score((uint16_t)(high << 8 | rom_byte(2, 0x5966)));
            break;
        }
    }
}

/* Move one visible floaty up (02:59AE): coins every frame, numbers every
   second frame. A coin spins through its three tiles. After 32 moves the
   floaty disappears and its slot is reset. */
static void move_floaty(uint8_t slot_oam)
{
    int slot;
    if (taken(slot_oam == 0x48)) slot = 3;
    else if (taken(slot_oam == 0x40)) slot = 2;
    else if (taken(slot_oam == 0x38)) slot = 1;
    else slot = 0;

    if (!taken(floaty_is_coin[slot] == FLOATY_COIN)) {
        uint8_t step = (uint8_t)(floaty_step[slot] + 1);
        floaty_step[slot] = step;
        if (taken(step != 2))
            return;
        floaty_step[slot] = 0;
    }

    uint16_t sprite = oam(slot_oam);
    shared_write(sprite, (uint8_t)(shared_read(sprite) - 1));
    shared_write(sprite + 4, (uint8_t)(shared_read(sprite + 4) - 1));
    uint8_t tile = shared_read(sprite + 2);
    if (!taken(tile < TILE_COIN_FIRST)) {
        /* a coin spins back and forth: F7, F8, F7, F6, F7 ... (on the
           way back floaty_coin_tile is left at F9 for one step) */
        tile = (uint8_t)(floaty_coin_tile[slot] + 1);
        floaty_coin_tile[slot] = tile;
        shared_write(sprite + 2, tile);
        if (!taken(tile < TILE_COIN_END)) {
            tile = (uint8_t)(tile - 2);
            shared_write(sprite + 2, tile);
            if (!taken(tile == 0xF7)) {
                tile = (uint8_t)(tile - 2);
                floaty_coin_tile[slot] = tile;
                shared_write(sprite + 2, tile);
            }
        }
    }

    uint8_t ttl = (uint8_t)(floaty_ttl[slot] - 1);
    floaty_ttl[slot] = ttl;
    if (taken(ttl != 0))
        return;
    floaty_ttl[slot] = FLOATY_TTL;
    floaty_coin_tile[slot] = TILE_COIN_FIRST;
    /* hide both sprites: left tile, X, Y; then right Y, X, tile */
    shared_write(sprite + 2, 0);
    shared_write(sprite + 1, 0);
    shared_write(sprite, 0);
    shared_write(sprite + 4, 0);
    shared_write(sprite + 5, 0);
    shared_write(sprite + 6, 0);
    uint8_t last = (uint8_t)(slot_oam + 6);
    int reset;
    if (taken(last == 0x36)) reset = 0;
    else if (taken(last == 0x3E)) reset = 1;
    else if (taken(last == 0x46)) reset = 2;
    else reset = 3;
    floaty_is_coin[reset] = 0;
    floaty_step[reset] = 0;
}

/*
 * The floating score numbers and coins (02:5892). Whoever awards points
 * (stomps, blocks, the goal) or a coin sets floaty_control, floaty_x and
 * floaty_y; here a new floaty is started from them, the points are added,
 * and all visible floaties move up.
 */
void update_floaties(void)
{
    uint8_t control = reg_b(floaty_control);
    if (!taken(control == 0))
        spawn_floaty(control);
    uint8_t slot_oam = FLOATY_FIRST_OAM;
    do {
        if (!taken(shared_read(oam(slot_oam)) == 0))
            move_floaty(slot_oam);
        slot_oam = (uint8_t)(slot_oam + 8);
    } while (taken(slot_oam != FLOATY_END_OAM));
}

/* Once per frame during the level (02:5844). */
void update_timer_and_floaties(void)
{
    update_game_timer();
    update_floaties();
}
