/*
 * Enemies, powerups, platforms and projectiles (bank 0, 2442-2D05).
 *
 * See enemies.h for the slot layout and docs/ENEMIES.md for the overview.
 */
#include "enemies.h"
#include "game.h"
#include "ram.h"
#include "sound.h"

/* ---- ROM tables (all in bank 0) ---- */
enum {
    ROM_SPRITES_RIGHT = 0x2FE2,   /* per sprite number: pointer to its tile list, facing right */
    ROM_SPRITES_LEFT  = 0x30B4,   /* ... facing left */
    ROM_ENEMY_RESPONSES = 0x3186, /* 5 bytes per enemy id, see enemy_response() */
    ROM_ENEMY_DEFS    = 0x3375,   /* 3 bytes per enemy id: move_flags, dimensions, health */
    ROM_ENEMY_SCRIPTS = 0x349E,   /* per enemy id: pointer to its script */
};

/* Which enemy an enemy turns into when something happens to it (the 5-byte
   entries of the table at 3186; 0 = nothing happens). */
enum {
    RESPONSE_STOMPED = 0,         /* Mario jumps on it */
    RESPONSE_HIT_FROM_BELOW = 1,  /* Mario bumps the block under it */
    RESPONSE_TOUCHED = 2,         /* it touches Mario from the side ($FF: hurts Mario) */
    RESPONSE_SUPERBALL = 3,       /* a superball hits it and it has no hit points left */
    RESPONSE_KILLED = 4,          /* a bullet, or Mario with a star, kills it */
};

/* The slot at an address: one of the ten in work RAM, or the work buffer in
   high RAM (the original's InitEnemy etc. work on either). */
static inline EnemySlot *slot_ptr(uint16_t addr)
{
    if (addr >= 0xFF80)
        return (EnemySlot *)&g_hw->hram[(addr - 0xFF80u) % 0x70];
    return (EnemySlot *)&g_hw->wram[(addr - 0xC000u) & 0x1FFF];
}

/* The original multiplies an id by three as `rlca; add e` with the carry
   carried into the high byte: for ids >= $80 that is not quite 3*id, and
   the C code keeps it exact. */
static inline uint16_t id_times_3(uint8_t id)
{
    uint8_t rotated = (uint8_t)((id << 1) | (id >> 7));
    return (uint16_t)(rotated + id);
}

static inline uint8_t enemy_response(uint8_t id, int which)
{
    return rom_byte(0, (uint16_t)(ROM_ENEMY_RESPONSES + 5 * id + which));
}

/*
 * What happens to an enemy when `which` (a RESPONSE_*) hits it: if the table
 * names a new enemy, the slot becomes that enemy (e.g. a Goomba turns into a
 * flat Goomba) and $FF is returned; otherwise 0.
 */
static uint8_t transform_enemy(uint16_t slot, int which)
{
    uint8_t new_id = enemy_response(slot_ptr(slot)->id, which);
    if (taken(new_id == 0))
        return 0;
    slot_ptr(slot)->id = new_id;
    init_enemy(slot);
    return 0xFF;
}

/* Mario jumps on an enemy (00:2A01). Returns $FF if it changed (a stomped
   enemy), 0 if it can't be stomped. */
uint8_t stomp_enemy(uint16_t slot)
{
    return transform_enemy(slot, RESPONSE_STOMPED);
}

/* Mario hits the block under an enemy: it flips over and dies (00:2A23). */
uint8_t hit_enemy_from_below(uint16_t slot)
{
    return transform_enemy(slot, RESPONSE_HIT_FROM_BELOW);
}

/*
 * An enemy touches Mario from the side (00:2A44). Returns nonzero if Mario
 * is hurt ($FF in the table); if the table gives another enemy, the enemy
 * turns into that (a touched powerup changes to its pickup form) and 0 is
 * returned.
 */
uint8_t enemy_touches_mario(uint16_t slot)
{
    uint8_t new_id = enemy_response(slot_ptr(slot)->id, RESPONSE_TOUCHED);
    if (taken(new_id == 0xFF))
        return 0xFF;
    if (taken(new_id == 0))
        return 0;
    slot_ptr(slot)->id = new_id;
    init_enemy(slot);
    return 0;
}

/*
 * Mario's superball hits an enemy (00:2A68). Returns $FE if the enemy
 * survives (bosses have several hit points; two of them cry out), $FF if it
 * changed into another enemy (its death), 0 if the superball does nothing.
 */
uint8_t superball_hits_enemy(uint16_t slot)
{
    EnemySlot *s = slot_ptr(slot);
    if (!taken((s->health & 0x3F) == 0)) {      /* the lower 6 bits are the hit points */
        s->health--;
        uint8_t id = s->id;
        if (taken(id == ENEMY_HIYOIHOI) || taken(id == ENEMY_KING_TOTOMESU))
            shared_write(SOUND_WAVE_REQUEST, WAVE_BOSS_HIT);
        return 0xFE;
    }
    return transform_enemy(slot, RESPONSE_SUPERBALL);
}

/*
 * A bullet from Mario's plane/submarine hits an enemy (00:2AAD). Like the
 * superball, but the bosses of the shooting levels react: the dragon and the
 * Biokinton cry out, Tatanga explodes; killing Tatanga sets boss_defeated.
 */
