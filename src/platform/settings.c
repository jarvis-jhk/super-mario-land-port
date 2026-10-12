/* Frontend settings file, see settings.h. */
#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const action_names[ACT_COUNT] = {
    "a", "b", "select", "start", "right", "left", "up", "down",
    "fast_forward", "fullscreen", "scaling", "palette", "screenshot", "quit",
};

static const char *const scaling_names[SCALE_MODES] = { "integer", "sharp", "nearest" };
static const char *const sync_names[SYNC_MODES] = { "auto", "clock", "vsync" };

static const struct { const char *name; uint32_t c[4]; } palettes[PALETTE_PRESETS] = {
    { "green",  { 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820 } },
    { "gray",   { 0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000 } },
    { "pocket", { 0xFFC4CFA1, 0xFF8B956D, 0xFF4D533C, 0xFF1F1F1F } },
};

/* Default bindings: "|" separates alternatives. */
static const char *const default_keys[ACT_COUNT] = {
    "X", "Z", "Right Shift|Backspace", "Return", "Right", "Left", "Up", "Down",
    "Tab", "F11", "F10", "F9", "F12", "Escape",
};
static const char *const default_buttons[ACT_COUNT] = {
    "a", "b|x", "back", "start", "dpright", "dpleft", "dpup", "dpdown",
    "rightshoulder", "", "", "", "", "",
};

const char *settings_action_name(Action a) { return a < ACT_COUNT ? action_names[a] : "?"; }
const char *settings_scaling_name(ScalingMode m) { return m < SCALE_MODES ? scaling_names[m] : "?"; }
const char *settings_sync_name(SyncMode m) { return m < SYNC_MODES ? sync_names[m] : "?"; }
const char *settings_palette_name(int p) { return p >= 0 && p < PALETTE_PRESETS ? palettes[p].name : NULL; }

int settings_palette_preset(const char *name, uint32_t out[4])
{
    for (int p = 0; p < PALETTE_PRESETS; p++)
        if (!strcmp(name, palettes[p].name)) {
            memcpy(out, palettes[p].c, sizeof palettes[p].c);
            return p;
        }
    return -1;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    size_t i = 0;
    while (isspace((unsigned char)s[i]))
        i++;
    memmove(s, s + i, n - i + 1);
}

/* Splits a list on sep into binds; "none" or empty clears. */
static int set_binds(char binds[BIND_MAX][BIND_NAME], const char *value, char sep,
                     char *err, size_t err_len)
{
    char buf[BIND_MAX * BIND_NAME * 2];
    snprintf(buf, sizeof buf, "%s", value);
    memset(binds, 0, BIND_MAX * BIND_NAME);
    trim(buf);
    if (!buf[0] || !strcmp(buf, "none"))
        return 0;
    int n = 0;
    for (char *p = buf; p; ) {
        char *next = strchr(p, sep);
        if (next)
            *next++ = 0;
        trim(p);
        if (*p) {
            if (n == BIND_MAX) {
                snprintf(err, err_len, "at most %d bindings per action", BIND_MAX);
                return -1;
            }
            if (strlen(p) >= BIND_NAME) {
                snprintf(err, err_len, "name too long: %s", p);
                return -1;
            }
            snprintf(binds[n++], BIND_NAME, "%s", p);
        }
        p = next;
    }
    return 0;
}

void settings_defaults(Settings *s)
{
    char err[64];
    memset(s, 0, sizeof *s);
    s->scale = 4;
    s->scaling = SCALE_INTEGER;
    s->vsync = 1;
    s->sync = SYNC_AUTO;
    memcpy(s->palette, palettes[0].c, sizeof s->palette);
    s->volume = 100;
    s->latency_ms = 64;
    s->stick = 1;
    s->deadzone = 16000;
    for (int a = 0; a < ACT_COUNT; a++) {
        set_binds(s->keys[a], default_keys[a], '|', err, sizeof err);
        set_binds(s->buttons[a], default_buttons[a], '|', err, sizeof err);
    }
}

