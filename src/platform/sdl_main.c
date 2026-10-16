/*
 * Super Mario Land port - SDL2 frontend (Linux, Windows).
 *
 *   sml [options]
 *     --rom FILE          the game ROM (default: sml.gb or rom/sml.gb in the
 *                         current directory, next to the executable or the
 *                         .AppImage, or in the data directory, see below)
 *     --model dmg|cgb|agb console to model (default dmg; TAS resyncs: agb)
 *     --config FILE       settings file (default: see below; created with
 *                         commented defaults when missing)
 *     --scale N           initial window size in multiples of 160x144
 *     --scaling integer|sharp|nearest
 *     --sync auto|clock|vsync
 *     --fullscreen, --windowed
 *     --palette NAME      green, gray, pocket or four #RRGGBB colours
 *     --widescreen off|on|W:H  show more of the level (on: fill the window)
 *     --mute, --volume N  (0..100)
 *     --movie FILE        play back an input movie (one entry per game frame)
 *     --record FILE       record the input to a movie when the game ends
 *     --headless N        no window/audio/settings: run N frames as fast as
 *                         possible
 *     --frames N          with a window: quit after N frames
 *     --screenshot FILE   save the last frame as PNG (with a window: the
 *                         whole window including the overlays)
 *     --screenshot-every N  with --headless: save PATH-NNNNNN.png every N frames
 *     --stats             print pacing and audio statistics on exit
 *     --no-update         do not look for new releases this time
 *     --changelog         show the changelog before starting
 *     --timer             show the speedrun timer this time
 *     --category any|normal+hard
 *     --import-lss FILE   take over personal best and best segments of a
 *                         LiveSplit split file for the timer's category
 *     --speedrun-report   with --headless: print the timer's splits
 *     --version           print the version and exit
 *
 * Settings: $XDG_CONFIG_HOME/super-mario-land-port/settings.ini on Linux
 * (~/.config/... when XDG_CONFIG_HOME is unset), settings.ini in the data
 * directory on Windows. Command-line options override the file for one run.
 * Data directory (screenshots, optional ROM location): SDL's preference path,
 * ~/.local/share/super-mario-land-port/ on Linux, %APPDATA%\
 * super-mario-land-port\ on Windows.
 *
 * Default keys: arrows = D-pad, X = A, Z = B, Enter = Start, Right Shift or
 * Backspace = Select, Tab (hold) = fast forward, F9 = next palette, F10 =
 * next scaling mode, F11 or Alt+Enter = fullscreen, F12 = screenshot,
 * F8 = speedrun timer, Esc or F1 = settings menu (also Guide or Select+Start
 * on a controller; it has Quit). Game controllers use SDL's controller mapping (south button
 * = A, west/east = B, right shoulder = fast forward); every binding can be
 * changed in the settings file.
 *
 * Started by the Warpzone launcher (WARPZONE_LAUNCHED=1), a ROM given with
 * --rom is kept as sml.gb in the data directory, and the launcher, not the
 * game, installs updates.
 *
 * The first start of a new version shows what changed (CHANGELOG.md, built
 * in). Release builds look for a newer release in the background and install
 * it, checksum-verified, for the next start (update.c, [update] check = no
 * turns it off).
 *
 * The speedrun timer (speedrun.c, [speedrun] timer = yes) splits on its own
 * and keeps its splits as speedrun-<category>.txt and .lss (LiveSplit) in the
 * data directory.
 *
 * Everything here is presentation: pacing, scaling, audio resampling and
 * fast forward never change what the game computes, so movies and TAS
 * playback stay in sync.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define make_dir(p) _mkdir(p)
#define isatty _isatty
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p, 0755)
#endif

#include "../core/core.h"
#include "../util/movie.h"
#include "../../verify/common/png.h"
#include "audio_sync.h"
#include "settings.h"
#include "speedrun.h"
#include "fault.h"
#include "text.h"
#include "widescreen.h"
#include "update.h"
#include "update_core.h"
#include "changelog_text.h"   /* generated from CHANGELOG.md */

#ifndef SML_VERSION
#define SML_VERSION "M0-dev"
#endif

#define APP_DIR "super-mario-land-port"
#define GB_CLOCK 4194304.0
#define GB_FRAME_CYCLES 70224.0
#define GB_FPS (GB_CLOCK / GB_FRAME_CYCLES)        /* 59.7275 */
#define MAX_PADS 4
#define AUDIO_OUT_FRAMES 16384

typedef struct {
    Settings set;
    Settings base;                /* set as of start-up or the last save: what
                                     differs was changed in the game and is kept */
    char config_path[1024];
    int config_ok;                /* file loaded without errors (safe to rewrite) */

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;         /* WIDE_MAX_W x 144, streaming */
    int prescale_w;               /* picture width the sharp texture was made for */
    const uint8_t *rom;
    size_t rom_size;
    uint8_t wide[WIDE_MAX_W * LCD_H];   /* shades of the widescreen picture */
    int wide_w;                   /* its width (LCD_W when widescreen is off) */
    WideState wide_state;
    SDL_Texture *prescaled;       /* integer-upscaled copy for sharp scaling */
    int prescale;
    int vsync_on;                 /* current renderer vsync state */
    SDL_GameController *pads[MAX_PADS];
    SDL_Scancode keys[ACT_COUNT][BIND_MAX];
    SDL_GameControllerButton buttons[ACT_COUNT][BIND_MAX];
    SDL_GameControllerButton buttons2[ACT_COUNT][BIND_MAX];   /* second button of "a+b" */
    int release_wait;             /* after the menu: no game input until released */

    /* pacing */
    int vsync_pacing;             /* one emulated frame per display refresh */
    double perf_freq;
    double next_frame_time;       /* in performance counter units */
    double last_present;
    double present_interval;      /* measured refresh period, seconds (EMA) */
    int present_samples;
    int fast_forward;
    double last_ff_present;

    /* audio */
    SDL_AudioDeviceID audio;
    int out_rate;
    double audio_in_rate;
    AudioSync sync;
    int16_t audio_buf[4096 * 2];
    int16_t audio_out[AUDIO_OUT_FRAMES * 2];

    /* title messages ("scaling: sharp") */
    Uint32 title_until;
    /* message drawn over the game (updates) */
    char banner[160];
    Uint32 banner_until;

    /* --stats */
    int stats;
    double start_time;
    double queue_sum, queue_min;
    long queue_samples, underruns;

    int headless;
    long headless_frames;
    long frames;
    uint8_t held;                 /* buttons sampled at the last frame */
    Movie movie;
    int playing_movie;
    Movie recording;
    const char *record_path;
    const char *screenshot_path;
    long screenshot_every;        /* --screenshot-every: headless picture series */

    /* speedrun timer */
    Speedrun sr;
    int sr_loaded;                /* sr holds this category's saved splits */
    int sr_report;                /* --speedrun-report */
    char sr_path[1100], sr_lss[1100];
    char sr_msg[48];              /* last split, shown for a while */
    int sr_msg_kind;              /* 0 neutral, 1 ahead, 2 behind, 3 gold */
    Uint32 sr_msg_until;
} App;

static uint8_t *load_file(const char *path, size_t *size);
static void draw_timer(App *app, int ow, int oh);

static double now_ticks(void) { return (double)SDL_GetPerformanceCounter(); }

/* Reports a start-up error on stderr and, when not started from a terminal
   (desktop launcher, AppImage), in a message box. Returns 1. */
static int fail(int headless, const char *fmt, ...)
{
    char msg[1600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", msg);
    if (!headless && !isatty(2))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Mario Land", msg, NULL);
    return 1;
}

/* --- paths ------------------------------------------------------------ */

static void make_dirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            make_dir(buf);
            *p = c;
        }
    make_dir(buf);
}

/* The data directory, with a trailing separator ("" if unavailable). */
static const char *data_dir(void)
{
    static char dir[1024];
    static int done;
    if (!done) {
        done = 1;
        char *p = SDL_GetPrefPath(NULL, APP_DIR);
        if (p) {
            snprintf(dir, sizeof dir, "%s", p);
            SDL_free(p);
        }
    }
    return dir;
}

static void default_config_path(char *out, size_t len)
{
#ifdef _WIN32
    snprintf(out, len, "%ssettings.ini", data_dir());
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[900];
    if (xdg && xdg[0] == '/')
        snprintf(dir, sizeof dir, "%s/" APP_DIR, xdg);
    else if (home && home[0])
        snprintf(dir, sizeof dir, "%s/.config/" APP_DIR, home);
    else {
        snprintf(out, len, "settings.ini");
        return;
    }
    make_dirs(dir);
    snprintf(out, len, "%s/settings.ini", dir);
#endif
}

/* --- settings and bindings ---------------------------------------------- */

static void resolve_bindings(App *app)
{
    for (int a = 0; a < ACT_COUNT; a++)
        for (int i = 0; i < BIND_MAX; i++) {
            const char *k = app->set.keys[a][i], *b = app->set.buttons[a][i];
            app->keys[a][i] = SDL_SCANCODE_UNKNOWN;
            app->buttons[a][i] = app->buttons2[a][i] = SDL_CONTROLLER_BUTTON_INVALID;
            if (k[0]) {
                app->keys[a][i] = SDL_GetScancodeFromName(k);
                if (app->keys[a][i] == SDL_SCANCODE_UNKNOWN)
                    fprintf(stderr, "settings: unknown key \"%s\" for %s\n", k,
                            settings_action_name((Action)a));
            }
            if (b[0]) {
                char first[BIND_NAME];
                snprintf(first, sizeof first, "%s", b);
                char *plus = strchr(first, '+');
                if (plus) {
                    *plus = 0;
                    app->buttons2[a][i] = SDL_GameControllerGetButtonFromString(plus + 1);
                }
                app->buttons[a][i] = SDL_GameControllerGetButtonFromString(first);
                if (app->buttons[a][i] == SDL_CONTROLLER_BUTTON_INVALID ||
                    (plus && app->buttons2[a][i] == SDL_CONTROLLER_BUTTON_INVALID))
                    fprintf(stderr, "settings: unknown controller button \"%s\" for %s\n", b,
                            settings_action_name((Action)a));
            }
        }
}

