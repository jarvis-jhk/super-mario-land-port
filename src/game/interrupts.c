/* Frame rendering, the HUD split and the timer/sound interrupt. */
#include "game.h"
#include "ram.h"

/* Copy the sprite buffer and wait for DMA to finish (00:FFB6, copied from
   00:3F92). The forty-iteration delay must remain a timed branch loop. */
void dma_sprites(void)
{
    io_write(rDMA, 0xC0); loop_taken(40);
}

/* Draw the pending map, HUD and sprites at vertical blank (00:0040).
   B is retained when DrawColumn has no work; the bouncing-block path of
   BlockEvents observes it, so the adapter supplies the interrupted value. */
void vblank_interrupt(uint8_t saved_b)
{
    uint8_t b = column_state == COLUMN_READY ? 0 : saved_b;
    draw_column(); block_events(b); update_lives(); dma_sprites();
    display_score(); display_timer(); animate_background();
    frame_counter++;
    if (!taken(game_state_isr != 0x3A)) io_write(rLCDC, io_read(rLCDC) | 0x20);
    io_write(rSCX, 0); io_write(rSCY, 0);
    HRAM(0xFF85) = 1;
}

/* Split the scrolling playfield below the HUD, and scroll the game-over
   window upwards with a second STAT interrupt (00:0048). */
void stat_interrupt(void)
{
    while (taken((io_read(rSTAT) & 3) != 0)) ;
    if (taken(WRAM(0xC0A5) != 0)) {
        io_write(rLCDC, io_read(rLCDC) & ~0x20);
        io_write(rLYC, 15); WRAM(0xC0A5) = 0; return;
    }
    io_write(rSCX, scroll_x);
    if (!taken(WRAM(0xC0DE) == 0)) io_write(rSCY, WRAM(0xC0DF));
    if (taken(game_state_isr != 0x3A)) return;
    uint8_t y = io_read(rWY);
    if (taken(y == 0x40)) {
        uint8_t timer = HRAM(0xFFFB);
        if (taken(timer == 0)) WRAM(0xC0AD) = 0xFF;
        else HRAM(0xFFFB) = timer - 1;
    } else {
        io_write(rWY, io_read(rWY) - 1);
        if (taken(y >= 0x87)) return;
    }
    io_write(rLYC, y + 8); WRAM(0xC0A5) = y + 8;
}

/* Run the sound engine in bank 3, then restore the main program's bank (00:0050). */
void timer_interrupt(void)
{
    set_rom_bank(3); sound_timer_tick(); set_rom_bank(active_rom_bank);
}