uint8_t bullet_hits_enemy(uint16_t slot)
{
    EnemySlot *s = slot_ptr(slot);
    if (!taken((s->health & 0x3F) == 0)) {
        s->health--;
        uint8_t id = s->id;
        if (taken(id == ENEMY_DRAGONZAMASU) || taken(id == ENEMY_BIOKINTON))
            shared_write(SOUND_WAVE_REQUEST, WAVE_BOSS_HIT);
        else if (taken(id == ENEMY_TATANGA))
            shared_write(SOUND_NOISE_REQUEST, NOISE_EXPLOSION);
        return 0xFE;
    }
    if (!taken(s->id != ENEMY_TATANGA))
        boss_defeated = ENEMY_TATANGA;
    return transform_enemy(slot, RESPONSE_KILLED);
}

/* Mario, flashing with a star, touches an enemy (00:2B06): it dies. */
uint8_t star_mario_hits_enemy(uint16_t slot)
{
    return transform_enemy(slot, RESPONSE_KILLED);
}

/*
 * Scroll all enemies left by `pixels` because the screen scrolled right
 * (00:2C9F): the work buffer and the X position of all ten slots (also the
 * free ones, which does no harm).
 */
void scroll_enemies(uint8_t pixels)
{
    if (taken(pixels == 0))
        return;
    enemy_buf.x = (uint8_t)(enemy_buf.x - pixels);
    uint8_t low = 0x03;                    /* L of D103 */
    do {
        WRAM(ENEMY_SLOT_BASE + low) = (uint8_t)(WRAM(ENEMY_SLOT_BASE + low) - pixels);
        low = (uint8_t)(low + 0x10);
    } while (taken(low < 0xA0));
}

/*
 * Set up a slot whose id has just been set (00:2CBB): the speed/behaviour
 * flags, size and hit points come from the enemy's entry in the table at
 * 3375, and the script and animation timers restart.
 */
void init_enemy(uint16_t slot)
{
    EnemySlot *s = slot_ptr(slot);
    uint16_t def = (uint16_t)(ROM_ENEMY_DEFS + id_times_3(s->id));
    s->script_pos = 0;
    s->move_flags = reg_b(rom_byte(0, def));
    s->wait = 0;
    s->tick = 0;
    s->dimensions = rom_byte(0, (uint16_t)(def + 1));
    s->health = rom_byte(0, (uint16_t)(def + 2));
}

/* ---- copying between slots and the work buffer ---- */

enum { BUFFER_BYTES = 13 };     /* only D1x0-D1xC are copied; D-F are unused */

/* Address of slot number n: the original swaps the nibbles of n and puts
   the result in L (so slot numbers wrap within D1xx). */
static uint16_t slot_address(uint8_t n)
{
    return (uint16_t)(0xD100 | (uint8_t)((n << 4) | (n >> 4)));
}

/* Load the slot at an address into the work buffer (00:2CEB). */
void copy_slot_to_buffer_at(uint16_t addr)
{
    for (int i = 0; i < BUFFER_BYTES; i++)
        HRAM(ENEMY_BUFFER + i) = ram_read((uint16_t)(addr + i));
    loop_taken(BUFFER_BYTES);
}

/* Load slot number n into the work buffer (00:2CE5). */
void copy_slot_to_buffer(uint8_t n)
{
    copy_slot_to_buffer_at(slot_address(n));
}

/* Store the work buffer into the slot at an address (00:2CFD). */
void copy_buffer_to_slot_at(uint16_t addr)
{
    for (int i = 0; i < BUFFER_BYTES; i++)
        ram_write((uint16_t)(addr + i), HRAM(ENEMY_BUFFER + i));
    loop_taken(BUFFER_BYTES);
}

/* Store the work buffer into slot number n (00:2CF7). */
void copy_buffer_to_slot(uint8_t n)
{
    copy_buffer_to_slot_at(slot_address(n));
}

/* ---- tile probes around the object in the work buffer ----
 *
 * Each one looks up the background tile at some point near the object and
 * reports whether it is free: tiles below $5F (background) and from $F0 up
 * are not solid, everything in between is (the original returns this in the
 * carry flag). The point is given in screen coordinates (x + 8, y + 16) and
 * the scroll position is added to X to get level coordinates.
 */

static inline uint8_t scroll_position(void) { return shared_read(0xFFA4); }

static bool tile_is_free_at(uint8_t x, uint8_t y)
{
    tile_x_px = x;
    tile_y_px = y;
    uint8_t tile = lookup_tile();
    if (taken(tile < 0x5F))
        return true;
    return tile >= 0xF0;
}

/* Half the object's width in pixels: width in tiles (bits 4-6 of the
   dimensions) times 8 */
static inline uint8_t width_px(void)
{
    return (uint8_t)((enemy_buf.dimensions & 0x70) >> 1);
}

/* The object's left edge, at its top (00:2B84). */
bool enemy_probe_left(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    return tile_is_free_at(x, enemy_buf.y);
}

/* The object's right edge, at its top (00:2B9A). */
bool enemy_probe_right(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    x = (uint8_t)(x + 8);
    x = (uint8_t)(x + width_px() - 8);
    return tile_is_free_at(x, enemy_buf.y);
}

/* Under the object, near its left edge (00:2BBB). The original tests the
   facing direction here but then jumps over the code that would have
   probed the right side (a bug), so it always probes the left. */