static void load_settings(App *app, const char *explicit_path)
{
    char err[300] = "";
    if (explicit_path)
        snprintf(app->config_path, sizeof app->config_path, "%s", explicit_path);
    else
        default_config_path(app->config_path, sizeof app->config_path);
    int r = settings_load(&app->set, app->config_path, err, sizeof err);
    if (r == 1) {
        if (settings_save(&app->set, app->config_path) == 0)
            fprintf(stderr, "wrote default settings to %s\n", app->config_path);
        app->config_ok = 1;
    } else if (r < 0) {
        fprintf(stderr, "settings: %s\n", err);
    } else {
        app->config_ok = 1;
    }
}

/* Changes made with hotkeys and the menu are kept: reload the file (so
   command-line overrides are not persisted) and write back only what was
   changed in the game. */
static void save_changes(App *app)
{
    if (!app->config_ok || !memcmp(&app->set, &app->base, sizeof app->set))
        return;
    Settings s;
    char err[300];
    settings_defaults(&s);
    if (settings_load(&s, app->config_path, err, sizeof err) < 0)
        return;
    const Settings *now = &app->set, *was = &app->base;
#define KEEP(field) do { if (memcmp(&now->field, &was->field, sizeof now->field)) \
        memcpy(&s.field, &now->field, sizeof s.field); } while (0)
    KEEP(scaling);
    KEEP(fullscreen);
    KEEP(palette);
    KEEP(widescreen);
    KEEP(mute);
    KEEP(volume);
    KEEP(update_check);
    KEEP(speedrun);
    KEEP(speedrun_category);
    for (int a = 0; a < ACT_COUNT; a++) {
        KEEP(keys[a]);
        KEEP(buttons[a]);
    }
#undef KEEP
    if (settings_save(&s, app->config_path))
        fprintf(stderr, "cannot write %s\n", app->config_path);
    app->base = app->set;
}

/* --- window title messages -------------------------------------------- */

static void update_title(App *app, const char *message)
{
    char title[160];
    if (message) {
        snprintf(title, sizeof title, "Super Mario Land - %s", message);
        app->title_until = SDL_GetTicks() + 2000;
    } else {
        snprintf(title, sizeof title, "Super Mario Land%s",
                 app->fast_forward ? " - fast forward" : "");
        app->title_until = 0;
    }
    SDL_SetWindowTitle(app->window, title);
}

/* --- input ---------------------------------------------------------------- */

static int action_held(App *app, Action a, const Uint8 *k)
{
    for (int i = 0; i < BIND_MAX; i++) {
        SDL_Scancode sc = app->keys[a][i];
        if (sc != SDL_SCANCODE_UNKNOWN && k[sc]) {
            /* Alt+Enter toggles fullscreen and is not Start. */
            if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) &&
                (SDL_GetModState() & KMOD_ALT))
                continue;
            return 1;
        }
        SDL_GameControllerButton b = app->buttons[a][i], b2 = app->buttons2[a][i];
        if (b != SDL_CONTROLLER_BUTTON_INVALID)
            for (int p = 0; p < MAX_PADS; p++)
                if (app->pads[p] && SDL_GameControllerGetButton(app->pads[p], b) &&
                    (b2 == SDL_CONTROLLER_BUTTON_INVALID || SDL_GameControllerGetButton(app->pads[p], b2)))
                    return 1;
    }
    return 0;
}

static uint8_t read_buttons(App *app)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    for (int a = 0; a < ACT_GAME_BUTTONS; a++)
        if (action_held(app, (Action)a, k))
            b |= (uint8_t)(1u << a);
    if (app->set.stick)
        for (int p = 0; p < MAX_PADS; p++) {
            if (!app->pads[p])
                continue;
            int x = SDL_GameControllerGetAxis(app->pads[p], SDL_CONTROLLER_AXIS_LEFTX);
            int y = SDL_GameControllerGetAxis(app->pads[p], SDL_CONTROLLER_AXIS_LEFTY);
            int dz = app->set.deadzone;
            if (x > dz) b |= BTN_RIGHT;
            if (x < -dz) b |= BTN_LEFT;
            if (y > dz) b |= BTN_DOWN;
            if (y < -dz) b |= BTN_UP;
        }
    /* The original hardware cannot report opposite directions at once. */
    if ((b & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) b &= (uint8_t)~(BTN_LEFT | BTN_RIGHT);
    if ((b & (BTN_UP | BTN_DOWN)) == (BTN_UP | BTN_DOWN)) b &= (uint8_t)~(BTN_UP | BTN_DOWN);
    /* The buttons that closed the menu do not reach the game. */
    if (app->release_wait) {
        if (b)
            b = 0;
        else
            app->release_wait = 0;
    }

    int ff = action_held(app, ACT_FAST_FORWARD, k);
    if (ff != app->fast_forward) {
        app->fast_forward = ff;
        if (app->audio)
            SDL_ClearQueuedAudio(app->audio);
        update_title(app, NULL);
    }
    return b;
}

static void add_pad(App *app, int device_index)
{
    if (!SDL_IsGameController(device_index))
        return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
    for (int p = 0; p < MAX_PADS; p++)
        if (app->pads[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app->pads[p])) == id)
            return;
    for (int p = 0; p < MAX_PADS; p++)
        if (!app->pads[p]) {
            app->pads[p] = SDL_GameControllerOpen(device_index);
            return;
        }
}

static void remove_pad(App *app, SDL_JoystickID id)
{
    for (int p = 0; p < MAX_PADS; p++)
        if (app->pads[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app->pads[p])) == id) {
            SDL_GameControllerClose(app->pads[p]);
            app->pads[p] = NULL;
        }
}

/* --- video ------------------------------------------------------------- */

static void set_fullscreen(App *app, int on)
{
    app->set.fullscreen = on;
    SDL_SetWindowFullscreen(app->window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

static void set_vsync(App *app, int on)
{
    if (app->vsync_on == on)
        return;
    if (SDL_RenderSetVSync(app->renderer, on) == 0)
        app->vsync_on = on;
}

/* The fitted, centred destination for an aspect-preserving scale. */
static SDL_Rect fit_rect(int ow, int oh, int w, double scale)
{
    SDL_Rect r;
    r.w = (int)(w * scale + 0.5);
    r.h = (int)(LCD_H * scale + 0.5);
    r.x = (ow - r.w) / 2;
    r.y = (oh - r.h) / 2;
    return r;
}

/* The picture's width for widescreen at a window size: a fixed aspect, or
   (on) as wide as the window at the scale the height allows. */
static int picture_width(App *app, int ow, int oh)
{
    if (!app->set.widescreen)
        return LCD_W;
    if (app->set.widescreen > 0 || ow <= 0 || oh <= 0)
        return settings_wide_width(app->set.widescreen, ow, oh);
    double scale = (double)oh / LCD_H;
    if (app->set.scaling == SCALE_INTEGER && scale >= 1)
        scale = (int)scale;
    return settings_wide_width(-1, ow, (int)(LCD_H * scale + 0.5));
}

/* Fills app->wide with the picture at width w. */
static void compose_picture(App *app, int w)
{
    Machine *m = core_machine();
    if (w <= LCD_W) {
        memcpy(app->wide, m->framebuffer, LCD_W * LCD_H);
        w = LCD_W;
    } else {
        widescreen_render(&app->wide_state, m, app->rom, app->rom_size, w, app->wide);
    }
    app->wide_w = w;
}

/* Draws the game and the overlays without presenting them. */
static void render_contents(App *app)
{
    int ow, oh;
    SDL_GetRendererOutputSize(app->renderer, &ow, &oh);
    int pw = picture_width(app, ow, oh);
    compose_picture(app, pw);
    SDL_Rect src = { 0, 0, pw, LCD_H };
    void *pixels;
    int pitch;
    if (SDL_LockTexture(app->texture, &src, &pixels, &pitch) == 0) {
        for (int y = 0; y < LCD_H; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
            for (int x = 0; x < pw; x++)
                row[x] = app->set.palette[app->wide[y * pw + x] & 3];
        }
        SDL_UnlockTexture(app->texture);
    }

    double fit = SDL_min((double)ow / pw, (double)oh / LCD_H);
    if (app->set.widescreen < 0)
        fit = (double)oh / LCD_H;     /* the width is cut to the window */
    if (fit <= 0)
        return;
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);

    ScalingMode mode = app->set.scaling;
    if (mode == SCALE_INTEGER && fit < 1)
        mode = SCALE_NEAREST;
    if (mode == SCALE_SHARP && !SDL_RenderTargetSupported(app->renderer))
        mode = SCALE_NEAREST;

    if (mode == SCALE_INTEGER) {
        SDL_Rect dst = fit_rect(ow, oh, pw, (double)(int)fit);
        SDL_RenderCopy(app->renderer, app->texture, &src, &dst);
    } else if (mode == SCALE_NEAREST) {
        SDL_Rect dst = fit_rect(ow, oh, pw, fit);
        SDL_RenderCopy(app->renderer, app->texture, &src, &dst);
    } else {
        /* Sharp: upscale by a whole factor with nearest neighbour, then
           filter the small remaining (down)scale, so pixels stay square
           and edges are at most one screen pixel soft. */
        int k = (int)fit;
        if (k < fit)
            k++;
        if (!app->prescaled || app->prescale != k || app->prescale_w != pw) {
            if (app->prescaled)
                SDL_DestroyTexture(app->prescaled);
            app->prescaled = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_TARGET, pw * k, LCD_H * k);
            app->prescale = k;
            app->prescale_w = pw;
            if (app->prescaled)
                SDL_SetTextureScaleMode(app->prescaled, SDL_ScaleModeLinear);
        }
        SDL_Rect dst = fit_rect(ow, oh, pw, fit);
        if (app->prescaled && SDL_SetRenderTarget(app->renderer, app->prescaled) == 0) {
            SDL_RenderCopy(app->renderer, app->texture, &src, NULL);
            SDL_SetRenderTarget(app->renderer, NULL);
            SDL_RenderCopy(app->renderer, app->prescaled, NULL, &dst);
        } else {
            SDL_RenderCopy(app->renderer, app->texture, &src, &dst);
        }
    }
    if (app->set.speedrun)
        draw_timer(app, ow, oh);
    if (app->banner_until) {
        int px = SDL_max(1, oh / 240);
        int w = (int)strlen(app->banner) * TEXT_CELL_W * px + 4 * px;
        SDL_Rect bar = { (ow - w) / 2, oh - (TEXT_CELL_H + 6) * px, w, (TEXT_CELL_H + 2) * px };
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(app->renderer, &bar);
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        text_draw(app->renderer, bar.x + 2 * px, bar.y + 2 * px, px, app->banner);
    }
}

