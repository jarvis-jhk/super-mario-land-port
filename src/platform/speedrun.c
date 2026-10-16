/* Speedrun timer, see speedrun.h. */
#include "speedrun.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Game states (hGameState) the timer distinguishes */
enum {
    GS_PLAYING = 0x00,
    GS_AUTOSCROLL = 0x0D,
    GS_PREPARE_MENU = 0x0E,
    GS_MENU = 0x0F,
};

#define WORLD_1_1 0x11
#define WORLD_4_3 0x43
#define TATANGA_DEFEATED 0x60
#define LEVELS 12

static const char *const category_names[SR_CATEGORIES] = { "any", "normal+hard" };
static const char *const category_titles[SR_CATEGORIES] = { "Any%", "Normal + Hard Mode" };

const char *speedrun_category_name(SrCategory c) { return c < SR_CATEGORIES ? category_names[c] : "?"; }
const char *speedrun_category_title(SrCategory c) { return c < SR_CATEGORIES ? category_titles[c] : "?"; }

int speedrun_category_from_name(const char *name)
{
    for (int c = 0; c < SR_CATEGORIES; c++)
        if (!strcmp(name, category_names[c]))
            return c;
    return -1;
}

const char *speedrun_split_name(const Speedrun *sr, int i)
{
    static const char *const names[SR_MAX_SPLITS] = {
        "1-1", "1-2", "1-3", "2-1", "2-2", "2-3", "3-1", "3-2", "3-3", "4-1", "4-2", "4-3",
        "Hard 1-1", "Hard 1-2", "Hard 1-3", "Hard 2-1", "Hard 2-2", "Hard 2-3",
        "Hard 3-1", "Hard 3-2", "Hard 3-3", "Hard 4-1", "Hard 4-2", "Hard 4-3",
    };
    return i >= 0 && i < sr->nsplits ? names[i] : "?";
}

void speedrun_init(Speedrun *sr, SrCategory category)
{
    memset(sr, 0, sizeof *sr);
    sr->category = category;
    sr->nsplits = category == SR_NORMAL_HARD ? 2 * LEVELS : LEVELS;
    for (int i = 0; i < SR_MAX_SPLITS; i++)
        sr->pb[i] = sr->best[i] = sr->times[i] = -1;
    for (int i = 0; i < SR_HISTORY; i++)
        sr->history[i] = -1;
}

/* Tatanga's sprite tiles (rows $AC-$AF, $BC-$BF, $CB-$CF of the sprite
   tile set while he is on screen). */
static int is_tatanga_tile(uint8_t t)
{
    return (t >= 0xAC && t <= 0xAF) || (t >= 0xBC && t <= 0xBF) || (t >= 0xCB && t <= 0xCF);
}

void speedrun_sample(SrFrame *f, uint64_t cycles, const uint8_t *wram,
                     const uint8_t *hram, const uint8_t *oam)
{
    f->cycles = cycles;
    f->state = hram[0xB3 - 0x80];
    f->world_level = hram[0xB4 - 0x80];
    f->demo = hram[0x9F - 0x80];
    f->boss_defeated = wram[0xD007 - 0xC000];
    f->tatanga_visible = 0;
    for (int i = 0; i < 40; i++) {
        uint8_t y = oam[i * 4], x = oam[i * 4 + 1];
        if (y > 0 && y < 160 && x > 0 && x < 168 && is_tatanga_tile(oam[i * 4 + 2]))
            f->tatanga_visible = 1;
    }
}

static void record_split(Speedrun *sr, int64_t t)
{
    int i = sr->split;
    if (i >= sr->nsplits)
        return;
    sr->times[i] = t;
    int64_t seg = t - (i ? sr->times[i - 1] : 0);
    sr->gold = 0;
    if (sr->best[i] < 0 || seg < sr->best[i]) {
        sr->gold = sr->best[i] >= 0;
        sr->best[i] = seg;
        sr->dirty = 1;
    }
    sr->split++;
}

