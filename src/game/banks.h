/*
 * ROM bank switching the way the original does it (its SAVE_AND_SWITCH_ROM_BANK
 * and RESTORE_ROM_BANK macros): the bank number is kept in active_rom_bank
 * so that the timer interrupt, which maps bank 3 for the music, can restore
 * it afterwards.
 *
 * Synchronize the active-bank byte as well as the MBC write. Otherwise C
 * can change the byte while the shadow is still finishing a banked callee;
 * a timer interrupt there would restore the next bank too early and make
 * the shadow fetch the callee's remaining instructions from the wrong ROM.
 */
#ifndef SML_BANKS_H
#define SML_BANKS_H

#include "ram.h"

static inline void switch_rom_bank_saving(uint8_t bank)
{
    saved_rom_bank = shared_read(ADDR_ACTIVE_ROM_BANK);
    shared_write(ADDR_ACTIVE_ROM_BANK, bank);
    set_rom_bank(bank);
}

static inline void restore_rom_bank(void)
{
    shared_write(ADDR_ACTIVE_ROM_BANK, saved_rom_bank);
    set_rom_bank(saved_rom_bank);
}

#endif
