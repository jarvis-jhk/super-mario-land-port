/* The status bar at the top of the screen: score, coins, lives. */
#include "bcd.h"
#include "game.h"
#include "ram.h"
#include "sound.h"
#include "level.h"

/* Background tiles 0-9 are the digits, $2C is a blank. */
enum { TILE_0 = 0x00, TILE_SPACE = 0x2C };

/* Positions in the background map (row 0: lives, row 1: score and coins) */
enum {
    MAP_LIVES_TENS = 0x9806, MAP_LIVES_ONES = 0x9807,
    MAP_SCORE      = 0x9820,
    MAP_COINS_TENS = 0x9829, MAP_COINS_ONES = 0x982A,
};

enum { GAME_STATE_GAME_OVER = 0x39 };

/* Copy the two visible rows of the HUD template into the map (00:060F).
   Each map row is 32 tiles wide, but only the first 20 are on screen. */
void prepare_hud(void)
{
    uint16_t src = 0x3F9C;
    uint8_t row = 0;
    do {
        uint8_t column = 0;
        do {
            vram_write((uint16_t)(0x9800 + row * 32 + column), rom_byte(0, src++));
        } while (taken(++column != 20));
    } while (taken(++row != 2));
}

/*
 * Apply a pending life gain or loss (vblank handler, 00:1C33). Running out
 * of lives starts the game over sequence; lives stop at 99.
 */
void update_lives(void)
{
    if (taken(demo_mode != 0))
        return;
    uint8_t change = lives_change;
    if (taken(change == 0))
        return;
    uint8_t lives = lives_bcd;
    int carry = 0;
    if (taken(change == 0xFF)) {
        if (taken(lives == 0)) {
            game_state_isr = GAME_STATE_GAME_OVER;
            game_over_state_copy = GAME_STATE_GAME_OVER;
            lives_change = 0;
            return;
        }
        lives = bcd_sub(lives, 1, &carry);
    } else {
        if (taken(lives == 0x99)) {
            lives_change = 0;
            return;
        }
        /* interrupt context: the sound mailboxes are accessed directly */
        WRAM(SOUND_SFX_REQUEST) = SFX_ONE_UP;
        HRAM(0xFFD3) = SFX_ONE_UP;
        lives = bcd_add(lives, 1, &carry);
    }
    lives_bcd = lives;
    display_lives();
}

/* Draw the lives counter and clear the pending change (00:1C56). */
void display_lives(void)
{
    uint8_t lives = reg_b(lives_bcd);
    vram_write(MAP_LIVES_ONES, lives & 0x0F);
    vram_write(MAP_LIVES_TENS, lives >> 4);
    shared_write(ADDR_LIVES_CHANGE, 0);
}

/* Draw the coin counter (00:1C1B). */
void display_coins(void)
{
    uint8_t coins = reg_b(coins_bcd);
    vram_write(MAP_COINS_ONES, coins & 0x0F);
    vram_write(MAP_COINS_TENS, coins >> 4);
    shared_write(ADDR_HUD_COIN_FLAG, 0);
    hud_coins_drawn = 1;
}

/*
 * Print a six-digit BCD number stored at src (most significant byte first,
 * going down in memory) to the background map at dst (00:3F4D). Leading
 * zeroes become blanks, but a value of zero prints a single "0".
 */
void print_bcd6(uint16_t src, uint16_t dst)
{
    shared_write(ADDR_SCORE_CHANGED, 0);              /* here: "a nonzero digit came already" */
    uint8_t pairs = 3;
    do {
        uint8_t pair = reg_b(shared_or_ram_read(src));
        uint8_t tile;
        if (taken((pair >> 4) != 0)) {
            shared_write(ADDR_SCORE_CHANGED, 1);
            tile = pair >> 4;
        } else {
            tile = taken(shared_read(ADDR_SCORE_CHANGED) != 0) ? TILE_0 : TILE_SPACE;
        }
        vram_write(dst++, tile);

        if (taken((pair & 0x0F) != 0)) {
            shared_write(ADDR_SCORE_CHANGED, 1);
            tile = pair & 0x0F;
        } else if (taken(shared_read(ADDR_SCORE_CHANGED) != 0)) {
            tile = TILE_0;
        } else {
            tile = taken(pairs == 1) ? TILE_0 : TILE_SPACE;   /* the very last digit */
        }
        vram_write(dst++, tile);
        src = (uint16_t)((src & 0xFF00) | ((src - 1) & 0xFF));
    } while (taken(--pairs != 0));
    shared_write(ADDR_SCORE_CHANGED, 0);
}

/* Redraw the score if it changed (vblank handler, 00:3F39). */
void display_score(void)
{
    if (taken(score_changed == 0))
        return;
    if (taken(hud_coins_drawn != 0))
        return;
    if (taken(column_state == COLUMN_DRAWN))  /* a column was drawn this frame: no time left */
        return;
    print_bcd6(0xC0A2, MAP_SCORE);
}

/* Where the three timer digits go (row 1, right side). */
enum { MAP_TIMER_ONES = 0x9833 };

/*
 * Redraw the level timer once per time unit (vblank handler, 00:3D6A). The
 * timer only counts in the normal game states (below $12), not during the
 * game over sequence.
 */
void display_timer(void)
{
    if (taken(game_over_state_copy != 0))
        return;
    if (taken(game_state_isr >= 0x12))
        return;
    if (taken(game_timer[0] != 40))     /* one time unit is 40 frames */
        return;
    print_timer();
}

/* Draw the three-digit timer (00:3D7E). */
void print_timer(void)
{
    uint16_t dst = MAP_TIMER_ONES;
    uint8_t digits = reg_b(shared_read(ADDR_GAME_TIMER + 1));
    vram_write(dst, digits & 0x0F);
    dst = (uint16_t)((dst & 0xFF00) | ((dst - 1) & 0xFF));
    vram_write(dst, digits >> 4);
    dst = (uint16_t)((dst & 0xFF00) | ((dst - 1) & 0xFF));
    vram_write(dst, shared_read(ADDR_GAME_TIMER + 2) & 0x0F);
}