static void finish(Speedrun *sr)
{
    int64_t total = sr->times[sr->nsplits - 1];
    sr->phase = SR_FINISHED;
    sr->completed++;
    memmove(sr->history + 1, sr->history, (SR_HISTORY - 1) * sizeof sr->history[0]);
    sr->history[0] = total;
    sr->new_pb = sr->pb[sr->nsplits - 1] < 0 || total < sr->pb[sr->nsplits - 1];
    if (sr->new_pb)
        memcpy(sr->pb, sr->times, sizeof sr->pb);
    sr->dirty = 1;
}

SrEvent speedrun_frame(Speedrun *sr, const SrFrame *f)
{
    uint64_t prev = sr->prev_cycles;
    sr->prev_cycles = f->cycles;
    int menu = f->state == GS_PREPARE_MENU || f->state == GS_MENU;
    int playing = f->state == GS_PLAYING || f->state == GS_AUTOSCROLL;
    SrEvent ev = SR_EV_NONE;

    switch (sr->phase) {
    case SR_IDLE:
    case SR_FINISHED:
        if (menu)
            sr->phase = SR_ARMED;
        break;
    case SR_ARMED:
        /* Start on the frame before 1-1 first appears (no attract demo). */
        if (playing && !f->demo && f->world_level == WORLD_1_1) {
            sr->phase = SR_RUNNING;
            sr->start = prev;
            sr->now = f->cycles;
            sr->split = 0;
            for (int i = 0; i < SR_MAX_SPLITS; i++)
                sr->times[i] = -1;
            memcpy(sr->cmp, sr->pb, sizeof sr->cmp);
            sr->level = f->world_level;
            sr->level_done = sr->gold = sr->new_pb = sr->fast_forwarded = 0;
            sr->attempts++;
            sr->dirty = 1;
            ev = SR_EV_START;
        }
        break;
    case SR_RUNNING:
        sr->now = f->cycles;
        if (f->demo)
            break;      /* the title's attract demo between the two quests */
        if (menu && !sr->level_done) {
            /* Game over, reset or leaving to the title: the attempt ends. */
            sr->phase = SR_ARMED;
            ev = SR_EV_RESET;
            break;
        }
        if (playing && f->world_level != sr->level) {
            if (!sr->level_done) {
                record_split(sr, (int64_t)(prev - sr->start));
                ev = SR_EV_SPLIT;
            }
            sr->level = f->world_level;
            sr->level_done = 0;
        } else if (!sr->level_done && f->world_level == WORLD_4_3 &&
                   f->boss_defeated == TATANGA_DEFEATED && !f->tatanga_visible) {
            record_split(sr, (int64_t)(f->cycles - sr->start));
            sr->level_done = 1;
            ev = SR_EV_SPLIT;
            if (sr->split == sr->nsplits) {
                finish(sr);
                ev = SR_EV_FINISH;
            }
        }
        break;
    }
    return ev;
}

int64_t speedrun_elapsed(const Speedrun *sr)
{
    if (sr->phase == SR_FINISHED)
        return sr->times[sr->nsplits - 1];
    if (sr->phase == SR_RUNNING || (sr->phase == SR_ARMED && sr->now > sr->start))
        return (int64_t)(sr->now - sr->start);
    return 0;
}

int speedrun_delta(const Speedrun *sr, int i, int64_t *delta)
{
    if (i < 0 || i >= sr->nsplits || sr->times[i] < 0)
        return 0;
    if (sr->cmp[i] < 0)
        return 0;
    *delta = sr->times[i] - sr->cmp[i];
    return 1;
}

int64_t speedrun_sum_of_best(const Speedrun *sr)
{
    int64_t sum = 0;
    for (int i = 0; i < sr->nsplits; i++) {
        if (sr->best[i] < 0)
            return -1;
        sum += sr->best[i];
    }
    return sum;
}

void speedrun_format(int64_t cycles, int sign, char *out, size_t len)
{
    const char *s = "";
    if (cycles < 0) {
        s = "-";
        cycles = -cycles;
    } else if (sign) {
        s = "+";
    }
    int64_t cs = cycles * 100 / SR_CLOCK;        /* hundredths, truncated like LiveSplit */
    int64_t m = cs / 6000, sec = cs / 100 % 60, h = cs % 100;
    if (m >= 60)
        snprintf(out, len, "%s%" PRId64 ":%02" PRId64 ":%02" PRId64 ".%02" PRId64, s, m / 60, m % 60, sec, h);
    else if (m > 0 || !sign)
        snprintf(out, len, "%s%" PRId64 ":%02" PRId64 ".%02" PRId64, s, m, sec, h);
    else
        snprintf(out, len, "%s%" PRId64 ".%02" PRId64, s, sec, h);
}