bool enemy_probe_below(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    x = (uint8_t)(x + 4);
    uint8_t y = (uint8_t)(enemy_buf.y + 8);
    return tile_is_free_at(x, y);
}

/* Same as enemy_probe_below, slightly more to the left (00:2BE4). */
bool enemy_probe_below_left(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    x = (uint8_t)(x + 3);
    uint8_t y = (uint8_t)(enemy_buf.y + 8);
    return tile_is_free_at(x, y);
}

/* Under the object, just beyond its right edge (00:2BFE). */
bool enemy_probe_below_right(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    x = (uint8_t)(x + 5);
    x = (uint8_t)(x + width_px() - 8);
    uint8_t y = (uint8_t)(enemy_buf.y + 8);
    return tile_is_free_at(x, y);
}

/* Above the object's left edge, at the top of its height (00:2C21). The
   same facing-test bug as enemy_probe_below. */
bool enemy_probe_above(void)
{
    uint8_t x = enemy_buf.x;
    x = (uint8_t)(x + scroll_position());
    x = (uint8_t)(x + 4);
    /* (height - 1) * 8 via swap and rotate; a height of 0 gives $FF */
    uint8_t h = (uint8_t)((enemy_buf.dimensions & 0x07) - 1);
    h = (uint8_t)((h << 4) | (h >> 4));
    h = (uint8_t)((h >> 1) | (h << 7));
    return tile_is_free_at(x, (uint8_t)(enemy_buf.y - h));
}

/*
 * Turn every active enemy into a mid-air explosion (00:2B2A): at the end of
 * a boss level, and for the script command F7. The work buffer is changed
 * too, since the caller may be an enemy script that is running.
 */
void explode_all_enemies(void)
{
    uint8_t low = 0x00;
    do {
        EnemySlot *s = slot_ptr((uint16_t)(ENEMY_SLOT_BASE + low));
        if (!taken(s->id == ENEMY_FREE)) {
            s->id = ENEMY_EXPLOSION;
            s->script_pos = 0;
            s->tick = 0;                /* (the original's comments label these wrongly) */
            s->carries_mario = 0;
        }
        low = (uint8_t)(low + 0x10);
    } while (taken(low < 0xA0));
    enemy_buf.id = ENEMY_EXPLOSION;
    enemy_buf.script_pos = 0;
    enemy_buf.move_flags = 0;
    shared_write(SOUND_NOISE_REQUEST, NOISE_EXPLOSION);
}

/* ---- spawning ---- */

enum {
    ROM_LEVEL_ENEMY_POINTERS = 0x401A,   /* in the level's own bank: per level, the address of its enemy list */
    ROM_ANIMATED_BACKGROUND = 0x2436,    /* per level: 1 if the background tile animates */
    ENEMY_LIST_ENTRY_SIZE = 3,           /* column, y | x << 6, id | hard-mode flag */
    HARD_MODE_ONLY = 0x80,               /* in the id byte: the enemy only appears after winning once */
    OAM_BUFFER = 0xC000,
    ENEMY_OAM_FIRST = 20,                /* the sprites from number 20 on are the enemies' */
    ENEMY_OAM_MAX = 20,
};

/*
 * Level start: the enemy list is positioned at column $0C (the first
 * columns of the level are drawn before the game starts), and the level's
 * "animated background" flag is set (00:2442).
 */
void reset_level_enemies(void)
{
    level_progress = 0x0C;
    init_enemy_slots();
    boss_defeated = 0;
    /* Unlike the enemy-pointer table below, this flag table is byte-indexed. */
    shared_write(ADDR_BACKGROUND_ANIMATED, rom_byte(0, (uint16_t)(ROM_ANIMATED_BACKGROUND + level_index)));
}

/*
 * Free all ten slots and find the first entry of the level's enemy list
 * that is at or beyond the current column (00:245C). The list is sorted by
 * column; each entry is 3 bytes.
 */
void init_enemy_slots(void)
{
    /* The pointer table is word-indexed: RLCA doubles the level index. */
    uint8_t rotated = (uint8_t)((level_index << 1) | (level_index >> 7));   /* the original uses RLCA, not ADD A */
    uint16_t table = (uint16_t)(ROM_LEVEL_ENEMY_POINTERS + rotated);
    uint16_t entry = (uint16_t)(mem_read(table) | mem_read((uint16_t)(table + 1)) << 8);
    uint8_t column = reg_b(level_progress);
    while (!taken(mem_read(entry) >= column))
        entry = (uint16_t)(entry + ENEMY_LIST_ENTRY_SIZE);
    next_enemy_lo = (uint8_t)entry;
    next_enemy_hi = (uint8_t)(entry >> 8);

    uint8_t low = 0x00;
    do {
        WRAM(ENEMY_SLOT_BASE + low) = ENEMY_FREE;
        low = (uint8_t)(low + 0x10);
    } while (taken(low != 0xA0));
}

/* Spawn, move and draw all enemies for one frame (00:2491). */
void update_enemies(void)
{
    spawn_enemies();
    update_enemy_scripts();
    draw_enemies();
}

/* Put the enemy in the work buffer into the first free slot of slots 0-8
   (slot 9 is for powerups); if there is none, it is lost (00:250B). It
   starts with its script at the beginning, no wait, and facing left/up. */