static void render_frame(App *app)
{
    render_contents(app);
    SDL_RenderPresent(app->renderer);
}

static void take_screenshot(App *app)
{
    static uint8_t rgb[WIDE_MAX_W * LCD_H * 3];
    const uint8_t *fb = app->wide;
    int w = app->wide_w ? app->wide_w : LCD_W;
    if (!app->wide_w)
        memcpy(app->wide, core_machine()->framebuffer, LCD_W * LCD_H);
    for (int i = 0; i < w * LCD_H; i++) {
        uint32_t c = app->set.palette[fb[i] & 3];
        rgb[i * 3] = (uint8_t)(c >> 16);
        rgb[i * 3 + 1] = (uint8_t)(c >> 8);
        rgb[i * 3 + 2] = (uint8_t)c;
    }
    char dir[1100], path[1200], stamp[32];
    snprintf(dir, sizeof dir, "%sscreenshots", data_dir());
    make_dirs(dir);
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    snprintf(path, sizeof path, "%s/sml-%s.png", dir, stamp);
    for (int n = 2; n < 100; n++) {
        FILE *f = fopen(path, "rb");
        if (!f)
            break;
        fclose(f);
        snprintf(path, sizeof path, "%s/sml-%s-%d.png", dir, stamp, n);
    }
    if (png_write_rgb(path, rgb, w, LCD_H) == 0) {
        fprintf(stderr, "screenshot: %s\n", path);
        update_title(app, "screenshot saved");
    } else {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
    }
}

/* --- pacing -------------------------------------------------------------- */

/* auto: pace by vsync when the display runs at about the game's 59.73 Hz. */
static void choose_pacing(App *app)
{
    int vsync = 0;
    if (app->set.sync == SYNC_VSYNC) {
        vsync = 1;
    } else if (app->set.sync == SYNC_AUTO && app->set.vsync) {
        SDL_DisplayMode dm;
        int d = SDL_GetWindowDisplayIndex(app->window);
        if (d >= 0 && SDL_GetCurrentDisplayMode(d, &dm) == 0)
            vsync = dm.refresh_rate >= 59 && dm.refresh_rate <= 61;
    }
    if (vsync != app->vsync_pacing) {
        app->vsync_pacing = vsync;
        app->present_samples = 0;
        app->next_frame_time = 0;
        if (app->audio) {
            app->audio_in_rate = APU_SAMPLE_RATE;
            audio_sync_set_rate(&app->sync, app->audio_in_rate, app->out_rate);
        }
    }
}

/* In vsync pacing, measure the refresh period from the presents. The game
   then runs at the display's rate, so the APU produces audio slightly
   faster or slower than real time; the resampler's nominal ratio follows.
   A display that does not block in present (vsync ignored) or skips
   refreshes falls back to clock pacing. */
static void measure_refresh(App *app, double t)
{
    double dt = (t - app->last_present) / app->perf_freq;
    app->last_present = t;
    if (dt <= 0 || dt > 0.1) {
        app->present_samples = 0;
        return;
    }
    /* Skip one second of warm-up (a new window often presents early until
       the compositor syncs it), average two seconds, then follow slowly. */
    enum { WARMUP = 60, MEASURE = 120 };
    int n = ++app->present_samples - WARMUP;
    if (n <= 0) {
        app->present_interval = 0;
        return;
    }
    if (n <= MEASURE)
        app->present_interval += (dt - app->present_interval) / n;
    else
        app->present_interval += (dt - app->present_interval) * 0.02;
    if (n < MEASURE)
        return;
    double hz = 1.0 / app->present_interval;
    if (hz < 57.0 || hz > 63.0) {
        fprintf(stderr, "display paces at %.2f Hz, not ~60: using clock pacing\n", hz);
        app->vsync_pacing = 0;
        app->next_frame_time = 0;
        app->audio_in_rate = APU_SAMPLE_RATE;
        if (app->audio) {
            audio_sync_set_rate(&app->sync, app->audio_in_rate, app->out_rate);
            SDL_ClearQueuedAudio(app->audio);
        }
        return;
    }
    /* The warm-up may have run fast and overfilled the audio queue. */
    if (app->audio && n == MEASURE &&
        SDL_GetQueuedAudioSize(app->audio) / 4.0 > 1.5 * app->set.latency_ms * app->out_rate / 1000.0)
        SDL_ClearQueuedAudio(app->audio);
    if (app->audio && n % 60 == 0) {
        app->audio_in_rate = APU_SAMPLE_RATE * hz / GB_FPS;
        audio_sync_set_rate(&app->sync, app->audio_in_rate, app->out_rate);
    }
}

static void wait_until(App *app, double deadline)
{
    for (;;) {
        double left = (deadline - now_ticks()) / app->perf_freq;
        if (left <= 0)
            return;
        if (left > 0.002)
            SDL_Delay((Uint32)((left - 0.0015) * 1000));
        /* spin the last ~1.5 ms for an even frame rate */
    }
}

static void clock_pacing(App *app, double period)
{
    double t = now_ticks();
    double step = app->perf_freq * period;
    /* Start, or late by more than two frames (a stall, a slow present):
       continue from now instead of running fast to catch up. */
    if (app->next_frame_time == 0 || t - app->next_frame_time > 2 * step)
        app->next_frame_time = t;
    app->next_frame_time += step;
    wait_until(app, app->next_frame_time);
}

static void queue_audio(App *app)
{
    Machine *m = core_machine();
    size_t in = m->apu.out_len;
    m->apu.out_len = 0;
    if (!app->audio || app->set.mute || app->fast_forward || in == 0)
        return;
    double target = (double)app->set.latency_ms * app->out_rate / 1000.0;
    double queued = SDL_GetQueuedAudioSize(app->audio) / 4.0;
    if (app->stats) {
        if (queued == 0 && app->frames > 60)
            app->underruns++;
        if (app->frames % 600 == 0)          /* report the last 10 s window */
            app->queue_samples = 0, app->queue_sum = 0;
        app->queue_sum += queued;
        if (app->queue_samples == 0 || queued < app->queue_min)
            app->queue_min = queued;
        app->queue_samples++;
    }
    /* Rate control only corrects small drifts. After a stall, fast
       forward or a pacing change, reset the queue to half the target with
       silence; the control loop then settles it at the target. */
    if (queued > target * 3) {
        SDL_ClearQueuedAudio(app->audio);
        queued = 0;
    }
    if (queued < target / 4) {
        static const int16_t silence[512 * 2];
        size_t fill = (size_t)(target / 2 - queued);
        for (size_t done = 0; done < fill; ) {
            size_t k = SDL_min(fill - done, (size_t)512);
            SDL_QueueAudio(app->audio, silence, (Uint32)(k * 4));
            done += k;
        }
        queued += (double)fill;
    }
    size_t n = audio_sync_resample(&app->sync, app->audio_buf, in, app->audio_out,
                                   AUDIO_OUT_FRAMES, queued, target);
    audio_apply_volume(app->audio_out, n * 2, app->set.volume);
    SDL_QueueAudio(app->audio, app->audio_out, (Uint32)(n * 4));
}

/* --- speedrun timer --------------------------------------------------------- */

