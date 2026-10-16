/* Custom levels, see custom_level.h and docs/LEVELS.md. */
#include "custom_level.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/sha256.h"

const uint8_t cl_item_ids[CL_ITEM_KINDS] = { 0x28, 0x2A, 0x2C, 0xC0 };
const char *const cl_item_names[CL_ITEM_KINDS] = { "Mushroom/Flower", "Heart (1-Up)", "Star", "Coins" };

enum {
    ROM_SIZE = 0x10000,
    LEVEL_POINTERS = 0x4000,        /* in the level's bank: screen list per level */
    ENEMY_POINTERS = 0x401A,        /* in the level's bank: enemy list per level */
    PIPE_TABLE = 0x651C,            /* bank 3: warp pipes per level */
    ITEM_TABLE = 0x6536,            /* bank 3: block items per level */
    FIRST_SCREEN = 3,               /* screens 0-2 are the underground rooms */
    LIST_END = 0xFF, COLUMN_END = 0xFE, REPEAT = 0xFD,
    /* Enemy list columns ("level progress") advance every second level
       column; the list starts at $0C with the first screen on display, and
       an enemy appears at the screen's right edge. */
    PROGRESS_START = 0x0C,
    FIRST_SOLID_TILE = 0x60,        /* tiles from here on are solid */
    ENEMY_SCRIPTS = 0x349E,         /* bank 0: per enemy id, its script */
    SPRITES_LEFT = 0x30B4,          /* bank 0: per sprite number, its tile list (facing left) */
    SCRIPT_SET_SPRITE = 0xF8,
    ENEMY_COL_OFFSET = 24,
};

/* The level data of each world's stock levels: free while a custom level
   replaces them (Custom mode only plays the custom level). Measured from
   the v1.1 ROM: column data of 1-2/1-3 that the title screen does not
   share, all of worlds 2 and 4 (bank 1), all of world 3 (bank 3). */
typedef struct { int bank; uint16_t start, end; } Pool;
static const Pool pools[4] = {
    { 2, 0x69A6, 0x7873 }, { 1, 0x56CD, 0x7DAE }, { 3, 0x52CC, 0x6417 }, { 1, 0x56CD, 0x7DAE },
};
static const int world_banks[4] = { 2, 1, 3, 1 };

static void set_err(char *err, size_t len, const char *fmt, ...)
{
    if (!err || !len)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, len, fmt, ap);
    va_end(ap);
}

int cl_holds_item(uint8_t tile)
{
    return tile == CL_ITEM_BLOCK || tile == CL_COIN_BLOCK || tile == CL_HIDDEN_BLOCK;
}

/* Tiles the game looks at one by one while loading (items, pipes): never
   inside a repeat command. */
static int special_tile(uint8_t tile) { return cl_holds_item(tile) || tile == CL_PIPE_TOP; }

void cl_new(CustomLevel *l, int world, int screens, uint8_t ground)
{
    memset(l, 0, sizeof *l);
    snprintf(l->name, sizeof l->name, "New level");
    l->world = world < 1 || world > 4 ? 1 : world;
    l->cols = 0;
    memset(l->tiles, CL_BLANK, sizeof l->tiles);
    cl_resize(l, screens, ground);
}

void cl_resize(CustomLevel *l, int screens, uint8_t ground)
{
    if (screens < CL_MIN_SCREENS) screens = CL_MIN_SCREENS;
    if (screens > CL_MAX_SCREENS) screens = CL_MAX_SCREENS;
    int cols = screens * CL_SCREEN_COLS;
    for (int c = l->cols; c < cols; c++) {
        for (int r = 0; r < CL_ROWS; r++)
            l->tiles[r][c] = r >= 14 ? ground : CL_BLANK;
        l->items[c] = 0;
    }
    for (int c = cols; c < CL_MAX_COLS; c++) {
        for (int r = 0; r < CL_ROWS; r++)
            l->tiles[r][c] = CL_BLANK;
        l->items[c] = 0;
    }
    for (int i = l->n_enemies - 1; i >= 0; i--)
        if (l->enemies[i].col >= cols)
            cl_remove_enemy(l, i);
    l->cols = cols;
}