static void place_new_enemy(void)
{
    enemy_buf.script_pos = 0;
    enemy_buf.facing = 0;
    enemy_buf.wait = 0;
    enemy_buf.tick = 0;
    enemy_buf.carries_mario = 0;
    uint16_t def = (uint16_t)(ROM_ENEMY_DEFS + id_times_3(enemy_buf.id));
    enemy_buf.dimensions = rom_byte(0, (uint16_t)(def + 1));
    enemy_buf.health = rom_byte(0, (uint16_t)(def + 2));
    if (!taken(enemy_buf.health < 0xC0))          /* a boss */
        shared_write(SOUND_MUSIC_REQUEST, MUS_BOSS);

    uint8_t low = 0x00;
    for (uint8_t n = 0;; n++) {
        if (taken(WRAM(ENEMY_SLOT_BASE + low) == ENEMY_FREE)) {
            copy_buffer_to_slot(n);
            return;
        }
        low = (uint8_t)(low + 0x10);
        if (!taken(low != 0x90))
            return;
    }
}

/*
 * Spawn every enemy of the level list whose column has been reached
 * (00:249B). An entry is [column, y | x << 6, id | hard-mode flag]: y in
 * tiles in the low 5 bits, and in the top 2 bits the X offset in units of
 * 4 pixels (0-12) from the right edge of the screen. If the screen has
 * already scrolled past the column by some steps, the enemy starts that
 * many columns further left.
 */
void spawn_enemies(void)
{
    for (;;) {
        uint16_t entry = (uint16_t)(next_enemy_lo | next_enemy_hi << 8);
        uint8_t behind = (uint8_t)(level_progress - reg_b(mem_read(entry)));
        if (taken(behind == 0))            /* column reached exactly... */
            return;
        if (taken(level_progress < mem_read(entry)))   /* ...or not yet */
            return;
        uint8_t offset = (uint8_t)((behind << 4) | (behind >> 4));   /* the original swaps nibbles */
        uint8_t pos = mem_read((uint16_t)(entry + 1));
        enemy_buf.y = (uint8_t)(((pos & 0x1F) << 3) + 0x10);         /* tile row * 8, below the HUD */
        enemy_buf.x = (uint8_t)(((pos & 0xC0) >> 4) + 0xD0 - offset);
        spawn_enemy_from_level_data((uint16_t)(entry + 2));
        entry = (uint16_t)(entry + ENEMY_LIST_ENTRY_SIZE);
        next_enemy_lo = (uint8_t)entry;
        next_enemy_hi = (uint8_t)(entry >> 8);
    }
}

/*
 * An enemy launches a projectile (script command F1, 00:24D6): the enemy
 * with the id in the script argument is created at the position of the
 * enemy that is in the work buffer. Argument $FF does nothing.
 */
void launch_enemy_projectile(void)
{
    uint8_t id = script_argument;
    enemy_buf.id = id;
    if (taken(id == 0xFF))
        return;
    enemy_buf.move_flags = rom_byte(0, (uint16_t)(ROM_ENEMY_DEFS + id_times_3(id)));
    place_new_enemy();
}

/*
 * Create the enemy of a level list entry in the work buffer and put it in
 * a free slot (00:24EF). `entry` points to the entry's id byte. Enemies
 * flagged hard-mode-only are skipped on the first playthrough.
 */
void spawn_enemy_from_level_data(uint16_t entry)
{
    if (!taken(win_count != 0))
        if (taken(mem_read(entry) & HARD_MODE_ONLY))
            return;
    uint8_t id = mem_read(entry) & 0x7F;
    enemy_buf.id = id;
    enemy_buf.move_flags = rom_byte(0, (uint16_t)(ROM_ENEMY_DEFS + id_times_3(id)));
    place_new_enemy();
}

/*
 * Release a powerup from a block (00:254D): slot 9 becomes the enemy `id`
 * (mushroom, flower, star...) at the position of the block that was hit,
 * which the block code left in the work buffer.
 */
void spawn_powerup(uint8_t id)
{
    EnemySlot *s = enemy_slot(POWERUP_SLOT);
    s->id = id;
    s->y = (uint8_t)((enemy_buf.y & 0xF8) + 7);      /* align with the block */
    s->x = enemy_buf.x;
    init_enemy(ENEMY_SLOT_BASE + 16 * POWERUP_SLOT);
    shared_write(SOUND_SFX_REQUEST, SFX_FLOWER);
}

/* ---- drawing ---- */

/*
 * Draw the object in the work buffer as sprites (00:25C0, the inner part of
 * DrawEnemies). Its sprite number selects a "tile list" through a pointer
 * table (one table for each facing). A tile list is a sequence of bytes:
 *   $FF         the end
 *   bit 7 set   a tile number: a sprite with that tile is added at the
 *               current drawing position
 *   bit 7 clear a command that moves the drawing position (bit 3 up 8
 *               pixels, bit 2 down 8, bit 1 left 8, bit 0 right 8) and sets
 *               the attributes of the following sprites: the byte shifted
 *               left once with bit 4 cleared, i.e. bit 6 -> behind the
 *               background, bit 5 -> flip Y, bit 4 -> flip X (the
 *               palette bit is always cleared).
 * The buffer's Y/X are moved along, so the buffer is left changed. The
 * sprites go into the OAM buffer from sprite 20 on, at most 20 of them.
 */