/* Loads the category's splits (once) from the data directory. */
static void sr_prepare(App *app)
{
    SrCategory cat = (SrCategory)app->set.speedrun_category;
    if (app->sr_loaded && app->sr.category == cat)
        return;
    speedrun_init(&app->sr, cat);
    app->sr_loaded = 1;
    if (app->headless || !data_dir()[0])
        return;
    char name[32];
    snprintf(name, sizeof name, "%s", speedrun_category_name(cat));
    for (char *c = name; *c; c++)
        if (*c == '+')
            *c = '-';
    snprintf(app->sr_path, sizeof app->sr_path, "%sspeedrun-%s.txt", data_dir(), name);
    snprintf(app->sr_lss, sizeof app->sr_lss, "%sspeedrun-%s.lss", data_dir(), name);
    if (speedrun_load(&app->sr, app->sr_path) < 0)
        fprintf(stderr, "speedrun: %s has unreadable lines\n", app->sr_path);
}

/* Writes the splits and their LiveSplit copy when they changed. */
static void sr_save(App *app)
{
    if (!app->sr_loaded || !app->sr.dirty || !app->sr_path[0])
        return;
    if (speedrun_save(&app->sr, app->sr_path) || speedrun_export_lss(&app->sr, app->sr_lss))
        fprintf(stderr, "speedrun: cannot write %s\n", app->sr_path);
    app->sr.dirty = 0;
}

static int import_lss(App *app, const char *path)
{
    size_t size;
    char err[160];
    uint8_t *xml = load_file(path, &size);
    if (!xml) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    sr_prepare(app);
    int r = speedrun_import_lss(&app->sr, (const char *)xml, err, sizeof err);
    free(xml);
    if (r) {
        fprintf(stderr, "%s: %s\n", path, err);
        return -1;
    }
    sr_save(app);
    printf("imported %s into %s\n", path, app->sr_path[0] ? app->sr_path : "the timer");
    return 0;
}

static void sr_message(App *app, int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->sr_msg, sizeof app->sr_msg, fmt, ap);
    va_end(ap);
    app->sr_msg_kind = kind;
    app->sr_msg_until = SDL_GetTicks() + 6000;
}

/* Called once per video frame: the timer only reads game memory. */
static void sr_on_frame(App *app)
{
    Machine *m = core_machine();
    Speedrun *sr = &app->sr;
    SrFrame f;
    speedrun_sample(&f, m->cycles, m->wram, m->hram, m->oam);
    int done = sr->split;
    SrEvent ev = speedrun_frame(sr, &f);
    if (sr->phase == SR_RUNNING && app->fast_forward)
        sr->fast_forwarded = 1;
    char t[24], d[24] = "";
    switch (ev) {
    case SR_EV_START:
        if (app->sr_report)
            printf("start frame=%ld cycles=%llu\n", app->frames - 1, (unsigned long long)sr->start);
        sr_message(app, 0, "%s attempt %ld", speedrun_category_title(sr->category), sr->attempts);
        break;
    case SR_EV_SPLIT:
    case SR_EV_FINISH: {
        int64_t delta;
        int kind = 0;
        speedrun_format(sr->times[done], 0, t, sizeof t);
        if (speedrun_delta(sr, done, &delta)) {
            speedrun_format(delta, 1, d, sizeof d);
            kind = delta < 0 ? 1 : delta > 0 ? 2 : 0;
        }
        if (sr->gold)
            kind = 3;
        if (app->sr_report)
            printf("split %-8s %s cycles=%lld frames=%.2f%s\n", speedrun_split_name(sr, done), t,
                   (long long)sr->times[done], sr->times[done] / GB_FRAME_CYCLES,
                   ev == SR_EV_FINISH ? " finish" : "");
        if (ev == SR_EV_FINISH && sr->new_pb)
            sr_message(app, 3, "%s %s PB!", speedrun_split_name(sr, done), t);
        else
            sr_message(app, kind, "%s %s %s", speedrun_split_name(sr, done), t, d);
        if (ev == SR_EV_FINISH)
            sr_save(app);
        break;
    }
    case SR_EV_RESET:
        if (app->sr_report)
            printf("reset after %d splits\n", sr->split);
        sr_save(app);
        break;
    default:
        break;
    }
}

/* Timer box in the bottom right corner of the window. */
static void draw_timer(App *app, int ow, int oh)
{
    Speedrun *sr = &app->sr;
    int px = SDL_max(1, oh / 240);
    char t[24];
    speedrun_format(speedrun_elapsed(sr), 0, t, sizeof t);
    int show_msg = app->sr_msg_until && !SDL_TICKS_PASSED(SDL_GetTicks(), app->sr_msg_until);
    char sub[64];
    snprintf(sub, sizeof sub, "%s%s", show_msg ? app->sr_msg : speedrun_category_title(sr->category),
             sr->fast_forwarded ? " FF" : "");
    int big = 2 * px;
    int w = SDL_max((int)strlen(t) * TEXT_CELL_W * big, (int)strlen(sub) * TEXT_CELL_W * px) + 4 * px;
    int h = TEXT_CELL_H * big + TEXT_CELL_H * px + 4 * px;
    SDL_Rect box = { ow - w - 2 * px, oh - h - 2 * px, w, h };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(app->renderer, &box);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
    if (sr->phase == SR_FINISHED && sr->new_pb)
        SDL_SetRenderDrawColor(app->renderer, 255, 200, 40, 255);
    else if (sr->phase == SR_RUNNING || sr->phase == SR_FINISHED)
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    else
        SDL_SetRenderDrawColor(app->renderer, 150, 150, 150, 255);
    text_draw(app->renderer, box.x + w - 2 * px - (int)strlen(t) * TEXT_CELL_W * big, box.y + 2 * px, big, t);
    static const Uint8 colours[4][3] = { { 200, 200, 200 }, { 80, 220, 80 }, { 240, 80, 60 }, { 255, 200, 40 } };
    const Uint8 *c = colours[show_msg ? app->sr_msg_kind : 0];
    SDL_SetRenderDrawColor(app->renderer, c[0], c[1], c[2], 255);
    text_draw(app->renderer, box.x + w - 2 * px - (int)strlen(sub) * TEXT_CELL_W * px,
              box.y + 2 * px + TEXT_CELL_H * big, px, sub);
}

/* --- settings menu ------------------------------------------------------- */

static int hotkey(App *app, Action a);
static int changelog_on_new_version(App *app, int force);
static void save_window_png(App *app, const char *file);

typedef enum { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_QUIT } MenuNav;

enum {
    MI_RESUME, MI_WIDESCREEN, MI_SCALING, MI_PALETTE, MI_FULLSCREEN, MI_VOLUME, MI_SOUND,
    MI_TIMER, MI_CATEGORY, MI_UPDATES, MI_CONTROLS, MI_CHANGELOG, MI_QUIT, MI_COUNT
};
static const char *const menu_labels[MI_COUNT] = {
    "Resume", "Widescreen", "Scaling", "Palette", "Fullscreen", "Volume", "Sound",
    "Speedrun timer", "Timer category", "Automatic updates", "Controls", "What's new",
    "Quit game",
};
static const int wide_values[] = { 0, -1, 1333, 1600, 1777, 2370, 3555 };
#define WIDE_VALUES (int)(sizeof wide_values / sizeof wide_values[0])

/* The actions the controls page lists, in order. */
static const Action control_actions[] = {
    ACT_A, ACT_B, ACT_SELECT, ACT_START, ACT_RIGHT, ACT_LEFT, ACT_UP, ACT_DOWN,
    ACT_FAST_FORWARD, ACT_MENU, ACT_TIMER, ACT_SCREENSHOT, ACT_FULLSCREEN,
};
#define CONTROL_ROWS (int)(sizeof control_actions / sizeof control_actions[0])
static const char *const control_labels[] = {
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down",
    "Fast forward", "Menu", "Timer", "Screenshot", "Fullscreen",
};

static int key_is(App *app, Action a, SDL_Scancode sc)
{
    for (int i = 0; i < BIND_MAX; i++)
        if (app->keys[a][i] == sc)
            return 1;
    return 0;
}

/* Menu navigation from the keyboard (fixed keys and the player's game
   buttons) and from every controller (D-pad, left stick, A/B/Start). */
static MenuNav menu_nav(App *app, const SDL_Event *e, int *stick)
{
    switch (e->type) {
    case SDL_QUIT:
        return NAV_QUIT;
    case SDL_KEYDOWN: {
        SDL_Scancode sc = e->key.keysym.scancode;
        if (sc == SDL_SCANCODE_UP || key_is(app, ACT_UP, sc)) return NAV_UP;
        if (sc == SDL_SCANCODE_DOWN || key_is(app, ACT_DOWN, sc)) return NAV_DOWN;
        if (sc == SDL_SCANCODE_LEFT || key_is(app, ACT_LEFT, sc)) return NAV_LEFT;
        if (sc == SDL_SCANCODE_RIGHT || key_is(app, ACT_RIGHT, sc)) return NAV_RIGHT;
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE ||
            key_is(app, ACT_A, sc) || key_is(app, ACT_START, sc))
            return NAV_OK;
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE || key_is(app, ACT_B, sc) ||
            key_is(app, ACT_MENU, sc))
            return NAV_BACK;
        return NAV_NONE;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return NAV_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return NAV_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return NAV_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NAV_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: return NAV_OK;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK:
        case SDL_CONTROLLER_BUTTON_GUIDE: return NAV_BACK;
        default: return NAV_NONE;
        }
    case SDL_CONTROLLERAXISMOTION: {
        if (e->caxis.axis != SDL_CONTROLLER_AXIS_LEFTX && e->caxis.axis != SDL_CONTROLLER_AXIS_LEFTY)
            return NAV_NONE;
        int v = e->caxis.value, dir = v > 20000 ? 1 : v < -20000 ? -1 : 0;
        int *last = &stick[e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY];
        if (dir == *last)
            return NAV_NONE;
        *last = dir;
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            return dir > 0 ? NAV_DOWN : dir < 0 ? NAV_UP : NAV_NONE;
        return dir > 0 ? NAV_RIGHT : dir < 0 ? NAV_LEFT : NAV_NONE;
    }
    default:
        return NAV_NONE;
    }
}

