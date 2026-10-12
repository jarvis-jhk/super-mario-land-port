/* The score. */
#include "bcd.h"
#include "game.h"
#include "ram.h"

/*
 * Add points to the six-digit BCD score (00:0166). bcd_points holds four BCD
 * digits; no single award is larger than 9999. Nothing is scored during the
 * demo, and the score stops at 999999.
 */
void add_score(uint16_t bcd_points)
{
    if (taken(shared_read(ADDR_DEMO_MODE) != 0))
        return;
    int carry = 0;
    shared_write(ADDR_SCORE, bcd_add(shared_read(ADDR_SCORE), bcd_points & 0xFF, &carry));
    shared_write(ADDR_SCORE + 1, bcd_add(shared_read(ADDR_SCORE + 1), bcd_points >> 8, &carry));
    shared_write(ADDR_SCORE + 2, bcd_add(shared_read(ADDR_SCORE + 2), 0, &carry));
    shared_write(ADDR_SCORE_CHANGED, 1);
    if (taken(!carry))
        return;
    for (int i = 2; i >= 0; i--)
        shared_write((uint16_t)(ADDR_SCORE + i), 0x99);
}