void draw_enemy(void)
{
    object_attributes = 0;
    /* two RLCAs multiply by 4; the count is below 20 so nothing wraps */
    uint16_t oam = (uint16_t)(OAM_BUFFER + 4 * ENEMY_OAM_FIRST
                              + (uint8_t)((objects_drawn << 2) | (objects_drawn >> 6)));
    reg_b(oam >> 8);                        /* the OAM pointer is kept in BC */
    uint16_t table = taken(enemy_buf.facing & 1) ? ROM_SPRITES_RIGHT : ROM_SPRITES_LEFT;
    table = (uint16_t)(table + (uint8_t)((enemy_buf.sprite << 1) | (enemy_buf.sprite >> 7)));
    uint16_t list = (uint16_t)(mem_read(table) | mem_read((uint16_t)(table + 1)) << 8);

    for (;;) {
        if (taken(objects_drawn >= ENEMY_OAM_MAX))
            return;
        for (;;) {
            uint8_t b = mem_read(list);
            if (taken(b == 0xFF))
                return;
            if (taken(b & 0x80))
                break;                              /* a tile: draw it below */
            object_attributes = (uint8_t)((b << 1) & ~0x10);
            if (!taken(!(b & 0x08))) enemy_buf.y = (uint8_t)(enemy_buf.y - 8);
            if (!taken(!(b & 0x04))) enemy_buf.y = (uint8_t)(enemy_buf.y + 8);
            if (!taken(!(b & 0x02))) enemy_buf.x = (uint8_t)(enemy_buf.x - 8);
            if (!taken(!(b & 0x01))) enemy_buf.x = (uint8_t)(enemy_buf.x + 8);
            list++;
        }
        shared_write(oam, enemy_buf.y);
        oam = inc_bc(oam);
        shared_write(oam, enemy_buf.x);
        oam = inc_bc(oam);
        shared_write(oam, mem_read(list));
        oam = inc_bc(oam);
        shared_write(oam, object_attributes);
        oam = inc_bc(oam);
        list++;
        objects_drawn++;
    }
}

/*
 * Draw all enemies (00:2568): copy each active slot to the work buffer, draw
 * it, and remove it if it is off the screen to the left (X >= $E0, which is
 * what a small X wrapping below zero looks like) or below the bottom (Y >=
 * $C0). Sprites that are not used are moved off the screen. Slot 9 (the
 * powerup) is drawn last.
 */
void draw_enemies(void)
{
    objects_drawn = 0;
    uint8_t slot = 0;
    do {
        if (taken(objects_drawn >= ENEMY_OAM_MAX))
            return;                       /* all 20 sprites used up */
        if (!taken(enemy_slot(slot)->id == ENEMY_FREE)) {
            copy_slot_to_buffer(slot);
            bool gone = false;
            if (!taken(enemy_buf.x < 0xE0))
                gone = true;
            else if (taken(enemy_buf.y >= 0xC0))
                gone = true;
            if (gone) {
                enemy_buf.id = ENEMY_FREE;
                copy_buffer_to_slot(slot);
            } else {
                draw_enemy();
            }
        }
    } while (taken(++slot != ENEMY_SLOTS));
    /* hide the unused sprites by giving them Y = 180 */
    uint16_t oam = (uint16_t)(OAM_BUFFER + 4 * ENEMY_OAM_FIRST
                              + (uint8_t)((objects_drawn << 2) | (objects_drawn >> 6)));
    while (!taken((uint8_t)oam >= 0xA0)) {
        shared_write(oam, 0xB4);
        oam = (uint16_t)(oam + 4);
    }
}

/* ---- behaviour: scripts and movement ---- */

/*
 * An enemy's behaviour is a small program in the table at 349E (one per
 * enemy id). Each command is one byte, optionally followed by an argument:
 *   $FF             end: start again from the beginning
 *   $E0-$EF         wait (keep moving at the current speed) for 0-15 frames
 *   other < $F0     set the speed: low nibble pixels per frame in X, high
 *                   nibble in Y (see SpeedCommand), and move for one frame
 *   $Fx             a command with an argument byte, see ScriptCommand
 */
enum ScriptCommand {
    CMD_ORIENT      = 0xF0,   /* arg: change the facing, see change_orientation() */
    CMD_LAUNCH      = 0xF1,   /* arg: id of the projectile to spawn at this enemy */
    CMD_SET_FLAGS   = 0xF2,   /* arg: new move_flags */
    CMD_BECOME      = 0xF3,   /* arg: change into the enemy with this id ($FF: disappear) */
    CMD_SET_TICK    = 0xF4,   /* arg: slow down: move only every (arg+1)th frame */
    CMD_MAYBE_LAUNCH = 0xF5,  /* arg ignored: like F1 with a 1 in 4 chance (unused) */
    CMD_WAIT_MARIO  = 0xF6,   /* arg 1: stall until Mario is near; else stall while he is near */
    CMD_EXPLODE_ALL = 0xF7,   /* every enemy explodes */
    CMD_SET_SPRITE  = 0xF8,   /* arg: sprite number */
    CMD_NOISE       = 0xF9,   /* arg: noise sound effect */
    CMD_SFX         = 0xFA,   /* arg: square wave sound effect */
    CMD_RESTART_IF_FAR = 0xFB, /* arg: restart the script unless Mario is less than arg pixels to the left */
    CMD_SET_POSITION = 0xFC,  /* arg: Y; X = $70 (the right side of the screen; Tatanga) */
    CMD_MUSIC       = 0xFD,   /* arg: music number */
};