static int parse_bool(const char *v, int *out)
{
    if (!strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "1")) { *out = 1; return 0; }
    if (!strcmp(v, "no") || !strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "0")) { *out = 0; return 0; }
    return -1;
}

static int parse_int(const char *v, int lo, int hi, int *out)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v || *end || n < lo || n > hi)
        return -1;
    *out = (int)n;
    return 0;
}

static int parse_palette(const char *v, uint32_t out[4])
{
    if (settings_palette_preset(v, out) >= 0)
        return 0;
    uint32_t c[4];
    const char *p = v;
    for (int i = 0; i < 4; i++) {
        while (*p == ' ' || *p == ',' || *p == '\t')
            p++;
        if (*p == '#')
            p++;
        char *end;
        unsigned long x = strtoul(p, &end, 16);
        if (end - p != 6)
            return -1;
        c[i] = 0xFF000000u | (uint32_t)x;
        p = end;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p)
        return -1;
    memcpy(out, c, sizeof c);
    return 0;
}

static int find_name(const char *const *names, int count, const char *v)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], v))
            return i;
    return -1;
}

int settings_set(Settings *s, const char *section, const char *key, const char *value,
                 char *err, size_t err_len)
{
    int ok = -1, i;
    if (!strcmp(section, "video")) {
        if (!strcmp(key, "scale")) ok = parse_int(value, 1, 16, &s->scale);
        else if (!strcmp(key, "fullscreen")) ok = parse_bool(value, &s->fullscreen);
        else if (!strcmp(key, "vsync")) ok = parse_bool(value, &s->vsync);
        else if (!strcmp(key, "palette")) ok = parse_palette(value, s->palette);
        else if (!strcmp(key, "scaling")) {
            if ((i = find_name(scaling_names, SCALE_MODES, value)) >= 0) { s->scaling = (ScalingMode)i; ok = 0; }
        } else if (!strcmp(key, "sync")) {
            if ((i = find_name(sync_names, SYNC_MODES, value)) >= 0) { s->sync = (SyncMode)i; ok = 0; }
        } else if (!strcmp(key, "fast_forward_speed")) ok = parse_int(value, 0, 100, &s->fast_forward_speed);
        else goto unknown;
    } else if (!strcmp(section, "audio")) {
        if (!strcmp(key, "mute")) ok = parse_bool(value, &s->mute);
        else if (!strcmp(key, "volume")) ok = parse_int(value, 0, 100, &s->volume);
        else if (!strcmp(key, "latency")) ok = parse_int(value, 16, 500, &s->latency_ms);
        else goto unknown;
    } else if (!strcmp(section, "keyboard") || !strcmp(section, "controller")) {
        int pad = section[0] == 'c';
        if (pad && !strcmp(key, "stick")) ok = parse_bool(value, &s->stick);
        else if (pad && !strcmp(key, "deadzone")) ok = parse_int(value, 0, 32767, &s->deadzone);
        else if ((i = find_name(action_names, ACT_COUNT, key)) >= 0)
            return set_binds(pad ? s->buttons[i] : s->keys[i], value, ',', err, err_len);
        else goto unknown;
    } else {
        snprintf(err, err_len, "unknown section [%s]", section);
        return -1;
    }
    if (ok)
        snprintf(err, err_len, "bad value for %s: %s", key, value);
    return ok;
unknown:
    snprintf(err, err_len, "unknown setting %s in [%s]", key, section);
    return -1;
}