static void menu_value(App *app, int item, char *out, size_t len)
{
    out[0] = 0;
    switch (item) {
    case MI_WIDESCREEN: settings_widescreen_name(app->set.widescreen, out, len); break;
    case MI_SCALING: snprintf(out, len, "%s", settings_scaling_name(app->set.scaling)); break;
    case MI_PALETTE: {
        snprintf(out, len, "custom");
        for (int p = 0; p < PALETTE_PRESETS; p++) {
            uint32_t c[4];
            settings_palette_preset(settings_palette_name(p), c);
            if (!memcmp(c, app->set.palette, sizeof c))
                snprintf(out, len, "%s", settings_palette_name(p));
        }
        break;
    }
    case MI_FULLSCREEN: snprintf(out, len, "%s", app->set.fullscreen ? "on" : "off"); break;
    case MI_VOLUME: snprintf(out, len, "%d%%", app->set.volume); break;
    case MI_SOUND: snprintf(out, len, "%s", app->set.mute ? "off" : "on"); break;
    case MI_TIMER: snprintf(out, len, "%s", app->set.speedrun ? "on" : "off"); break;
    case MI_CATEGORY:
        snprintf(out, len, "%s%s", speedrun_category_title((SrCategory)app->set.speedrun_category),
                 app->set.speedrun && app->sr.phase == SR_RUNNING ? " (in a run)" : "");
        break;
    case MI_UPDATES: snprintf(out, len, "%s", app->set.update_check ? "on" : "off"); break;
    default: break;
    }
}

/* Changes a setting by one step (dir -1 / +1; OK counts as +1). */
static void menu_change(App *app, int item, int dir)
{
    switch (item) {
    case MI_WIDESCREEN: {
        int i = 0;
        for (int k = 0; k < WIDE_VALUES; k++)
            if (wide_values[k] == app->set.widescreen)
                i = k;
        app->set.widescreen = wide_values[(i + dir + WIDE_VALUES) % WIDE_VALUES];
        break;
    }
    case MI_SCALING:
        app->set.scaling = (ScalingMode)((app->set.scaling + dir + SCALE_MODES) % SCALE_MODES);
        break;
    case MI_PALETTE: {
        int cur = -1;
        for (int p = 0; p < PALETTE_PRESETS; p++) {
            uint32_t c[4];
            settings_palette_preset(settings_palette_name(p), c);
            if (!memcmp(c, app->set.palette, sizeof c))
                cur = p;
        }
        int next = cur < 0 ? 0 : (cur + dir + PALETTE_PRESETS) % PALETTE_PRESETS;
        settings_palette_preset(settings_palette_name(next), app->set.palette);
        break;
    }
    case MI_FULLSCREEN: set_fullscreen(app, !app->set.fullscreen); break;
    case MI_VOLUME: app->set.volume = SDL_max(0, SDL_min(100, (app->set.volume + 5) / 10 * 10 + dir * 10)); break;
    case MI_SOUND: app->set.mute = !app->set.mute; break;
    case MI_TIMER: hotkey(app, ACT_TIMER); break;
    case MI_CATEGORY:
        if (app->set.speedrun && app->sr.phase == SR_RUNNING)
            break;              /* a run keeps its category */
        sr_save(app);
        app->set.speedrun_category = (app->set.speedrun_category + 1) % SR_CATEGORIES;
        if (app->set.speedrun)
            sr_prepare(app);
        break;
    case MI_UPDATES: app->set.update_check = !app->set.update_check; break;
    default: break;
    }
}

static void binds_text(const char binds[BIND_MAX][BIND_NAME], char *out, size_t len)
{
    out[0] = 0;
    for (int i = 0; i < BIND_MAX; i++)
        if (binds[i][0]) {
            size_t n = strlen(out);
            snprintf(out + n, len - n, "%s%s", n ? ", " : "", binds[i]);
        }
    if (!out[0])
        snprintf(out, len, "-");
}

/* Draws a page: the game dimmed behind a box of rows, row sel highlighted. */
static void menu_draw(App *app, const char *title, char rows[][64], int n, int sel, const char *hint)
{
    enum { CANVAS_W = 320, CANVAS_H = 216, COLS = 50 };
    render_contents(app);
    int ow, oh;
    SDL_GetRendererOutputSize(app->renderer, &ow, &oh);
    int px = SDL_max(1, SDL_min(ow / CANVAS_W, oh / CANVAS_H));
    int line = TEXT_CELL_H + 1;
    int w = (COLS + 2) * TEXT_CELL_W * px, h = (n + 5) * line * px;
    SDL_Rect box = { (ow - w) / 2, (oh - h) / 2, w, h };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 120);
    SDL_RenderFillRect(app->renderer, NULL);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(app->renderer, &box);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
    int x0 = box.x + TEXT_CELL_W * px, y = box.y + line * px / 2;
    SDL_SetRenderDrawColor(app->renderer, 255, 200, 40, 255);
    text_draw(app->renderer, x0, y, px, title);
    y += 2 * line * px;
    for (int i = 0; i < n; i++, y += line * px) {
        if (i == sel) {
            SDL_Rect bar = { box.x + px, y - px, w - 2 * px, line * px };
            SDL_SetRenderDrawColor(app->renderer, 60, 90, 160, 255);
            SDL_RenderFillRect(app->renderer, &bar);
        }
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        text_draw(app->renderer, x0, y, px, rows[i]);
    }
    SDL_SetRenderDrawColor(app->renderer, 160, 160, 160, 255);
    text_draw(app->renderer, x0, y + line * px / 2, px, hint);
    SDL_RenderPresent(app->renderer);
}

/* Waits for the next key or controller button (or 6 s) and makes it the
   only binding of its kind for the action. */
static void capture_binding(App *app, Action a, const char *label)
{
    char rows[2][64], title[64];
    snprintf(title, sizeof title, "Controls: %s", label);
    snprintf(rows[0], sizeof rows[0], "Press a key or a controller button");
    snprintf(rows[1], sizeof rows[1], "(Esc or wait to cancel)");
    Uint32 until = SDL_GetTicks() + 6000;
    while (!SDL_TICKS_PASSED(SDL_GetTicks(), until)) {
        menu_draw(app, title, rows, 2, -1, "");
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100))
            continue;
        if (e.type == SDL_QUIT) {
            SDL_PushEvent(&e);
            return;
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                return;
            const char *name = SDL_GetScancodeName(e.key.keysym.scancode);
            if (!name[0])
                continue;
            memset(app->set.keys[a], 0, sizeof app->set.keys[a]);
            snprintf(app->set.keys[a][0], BIND_NAME, "%s", name);
            break;
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            const char *name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button);
            if (!name)
                continue;
            memset(app->set.buttons[a], 0, sizeof app->set.buttons[a]);
            snprintf(app->set.buttons[a][0], BIND_NAME, "%s", name);
            break;
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED)
            add_pad(app, e.cdevice.which);
    }
    resolve_bindings(app);
}

/* The controls page. Returns 1 when the window was closed. */
static int controls_page(App *app)
{
    int sel = 0, stick[2] = { 0, 0 };
    enum { RESET = CONTROL_ROWS, BACK, ROWS };
    for (;;) {
        char rows[ROWS][64];
        for (int i = 0; i < CONTROL_ROWS; i++) {
            char k[64], b[64];
            binds_text(app->set.keys[control_actions[i]], k, sizeof k);
            binds_text(app->set.buttons[control_actions[i]], b, sizeof b);
            snprintf(rows[i], sizeof rows[i], "%-13s %-20.20s %.15s", control_labels[i], k, b);
        }
        snprintf(rows[RESET], sizeof rows[RESET], "Reset all controls");
        snprintf(rows[BACK], sizeof rows[BACK], "Back");
        menu_draw(app, "Controls            keyboard             controller", rows, ROWS, sel,
                  "A: change   B: back");
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100))
            continue;
        if (e.type == SDL_CONTROLLERDEVICEADDED)
            add_pad(app, e.cdevice.which);
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED)
            remove_pad(app, e.cdevice.which);
        switch (menu_nav(app, &e, stick)) {
        case NAV_QUIT: return 1;
        case NAV_UP: sel = (sel + ROWS - 1) % ROWS; break;
        case NAV_DOWN: sel = (sel + 1) % ROWS; break;
        case NAV_BACK: return 0;
        case NAV_OK:
            if (sel == BACK)
                return 0;
            if (sel == RESET) {
                Settings d;
                settings_defaults(&d);
                memcpy(app->set.keys, d.keys, sizeof d.keys);
                memcpy(app->set.buttons, d.buttons, sizeof d.buttons);
                resolve_bindings(app);
            } else {
                capture_binding(app, control_actions[sel], control_labels[sel]);
            }
            break;
        default: break;
        }
    }
}

/* The settings menu. The game stands still while it is open: no frame is
   run, so nothing here can change what the game computes. */