/* Script address of an enemy id: the table at 349E holds pointers. */
static uint16_t script_of(uint8_t id)
{
    uint16_t entry = (uint16_t)(ROM_ENEMY_SCRIPTS + (uint8_t)((id << 1) | (id >> 7)));
    return (uint16_t)(rom_byte(0, entry) | rom_byte(0, (uint16_t)(entry + 1)) << 8);
}

/*
 * Run one frame of every active enemy (00:2648): copy it to the work
 * buffer, run its script and movement, copy it back.
 */
void update_enemy_scripts(void)
{
    uint16_t slot = ENEMY_SLOT_BASE;
    do {
        if (!taken(ram_read(slot) == ENEMY_FREE)) {
            copy_slot_to_buffer_at(slot);
            update_enemy(script_of(enemy_buf.id));
            copy_buffer_to_slot_at(slot);
        }
        slot = (uint16_t)((slot & 0xFF00) | (uint8_t)(slot + 0x10));
    } while (taken((uint8_t)slot != 0xA0));
}

/*
 * Script command F0: turn the enemy. The argument's bits:
 *   7  face down/up towards Mario's height (facing bit 1 = enemy is above him)
 *   6  face towards Mario horizontally (facing bit 0 = Mario is to the right
 *      of the enemy's centre)
 *   3,2  flip facing bit 1 / bit 0 (XOR)
 *   5  then set facing bit 1 to bit 1 of the argument
 *   4  then set facing bit 0 to bit 0 of the argument
 */
static void change_orientation(uint8_t arg)
{
    if (!taken((arg & 0xC0) == 0)) {
        if (!taken(!(arg & 0x80))) {
            reg_b(enemy_buf.facing & ~0x02);
            uint8_t above = enemy_buf.y < mario_y;
            enemy_buf.facing = (uint8_t)((enemy_buf.facing & ~0x02) | (above << 1));
        }
        if (!taken(!(arg & 0x40))) {
            uint8_t centre = (uint8_t)(((enemy_buf.dimensions & 0x70) >> 2) + reg_b(enemy_buf.x));
            uint8_t left_of_mario = reg_b(centre < mario_x);
            enemy_buf.facing = (uint8_t)((enemy_buf.facing & ~0x01) | left_of_mario);
        }
    }
    if (!taken((arg & 0x0C) == 0))
        enemy_buf.facing ^= reg_b((arg & 0x0C) >> 2);
    if (!taken(!(arg & 0x20)))
        enemy_buf.facing = (uint8_t)((enemy_buf.facing | 0x02) & reg_b((arg & 0x02) | 0xFD));
    if (!taken(!(arg & 0x10)))
        enemy_buf.facing = (uint8_t)((enemy_buf.facing | 0x01) & reg_b((arg & 0x01) | 0xFE));
}

/* What the interpreter does after a command */
enum ScriptResult {
    SCRIPT_NEXT,        /* go on with the next command */
    SCRIPT_UPDATE,      /* the command set a wait or a speed: go to the movement phase */
    SCRIPT_STOP,        /* the command ends this enemy's frame without movement */
};

/* The original tests the command against the opcodes one by one; its
   branch outcomes are reproduced in this order. */
static const uint8_t command_test_order[] = {
    CMD_SET_SPRITE, CMD_ORIENT, CMD_LAUNCH, CMD_SET_FLAGS, CMD_BECOME, CMD_SET_TICK, CMD_MAYBE_LAUNCH,
    CMD_WAIT_MARIO, CMD_EXPLODE_ALL, CMD_NOISE, CMD_SFX, CMD_RESTART_IF_FAR, CMD_SET_POSITION, CMD_MUSIC,
};

/* F1 (also reached from F5): put the projectile in a slot and keep the
   shooter in the work buffer. Slot 10 (D1A0, outside the ten real slots) is
   used to keep the shooter safe while the buffer is used for the projectile. */
static void launch_projectile(void)
{
    copy_buffer_to_slot(10);
    launch_enemy_projectile();
    copy_slot_to_buffer(10);
}

/* Execute the command at script[script_pos] (00:26B8-). *script may change
   (F3). */
static enum ScriptResult run_script_command(uint16_t *script)
{
    uint8_t cmd = mem_read((uint16_t)(*script + enemy_buf.script_pos));
    script_command = cmd;
    if (!taken(cmd != 0xFF)) {
        enemy_buf.script_pos = 0;                 /* end of script: restart */
        return SCRIPT_NEXT;
    }
    uint16_t cmd_address = (uint16_t)(*script + enemy_buf.script_pos);
    enemy_buf.script_pos++;
    if (!taken((cmd & 0xF0) == 0xF0)) {
        if (taken((cmd & 0xE0) != 0xE0)) {
            enemy_buf.speed = cmd;                /* a speed: move for one frame */
            enemy_buf.wait = 1;
        } else {
            enemy_buf.wait = cmd & 0x0F;          /* a wait */
        }
        return SCRIPT_UPDATE;
    }

