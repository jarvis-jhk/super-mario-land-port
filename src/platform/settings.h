/*
 * Frontend settings: video, audio and input bindings, stored as a small
 * INI-style text file (see settings_save() for the format and defaults).
 *
 * Bindings are kept as names (SDL key names such as "X", "Right Shift",
 * SDL controller button names such as "a", "dpright"); the frontend
 * resolves them. This module has no SDL dependency so it can be unit-tested.
 * Settings are presentation only and never change game state.
 */
#ifndef SML_SETTINGS_H
#define SML_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

/* The first eight actions are the Game Boy buttons, in BTN_* bit order. */
typedef enum {
    ACT_A, ACT_B, ACT_SELECT, ACT_START, ACT_RIGHT, ACT_LEFT, ACT_UP, ACT_DOWN,
    ACT_FAST_FORWARD, ACT_FULLSCREEN, ACT_SCALING, ACT_PALETTE, ACT_SCREENSHOT,
    ACT_QUIT,
    ACT_COUNT
} Action;
#define ACT_GAME_BUTTONS 8

#define BIND_MAX 4
#define BIND_NAME 32

typedef enum { SCALE_INTEGER, SCALE_SHARP, SCALE_NEAREST, SCALE_MODES } ScalingMode;
typedef enum { SYNC_AUTO, SYNC_CLOCK, SYNC_VSYNC, SYNC_MODES } SyncMode;

#define PALETTE_PRESETS 3

typedef struct {
    /* video */
    int scale;                  /* initial window size, multiples of 160x144 */
    ScalingMode scaling;
    int fullscreen;
    int vsync;
    SyncMode sync;
    uint32_t palette[4];        /* 0xFFRRGGBB, lightest first */
    /* audio */
    int mute;
    int volume;                 /* 0..100 */
    int latency_ms;             /* wanted device queue */
    /* input */
    char keys[ACT_COUNT][BIND_MAX][BIND_NAME];
    char buttons[ACT_COUNT][BIND_MAX][BIND_NAME];
    int stick;                  /* left stick acts as the D-pad */
    int deadzone;               /* 0..32767 */
    int fast_forward_speed;     /* 0 = unlimited, else multiple of normal speed */
} Settings;

void settings_defaults(Settings *s);

/* Parses one "key = value" assignment in a section ("video", "audio",
   "keyboard", "controller"). Returns 0, or -1 with a message in err. */
int settings_set(Settings *s, const char *section, const char *key, const char *value,
                 char *err, size_t err_len);

/* Loads a settings file over the current values. Returns 0, 1 if the file
   does not exist, or -1 with "file:line: message" in err (the remaining
   lines are still applied). */
int settings_load(Settings *s, const char *path, char *err, size_t err_len);

/* Writes all settings with comments. Returns 0 on success. */
int settings_save(const Settings *s, const char *path);

const char *settings_action_name(Action a);
const char *settings_scaling_name(ScalingMode m);
const char *settings_sync_name(SyncMode m);
/* Preset palettes by index (green, gray, pocket) and name. */
const char *settings_palette_name(int preset);
int settings_palette_preset(const char *name, uint32_t out[4]);

#endif
