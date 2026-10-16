/*
 * RetroAchievements (rcheevos rc_client) for the SDL frontend.
 *
 * The port keeps the Game Boy's memory map, so the official Super Mario Land
 * set (game 504, the v1.1 ROM's MD5) works unchanged: after every emulated
 * frame the achievement conditions read the machine's memory with
 * machine_peek, which has no side effects. Nothing here writes to the game,
 * so stock TAS playback is identical with achievements on or off.
 *
 * Softcore only: the server is reached with the system's curl in a worker
 * thread (curl.exe on Windows), answers are handed to rc_client on the main
 * thread. The login token, never the password, is kept in the data
 * directory (retroachievements.login).
 *
 * Testing: with SML_RA_FAKE=<dir> no network is used; each request "r=NAME"
 * is answered with <dir>/NAME.json, and an automatic login as "test" is
 * made. The test set lives in verify/tests/ra_fake/.
 */
#ifndef SML_ACHIEVEMENTS_H
#define SML_ACHIEVEMENTS_H

#include <stddef.h>
#include <stdint.h>
#include "../hw/machine.h"

/* Starts the client: signs in with the stored token (if any) and loads the
   set for this ROM. data_dir ends with a separator (or is ""). */
void ach_start(const char *data_dir, const char *version, const Machine *m, const uint8_t *rom,
               size_t rom_size);
void ach_stop(void);
int ach_running(void);

/* After every emulated frame; while the game stands still (menu). Both also
   deliver finished server answers. */
void ach_frame(void);
void ach_idle(void);

/* Print every unlock to stdout ("achievement ID frame N TITLE"). */
void ach_set_report(int on);

/* Signs in with a password (asynchronous; see ach_status). */
void ach_login(const char *user, const char *password);
void ach_logout(void);
int ach_logged_in(void);
/* One line about the login and the loaded set. */
void ach_status(char *out, size_t len);

/* Takes the oldest message for the player ("Unlocked: ..."); returns how
   many were waiting (0: none). */
int ach_take_message(char *out, size_t len);
int ach_messages_waiting(void);
/* The latest progress of a measured achievement ("Coins 12/30"), once;
   "" when it should be hidden. */
int ach_take_progress(char *out, size_t len);

typedef struct {
    char title[80];
    char description[200];
    char progress[24];          /* "12/50", or "" */
    unsigned points;
    int unlocked;
} AchRow;

/* Fills up to max rows (locked first, then unlocked); returns how
   many; summary gets "N of M unlocked, P of Q points". */
int ach_list(AchRow *rows, int max, char *summary, size_t len);

#endif