int cl_enemy_at(const CustomLevel *l, int col, int row)
{
    for (int i = 0; i < l->n_enemies; i++)
        if (l->enemies[i].col == col && l->enemies[i].row == row)
            return i;
    return -1;
}

int cl_place_enemy(CustomLevel *l, int col, int row, uint8_t id)
{
    if (col < CL_FIRST_ENEMY_COL || col >= l->cols || row < 0 || row >= CL_ROWS)
        return -1;
    int i = cl_enemy_at(l, col, row);
    if (i >= 0) {
        l->enemies[i].id = id;
        return 0;
    }
    if (l->n_enemies >= CL_MAX_ENEMIES)
        return -1;
    l->enemies[l->n_enemies++] = (ClEnemy){ col, row, (uint8_t)(id & 0x7F) };
    return 0;
}

void cl_remove_enemy(CustomLevel *l, int index)
{
    if (index < 0 || index >= l->n_enemies)
        return;
    memmove(&l->enemies[index], &l->enemies[index + 1], (size_t)(l->n_enemies - index - 1) * sizeof l->enemies[0]);
    l->n_enemies--;
}

/* --- text file --------------------------------------------------------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

char *cl_format(const CustomLevel *l)
{
    size_t cap = 512 + (size_t)CL_ROWS * (l->cols * 2 + 8) + (size_t)l->n_enemies * 32 + (size_t)l->cols * 24;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    size_t n = 0;
#define PUT(...) n += (size_t)snprintf(out + n, cap - n, __VA_ARGS__)
    PUT("SML-LEVEL 1\n");
    PUT("name %s\n", l->name);
    PUT("world %d\n", l->world);
    PUT("columns %d\n", l->cols);
    for (int r = 0; r < CL_ROWS; r++) {
        PUT("row %2d ", r);
        for (int c = 0; c < l->cols; c++)
            PUT("%02X", l->tiles[r][c]);
        PUT("\n");
    }
    /* enemies sorted by column, then row: the canonical order */
    int order[CL_MAX_ENEMIES];
    for (int i = 0; i < l->n_enemies; i++)
        order[i] = i;
    for (int i = 1; i < l->n_enemies; i++)
        for (int j = i; j > 0; j--) {
            const ClEnemy *a = &l->enemies[order[j - 1]], *b = &l->enemies[order[j]];
            if (a->col > b->col || (a->col == b->col && a->row > b->row)) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            } else
                break;
        }
    for (int i = 0; i < l->n_enemies; i++) {
        const ClEnemy *e = &l->enemies[order[i]];
        PUT("enemy %d %d %02X\n", e->col, e->row, e->id);
    }
    for (int c = 0; c < l->cols; c++)
        if (l->items[c])
            PUT("item %d %02X\n", c, l->items[c]);
#undef PUT
    return out;
}