/* --- own split file ------------------------------------------------------- */

int speedrun_save(const Speedrun *sr, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# Super Mario Land port speedrun splits. Times are Game Boy cycles\n"
               "# (4194304 per second), -1 = none. Exported to LiveSplit as .lss.\n"
               "category %s\nattempts %ld\ncompleted %ld\n",
            category_names[sr->category], sr->attempts, sr->completed);
    for (int i = 0; i < sr->nsplits; i++)
        fprintf(f, "split %d %" PRId64 " %" PRId64 "\n", i, sr->pb[i], sr->best[i]);
    for (int i = 0; i < SR_HISTORY && sr->history[i] >= 0; i++)
        fprintf(f, "run %" PRId64 "\n", sr->history[i]);
    return fclose(f) ? -1 : 0;
}

int speedrun_load(Speedrun *sr, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 1;
    char line[256], name[32];
    int runs = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        int i;
        long n;
        int64_t a, b;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "category %31s", name) == 1) {
            if (speedrun_category_from_name(name) != (int)sr->category)
                bad = 1;
        } else if (sscanf(line, "attempts %ld", &n) == 1) {
            sr->attempts = n;
        } else if (sscanf(line, "completed %ld", &n) == 1) {
            sr->completed = n;
        } else if (sscanf(line, "split %d %" SCNd64 " %" SCNd64, &i, &a, &b) == 3) {
            if (i >= 0 && i < sr->nsplits) {
                sr->pb[i] = a;
                sr->best[i] = b;
            }
        } else if (sscanf(line, "run %" SCNd64, &a) == 1) {
            if (runs < SR_HISTORY)
                sr->history[runs++] = a;
        } else {
            bad = 1;
        }
    }
    fclose(f);
    return bad ? -1 : 0;
}

/* --- LiveSplit ------------------------------------------------------------ */

/* LiveSplit stores TimeSpans as [d.]hh:mm:ss.fffffff (100 ns ticks). */
static void lss_time(int64_t cycles, char *out, size_t len)
{
    int64_t ticks = cycles * 10000000 / SR_CLOCK;
    int64_t sec = ticks / 10000000, frac = ticks % 10000000;
    snprintf(out, len, "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%07" PRId64,
             sec / 3600, sec / 60 % 60, sec % 60, frac);
}

static int64_t parse_lss_time(const char *s)
{
    int64_t parts[4] = { 0 };
    int n = 0;
    int64_t days = 0;
    const char *p = s;
    /* optional "d." day prefix: a '.' before the first ':' */
    const char *colon = strchr(s, ':'), *dot = strchr(s, '.');
    if (colon && dot && dot < colon) {
        days = strtoll(s, NULL, 10);
        p = dot + 1;
    }
    while (n < 3) {
        char *end;
        parts[n++] = strtoll(p, &end, 10);
        if (end == p)
            return -1;
        p = end;
        if (*p != ':')
            break;
        p++;
    }
    int64_t sec = parts[0];
    if (n == 2)
        sec = parts[0] * 60 + parts[1];
    else if (n == 3)
        sec = parts[0] * 3600 + parts[1] * 60 + parts[2];
    sec += days * 86400;
    int64_t ticks = sec * 10000000;
    if (*p == '.') {
        int64_t scale = 1000000;
        for (p++; *p >= '0' && *p <= '9' && scale; p++, scale /= 10)
            ticks += (*p - '0') * scale;
    }
    /* round to the nearest cycle */
    return (ticks * SR_CLOCK + 5000000) / 10000000;
}

static void xml_escape(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '&') fputs("&amp;", f);
        else if (*s == '<') fputs("&lt;", f);
        else if (*s == '>') fputs("&gt;", f);
        else fputc(*s, f);
    }
}

