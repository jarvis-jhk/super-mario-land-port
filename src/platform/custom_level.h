/*
 * Custom levels for the level editor: a documented text file of tile,
 * enemy and item numbers (docs/LEVELS.md), never ROM bytes.
 *
 * To play one, cl_build_rom() writes the level in the game's own level
 * format into a copy of the player's ROM in memory: in place of the first
 * level of the chosen world (1-1, 2-1, 3-1 or 4-1), using the space of that
 * world's stock level data. The copy is only used in the separate Custom
 * mode; the stock game always runs on the unchanged ROM.
 *
 * No SDL here, so it can be unit-tested.
 */
#ifndef SML_CUSTOM_LEVEL_H
#define SML_CUSTOM_LEVEL_H

#include <stddef.h>
#include <stdint.h>

#define CL_ROWS 16              /* level rows: map rows 2-17 under the status bar */
#define CL_SCREEN_COLS 20
#define CL_MIN_SCREENS 2
#define CL_MAX_SCREENS 24     /* the enemy list counts columns in a byte */
#define CL_MAX_COLS (CL_MAX_SCREENS * CL_SCREEN_COLS)
#define CL_MAX_ENEMIES 160
#define CL_NAME_LEN 40
#define CL_BLANK 0x2C           /* the empty tile */
#define CL_ITEM_BLOCK 0x80      /* tiles that can hold an item: */
#define CL_COIN_BLOCK 0x81
#define CL_HIDDEN_BLOCK 0x5F
#define CL_PIPE_TOP 0x70
/* Enemies cannot start on the first screen: the game spawns them when the
   screen has scrolled to them (the list starts at its column $0C). */
#define CL_FIRST_ENEMY_COL 26
/* Mario starts in columns 4-5, rows 12-13, standing on row 14. */
#define CL_START_COL 4
#define CL_START_ROW 13

typedef struct {
    int col, row;               /* where the enemy appears (its top-left tile) */
    uint8_t id;                 /* the game's enemy number */
} ClEnemy;

typedef struct {
    char name[CL_NAME_LEN];
    int world;                  /* 1-4: graphics, music, enemy set */
    int cols;                   /* width in columns, a multiple of 20 */
    uint8_t tiles[CL_ROWS][CL_MAX_COLS];
    uint8_t items[CL_MAX_COLS]; /* item of the first item block in a column, 0 none */
    int n_enemies;
    ClEnemy enemies[CL_MAX_ENEMIES];
} CustomLevel;

/* What the editor offers for a world, taken from the player's ROM: the
   tiles and enemies that the world's stock levels use. */
#define CL_MAX_TILES 256
#define CL_MAX_KINDS 64
typedef struct {
    int n_tiles;
    uint8_t tiles[CL_MAX_TILES];        /* most used first */
    uint8_t ground;                     /* the usual floor tile */
    int n_enemies;
    uint8_t enemies[CL_MAX_KINDS];      /* without the hard-mode flag, most used first */
} ClWorldInfo;

/* The item kinds a block can hold. */
#define CL_ITEM_KINDS 4
extern const uint8_t cl_item_ids[CL_ITEM_KINDS];
extern const char *const cl_item_names[CL_ITEM_KINDS];

/* A new level: blank with a floor (rows 14-15) all along. */
void cl_new(CustomLevel *l, int world, int screens, uint8_t ground);

/* Text file. 0 on success, else -1 with a message in err. */
int cl_save(const CustomLevel *l, const char *path, char *err, size_t err_len);
int cl_load(CustomLevel *l, const char *path, char *err, size_t err_len);
/* The same as text in memory (malloc'd, NUL-terminated); cl_parse reads it. */
char *cl_format(const CustomLevel *l);
int cl_parse(CustomLevel *l, const char *text, char *err, size_t err_len);

/* SHA-256 of the canonical file text: the level's identity for recordings
   and results (64 hex digits + NUL). */
void cl_identity(const CustomLevel *l, char out[65]);

/* Editing helpers. */
int cl_enemy_at(const CustomLevel *l, int col, int row);     /* index or -1 */
int cl_place_enemy(CustomLevel *l, int col, int row, uint8_t id); /* 0 ok, -1 full or not allowed */
void cl_remove_enemy(CustomLevel *l, int index);
void cl_resize(CustomLevel *l, int screens, uint8_t ground);
int cl_holds_item(uint8_t tile);

/* Writes the level into rom_out (a copy of rom, size bytes). 0 on success,
   -1 with a message when it does not fit or the ROM is not 64 KiB. */
int cl_build_rom(const CustomLevel *l, const uint8_t *rom, size_t size,
                 uint8_t *rom_out, char *err, size_t err_len);

/* The level as the game will read it back from a built ROM (for tests):
   decodes the patched tables into l (world given). 0 on success. */
int cl_read_rom(const uint8_t *rom, size_t size, int world, CustomLevel *l);

/* The tiles/enemies a world's stock levels use. */
void cl_world_info(const uint8_t *rom, size_t size, int world, ClWorldInfo *info);

/* How an enemy looks when it appears: the sprite its script sets first,
   as tiles (sprite tile numbers, OAM attributes) at pixel offsets from its
   top-left corner. Returns the number of tiles, 0 when unknown. */
typedef struct { int dx, dy; uint8_t tile, attr; } ClSpriteTile;
#define CL_MAX_SPRITE_TILES 16
int cl_enemy_sprite(const uint8_t *rom, size_t size, int world, uint8_t id,
                    ClSpriteTile out[CL_MAX_SPRITE_TILES]);

/* For Custom mode: the game state values it needs to know about. */
#define CL_LEVEL_INDEX(world) (((world) - 1) * 3)

#endif