static void run_menu(App *app)
{
    if (app->headless || !app->renderer)
        return;
    if (app->audio) {
        SDL_ClearQueuedAudio(app->audio);
        SDL_PauseAudioDevice(app->audio, 1);
    }
    int sel = 0, stick[2] = { 0, 0 }, open = 1;
    while (open) {
        char rows[MI_COUNT][64];
        for (int i = 0; i < MI_COUNT; i++) {
            char v[40];
            menu_value(app, i, v, sizeof v);
            snprintf(rows[i], sizeof rows[i], "%-20s %s%s", menu_labels[i], v,
                     i == MI_CONTROLS ? "..." : "");
        }
        menu_draw(app, "Super Mario Land " SML_VERSION, rows, MI_COUNT, sel,
                  "Up/Down: choose  Left/Right: change  B: back");
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100))
            continue;
        if (e.type == SDL_CONTROLLERDEVICEADDED)
            add_pad(app, e.cdevice.which);
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED)
            remove_pad(app, e.cdevice.which);
        else if (e.type == SDL_KEYDOWN && key_is(app, ACT_SCREENSHOT, e.key.keysym.scancode)) {
            char path[1200];
            snprintf(path, sizeof path, "%smenu.png", data_dir());
            save_window_png(app, path);
            continue;
        }
        MenuNav nav = menu_nav(app, &e, stick);
        switch (nav) {
        case NAV_QUIT:
            core_quit();
            open = 0;
            break;
        case NAV_UP: sel = (sel + MI_COUNT - 1) % MI_COUNT; break;
        case NAV_DOWN: sel = (sel + 1) % MI_COUNT; break;
        case NAV_BACK: open = 0; break;
        case NAV_LEFT: case NAV_RIGHT:
            menu_change(app, sel, nav == NAV_LEFT ? -1 : 1);
            break;
        case NAV_OK:
            if (sel == MI_RESUME)
                open = 0;
            else if (sel == MI_QUIT) {
                core_quit();
                open = 0;
            } else if (sel == MI_CONTROLS) {
                if (controls_page(app)) {
                    core_quit();
                    open = 0;
                }
            } else if (sel == MI_CHANGELOG) {
                if (changelog_on_new_version(app, 1) < 0) {
                    core_quit();
                    open = 0;
                }
            } else
                menu_change(app, sel, 1);
            break;
        default: break;
        }
    }
    save_changes(app);
    app->release_wait = 1;
    app->next_frame_time = 0;
    app->last_present = 0;
    app->present_samples = 0;
    if (app->audio)
        SDL_PauseAudioDevice(app->audio, 0);
}

/* --- events ---------------------------------------------------------------- */

/* A controller button press completes binding i of action a: the button
   itself, or either button of a pair with the other one held. */
static int pad_chord_pressed(App *app, Action a, int i, const SDL_ControllerButtonEvent *ev)
{
    SDL_GameControllerButton b = app->buttons[a][i], b2 = app->buttons2[a][i];
    if (b2 == SDL_CONTROLLER_BUTTON_INVALID)
        return b != SDL_CONTROLLER_BUTTON_INVALID && b == ev->button;
    if (ev->button != b && ev->button != b2)
        return 0;
    SDL_GameController *pad = SDL_GameControllerFromInstanceID(ev->which);
    return pad && SDL_GameControllerGetButton(pad, ev->button == b ? b2 : b);
}

static int hotkey(App *app, Action a)
{
    switch (a) {
    case ACT_QUIT:
        core_quit();
        return 1;
    case ACT_FULLSCREEN:
        set_fullscreen(app, !app->set.fullscreen);
        return 1;
    case ACT_SCALING: {
        app->set.scaling = (ScalingMode)((app->set.scaling + 1) % SCALE_MODES);
        char msg[64];
        snprintf(msg, sizeof msg, "scaling: %s", settings_scaling_name(app->set.scaling));
        update_title(app, msg);
        return 1;
    }
    case ACT_PALETTE: {
        int next = 0;
        for (int p = 0; p < PALETTE_PRESETS; p++) {
            uint32_t c[4];
            settings_palette_preset(settings_palette_name(p), c);
            if (!memcmp(c, app->set.palette, sizeof c))
                next = (p + 1) % PALETTE_PRESETS;
        }
        settings_palette_preset(settings_palette_name(next), app->set.palette);
        char msg[64];
        snprintf(msg, sizeof msg, "palette: %s", settings_palette_name(next));
        update_title(app, msg);
        return 1;
    }
    case ACT_SCREENSHOT:
        take_screenshot(app);
        return 1;
    case ACT_MENU:
        run_menu(app);
        return 1;
    case ACT_TIMER:
        app->set.speedrun = !app->set.speedrun;
        if (app->set.speedrun)
            sr_prepare(app);
        else
            sr_save(app);
        update_title(app, app->set.speedrun ? "speedrun timer on" : "speedrun timer off");
        return 1;
    default:
        return 0;
    }
}

static void handle_events(App *app)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            core_quit();
            break;
        case SDL_KEYDOWN:
            if (e.key.repeat)
                break;
            if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                hotkey(app, ACT_FULLSCREEN);
                break;
            }
            /* The menu wins over other actions on the same key (older
               settings files still bind Escape to quit). */
            for (int a = ACT_COUNT - 1, done = 0; a >= ACT_GAME_BUTTONS && !done; a--)
                for (int i = 0; i < BIND_MAX && !done; i++)
                    if (app->keys[a][i] == e.key.keysym.scancode)
                        done = hotkey(app, (Action)a);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            for (int a = ACT_COUNT - 1, done = 0; a >= ACT_GAME_BUTTONS && !done; a--)
                for (int i = 0; i < BIND_MAX && !done; i++)
                    if (pad_chord_pressed(app, (Action)a, i, &e.cbutton))
                        done = hotkey(app, (Action)a);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            add_pad(app, e.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            remove_pad(app, e.cdevice.which);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
                choose_pacing(app);
            break;
        }
    }
    if (app->title_until && SDL_TICKS_PASSED(SDL_GetTicks(), app->title_until))
        update_title(app, NULL);
    const char *news = update_message();
    if (news) {
        snprintf(app->banner, sizeof app->banner, "%s", news);
        app->banner_until = SDL_GetTicks() + 8000;
        update_title(app, news);
        app->title_until = SDL_GetTicks() + 8000;
    }
    if (app->banner_until && SDL_TICKS_PASSED(SDL_GetTicks(), app->banner_until))
        app->banner_until = 0;
}

/* --- core callbacks --------------------------------------------------- */

/* Headless pictures: the game's 160x144, or the widescreen picture (16:9
   for "on"), as grey shades; with frame >= 0 numbered PATH-NNNNNN.png. */
static void headless_picture(App *app, long frame)
{
    int w = app->set.widescreen ? settings_wide_width(app->set.widescreen > 0 ? app->set.widescreen : 1777, 0, 0)
                                : LCD_W;
    char path[1200];
    if (frame >= 0)
        snprintf(path, sizeof path, "%s-%06ld.png", app->screenshot_path, frame);
    else
        snprintf(path, sizeof path, "%s", app->screenshot_path);
    compose_picture(app, w);
    png_write_shades(path, app->wide, w, LCD_H, 1);
}


static void on_frame(void *user)
{
    App *app = user;
    app->frames++;
    widescreen_track(&app->wide_state, core_machine());
    if (app->set.speedrun)
        sr_on_frame(app);
    if (app->headless) {
        core_machine()->apu.out_len = 0;
        if (app->screenshot_every && app->screenshot_path && app->frames % app->screenshot_every == 0)
            headless_picture(app, app->frames);
        if (app->frames >= app->headless_frames)
            core_quit();
        return;
    }
    if (app->headless_frames && app->frames >= app->headless_frames)
        core_quit();
    handle_events(app);
    app->held = read_buttons(app);

    if (app->fast_forward) {
        /* Show at most ~60 frames per second and never wait for vsync. */
        set_vsync(app, 0);
        double t = now_ticks();
        if (t - app->last_ff_present >= app->perf_freq / 60) {
            app->last_ff_present = t;
            render_frame(app);
        }
        queue_audio(app);
        if (app->set.fast_forward_speed > 0)
            clock_pacing(app, 1.0 / (GB_FPS * app->set.fast_forward_speed));
        else
            app->next_frame_time = 0;
        app->present_samples = 0;
        return;
    }

    set_vsync(app, app->set.vsync || app->vsync_pacing);
    render_frame(app);
    queue_audio(app);
    if (app->vsync_pacing && app->vsync_on) {
        /* Never faster than 63.5 fps, should present return early. */
        double t = now_ticks(), min_step = app->perf_freq / 63.5;
        if (app->last_present > 0 && t - app->last_present < min_step) {
            wait_until(app, app->last_present + min_step);
            t = now_ticks();
        }
        measure_refresh(app, t);
    } else
        clock_pacing(app, 1.0 / GB_FPS);
}

static uint8_t on_poll(void *user)
{
    App *app = user;
    uint8_t b;
    if (app->playing_movie)
        b = movie_get(&app->movie, (size_t)core_poll_count());
    else
        b = app->held;
    if (app->record_path)
        movie_append(&app->recording, b);
    return b;
}

/* --- changelog ------------------------------------------------------------ */

