/*
 * Built-in speedrun timer: automatic start, one split per level, end on
 * Tatanga's defeat, personal best and best segments, LiveSplit .lss files.
 *
 * Timing follows the speedrun.com rules for Super Mario Land (checked
 * 2026-10-16, real-time timing): it starts on the frame before 1-1 and its
 * status bar first appear after Start on the title screen, and ends on the
 * first frame in which Tatanga's sprite has disappeared. Time is counted in
 * emulated Game Boy cycles (4194304 per second) between those frames, so it
 * does not depend on the host's display rate, pauses of the window or fast
 * forward, and equals the same run on a console.
 *
 * Splits are taken on the frame before the next level first appears (the
 * same rule as the start), the last one when Tatanga disappears. Normal +
 * Hard Mode continues through the ending into the second quest.
 *
 * The timer only reads game memory once per frame; it never changes game
 * state. No SDL here, so it can be unit-tested.
 */
#ifndef SML_SPEEDRUN_H
#define SML_SPEEDRUN_H

#include <stddef.h>
#include <stdint.h>

#define SR_CLOCK 4194304
#define SR_MAX_SPLITS 24
#define SR_HISTORY 32

typedef enum { SR_ANY, SR_NORMAL_HARD, SR_CATEGORIES } SrCategory;

typedef enum { SR_IDLE, SR_ARMED, SR_RUNNING, SR_FINISHED } SrPhase;

typedef enum { SR_EV_NONE, SR_EV_START, SR_EV_SPLIT, SR_EV_FINISH, SR_EV_RESET } SrEvent;

/* What the timer looks at, sampled once per video frame. */
typedef struct {
    uint64_t cycles;          /* emulated T-cycles since power on */
    uint8_t state;            /* hGameState (FFB3) */
    uint8_t world_level;      /* hWorldAndLevel (FFB4), BCD */
    uint8_t demo;             /* attract-mode demo flag (FF9F) */
    uint8_t boss_defeated;    /* D007, $60 once Tatanga is killed */
    uint8_t tatanga_visible;  /* one of Tatanga's sprite tiles is in OAM */
} SrFrame;

typedef struct {
    SrCategory category;
    int nsplits;
    /* saved */
    long attempts, completed;
    int64_t pb[SR_MAX_SPLITS];      /* personal best, cumulative cycles; -1 = none */
    int64_t best[SR_MAX_SPLITS];    /* best segments in cycles; -1 = none */
    int64_t history[SR_HISTORY];    /* last finished runs, newest first; -1 = none */
    /* the current attempt */
    SrPhase phase;
    uint64_t start, prev_cycles, now;
    int split;                      /* splits done */
    int64_t times[SR_MAX_SPLITS];   /* cumulative split times of this attempt */
    int64_t cmp[SR_MAX_SPLITS];     /* the personal best when it started */
    uint8_t level;                  /* level being played */
    int level_done;                 /* Tatanga defeated, waiting for the next quest */
    int gold;                       /* the last split was a best segment */
    int new_pb;                     /* the finished run is a new personal best */
    int fast_forwarded;             /* fast forward was used during this attempt */
    int dirty;                      /* saved data changed */
} Speedrun;

void speedrun_init(Speedrun *sr, SrCategory category);

/* Fills an SrFrame from game memory (wram C000-DFFF, hram FF80-FFFE,
   oam FE00-FE9F). */
void speedrun_sample(SrFrame *f, uint64_t cycles, const uint8_t *wram,
                     const uint8_t *hram, const uint8_t *oam);

/* Feeds one video frame. Returns what happened in it. */
SrEvent speedrun_frame(Speedrun *sr, const SrFrame *f);

/* Elapsed time of the current or last attempt in cycles (0 when idle). */
int64_t speedrun_elapsed(const Speedrun *sr);

/* Difference to the personal best (as it was when the attempt started) at
   split `i` of the current attempt
   (cycles, negative = ahead). Returns 0 and leaves *delta alone if there
   is no comparison. */
int speedrun_delta(const Speedrun *sr, int i, int64_t *delta);

/* Sum of best segments, or -1 if a segment has no time yet. */
int64_t speedrun_sum_of_best(const Speedrun *sr);

const char *speedrun_category_name(SrCategory c);   /* "any", "normal+hard" */
const char *speedrun_category_title(SrCategory c);  /* "Any%", ... */
int speedrun_category_from_name(const char *name);  /* -1 if unknown */
/* "1-1" .. "4-3", "Hard 1-1" .. */
const char *speedrun_split_name(const Speedrun *sr, int i);

/* "12:17.03" (minutes, seconds, hundredths); "-0.53"/"+1.20" with sign. */
void speedrun_format(int64_t cycles, int sign, char *out, size_t len);

/* Own split file (text, cycles). load: 0 ok, 1 missing, -1 error. */
int speedrun_load(Speedrun *sr, const char *path);
int speedrun_save(const Speedrun *sr, const char *path);

/* LiveSplit .lss with the personal best, best segments, attempt count and
   history as real-time values. */
int speedrun_export_lss(const Speedrun *sr, const char *path);
/* Reads segment names, "Personal Best" split times, best segments and the
   attempt count of a LiveSplit file. The segment count must match the
   category. Returns 0 or -1 with a message. */
int speedrun_import_lss(Speedrun *sr, const char *xml, char *err, size_t err_len);

#endif
