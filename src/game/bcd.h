/* Binary coded decimal arithmetic, bit-exact with the SM83's ADD/ADC + DAA. */
#ifndef SML_BCD_H
#define SML_BCD_H
#include <stdint.h>

/* Returns the DAA-adjusted a + b + *carry and sets *carry to the carry out. */
static inline uint8_t bcd_add(uint8_t a, uint8_t b, int *carry)
{
    int sum = a + b + *carry;
    int half = ((a & 0x0F) + (b & 0x0F) + *carry) > 0x0F;
    int c = sum > 0xFF;
    int r = sum & 0xFF;
    if (c || r > 0x99) {
        r += 0x60;
        c = 1;
    }
    if (half || (r & 0x0F) > 0x09)
        r += 0x06;
    *carry = c;
    return (uint8_t)r;
}

/* Returns the DAA-adjusted a - b (SUB followed by DAA) and sets *borrow. */
static inline uint8_t bcd_sub(uint8_t a, uint8_t b, int *borrow)
{
    int half = (a & 0x0F) < (b & 0x0F);
    int c = a < b;
    int r = (a - b) & 0xFF;
    if (c) r -= 0x60;
    if (half) r -= 0x06;
    *borrow = c;
    return (uint8_t)r;
}

#endif