    enemy_buf.script_pos++;                       /* commands $Fx have an argument */
    uint8_t arg = mem_read((uint16_t)(cmd_address + 1));
    script_argument = arg;

    int which;
    for (which = 0; which < (int)sizeof command_test_order; which++)
        if (!taken(cmd != command_test_order[which]))
            break;
    if (which == (int)sizeof command_test_order)
        return SCRIPT_NEXT;                       /* unknown command: ignored */

    switch (cmd) {
    case CMD_SET_SPRITE:
        enemy_buf.sprite = arg;
        break;
    case CMD_ORIENT:
        change_orientation(arg);
        break;
    case CMD_MAYBE_LAUNCH:
        if (!taken((io_read(rDIV) & 3) == 0))     /* 1 in 4 */
            break;
        taken(false);                             /* then continues as F1, which tests again */
        /* fall through */
    case CMD_LAUNCH:
        launch_projectile();
        break;
    case CMD_SET_FLAGS:
        enemy_buf.move_flags = arg;
        break;
    case CMD_BECOME:
        enemy_buf.id = arg;
        if (taken(arg == 0xFF))
            return SCRIPT_STOP;
        init_enemy(ENEMY_BUFFER);
        *script = script_of(enemy_buf.id);
        break;
    case CMD_SET_TICK:
        enemy_buf.tick = arg;
        break;
    case CMD_WAIT_MARIO: {
        uint8_t near = (uint8_t)(enemy_buf.x - reg_b(mario_x) + 0x14) < 0x20;
        bool go = arg == 1 ? near : !near;
        taken(arg == 1);
        if (!taken(go)) {
            enemy_buf.script_pos -= 2;            /* try again next frame */
            return SCRIPT_STOP;
        }
        break;
    }
    case CMD_EXPLODE_ALL:
        explode_all_enemies();
        return SCRIPT_STOP;
    case CMD_NOISE:
        shared_write(SOUND_NOISE_REQUEST, arg);
        return SCRIPT_STOP;
    case CMD_SFX:
        shared_write(SOUND_SFX_REQUEST, arg);
        return SCRIPT_STOP;
    case CMD_RESTART_IF_FAR:
        if (!taken((uint8_t)(enemy_buf.x - reg_b(mario_x)) < arg))
            enemy_buf.script_pos = 0;
        break;
    case CMD_SET_POSITION:
        enemy_buf.y = arg;
        enemy_buf.x = 0x70;
        break;
    case CMD_MUSIC:
        shared_write(SOUND_MUSIC_REQUEST, arg);
        return SCRIPT_STOP;
    }
    return SCRIPT_NEXT;
}

/* What an obstacle does to an enemy, from the three 2-bit fields of move_flags */
enum { WALL_IGNORE = 0x00, WALL_TURN = 0x04, WALL_RESTART = 0x0C };
enum { FLOOR_IGNORE = 0x00, FLOOR_REVERSE = 0x10, FLOOR_RESTART = 0x30 };
enum { CEILING_IGNORE = 0x00, CEILING_REVERSE = 0x40, CEILING_RESTART = 0xC0 };

/*
 * An enemy that carries Mario (a lift) has moved sideways by `speed` pixels
 * to the left (00:289B-28C7): Mario is pushed along, unless he is blocked
 * by a wall. The side collision test looks at the side given in
 * mario_facing, which is temporarily forced to `direction`.
 */
static void push_mario_left(uint8_t speed)
{
    uint8_t saved = mario_facing;
    mario_facing = 0x20;
    uint8_t blocked = mario_side_collision();
    if (!taken(blocked != 0)) {
        mario_x = (uint8_t)(mario_x - speed);
        if (!taken(mario_x >= 0x0F))
            mario_x = 0x0F;                       /* but not off the left edge of the screen */
    }
    mario_facing = saved;
}

/* The same to the right (00:2913-2958). Pushing Mario beyond X = $50 scrolls
   the screen, and the other enemies with it. */
static void push_mario_right(uint8_t speed)
{
    uint8_t saved = mario_facing;
    mario_facing = 0;
    uint8_t blocked = mario_side_collision();
    if (!taken(blocked != 0)) {
        mario_x = (uint8_t)(mario_x + speed);
        if (!taken(mario_x < 0x51)) {
            bool scroll = true;
            if (taken(level_end_counter >= 7)) {
                /* the level end is scrolling in: only scroll in whole steps of 4 */
                if (!taken((shared_read(0xFFA4) & 0x0C) != 0)) {
                    shared_write(0xFFA4, shared_read(0xFFA4) & 0xFC);
                    scroll = false;
                }
            }
            if (scroll) {
                uint8_t excess = reg_b((uint8_t)(mario_x - 0x50));
                mario_x = 0x50;
                shared_write(0xFFA4, (uint8_t)(shared_read(0xFFA4) + excess));
                scroll_enemies(excess);
            }
        }
    }
    mario_facing = saved;
}