int settings_load(Settings *s, const char *path, char *err, size_t err_len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 1;
    char line[512], section[32] = "", msg[200];
    int lineno = 0, result = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        /* Only whole-line comments: '#' also starts a colour (#RRGGBB). */
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';')
            continue;
        int bad = 0;
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end || end[1]) {
                snprintf(msg, sizeof msg, "bad section header");
                bad = 1;
            } else {
                *end = 0;
                snprintf(section, sizeof section, "%.31s", line + 1);
                trim(section);
            }
        } else {
            char *eq = strchr(line, '=');
            if (!eq) {
                snprintf(msg, sizeof msg, "expected key = value");
                bad = 1;
            } else {
                *eq = 0;
                char *key = line, *value = eq + 1;
                trim(key);
                trim(value);
                bad = settings_set(s, section, key, value, msg, sizeof msg) != 0;
            }
        }
        if (bad && result == 0) {
            snprintf(err, err_len, "%s:%d: %s", path, lineno, msg);
            result = -1;
        }
    }
    fclose(f);
    return result;
}

static void write_binds(FILE *f, const char binds[BIND_MAX][BIND_NAME])
{
    int n = 0;
    for (int i = 0; i < BIND_MAX; i++)
        if (binds[i][0])
            fprintf(f, "%s%s", n++ ? ", " : "", binds[i]);
    if (!n)
        fprintf(f, "none");
    fputc('\n', f);
}

int settings_save(const Settings *s, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f,
            "# Super Mario Land port settings. Options given on the command line\n"
            "# override this file. None of these change how the game plays.\n\n"
            "[video]\n"
            "# initial window size in multiples of 160x144\n"
            "scale = %d\n"
            "# integer (whole multiples, borders) | sharp (fills the window, smooth\n"
            "# pixel edges) | nearest (fills the window, uneven pixel sizes)\n"
            "scaling = %s\n"
            "fullscreen = %s\n"
            "vsync = %s\n"
            "# pacing: clock (exact 59.73 fps) | vsync (one game frame per display\n"
            "# refresh; for ~60 Hz displays) | auto (vsync when the display is ~60 Hz)\n"
            "sync = %s\n"
            "# green | gray | pocket, or four colours lightest first: #E0F8D0 #88C070 ...\n",
            s->scale, scaling_names[s->scaling], s->fullscreen ? "yes" : "no",
            s->vsync ? "yes" : "no", sync_names[s->sync]);
    int preset = -1;
    for (int p = 0; p < PALETTE_PRESETS; p++)
        if (!memcmp(s->palette, palettes[p].c, sizeof s->palette))
            preset = p;
    if (preset >= 0)
        fprintf(f, "palette = %s\n", palettes[preset].name);
    else
        fprintf(f, "palette = #%06X #%06X #%06X #%06X\n", s->palette[0] & 0xFFFFFF,
                s->palette[1] & 0xFFFFFF, s->palette[2] & 0xFFFFFF, s->palette[3] & 0xFFFFFF);
    fprintf(f,
            "# speed while fast_forward is held: 0 = as fast as possible, N = N times\n"
            "fast_forward_speed = %d\n\n"
            "[audio]\n"
            "mute = %s\n"
            "# 0..100\n"
            "volume = %d\n"
            "# wanted output latency in milliseconds (16..500)\n"
            "latency = %d\n\n"
            "[keyboard]\n"
            "# SDL key names, separated by commas; none = unbound\n",
            s->fast_forward_speed, s->mute ? "yes" : "no", s->volume, s->latency_ms);
    for (int a = 0; a < ACT_COUNT; a++) {
        fprintf(f, "%s = ", action_names[a]);
        write_binds(f, s->keys[a]);
    }
    fprintf(f,
            "\n[controller]\n"
            "# SDL game controller buttons: a b x y back guide start leftstick\n"
            "# rightstick leftshoulder rightshoulder dpup dpdown dpleft dpright\n"
            "# misc1 paddle1..paddle4 touchpad\n"
            "stick = %s\n"
            "deadzone = %d\n", s->stick ? "yes" : "no", s->deadzone);
    for (int a = 0; a < ACT_COUNT; a++) {
        fprintf(f, "%s = ", action_names[a]);
        write_binds(f, s->buttons[a]);
    }
    return fclose(f) ? -1 : 0;
}