/* Saves the window as PNG in the data directory (F12 on the changelog). */
static void save_window_png(App *app, const char *file)
{
    int w, h;
    SDL_GetRendererOutputSize(app->renderer, &w, &h);
    uint8_t *rgba = malloc((size_t)w * h * 4), *rgb = malloc((size_t)w * h * 3);
    if (rgba && rgb && SDL_RenderReadPixels(app->renderer, NULL, SDL_PIXELFORMAT_RGBA32, rgba, w * 4) == 0) {
        for (int i = 0; i < w * h; i++)
            memcpy(rgb + i * 3, rgba + i * 4, 3);
        char path[1200];
        if (file)
            snprintf(path, sizeof path, "%s", file);
        else
            snprintf(path, sizeof path, "%schangelog.png", data_dir());
        png_write_rgb(path, rgb, w, h);
    }
    free(rgba);
    free(rgb);
}

/* Shows the changelog until a button is pressed; Up/Down scroll. Returns 0,
   or -1 when the window was closed. */
static int show_changelog(App *app, const char *text)
{
    static char wrapped[65536];
    enum { COLS = 52, CANVAS_W = 320, CANVAS_H = 216 };
    int lines = text_wrap(text, COLS, wrapped, sizeof wrapped), top = 0, redraw = 1;
    for (;;) {
        SDL_Event e;
        int ow, oh;
        SDL_GetRendererOutputSize(app->renderer, &ow, &oh);
        int px = SDL_max(1, SDL_min(ow / CANVAS_W, oh / CANVAS_H));
        int x0 = (ow - (COLS * TEXT_CELL_W) * px) / 2, y0 = (oh - CANVAS_H * px) / 2 + 4 * px;
        int rows = (CANVAS_H - 4 * TEXT_CELL_H) / TEXT_CELL_H;
        if (redraw) {
            const uint32_t *pal = app->set.palette;
            SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
            SDL_RenderClear(app->renderer);
            SDL_SetRenderDrawColor(app->renderer, pal[0] >> 16 & 255, pal[0] >> 8 & 255, pal[0] & 255, 255);
            text_draw(app->renderer, x0, y0, px, "What's new in Super Mario Land " SML_VERSION);
            const char *line = wrapped;
            for (int i = 0; i < top && line; i++)
                line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL;
            SDL_SetRenderDrawColor(app->renderer, pal[1] >> 16 & 255, pal[1] >> 8 & 255, pal[1] & 255, 255);
            for (int r = 0; r < rows && line && *line; r++) {
                char buf[COLS + 8];
                size_t len = strcspn(line, "\n");
                snprintf(buf, sizeof buf, "%.*s", (int)SDL_min(len, sizeof buf - 1), line);
                text_draw(app->renderer, x0, y0 + (r + 2) * TEXT_CELL_H * px, px, buf);
                line += len + (line[len] == '\n');
            }
            SDL_SetRenderDrawColor(app->renderer, pal[0] >> 16 & 255, pal[0] >> 8 & 255, pal[0] & 255, 255);
            text_draw(app->renderer, x0, y0 + (rows + 3) * TEXT_CELL_H * px, px,
                      lines > rows ? "A / Start / Enter: play     Up / Down: scroll"
                                   : "A / Start / Enter: play");
            SDL_RenderPresent(app->renderer);
            redraw = 0;
        }
        if (!SDL_WaitEventTimeout(&e, 100))
            continue;
        int step = 0, go = 0;
        switch (e.type) {
        case SDL_QUIT:
            return -1;
        case SDL_WINDOWEVENT:
            redraw = 1;
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_UP: step = -1; break;
            case SDLK_DOWN: step = 1; break;
            case SDLK_PAGEUP: step = -rows; break;
            case SDLK_PAGEDOWN: step = rows; break;
            case SDLK_F12: save_window_png(app, NULL); break;
            case SDLK_RETURN: case SDLK_SPACE: case SDLK_ESCAPE: case SDLK_x: go = 1; break;
            default: break;
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            add_pad(app, e.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: step = -1; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: step = 1; break;
            case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_B:
            case SDL_CONTROLLER_BUTTON_START: go = 1; break;
            default: break;
            }
            break;
        }
        if (go)
            return 0;
        if (step) {
            top = SDL_max(0, SDL_min(top + step, lines - rows));
            redraw = 1;
        }
    }
}

/* The first start of a new version shows its changelog sections. */
static int changelog_on_new_version(App *app, int force)
{
    char path[1200], seen[64] = "";
    snprintf(path, sizeof path, "%slast_version.txt", data_dir());
    FILE *f = data_dir()[0] ? fopen(path, "r") : NULL;
    if (f) {
        if (!fgets(seen, sizeof seen, f))
            seen[0] = 0;
        seen[strcspn(seen, "\r\n")] = 0;
        fclose(f);
    }
    int is_release = version_compare(SML_VERSION, SML_VERSION) == 0;
    char *text = NULL;
    if (force)
        text = changelog_since(changelog_text, "0", is_release ? SML_VERSION : "99999");
    else if (is_release && strcmp(seen, SML_VERSION))
        text = changelog_since(changelog_text, seen, SML_VERSION);
    char *shown = text ? changelog_reflow(text) : NULL;
    int r = shown ? show_changelog(app, shown) : 0;
    free(shown);
    free(text);
    if (r == 0 && is_release && strcmp(seen, SML_VERSION) && data_dir()[0] && (f = fopen(path, "w"))) {
        fprintf(f, "%s\n", SML_VERSION);
        fclose(f);
    }
    return r;
}

/* --- start-up -------------------------------------------------------------- */

static uint8_t *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (8 << 20)) { fclose(f); return NULL; }
    uint8_t *data = malloc((size_t)n);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static uint8_t *find_rom(const char *explicit_path, size_t *size, char *used, size_t used_len)
{
    if (explicit_path) {
        snprintf(used, used_len, "%s", explicit_path);
        return load_file(explicit_path, size);
    }
    char paths[6][1100];
    int n = 0;
    snprintf(paths[n++], sizeof paths[0], "sml.gb");
    snprintf(paths[n++], sizeof paths[0], "rom/sml.gb");
    /* next to the .AppImage file (the base path is inside its mount) */
    const char *appimage = getenv("APPIMAGE");
    const char *slash = appimage ? strrchr(appimage, '/') : NULL;
    if (slash)
        snprintf(paths[n++], sizeof paths[0], "%.*ssml.gb", (int)(slash - appimage + 1), appimage);
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(paths[n++], sizeof paths[0], "%ssml.gb", base);
        snprintf(paths[n++], sizeof paths[0], "%s../rom/sml.gb", base);
        SDL_free(base);
    }
    if (data_dir()[0])
        snprintf(paths[n++], sizeof paths[0], "%ssml.gb", data_dir());
    for (int i = 0; i < n; i++) {
        uint8_t *d = load_file(paths[i], size);
        if (d) {
            snprintf(used, used_len, "%.1099s", paths[i]);
            return d;
        }
    }
    return NULL;
}

/* The Warpzone launcher hands over the player's ROM once with --rom; keep a
   copy as sml.gb in the data directory so later starts find it. */
static void keep_launcher_rom(const char *rom_path, const uint8_t *rom, size_t size)
{
    const char *env = getenv("WARPZONE_LAUNCHED");
    if (!rom_path || !env || strcmp(env, "1") || !data_dir()[0])
        return;
    char path[1100];
    snprintf(path, sizeof path, "%ssml.gb", data_dir());
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(path, "wb");
    if (!f)
        return;
    int ok = fwrite(rom, 1, size, f) == size;
    if (fclose(f) || !ok)
        remove(path);
}

static void usage(FILE *f)
{
    fprintf(f,
            "usage: sml [options]\n"
            "  --rom FILE                  Super Mario Land (World) Rev A ROM\n"
            "  --model dmg|cgb|agb         console to model (default dmg)\n"
            "  --config FILE               settings file\n"
            "  --scale N                   initial window size (x 160x144)\n"
            "  --scaling integer|sharp|nearest\n"
            "  --sync auto|clock|vsync     frame pacing\n"
            "  --fullscreen | --windowed\n"
            "  --palette green|gray|pocket|\"#RRGGBB #RRGGBB #RRGGBB #RRGGBB\"\n"
            "  --widescreen off|on|W:H   show more of the level (on: fill the window)\n"
            "  --mute | --volume N         (0..100)\n"
            "  --movie FILE                play back an input movie\n"
            "  --record FILE               record input to a movie on exit\n"
            "  --headless N [--screenshot FILE]\n"
            "                              run N frames without window or audio\n"
            "  --frames N                  quit after N frames\n"
            "  --screenshot FILE           save the last frame (the window) as PNG\n"
            "  --screenshot-every N        with --headless: PNG series FILE-NNNNNN.png\n"
            "  --stats                     print pacing/audio statistics on exit\n"
            "  --no-update                 do not look for new releases\n"
            "  --changelog                 show the changelog first\n"
            "  --timer                     show the speedrun timer (F8 toggles)\n"
            "  --category any|normal+hard  speedrun category\n"
            "  --import-lss FILE           use a LiveSplit file's PB and best segments\n"
            "  --speedrun-report           with --headless: print the timer's splits\n"
            "  --version                   print the version\n");
}

