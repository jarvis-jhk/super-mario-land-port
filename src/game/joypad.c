/* Reading the joypad. */
#include "game.h"
#include "ram.h"

enum {
    P1_SELECT_DPAD    = 0x20,   /* bit 4 low: the P1 register shows Right, Left, Up, Down */
    P1_SELECT_BUTTONS = 0x10,   /* bit 5 low: it shows A, B, Select, Start */
    P1_DESELECT       = 0x30,
};

/*
 * Read the joypad, once per frame (03:47F2). The d-pad goes to the upper
 * nibble of joy_held, the buttons to the lower one; joy_pressed gets the
 * buttons that were not held at the previous read.
 */
void read_joypad(void)
{
    io_write(rP1, P1_SELECT_DPAD);
    io_read(rP1);                       /* read twice, letting the lines settle */
    uint8_t dpad = reg_b((uint8_t)((~io_read(rP1) & 0x0F) << 4));

    io_write(rP1, P1_SELECT_BUTTONS);
    for (int i = 0; i < 5; i++)         /* read six times */
        io_read(rP1);
    uint8_t held = (uint8_t)((~io_read(rP1) & 0x0F) | dpad);

    joy_pressed = (joy_held ^ held) & held;
    joy_held = held;
    io_write(rP1, P1_DESELECT);
}