static void move_enemy_x(void)
{
    uint8_t speed = enemy_buf.speed & 0x0F;
    if (taken(speed == 0))
        return;
    if (!taken(enemy_buf.facing & 1)) {
        /* ---- walking left ---- */
        bool walk = true;
        if (taken(!enemy_probe_left())) {         /* wall ahead */
            uint8_t wall = enemy_buf.move_flags & 0x0C;
            if (!taken(wall == WALL_IGNORE)) {
                walk = false;
                if (!taken(wall != WALL_TURN)) {
                    enemy_buf.facing |= 1;
                } else if (!taken(wall != WALL_RESTART)) {
                    enemy_buf.script_pos = 0;
                    enemy_buf.wait = 0;
                }
            }
        } else if (!taken(!(enemy_buf.move_flags & MOVE_STAY_ON_LEDGE))) {
            if (taken(enemy_probe_below_left())) {    /* no floor ahead: turn */
                enemy_buf.facing |= 1;
                walk = false;
            }
        }
        if (walk) {
            enemy_buf.x = (uint8_t)(enemy_buf.x - reg_b(speed));
            if (!taken(enemy_buf.carries_mario == 0))
                push_mario_left(speed);
        }
    } else {
        /* ---- walking right ---- */
        bool walk = true;
        if (taken(!enemy_probe_right())) {
            uint8_t wall = enemy_buf.move_flags & 0x0C;
            if (!taken(wall == WALL_IGNORE)) {
                walk = false;
                if (!taken(wall != WALL_TURN)) {
                    enemy_buf.facing &= (uint8_t)~1;
                } else if (!taken(wall != WALL_RESTART)) {
                    enemy_buf.script_pos = 0;
                    enemy_buf.wait = 0;
                }
            }
        } else if (!taken(!(enemy_buf.move_flags & MOVE_STAY_ON_LEDGE))) {
            if (taken(enemy_probe_below_right())) {
                enemy_buf.facing &= (uint8_t)~1;
                walk = false;
            }
        }
        if (walk) {
            enemy_buf.x = (uint8_t)(enemy_buf.x + reg_b(speed));
            if (!taken(enemy_buf.carries_mario == 0))
                push_mario_right(speed);
        }
    }
}

static void move_enemy_y(void)
{
    uint8_t speed = (uint8_t)(enemy_buf.speed >> 4);
    if (taken(speed == 0))
        return;
    if (!taken(enemy_buf.facing & 2)) {
        /* ---- moving up ---- */
        bool move = true;
        if (taken(!enemy_probe_above())) {
            uint8_t ceiling = enemy_buf.move_flags & 0xC0;
            if (!taken(ceiling == CEILING_IGNORE)) {
                move = false;
                if (!taken(ceiling != CEILING_REVERSE)) {
                    enemy_buf.facing |= 2;
                } else if (!taken(ceiling != CEILING_RESTART)) {
                    enemy_buf.script_pos = 0;
                    enemy_buf.wait = 0;
                }
            }
        }
        if (move) {
            enemy_buf.y = (uint8_t)(enemy_buf.y - reg_b(speed));
            if (!taken(enemy_buf.carries_mario == 0))
                mario_y = (uint8_t)(mario_y - speed);
        }
    } else {
        /* ---- moving down ---- */
        bool move = true;
        if (taken(!enemy_probe_below())) {
            uint8_t floor = enemy_buf.move_flags & 0x30;
            if (!taken(floor == FLOOR_IGNORE)) {
                move = false;
                if (!taken(floor != FLOOR_REVERSE)) {
                    enemy_buf.facing &= (uint8_t)~2;
                } else if (!taken(floor != FLOOR_RESTART)) {
                    enemy_buf.script_pos = 0;
                    enemy_buf.wait = 0;
                }
            }
        }
        if (move) {
            enemy_buf.y = (uint8_t)(enemy_buf.y + reg_b(speed));
            if (!taken(enemy_buf.carries_mario == 0))
                mario_y = (uint8_t)(mario_y + speed);
        }
    }
}

static void move_enemy(void)
{
    move_enemy_x();
    move_enemy_y();
    enemy_buf.carries_mario = 0;     /* set again by the player code every frame */
}

/*
 * One frame of the enemy in the work buffer (00:2676). While it still has
 * to wait, it keeps moving with its current speed (also falling, if gravity
 * applies); when the wait is over the script is run until a command sets the
 * next speed or wait.
 */
void update_enemy(uint16_t script)
{
    for (;;) {
        if (taken(enemy_buf.wait == 0)) {
            enum ScriptResult r;
            do
                r = run_script_command(&script);
            while (r == SCRIPT_NEXT);
            if (r == SCRIPT_STOP)
                return;
            continue;                      /* SCRIPT_UPDATE */
        }
        if (!taken(!(enemy_buf.move_flags & MOVE_GRAVITY))) {
            if (!taken(!enemy_probe_below())) {   /* nothing below: fall (the wait doesn't count down) */
                enemy_buf.y++;
                return;
            }
            enemy_buf.y &= 0xF8;                  /* landed: snap to the tile grid */
        }
        /* tick: the high nibble counts up to the low nibble; the enemy only
           moves (and the wait counts down) when they are equal */
        uint8_t limit = enemy_buf.tick & 0x0F;
        uint8_t count = reg_b(enemy_buf.tick >> 4);
        if (!taken(limit == count)) {
            uint8_t next = (uint8_t)(count + 1);
            next = (uint8_t)((next << 4) | (next >> 4));      /* the original swaps nibbles: 16 -> 1 */
            enemy_buf.tick = (uint8_t)(limit | next);
            return;
        }
        enemy_buf.tick &= 0x0F;
        enemy_buf.wait--;
        move_enemy();
        return;
    }
}