int main(int argc, char **argv)
{
    static App app;
    const char *rom_path = NULL, *movie_path = NULL, *config_path = NULL, *lss_path = NULL;
    int no_update = 0, force_changelog = 0;
    MachineModel model = MODEL_DMG;
    /* options that override the settings file: section, key, value */
    const char *over[32][3];
    int n_over = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has = i + 1 < argc;
#define OVERRIDE(sec, key, val) do { if (n_over < 32) { over[n_over][0] = sec; \
            over[n_over][1] = key; over[n_over][2] = val; n_over++; } } while (0)
        if (!strcmp(a, "--rom") && has) rom_path = argv[++i];
        else if (!strcmp(a, "--model") && has) {
            int mm = machine_model_from_name(argv[++i]);
            if (mm < 0) { fprintf(stderr, "unknown model %s\n", argv[i]); return 2; }
            model = (MachineModel)mm;
        } else if (!strcmp(a, "--config") && has) config_path = argv[++i];
        else if (!strcmp(a, "--scale") && has) OVERRIDE("video", "scale", argv[++i]);
        else if (!strcmp(a, "--scaling") && has) OVERRIDE("video", "scaling", argv[++i]);
        else if (!strcmp(a, "--sync") && has) OVERRIDE("video", "sync", argv[++i]);
        else if (!strcmp(a, "--palette") && has) OVERRIDE("video", "palette", argv[++i]);
        else if (!strcmp(a, "--widescreen") && has) OVERRIDE("video", "widescreen", argv[++i]);
        else if (!strcmp(a, "--fullscreen")) OVERRIDE("video", "fullscreen", "yes");
        else if (!strcmp(a, "--windowed")) OVERRIDE("video", "fullscreen", "no");
        else if (!strcmp(a, "--mute")) OVERRIDE("audio", "mute", "yes");
        else if (!strcmp(a, "--volume") && has) OVERRIDE("audio", "volume", argv[++i]);
        else if (!strcmp(a, "--movie") && has) movie_path = argv[++i];
        else if (!strcmp(a, "--record") && has) app.record_path = argv[++i];
        else if (!strcmp(a, "--headless") && has) {
            app.headless = 1;
            app.headless_frames = atol(argv[++i]);
        } else if (!strcmp(a, "--frames") && has) app.headless_frames = atol(argv[++i]);
        else if (!strcmp(a, "--screenshot") && has) app.screenshot_path = argv[++i];
        else if (!strcmp(a, "--screenshot-every") && has) app.screenshot_every = atol(argv[++i]);
        else if (!strcmp(a, "--stats")) app.stats = 1;
        else if (!strcmp(a, "--no-update")) no_update = 1;
        else if (!strcmp(a, "--changelog")) force_changelog = 1;
        else if (!strcmp(a, "--timer")) OVERRIDE("speedrun", "timer", "yes");
        else if (!strcmp(a, "--category") && has) OVERRIDE("speedrun", "category", argv[++i]);
        else if (!strcmp(a, "--import-lss") && has) lss_path = argv[++i];
        else if (!strcmp(a, "--speedrun-report")) {
            app.sr_report = 1;
            OVERRIDE("speedrun", "timer", "yes");
        }
        else if (!strcmp(a, "--version")) { printf("%s\n", SML_VERSION); return 0; }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); return 0; }
        else {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            return 2;
        }
#undef OVERRIDE
    }

    settings_defaults(&app.set);
    if (!app.headless)
        load_settings(&app, config_path);
    for (int i = 0; i < n_over; i++) {
        char err[200];
        if (settings_set(&app.set, over[i][0], over[i][1], over[i][2], err, sizeof err)) {
            fprintf(stderr, "%s\n", err);
            return 2;
        }
    }

    app.base = app.set;
    if (app.set.speedrun)
        sr_prepare(&app);
    if (lss_path && import_lss(&app, lss_path))
        return 1;

    if (!core_has_access_table())
        return fail(app.headless, "This is a no-ROM CI build without the locally generated "
                    "access table; it cannot run the game. Build the port locally "
                    "(docs/BUILDING.md).");
    size_t rom_size = 0;
    char rom_used[1100] = "";
    uint8_t *rom = find_rom(rom_path, &rom_size, rom_used, sizeof rom_used);
    if (!rom) {
        if (rom_path)
            return fail(app.headless, "cannot read %s", rom_path);
        return fail(app.headless, "Super Mario Land ROM not found.\n\nPut your copy of the "
                    "cartridge as sml.gb in %s or next to the program, or pass --rom FILE.",
                    data_dir()[0] ? data_dir() : "the current directory");
    }
    RomVersion version = core_identify_rom(rom, rom_size);
    if (version != ROM_SML_V11)
        return fail(app.headless, "%s: %s.\n\nThis port needs Super Mario Land (World) "
                    "(Rev A / v1.1), SHA1 418203621b887caa090215d97e3f509b79affd3e.",
                    rom_used, core_rom_description(version));
    keep_launcher_rom(rom_path, rom, rom_size);
    app.rom = rom;
    app.rom_size = rom_size;
    if (movie_path) {
        if (movie_load(&app.movie, movie_path)) {
            fprintf(stderr, "cannot load movie %s\n", movie_path);
            return 1;
        }
        app.playing_movie = 1;
    }

    fault_install();
    SDL_SetMainReady();
    if (app.headless)
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO | (app.headless ? 0 : SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) != 0) {
        fault_report();
        return fail(app.headless, "cannot start SDL: %s", SDL_GetError());
    }
    app.perf_freq = (double)SDL_GetPerformanceFrequency();
    Machine *m = core_machine();
    m->apu.out = app.audio_buf;
    m->apu.out_cap = sizeof app.audio_buf / (2 * sizeof app.audio_buf[0]);

    if (!app.headless) {
        int scale = app.set.scale;
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
            while (scale > 1 && (LCD_W * scale > usable.w || LCD_H * scale > usable.h))
                scale--;
        app.window = SDL_CreateWindow("Super Mario Land", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      LCD_W * scale, LCD_H * scale,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                      (app.set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!app.window) {
            fault_report();
            return fail(0, "cannot create window: %s", SDL_GetError());
        }
        SDL_SetWindowMinimumSize(app.window, LCD_W, LCD_H);
        if (app.set.fullscreen)
            SDL_ShowCursor(SDL_DISABLE);
        int want_vsync = app.set.vsync || app.set.sync == SYNC_VSYNC;
        app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED |
                                          (want_vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
        if (!app.renderer)
            app.renderer = SDL_CreateRenderer(app.window, -1, 0);
        if (!app.renderer) {
            fault_report();
            return fail(0, "cannot create renderer: %s", SDL_GetError());
        }
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(app.renderer, &info) == 0)
            app.vsync_on = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
        app.texture = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, WIDE_MAX_W, LCD_H);
        if (!app.texture) {
            fault_report();
            return fail(0, "cannot create texture: %s", SDL_GetError());
        }
        SDL_SetTextureScaleMode(app.texture, SDL_ScaleModeNearest);

        SDL_AudioSpec want = { 0 }, have;
        want.freq = APU_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        app.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (app.audio) {
            app.out_rate = have.freq;
            app.audio_in_rate = APU_SAMPLE_RATE;
            audio_sync_init(&app.sync, APU_SAMPLE_RATE, app.out_rate, 0.005);
            SDL_PauseAudioDevice(app.audio, 0);
        } else {
            fprintf(stderr, "no audio: %s\n", SDL_GetError());
        }
        resolve_bindings(&app);
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            add_pad(&app, i);
        choose_pacing(&app);
    }

    if (!app.headless) {
        const char *env = getenv("SML_NO_UPDATE");
        if (app.set.update_check && !no_update && !(env && !strcmp(env, "1")))
            update_start(data_dir());
    }
    int closed = !app.headless && changelog_on_new_version(&app, force_changelog);

    CoreCallbacks cb = { &app, on_frame, on_poll };
    app.start_time = now_ticks();
    if (!closed)
        core_run(model, rom, rom_size, &cb);

    if (app.stats && !app.headless) {
        double secs = (now_ticks() - app.start_time) / app.perf_freq;
        printf("frames=%ld seconds=%.2f fps=%.3f pacing=%s vsync=%d\n", app.frames, secs,
               app.frames / secs, app.vsync_pacing ? "vsync" : "clock", app.vsync_on);
        if (app.vsync_pacing)
            printf("refresh=%.3f Hz\n", app.present_interval > 0 ? 1 / app.present_interval : 0);
        if (app.audio && app.queue_samples)
            printf("audio: device %d Hz, nominal input %.1f Hz, last 10 s: queue avg %.1f ms min %.1f ms "
                   "(target %d ms), underruns %ld, last adjust %+.4f%%\n",
                   app.out_rate, app.audio_in_rate,
                   app.queue_sum / app.queue_samples * 1000 / app.out_rate,
                   app.queue_min * 1000 / app.out_rate, app.set.latency_ms, app.underruns,
                   app.sync.last_adjust * 100);
    }

    if (app.screenshot_path && app.headless && !app.screenshot_every)
        headless_picture(&app, -1);
    else if (app.screenshot_path && app.renderer) {
        render_contents(&app);
        save_window_png(&app, app.screenshot_path);
    }
    if (app.record_path && movie_save(&app.recording, app.record_path))
        fprintf(stderr, "cannot write %s\n", app.record_path);
    if (app.headless)
        printf("frames=%ld polls=%ld\n", app.frames, core_poll_count());
    if (app.sr_report && app.sr.phase == SR_RUNNING)
        printf("running %d splits\n", app.sr.split);
    sr_save(&app);
    save_changes(&app);
    for (int p = 0; p < MAX_PADS; p++)
        if (app.pads[p])
            SDL_GameControllerClose(app.pads[p]);
    if (app.audio)
        SDL_CloseAudioDevice(app.audio);
    if (app.prescaled)
        SDL_DestroyTexture(app.prescaled);
    SDL_Quit();
    free(rom);
    return 0;
}
