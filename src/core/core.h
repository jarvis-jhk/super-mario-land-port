/*
 * The game core: everything needed to run Super Mario Land without any
 * platform specifics (no window, no audio device, no files).
 *
 * The platform layer (src/platform) owns the main loop of the program in the
 * usual sense, but the *game* has its own endless main loop, exactly like
 * the original. So the core calls back into the platform:
 *
 *   - frame():      once per video frame (start of vertical blank, or every
 *                   70224 cycles while the game has the LCD switched off).
 *                   The platform presents core_framebuffer(), plays the audio
 *                   produced so far, handles OS events and throttles to
 *                   59.73 fps. It may call core_quit() to leave core_run().
 *   - poll_input(): whenever the game reads the joypad (once per game
 *                   frame). Returns the buttons held (BTN_* bits). Movies
 *                   and TAS playback plug in here.
 */
#ifndef SML_CORE_H
#define SML_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "../hw/machine.h"

typedef struct {
    void *user;
    void (*frame)(void *user);
    uint8_t (*poll_input)(void *user);
} CoreCallbacks;

/* The ROM image must stay valid while the core runs. Returns after
   core_quit(). */
void core_run(MachineModel model, const uint8_t *rom, size_t rom_size,
              const CoreCallbacks *callbacks);
void core_quit(void);

/* The hardware state (for the platform: framebuffer, audio buffer; for
   tools: memory). */
Machine *core_machine(void);

/* False in no-ROM CI builds, which lack the locally generated access table
   and cannot run the game. */
int core_has_access_table(void);

/* Number of joypad polls (= game frames) so far. */
long core_poll_count(void);

/* ROM identification */
typedef enum { ROM_UNKNOWN, ROM_SML_V11, ROM_SML_V10 } RomVersion;
RomVersion core_identify_rom(const uint8_t *rom, size_t size);
const char *core_rom_description(RomVersion v);

#endif