int cl_parse(CustomLevel *l, const char *text, char *err, size_t err_len)
{
    memset(l, 0, sizeof *l);
    memset(l->tiles, CL_BLANK, sizeof l->tiles);
    l->world = 1;
    int line_no = 0, have_magic = 0, have_cols = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[CL_MAX_COLS * 2 + 64];
        line_no++;
        if (len >= sizeof line) {
            set_err(err, err_len, "line %d is too long", line_no);
            return -1;
        }
        memcpy(line, p, len);
        line[len] = 0;
        p += len + (eol ? 1 : 0);
        if (len && line[len - 1] == '\r')
            line[--len] = 0;
        if (!line[0] || line[0] == '#')
            continue;
        if (!have_magic) {
            if (strcmp(line, "SML-LEVEL 1")) {
                set_err(err, err_len, "not a Super Mario Land level file (version 1)");
                return -1;
            }
            have_magic = 1;
            continue;
        }
        int a, b;
        unsigned id;
        if (!strncmp(line, "name ", 5)) {
            snprintf(l->name, sizeof l->name, "%.*s", CL_NAME_LEN - 1, line + 5);
        } else if (sscanf(line, "world %d", &a) == 1) {
            if (a < 1 || a > 4) {
                set_err(err, err_len, "line %d: world must be 1-4", line_no);
                return -1;
            }
            l->world = a;
        } else if (sscanf(line, "columns %d", &a) == 1) {
            if (a < CL_MIN_SCREENS * CL_SCREEN_COLS || a > CL_MAX_COLS || a % CL_SCREEN_COLS) {
                set_err(err, err_len, "line %d: columns must be a multiple of 20 from %d to %d",
                        line_no, CL_MIN_SCREENS * CL_SCREEN_COLS, CL_MAX_COLS);
                return -1;
            }
            l->cols = a;
            have_cols = 1;
        } else if (!strncmp(line, "row ", 4)) {
            char *end;
            long r = strtol(line + 4, &end, 10);
            while (*end == ' ')
                end++;
            if (!have_cols || r < 0 || r >= CL_ROWS || strlen(end) != (size_t)l->cols * 2) {
                set_err(err, err_len, "line %d: bad row (columns first, 0-15, two hex digits per column)", line_no);
                return -1;
            }
            for (int c = 0; c < l->cols; c++) {
                int hi = hexval(end[2 * c]), lo = hexval(end[2 * c + 1]);
                if (hi < 0 || lo < 0) {
                    set_err(err, err_len, "line %d: bad tile number", line_no);
                    return -1;
                }
                uint8_t t = (uint8_t)(hi << 4 | lo);
                if (t >= REPEAT) {
                    set_err(err, err_len, "line %d: tile %02X is reserved", line_no, t);
                    return -1;
                }
                l->tiles[r][c] = t;
            }
        } else if (sscanf(line, "enemy %d %d %x", &a, &b, &id) == 3) {
            if (!have_cols || cl_enemy_at(l, a, b) >= 0 || id > 0x7F || cl_place_enemy(l, a, b, (uint8_t)id)) {
                set_err(err, err_len, "line %d: enemy outside the level, before column %d, doubled or too many",
                        line_no, CL_FIRST_ENEMY_COL);
                return -1;
            }
        } else if (sscanf(line, "item %d %x", &a, &id) == 2) {
            int known = 0;
            for (int k = 0; k < CL_ITEM_KINDS; k++)
                known |= cl_item_ids[k] == id;
            if (!have_cols || a < 0 || a >= l->cols || !known) {
                set_err(err, err_len, "line %d: bad item", line_no);
                return -1;
            }
            l->items[a] = (uint8_t)id;
        } else {
            set_err(err, err_len, "line %d: unknown line", line_no);
            return -1;
        }
    }
    if (!have_magic || !have_cols) {
        set_err(err, err_len, "not a Super Mario Land level file (version 1)");
        return -1;
    }
    return 0;
}