int speedrun_export_lss(const Speedrun *sr, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    char t[40];
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n"
               "  <GameIcon />\n  <GameName>Super Mario Land</GameName>\n  <CategoryName>");
    xml_escape(f, category_titles[sr->category]);
    fprintf(f, "</CategoryName>\n  <LayoutPath>\n  </LayoutPath>\n"
               "  <Metadata>\n    <Run id=\"\" />\n    <Platform usesEmulator=\"False\">\n    </Platform>\n"
               "    <Region>\n    </Region>\n    <Variables />\n  </Metadata>\n"
               "  <Offset>00:00:00</Offset>\n  <AttemptCount>%ld</AttemptCount>\n  <AttemptHistory>\n",
            sr->attempts);
    int runs = 0;
    while (runs < SR_HISTORY && sr->history[runs] >= 0)
        runs++;
    for (int i = runs - 1, id = 1; i >= 0; i--, id++) {
        lss_time(sr->history[i], t, sizeof t);
        fprintf(f, "    <Attempt id=\"%d\">\n      <RealTime>%s</RealTime>\n    </Attempt>\n", id, t);
    }
    fprintf(f, "  </AttemptHistory>\n  <Segments>\n");
    for (int i = 0; i < sr->nsplits; i++) {
        fprintf(f, "    <Segment>\n      <Name>%s</Name>\n      <Icon />\n      <SplitTimes>\n"
                   "        <SplitTime name=\"Personal Best\">",
                speedrun_split_name(sr, i));
        if (sr->pb[i] >= 0) {
            lss_time(sr->pb[i], t, sizeof t);
            fprintf(f, "\n          <RealTime>%s</RealTime>\n        ", t);
        }
        fprintf(f, "</SplitTime>\n      </SplitTimes>\n      <BestSegmentTime>");
        if (sr->best[i] >= 0) {
            lss_time(sr->best[i], t, sizeof t);
            fprintf(f, "\n        <RealTime>%s</RealTime>\n      ", t);
        }
        fprintf(f, "</BestSegmentTime>\n      <SegmentHistory />\n    </Segment>\n");
    }
    fprintf(f, "  </Segments>\n  <AutoSplitterSettings />\n</Run>\n");
    return fclose(f) ? -1 : 0;
}

/* The RealTime inside the first `tag` element between p and end, or -1. */
static int64_t find_real_time(const char *p, const char *end, const char *tag)
{
    const char *e = strstr(p, tag);
    if (!e || e >= end)
        return -1;
    const char *close = strchr(e, '>');
    if (!close || close >= end || close[-1] == '/')
        return -1;
    const char *rt = strstr(close, "<RealTime>");
    const char *stop = strstr(close, "</");
    if (!rt || rt >= end || (stop && stop < rt))
        return -1;
    return parse_lss_time(rt + strlen("<RealTime>"));
}

int speedrun_import_lss(Speedrun *sr, const char *xml, char *err, size_t err_len)
{
    int64_t pb[SR_MAX_SPLITS], best[SR_MAX_SPLITS];
    int n = 0;
    const char *p = strstr(xml, "<Segments>");
    if (!p) {
        snprintf(err, err_len, "not a LiveSplit file (no <Segments>)");
        return -1;
    }
    while ((p = strstr(p, "<Segment>")) != NULL) {
        const char *end = strstr(p, "</Segment>");
        if (!end) {
            snprintf(err, err_len, "unterminated <Segment>");
            return -1;
        }
        if (n == sr->nsplits) {
            n++;
            break;
        }
        pb[n] = find_real_time(p, end, "<SplitTime name=\"Personal Best\"");
        best[n] = find_real_time(p, end, "<BestSegmentTime");
        n++;
        p = end;
    }
    if (n != sr->nsplits) {
        snprintf(err, err_len, "%s has %d splits (one per level), the file has %s%d",
                 category_titles[sr->category], sr->nsplits, n > sr->nsplits ? "more than " : "",
                 n > sr->nsplits ? sr->nsplits : n);
        return -1;
    }
    memcpy(sr->pb, pb, sizeof pb[0] * n);
    memcpy(sr->best, best, sizeof best[0] * n);
    const char *a = strstr(xml, "<AttemptCount>");
    if (a)
        sr->attempts = strtol(a + strlen("<AttemptCount>"), NULL, 10);
    sr->dirty = 1;
    return 0;
}