int cl_save(const CustomLevel *l, const char *path, char *err, size_t err_len)
{
    char *text = cl_format(l);
    if (!text) {
        set_err(err, err_len, "out of memory");
        return -1;
    }
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fputs(text, f) >= 0;
    if (f && fclose(f))
        ok = 0;
    free(text);
    if (ok) {
        remove(path);           /* Windows: rename does not replace */
        ok = rename(tmp, path) == 0;
    }
    if (!ok) {
        remove(tmp);
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

int cl_load(CustomLevel *l, const char *path, char *err, size_t err_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, err_len, "cannot read %s", path);
        return -1;
    }
    size_t cap = 64 * 1024, n = 0;
    char *text = malloc(cap + 1);
    if (!text) {
        fclose(f);
        set_err(err, err_len, "out of memory");
        return -1;
    }
    size_t got;
    while ((got = fread(text + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            fclose(f);
            free(text);
            set_err(err, err_len, "%s is too large for a level", path);
            return -1;
        }
    }
    fclose(f);
    text[n] = 0;
    int r = cl_parse(l, text, err, err_len);
    free(text);
    return r;
}

void cl_identity(const CustomLevel *l, char out[65])
{
    char *text = cl_format(l);
    Sha256 s;
    sha256_init(&s);
    if (text)
        sha256_update(&s, text, strlen(text));
    sha256_hex_final(&s, out);
    free(text);
}

/* --- building the ROM ------------------------------------------------- */

typedef struct {
    uint8_t *rom;
    int bank;
    uint16_t pos, end;
} Alloc;

static size_t offset_of(int bank, uint16_t addr)
{
    return addr < 0x4000 ? addr : (size_t)bank * 0x4000 + (addr - 0x4000);
}

/* Reserves n bytes; returns their address or 0 when the pool is full. */
static uint16_t take(Alloc *a, const uint8_t *data, size_t n)
{
    if ((size_t)a->pos + n > a->end)
        return 0;
    uint16_t at = a->pos;
    memcpy(a->rom + offset_of(a->bank, at), data, n);
    a->pos = (uint16_t)(a->pos + n);
    return at;
}

static void put_word(uint8_t *rom, int bank, uint16_t addr, uint16_t value)
{
    rom[offset_of(bank, addr)] = (uint8_t)value;
    rom[offset_of(bank, addr) + 1] = (uint8_t)(value >> 8);
}

static uint16_t get_word(const uint8_t *rom, int bank, uint16_t addr)
{
    return (uint16_t)(rom[offset_of(bank, addr)] | rom[offset_of(bank, addr) + 1] << 8);
}

/* One column in the game's format: commands "row << 4 | count" followed by
   count tiles, or by $FD and one tile repeated count times; $FE ends it. */
static size_t encode_column(const CustomLevel *l, int c, uint8_t *out)
{
    size_t n = 0;
    int r = 0;
    while (r < CL_ROWS) {
        if (l->tiles[r][c] == CL_BLANK) {
            r++;
            continue;
        }
        uint8_t t = l->tiles[r][c];
        int same = 1;
        while (r + same < CL_ROWS && l->tiles[r + same][c] == t)
            same++;
        if (same >= 3 && !special_tile(t)) {
            out[n++] = (uint8_t)(r << 4 | (same & 15));
            out[n++] = REPEAT;
            out[n++] = t;
            r += same;
            continue;
        }
        /* tiles one by one up to a blank or a run worth repeating */
        int len = 0;
        while (r + len < CL_ROWS && l->tiles[r + len][c] != CL_BLANK) {
            uint8_t u = l->tiles[r + len][c];
            int run = 1;
            while (r + len + run < CL_ROWS && l->tiles[r + len + run][c] == u)
                run++;
            if (len > 0 && run >= 3 && !special_tile(u))
                break;
            len++;
        }
        out[n++] = (uint8_t)(r << 4 | (len & 15));
        for (int i = 0; i < len; i++)
            out[n++] = l->tiles[r + i][c];
        r += len;
    }
    out[n++] = COLUMN_END;
    return n;
}

static int enemy_cmp(const void *a, const void *b)
{
    const ClEnemy *x = a, *y = b;
    return x->col != y->col ? x->col - y->col : x->row - y->row;
}

int cl_build_rom(const CustomLevel *l, const uint8_t *rom, size_t size,
                 uint8_t *rom_out, char *err, size_t err_len)
{
    if (size != ROM_SIZE) {
        set_err(err, err_len, "the ROM is not 64 KiB");
        return -1;
    }
    if (l->world < 1 || l->world > 4 || l->cols < CL_MIN_SCREENS * CL_SCREEN_COLS ||
        l->cols > CL_MAX_COLS || l->cols % CL_SCREEN_COLS) {
        set_err(err, err_len, "the level's size or world is invalid");
        return -1;
    }
    memcpy(rom_out, rom, size);
    const Pool *pool = &pools[l->world - 1];
    int bank = world_banks[l->world - 1], level = CL_LEVEL_INDEX(l->world);
    Alloc a = { rom_out, pool->bank, pool->start, pool->end };
    /* Bank 3 tables (items, pipes) at the end of bank 3's pool; when the
       level itself is in bank 3 they share it. */
    Alloc a3 = { rom_out, 3, pools[2].start, pools[2].end };

    int screens = l->cols / CL_SCREEN_COLS;
    uint8_t list[2 * (FIRST_SCREEN + CL_MAX_SCREENS) + 1];
    uint16_t screen_at[CL_MAX_SCREENS];
    uint8_t data[CL_SCREEN_COLS * (2 * CL_ROWS + 2)];
    static uint8_t encoded[CL_MAX_SCREENS][CL_SCREEN_COLS * (2 * CL_ROWS + 2)];
    size_t encoded_len[CL_MAX_SCREENS];

    /* the empty screen for the underground rooms 0-2 (never entered:
       custom levels have no warp pipes yet) */
    memset(data, COLUMN_END, CL_SCREEN_COLS);
    uint16_t empty = take(&a, data, CL_SCREEN_COLS);
    for (int s = 0; s < screens && empty; s++) {
        size_t n = 0;
        for (int c = 0; c < CL_SCREEN_COLS; c++)
            n += encode_column(l, s * CL_SCREEN_COLS + c, encoded[s] + n);
        encoded_len[s] = n;
        screen_at[s] = 0;
        for (int t = 0; t < s; t++)     /* identical screens are stored once */
            if (encoded_len[t] == n && !memcmp(encoded[t], encoded[s], n))
                screen_at[s] = screen_at[t];
        if (!screen_at[s] && !(screen_at[s] = take(&a, encoded[s], n)))
            empty = 0;
    }
    size_t k = 0;
    for (int s = 0; s < FIRST_SCREEN; s++) {
        list[k++] = (uint8_t)empty;
        list[k++] = (uint8_t)(empty >> 8);
    }
    for (int s = 0; s < screens; s++) {
        list[k++] = (uint8_t)screen_at[s];
        list[k++] = (uint8_t)(screen_at[s] >> 8);
    }
    list[k++] = LIST_END;
    uint16_t list_at = empty ? take(&a, list, k) : 0;

    /* enemies: [column, y | x << 6, id], sorted, $FF */
    ClEnemy sorted[CL_MAX_ENEMIES];
    memcpy(sorted, l->enemies, (size_t)l->n_enemies * sizeof sorted[0]);
    qsort(sorted, (size_t)l->n_enemies, sizeof sorted[0], enemy_cmp);
    uint8_t elist[CL_MAX_ENEMIES * 3 + 1];
    size_t en = 0;
    for (int i = 0; i < l->n_enemies; i++) {
        int rel = sorted[i].col - ENEMY_COL_OFFSET;
        elist[en++] = (uint8_t)(PROGRESS_START + rel / 2);
        elist[en++] = (uint8_t)((sorted[i].row + 2) | ((rel & 1) * 2) << 6);
        elist[en++] = sorted[i].id;
    }
    elist[en++] = LIST_END;
    uint16_t enemies_at = list_at ? take(&a, elist, en) : 0;

    /* block items: [screen, column, item], $FF; no warp pipes */
    uint8_t ilist[CL_MAX_COLS * 3 + 2];
    size_t in = 0;
    for (int c = 0; c < l->cols; c++)
        if (l->items[c]) {
            ilist[in++] = (uint8_t)(FIRST_SCREEN + c / CL_SCREEN_COLS);
            ilist[in++] = (uint8_t)(c % CL_SCREEN_COLS);
            ilist[in++] = l->items[c];
        }
    ilist[in++] = LIST_END;
    ilist[in++] = LIST_END;     /* the pipe list */
    if (pool->bank == 3)
        a3 = a;
    a3.pos = (uint16_t)(a3.end - in);
    if (enemies_at && a3.pos < a.pos && pool->bank == 3)
        enemies_at = 0;
    uint16_t items_at = enemies_at ? take(&a3, ilist, in) : 0;
    if (!items_at) {
        set_err(err, err_len, "the level is too big for world %d's level space (%d bytes); "
                "use fewer different screens or fewer tiles", l->world, pool->end - pool->start);
        memcpy(rom_out, rom, size);
        return -1;
    }
    put_word(rom_out, bank, (uint16_t)(LEVEL_POINTERS + level * 2), list_at);
    put_word(rom_out, bank, (uint16_t)(ENEMY_POINTERS + level * 2), enemies_at);
    put_word(rom_out, 3, (uint16_t)(ITEM_TABLE + level * 2), items_at);
    put_word(rom_out, 3, (uint16_t)(PIPE_TABLE + level * 2), (uint16_t)(items_at + in - 1));
    return 0;
}

/* --- reading levels back --------------------------------------------- */

static uint8_t rb(const uint8_t *rom, int bank, uint16_t addr) { return rom[offset_of(bank, addr)]; }

/* Decodes one screen (20 columns) starting at src into tiles[][col0..]. */
static void decode_screen(const uint8_t *rom, int bank, uint16_t src, uint8_t tiles[CL_ROWS][CL_MAX_COLS], int col0)
{
    for (int c = 0; c < CL_SCREEN_COLS; c++) {
        for (int guard = 0; guard < 32; guard++) {
            uint8_t cmd = rb(rom, bank, src++);
            if (cmd == COLUMN_END)
                break;
            int row = cmd >> 4, count = cmd & 15 ? cmd & 15 : 16;
            for (;;) {
                uint8_t t = rb(rom, bank, src++);
                if (t == REPEAT) {
                    t = rb(rom, bank, src++);
                    for (; count > 0; count--, row++)
                        if (row < CL_ROWS && tiles)
                            tiles[row][col0 + c] = t;
                    break;
                }
                if (row < CL_ROWS && tiles)
                    tiles[row][col0 + c] = t;
                row++;
                if (--count == 0)
                    break;
            }
        }
    }
}

int cl_read_rom(const uint8_t *rom, size_t size, int world, CustomLevel *l)
{
    if (size != ROM_SIZE || world < 1 || world > 4)
        return -1;
    memset(l, 0, sizeof *l);
    memset(l->tiles, CL_BLANK, sizeof l->tiles);
    l->world = world;
    int bank = world_banks[world - 1], level = CL_LEVEL_INDEX(world);
    uint16_t list = get_word(rom, bank, (uint16_t)(LEVEL_POINTERS + level * 2));
    int s = 0;
    for (; s < CL_MAX_SCREENS; s++) {
        uint16_t e = (uint16_t)(list + 2 * (FIRST_SCREEN + s));
        if (rb(rom, bank, e) == LIST_END)
            break;
        decode_screen(rom, bank, get_word(rom, bank, e), l->tiles, s * CL_SCREEN_COLS);
    }
    l->cols = s * CL_SCREEN_COLS;
    uint16_t e = get_word(rom, bank, (uint16_t)(ENEMY_POINTERS + level * 2));
    for (; rb(rom, bank, e) != LIST_END && l->n_enemies < CL_MAX_ENEMIES; e = (uint16_t)(e + 3)) {
        uint8_t p = rb(rom, bank, e), pos = rb(rom, bank, (uint16_t)(e + 1));
        ClEnemy *x = &l->enemies[l->n_enemies++];
        x->col = (p - PROGRESS_START) * 2 + ENEMY_COL_OFFSET + ((pos >> 6) >= 2);
        x->row = (pos & 0x1F) - 2;
        x->id = rb(rom, bank, (uint16_t)(e + 2));
    }
    uint16_t it = get_word(rom, 3, (uint16_t)(ITEM_TABLE + level * 2));
    for (; rb(rom, 3, it) != LIST_END; it = (uint16_t)(it + 3)) {
        int c = (rb(rom, 3, it) - FIRST_SCREEN) * CL_SCREEN_COLS + rb(rom, 3, (uint16_t)(it + 1));
        if (c >= 0 && c < CL_MAX_COLS)
            l->items[c] = rb(rom, 3, (uint16_t)(it + 2));
    }
    return 0;
}

/* --- what a world offers ----------------------------------------------- */

void cl_world_info(const uint8_t *rom, size_t size, int world, ClWorldInfo *info)
{
    memset(info, 0, sizeof *info);
    info->ground = CL_BLANK;
    if (size != ROM_SIZE || world < 1 || world > 4)
        return;
    long tile_count[256] = { 0 }, floor_count[256] = { 0 }, enemy_count[128] = { 0 };
    int bank = world_banks[world - 1];
    static uint8_t tiles[CL_ROWS][CL_MAX_COLS];
    for (int level = CL_LEVEL_INDEX(world); level < CL_LEVEL_INDEX(world) + 3; level++) {
        uint16_t list = get_word(rom, bank, (uint16_t)(LEVEL_POINTERS + level * 2));
        for (int s = 0; s < 40; s++) {
            uint16_t e = (uint16_t)(list + 2 * s);
            if (rb(rom, bank, e) == LIST_END)
                break;
            memset(tiles, CL_BLANK, sizeof tiles);
            decode_screen(rom, bank, get_word(rom, bank, e), tiles, 0);
            for (int r = 0; r < CL_ROWS; r++)
                for (int c = 0; c < CL_SCREEN_COLS; c++) {
                    tile_count[tiles[r][c]]++;
                    if (r == CL_ROWS - 1 && tiles[r][c] >= FIRST_SOLID_TILE)
                        floor_count[tiles[r][c]]++;
                }
        }
        uint16_t en = get_word(rom, bank, (uint16_t)(ENEMY_POINTERS + level * 2));
        for (int i = 0; i < 200 && rb(rom, bank, en) != LIST_END; i++, en = (uint16_t)(en + 3))
            enemy_count[rb(rom, bank, (uint16_t)(en + 2)) & 0x7F]++;
    }
    tile_count[CL_BLANK] = 0;
    floor_count[CL_BLANK] = 0;
    long best = 0;
    for (int t = 0; t < 256; t++)
        if (floor_count[t] > best) {
            best = floor_count[t];
            info->ground = (uint8_t)t;
        }
    /* the blank tile first (the eraser), then by use */
    info->tiles[info->n_tiles++] = CL_BLANK;
    for (;;) {
        int pick = -1;
        for (int t = 0; t < 256; t++)
            if (tile_count[t] > 0 && (pick < 0 || tile_count[t] > tile_count[pick]))
                pick = t;
        if (pick < 0 || info->n_tiles >= CL_MAX_TILES)
            break;
        info->tiles[info->n_tiles++] = (uint8_t)pick;
        tile_count[pick] = 0;
    }
    for (;;) {
        int pick = -1;
        for (int id = 0; id < 128; id++)
            if (enemy_count[id] > 0 && (pick < 0 || enemy_count[id] > enemy_count[pick]))
                pick = id;
        if (pick < 0 || info->n_enemies >= CL_MAX_KINDS)
            break;
        info->enemies[info->n_enemies++] = (uint8_t)pick;
        enemy_count[pick] = 0;
    }
}

/* --- enemy pictures ---------------------------------------------------- */

int cl_enemy_sprite(const uint8_t *rom, size_t size, int world, uint8_t id,
                    ClSpriteTile out[CL_MAX_SPRITE_TILES])
{
    if (size != ROM_SIZE || world < 1 || world > 4 || id > 0x7F)
        return 0;
    int bank = world_banks[world - 1];
    /* the script: <$E0 speed, $E0-$EF wait (one byte), $Fx and an argument */
    uint16_t script = get_word(rom, 0, (uint16_t)(ENEMY_SCRIPTS + id * 2));
    int sprite = -1;
    for (int i = 0; i < 64 && sprite < 0; i++) {
        uint8_t cmd = rb(rom, bank, script++);
        if (cmd == LIST_END)
            break;
        if (cmd >= 0xF0) {
            uint8_t arg = rb(rom, bank, script++);
            if (cmd == SCRIPT_SET_SPRITE)
                sprite = arg;
        }
    }
    if (sprite < 0)
        return 0;
    /* the tile list: control bytes (< $80: flips, moves by 8 pixels) and tiles */
    uint16_t list = get_word(rom, 0, (uint16_t)(SPRITES_LEFT + (uint8_t)(sprite * 2)));
    int n = 0, x = 0, y = 0;
    uint8_t attr = 0;
    for (int i = 0; i < 64 && n < CL_MAX_SPRITE_TILES; i++) {
        uint8_t b = rb(rom, bank, list++);
        if (b == LIST_END)
            break;
        if (b & 0x80) {
            out[n++] = (ClSpriteTile){ x, y, b, attr };
            continue;
        }
        attr = (uint8_t)((b << 1) & ~0x10);
        if (b & 0x08) y -= 8;
        if (b & 0x04) y += 8;
        if (b & 0x02) x -= 8;
        if (b & 0x01) x += 8;
    }
    return n;
}
