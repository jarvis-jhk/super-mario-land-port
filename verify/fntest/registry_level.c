/* Level subsystem functions under test (see fntest.h). Sorted by address. */
#include "fntest.h"
#include "../../src/game/game.h"

static uint8_t peek_ram(const Machine *m, unsigned addr)
{
    return addr >= 0xFF80 ? m->hram[addr - 0xFF80] : m->wram[addr - 0xC000];
}

/* ROM byte at 4000-7FFF of a bank; -1 outside the ROM area. */
static int rom_at(const Machine *m, unsigned bank, unsigned addr)
{
    if (addr < 0x4000 || addr > 0x7FFF)
        return -1;
    return m->rom[bank * 0x4000 + (addr - 0x4000)];
}

static int rom_word_at(const Machine *m, unsigned bank, unsigned addr)
{
    int lo = rom_at(m, bank, addr), hi = rom_at(m, bank, addr + 1);
    return lo < 0 || hi < 0 ? -1 : (hi << 8 | lo);
}

/* The warp pipe and item block lists (bank 3) are scanned for an entry with
   this screen and column, or up to their $FF end. */
static int item_scan_stays_in_rom(const Machine *m, unsigned table, unsigned stride,
                                  unsigned level, unsigned screen, unsigned column)
{
    int p = rom_word_at(m, 3, table + level * 2);
    for (int n = 0; p >= 0 && n < 256; n++, p += stride) {
        int s = rom_at(m, 3, (unsigned)p), c = rom_at(m, 3, (unsigned)p + 1);
        if (s < 0 || c < 0)
            return 0;
        if ((unsigned)s == screen && (unsigned)c == column)
            return 1;
        if (s == 0xFF && (unsigned)s != screen)
            return 1;
    }
    return 0;
}

/* Every pointer the column loader follows points into ROM: the level index
   selects one of 13 entries (levels 0-11 and the title/hangar level 12) of
   the per-level tables, the screen index runs up to the $FF that ends the
   level's screen list, a screen has 20 columns, and a column pointer in the
   middle of a screen points to a well-formed column. Fuzzed values make the
   original read "pointers" out of unrelated bytes, into VRAM, I/O or the
   sound engine's variables, where reads depend on hardware state the C code
   (which reads the ROM tables directly) never sees. */
static int level_columns_valid(const Machine *m)
{
    unsigned bank = m->rom_bank;
    unsigned level = peek_ram(m, 0xFFE4), screen = peek_ram(m, 0xFFE5), column = peek_ram(m, 0xFFE6);
    if (level > 12 || column >= 20 || bank < 1 || bank > 3)
        return 0;
    /* hActiveRomBank is the mapped bank (outside the timer interrupt): the
       pipe/item lookups switch back to it in the middle of the column */
    if (peek_ram(m, 0xFFFD) != bank)
        return 0;
    int screens = rom_word_at(m, bank, 0x4000 + level * 2);
    for (unsigned i = 0; i < screen; i++) {
        int lo = screens < 0 ? -1 : rom_at(m, bank, (unsigned)screens + i * 2);
        if (lo < 0 || lo == 0xFF)
            return 0;
    }
    if (screens < 0 || rom_at(m, bank, (unsigned)screens + screen * 2) < 0)
        return 0;
    if (!item_scan_stays_in_rom(m, 0x651C, 6, level, screen, column) ||
        !item_scan_stays_in_rom(m, 0x6536, 3, level, screen, column))
        return 0;
    if (column == 0)
        return 1;
    /* decode the column as load_next_column does, staying within ROM */
    unsigned p = (unsigned)(peek_ram(m, 0xFFE7) << 8 | peek_ram(m, 0xFFE8));
    for (;;) {
        int cmd = rom_at(m, bank, p++);
        if (cmd < 0)
            return 0;
        if (cmd == 0xFE)
            return 1;
        unsigned count = (cmd & 0x0F) ? (unsigned)(cmd & 0x0F) : 16;
        /* level data never writes past the 16-row column buffer (C0B0-C0BF);
           random bytes would, into unrelated variables */
        if ((unsigned)(cmd >> 4) + count > 16)
            return 0;
        for (;;) {
            int tile = rom_at(m, bank, p++);
            if (tile < 0)
                return 0;
            if (tile == 0xFD) {
                if (rom_at(m, bank, p++) < 0)
                    return 0;
                break;
            }
            if (--count == 0)
                break;
        }
    }
}

/* The vblank handler draws columns at map columns 40-5F of row 2 on
   (98xx): DrawColumn wraps 60 to 40, the level start sets 5B. Others would
   put overlay entries past its end (CA3F). */
static int draw_column_valid(const Machine *m)
{
    unsigned lo = peek_ram(m, 0xFFE9);
    return lo >= 0x40 && lo < 0x60;
}

/* wNextFloatyOAMIndex is set to $30 when a level starts and only ever
   steps through the four floaty slots 30, 38, 40, 48. */
static int floaties_valid(const Machine *m)
{
    unsigned next = peek_ram(m, 0xDA0B);
    return next == 0x30 || next == 0x38 || next == 0x40 || next == 0x48;
}

/* The whole frame has the same ROM pointer, world, sprite and enemy-id
   invariants as its callees. Reuse their predicates rather than letting
   random RAM send an enemy script or sprite lookup into unrelated data.
   Normal gameplay also needs the head-collision world and demo-input
   invariants; the shooter frame calls neither of those routines.
   MarioJumpAndFire has no validity restriction of its own. */
static int gameplay_valid(const Machine *m, int normal)
{
    if (!level_columns_valid(m) || !floaties_valid(m))
        return 0;
    const FnTest *tables[] = { fn_tests_player, fn_tests_enemies };
    for (unsigned i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        if (!tables[i])
            continue;
        for (const FnTest *t = tables[i]; t->name; t++) {
            if ((t->bank == 0 && (t->addr == 0x1736 || (normal && t->addr == 0x198C) ||
                                  t->addr == 0x2491)) ||
                (normal && t->bank == 3 && t->addr == 0x4A94)) {
                if (t->valid_state && !t->valid_state(m))
                    return 0;
            }
        }
    }
    return 1;
}

static int normal_gameplay_valid(const Machine *m) { return gameplay_valid(m, 1); }

/* The paused shooter frame returns before any callee needs its invariants.
   Otherwise bank 1 supplies both shooter code and level data. */
static int autoscroll_gameplay_valid(const Machine *m)
{
    return peek_ram(m, 0xFFB2) != 0 || (m->rom_bank == 1 && gameplay_valid(m, 0));
}

/* Initial drawing resets the column pointer/index itself. Only the level,
   screen and mapped-bank invariants survive from LoadNextColumn's inputs. */
static int first_screen_valid(const Machine *m)
{
    Machine initial = *m;
    initial.hram[0x66] = 0;
    return level_columns_valid(&initial);
}

/* Exit walking uses the same world/sprite inputs as MarioAnimateAndWalk. */
static int exit_walk_valid(const Machine *m)
{
    for (const FnTest *t = fn_tests_player; t && t->name; t++)
        if (t->bank == 0 && t->addr == 0x16F5)
            return !t->valid_state || t->valid_state(m);
    return 0;
}

/* Dialogue cursors are background-map addresses. Random high bytes can
   turn the original tile store into an MBC write, outside game states.
   The countdown-only path never dereferences the cursor. */
static int dialogue_valid(const Machine *m)
{
    unsigned cursor = peek_ram(m, 0xFFE2) << 8 | peek_ram(m, 0xFFE3);
    return peek_ram(m, 0xFFA6) != 0 || (cursor >= 0x9800 && cursor <= 0x9BFF);
}

/* Composite states retain only the invariants needed on the path they take. */
static int callee_valid(const Machine *m, const FnTest *table, unsigned addr)
{
    for (const FnTest *t = table; t && t->name; t++)
        if (t->bank == 0 && t->addr == addr)
            return !t->valid_state || t->valid_state(m);
    return 0;
}
static int enemies_frame_valid(const Machine *m)
{
    return callee_valid(m, fn_tests_enemies, 0x2491);
}
static int level_score_valid(const Machine *m)
{
    if ((peek_ram(m, 0xFFB4) & 15) == 3 && !enemies_frame_valid(m)) return 0;
    return peek_ram(m, 0xFFA6) != 0 || floaties_valid(m);
}
static int boss_gate_valid(const Machine *m)
{
    return peek_ram(m, 0xFFA6) == 0 ||
        (level_columns_valid(m) && enemies_frame_valid(m) &&
         callee_valid(m, fn_tests_player, 0x1736));
}
static int locate_gate_valid(const Machine *m) { return enemies_frame_valid(m); }

static void t_finish_death(Sm83Regs *r) { (void)r; finish_death(); }
static void t_prepare_death_animation(Sm83Regs *r) { (void)r; prepare_death_animation(); }
static void t_update_death_animation(Sm83Regs *r) { (void)r; update_death_animation(); }
static void t_update_level_exit(Sm83Regs *r) { (void)r; update_level_exit(); }
static void t_count_down_level_score(Sm83Regs *r) { (void)r; count_down_level_score(); }
static void t_finish_level(Sm83Regs *r) { (void)r; finish_level(); }
static void t_advance_level(Sm83Regs *r) { (void)r; advance_level(); }
static void t_load_world_tiles(Sm83Regs *r) { load_world_tiles(r->a); }
static void t_leave_bonus_game(Sm83Regs *r) { (void)r; leave_bonus_game(); }
static void t_prepare_boss_gate(Sm83Regs *r) { (void)r; prepare_boss_gate(); }
static void t_locate_boss_gate(Sm83Regs *r) { (void)r; locate_boss_gate(); }
static void t_open_boss_gate(Sm83Regs *r) { (void)r; open_boss_gate(); }
static void t_finish_boss_gate(Sm83Regs *r) { (void)r; finish_boss_gate(); }
static void t_prepare_game_over(Sm83Regs *r) { (void)r; prepare_game_over(); }
static void t_update_game_over(Sm83Regs *r) { (void)r; update_game_over(); }
static void t_prepare_time_up(Sm83Regs *r) { (void)r; prepare_time_up(); }
static void t_finish_time_up(Sm83Regs *r) { (void)r; finish_time_up(); }

/* Level start loads enemies from one of the thirteen real level lists.
   The checkpoint screen is chosen by the routine itself. */
static int level_start_valid(const Machine *m)
{
    unsigned world = peek_ram(m, 0xFFB4) >> 4;
    return world >= 1 && world <= 4 && callee_valid(m, fn_tests_enemies, 0x2442);
}
static int checkpoint_valid(const Machine *m)
{
    Machine reset = *m;
    unsigned screen = peek_ram(m, 0xFFF9) ? (uint8_t)(peek_ram(m, 0xFFF5) + 1) : peek_ram(m, 0xFFE5);
    if (screen != 3) screen = (uint8_t)(screen - 1);
    unsigned checkpoint = screen < 7 ? 3 : screen < 11 ? 7 : screen < 15 ? 11 : screen < 19 ? 15 : screen < 23 ? 19 : 23;
    reset.hram[0x65] = checkpoint;
    reset.hram[0x66] = 0;
    return level_columns_valid(&reset);
}
static int menu_setup_valid(const Machine *m)
{
    Machine menu = *m;
    menu.hram[0x64] = 12; menu.hram[0x65] = 0; menu.hram[0x66] = 0;
    return level_columns_valid(&menu);
}
static int advance_level_valid(const Machine *m)
{
    if (peek_ram(m, 0xFFA6) || peek_ram(m, 0xDFF9)) return 1;
    Machine next = *m;
    unsigned level = (uint8_t)(peek_ram(m, 0xFFE4) + 1);
    next.hram[0x64] = level == 12 ? 0 : level;
    unsigned world = (uint8_t)(peek_ram(m, 0xFFB4) + 1);
    if ((world & 15) == 4) world = (uint8_t)(world + 13);
    next.hram[0x34] = world;
    return level_start_valid(&next);
}
static int load_world_valid(const Machine *m) { return callee_valid(m, fn_tests_enemies, 0x2442); }
static int exit_state_valid(const Machine *m)
{
    return peek_ram(m, 0xFFA6) == 0 || callee_valid(m, fn_tests_player, 0x1736);
}
static void t_fill_menu_top_row(Sm83Regs *r) { fill_menu_top_row((uint16_t)(r->h << 8 | r->l), r->a); }
static void t_prepare_menu(Sm83Regs *r) { (void)r; prepare_menu(); }
static void t_update_menu(Sm83Regs *r) { (void)r; update_menu(); }
static void t_start_level(Sm83Regs *r) { (void)r; start_level(); }
static void t_restart_checkpoint(Sm83Regs *r) { (void)r; restart_checkpoint(); }

/* Ladder locations are map tiles; arbitrary fuzzed high bytes would
   make the original ladder stores overwrite unrelated RAM or the MBC. */
static int ladder_valid(const Machine *m)
{
    unsigned hi = peek_ram(m, 0xDA18);
    return hi == 0x9A || (hi >= 0x98 && hi <= 0x99 &&
        peek_ram(m, 0xDA28) >= 1 && peek_ram(m, 0xDA28) <= 4 &&
        peek_ram(m, 0xDA29) >= 1 && peek_ram(m, 0xDA29) <= 17);
}
static void t_prepare_bonus_game(Sm83Regs *r) { (void)r; prepare_bonus_game(); }
static void t_draw_bonus_room(Sm83Regs *r) { (void)r; draw_bonus_room(); }
static void t_draw_bonus_ladder(Sm83Regs *r) { (void)r; draw_bonus_ladder(); }
static void t_prepare_bonus_mario(Sm83Regs *r) { (void)r; prepare_bonus_mario(); }
static void t_update_bonus_selection(Sm83Regs *r) { (void)r; update_bonus_selection(); }
static void t_walk_bonus_mario(Sm83Regs *r) { (void)r; walk_bonus_mario(); }
static void t_descend_bonus_ladder(Sm83Regs *r) { (void)r; descend_bonus_ladder(); }
static void t_ascend_bonus_ladder(Sm83Regs *r) { (void)r; ascend_bonus_ladder(); }
static void t_award_bonus_prize(Sm83Regs *r) { (void)r; award_bonus_prize(); }

static int daisy_scroll_valid(const Machine *m)
{
    return peek_ram(m, 0xFFA6) == 0 ||
        (level_columns_valid(m) && callee_valid(m, fn_tests_player, 0x1736));
}
static int daisy_escape_valid(const Machine *m)
{
    return peek_ram(m, 0xFFA6) != 0 || callee_valid(m, fn_tests_player, 0x1736);
}
static int underground_entry_valid(const Machine *m)
{
    Machine room = *m;
    room.hram[0x65] = peek_ram(m, 0xFFF4); room.hram[0x66] = 0;
    return level_columns_valid(&room);
}
static int pipe_side_valid(const Machine *m)
{
    if (!(peek_ram(m, 0xFFAC) & 1)) return 1;
    if (peek_ram(m, 0xFFF8) >= peek_ram(m, 0xC202)) return exit_walk_valid(m);
    Machine room = *m;
    room.hram[0x65] = peek_ram(m, 0xFFF5); room.hram[0x66] = 0;
    return level_columns_valid(&room);
}
static int reunion_valid(const Machine *m) { return first_screen_valid(m); }
static int daisy_run_valid(const Machine *m)
{
    return dialogue_valid(m) && callee_valid(m, fn_tests_player, 0x1736);
}
static void t_scroll_to_daisy(Sm83Regs *r) { (void)r; scroll_to_daisy(); }
static void t_approach_daisy(Sm83Regs *r) { (void)r; approach_daisy(); }
static void t_update_daisy_escape(Sm83Regs *r) { (void)r; update_daisy_escape(); }
static void t_update_tatanga_defeat(Sm83Regs *r) { (void)r; update_tatanga_defeat(); }
static void t_prepare_daisy_reunion(Sm83Regs *r) { (void)r; prepare_daisy_reunion(); }
static void t_update_reunion_dialogue(Sm83Regs *r) { (void)r; update_reunion_dialogue(); }
static void t_update_daisy_run(Sm83Regs *r) { (void)r; update_daisy_run(); }
static void t_update_daisy_kiss(Sm83Regs *r) { (void)r; update_daisy_kiss(); }
static void t_update_quest_complete(Sm83Regs *r) { (void)r; update_quest_complete(); }
static void t_enter_pipe_down(Sm83Regs *r) { (void)r; enter_pipe_down(); }
static void t_enter_underground(Sm83Regs *r) { (void)r; enter_underground(); }
static void t_exit_pipe_sideways(Sm83Regs *r) { (void)r; exit_pipe_sideways(); }
static void t_exit_pipe_up(Sm83Regs *r) { (void)r; exit_pipe_up(); }

static int plane_board_valid(const Machine *m) { return level_columns_valid(m) && exit_walk_valid(m); }
/* The five ending objects must fit the 37 OAM entries available from C00C.
   Randomly making the old plane visible beside the new plane and clouds
   overflows the original buffer into unrelated counters. The game hides
   that old slot before the cloud scenes. Count tiles from the real layouts. */
static int plane_draw_valid(const Machine *m)
{
    if (!callee_valid(m, fn_tests_player, 0x1736)) return 0;
    unsigned sprites = 0;
    for (unsigned i = 0; i < 5; i++) {
        unsigned obj = 0xC200 + i * 16, flags = peek_ram(m, obj);
        if (flags != 0 && flags != 0x80) continue;
        unsigned n = peek_ram(m, obj + 3);
        int layout = rom_word_at(m, 3, 0x4C37 + n * 2);
        int list = layout < 0 ? -1 : rom_word_at(m, 3, (unsigned)layout);
        if (list < 0) return 0;
        unsigned k = 2;
        for (; k < 256; k++) {
            int tile = rom_at(m, 3, (unsigned)list + k);
            if (tile < 0) return 0;
            if (tile == 0xFF) break;
            if (tile != 0xFD && tile != 0xFE) sprites++;
        }
        if (k == 256 || sprites > 37) return 0;
    }
    return 1;
}
static int plane_cruise_valid(const Machine *m) { return level_columns_valid(m) && plane_draw_valid(m); }
/* The credits cursor always points into the immutable credits text. */
static int credits_valid(const Machine *m)
{
    unsigned cursor = peek_ram(m, 0xFFE2) << 8 | peek_ram(m, 0xFFE3);
    return plane_draw_valid(m) && (peek_ram(m, 0xFFA6) != 0 || (cursor >= 0x1557 && cursor < 0x161B));
}
static void t_board_plane(Sm83Regs *r) { (void)r; board_plane(); }
static void t_prepare_plane_takeoff(Sm83Regs *r) { (void)r; prepare_plane_takeoff(); }
static void t_update_plane_takeoff(Sm83Regs *r) { (void)r; update_plane_takeoff(); }
static void t_update_plane_cruise(Sm83Regs *r) { (void)r; update_plane_cruise(); }
static void t_leave_hangar(Sm83Regs *r) { (void)r; leave_hangar(); }
static void t_prepare_credit_lines(Sm83Regs *r) { (void)r; prepare_credit_lines(); }
static void t_scroll_credits_in(Sm83Regs *r) { (void)r; scroll_credits_in(); }
static void t_hold_credits(Sm83Regs *r) { (void)r; hold_credits(); }
static void t_scroll_credits_out(Sm83Regs *r) { (void)r; scroll_credits_out(); }
static void t_prepare_ending_letters(Sm83Regs *r) { (void)r; prepare_ending_letters(); }
static void t_update_ending_letters(Sm83Regs *r) { (void)r; update_ending_letters(); }

static void t_draw_first_level_screen(Sm83Regs *r) { (void)r; draw_first_level_screen(); }
static void t_reset_to_menu(Sm83Regs *r) { (void)r; reset_to_menu(); }
static void t_walk_right_at_exit(Sm83Regs *r) { (void)r; walk_right_at_exit(); }
static void t_update_exit_walk(Sm83Regs *r) { (void)r; update_exit_walk(); }
static void t_prepare_mario_and_daisy(Sm83Regs *r) { (void)r; prepare_mario_and_daisy(); }
static void t_update_daisy_meeting(Sm83Regs *r) { (void)r; update_daisy_meeting(); }
static void t_print_victory_message(Sm83Regs *r) { r->a = print_victory_message((uint16_t)(r->h << 8 | r->l)); }
static void t_update_daisy_dialogue(Sm83Regs *r) { (void)r; update_daisy_dialogue(); }
static void t_write_daisy_sprites(Sm83Regs *r) { write_daisy_sprites((uint16_t)(r->h << 8 | r->l)); }
static void t_update_daisy_morph(Sm83Regs *r) { (void)r; update_daisy_morph(); }

static void t_animate_background(Sm83Regs *r) { (void)r; animate_background(); }
static void t_update_level_columns(Sm83Regs *r) { (void)r; update_level_columns(); }
static void t_load_next_column(Sm83Regs *r) { (void)r; load_next_column(); }
static void t_draw_column(Sm83Regs *r) { (void)r; draw_column(); }
static void t_check_pipe_for_warp(Sm83Regs *r) { (void)r; check_pipe_for_warp(); }
static void t_put_pipe_warp(Sm83Regs *r) { put_pipe_warp((uint16_t)(r->h << 8 | r->l)); }
static void t_check_block_for_item(Sm83Regs *r) { (void)r; check_block_for_item(); }
static void t_put_block_item(Sm83Regs *r) { put_block_item((uint16_t)(r->h << 8 | r->l)); }
static void t_start_level_music(Sm83Regs *r) { (void)r; start_level_music(); }
static void t_update_timer_and_floaties(Sm83Regs *r) { (void)r; update_timer_and_floaties(); }
static void t_update_game_timer(Sm83Regs *r) { (void)r; update_game_timer(); }
static void t_update_floaties(Sm83Regs *r) { (void)r; update_floaties(); }
static void t_display_timer(Sm83Regs *r) { (void)r; display_timer(); }
static void t_print_timer(Sm83Regs *r) { (void)r; print_timer(); }
static void t_init_level_counters(Sm83Regs *r) { (void)r; init_level_counters(); }
static void t_update_normal_gameplay(Sm83Regs *r) { (void)r; update_normal_gameplay(); }
static void t_update_autoscroll_gameplay(Sm83Regs *r) { (void)r; update_autoscroll_gameplay(); }
static void t_dispatch_game_state(Sm83Regs *r) { (void)r; dispatch_game_state(); }
static void t_pause_or_reset(Sm83Regs *r) { (void)r; pause_or_reset(); }

/* Reset jumps into the endless bootstrap. The C main-loop replay tests
   that path; a returning function test can only exercise pause control. */
static int pause_valid(const Machine *m) { return (peek_ram(m, 0xFF80) & 15) != 15; }
static int dispatch_valid(const Machine *m)
{
    unsigned state = peek_ram(m, 0xFFB3);
    if (state > 0x3D) return 0;
    unsigned target = m->rom[0x2A6 + state * 2] | m->rom[0x2A7 + state * 2] << 8;
    if (state == 0x10 || state == 0x3D) return 1;
    unsigned bank = target < 0x4000 ? 0 : m->rom_bank;
    for (const FnTest *t = fn_tests_level; t && t->name; t++)
        if ((unsigned)t->bank == bank && t->addr == target)
            return !t->valid_state || t->valid_state(m);
    return 0;
}

const FnTest fn_tests_level[] = {
    { "DispatchGameState", 0, 0x02A3, t_dispatch_game_state, 0, 0, dispatch_valid },
    { "PrepareMenu", 0, 0x0322, t_prepare_menu, 0, 0, menu_setup_valid },
    { "UpdateMenu", 0, 0x04C3, t_update_menu, 0, 0 },
    { "FillMenuTopRow", 0, 0x056F, t_fill_menu_top_row, 0, 0 },
    { "StartLevel", 0, 0x0576, t_start_level, 0, 0, level_start_valid },
    { "UpdateNormalGameplay", 0, 0x0627, t_update_normal_gameplay, 0, 0, normal_gameplay_valid },
    { "FinishDeath", 0, 0x06BC, t_finish_death, 0, 0 },
    { "RestartCheckpoint", 0, 0x06DC, t_restart_checkpoint, 0, 0, checkpoint_valid },
    { "StartLevelMusic",     0, 0x07A3, t_start_level_music, 0, 0 },
    { "PauseOrReset", 0, 0x07DA, t_pause_or_reset, 0, 0, pause_valid },
    { "DrawFirstLevelScreen", 0, 0x0807, t_draw_first_level_screen, 0, 0, first_screen_valid },
    { "PrepareDeathAnimation", 0, 0x0B8D, t_prepare_death_animation, 0, 0 },
    { "UpdateDeathAnimation", 0, 0x0BD6, t_update_death_animation, 0, 0 },
    { "UpdateLevelExit", 0, 0x0C40, t_update_level_exit, 0, 0, exit_state_valid },
    { "CountDownLevelScore", 0, 0x0C73, t_count_down_level_score, 0, 0, level_score_valid },
    { "FinishLevel", 0, 0x0CCB, t_finish_level, 0, 0 },
    { "AdvanceLevel", 0, 0x0D49, t_advance_level, 0, 0, advance_level_valid },
    { "LoadWorldTiles", 0, 0x0D6D, t_load_world_tiles, 0, 0, load_world_valid },
    { "LeaveBonusGame", 0, 0x0DF9, t_leave_bonus_game, 0, 0 },
    { "PrepareBossGate", 0, 0x0E15, t_prepare_boss_gate, 0, 0, boss_gate_valid },
    { "LocateBossGate", 0, 0x0E31, t_locate_boss_gate, 0, 0, locate_gate_valid },
    { "OpenBossGate", 0, 0x0E5D, t_open_boss_gate, 0, 0 },
    { "FinishBossGate", 0, 0x0E96, t_finish_boss_gate, 0, 0 },
    { "UpdateExitWalk", 0, 0x0EA9, t_update_exit_walk, 0, 0, exit_walk_valid },
    { "WalkRightAtExit", 0, 0x0EBB, t_walk_right_at_exit, 0, 0, exit_walk_valid },
    { "UpdateDaisyMeeting", 0, 0x0ECD, t_update_daisy_meeting, 0, 0 },
    { "PrepareMarioAndDaisy", 0, 0x0EE7, t_prepare_mario_and_daisy, 0, 0 },
    { "ScrollToDaisy", 0, 0x0F12, t_scroll_to_daisy, 0, 0, daisy_scroll_valid },
    { "ApproachDaisy", 0, 0x0F33, t_approach_daisy, 0, 0, exit_walk_valid },
    { "UpdateDaisyDialogue", 0, 0x0F6A, t_update_daisy_dialogue, 0, 0, dialogue_valid },
    { "PrintVictoryMessage", 0, 0x0F8A, t_print_victory_message, OUT_A, 0, dialogue_valid },
    { "UpdateDaisyMorph", 0, 0x0FFD, t_update_daisy_morph, 0, 0 },
    { "WriteDaisySprites", 0, 0x1029, t_write_daisy_sprites, 0, 0 },
    { "UpdateDaisyEscape", 0, 0x1055, t_update_daisy_escape, 0, 0, daisy_escape_valid },
    { "UpdateTatangaDefeat", 0, 0x1099, t_update_tatanga_defeat, 0, 0, enemies_frame_valid },
    { "PrepareDaisyReunion", 0, 0x1116, t_prepare_daisy_reunion, 0, 0, reunion_valid },
    { "UpdateReunionDialogue", 0, 0x1165, t_update_reunion_dialogue, 0, 0, dialogue_valid },
    { "UpdateDaisyRun", 0, 0x1194, t_update_daisy_run, 0, 0, daisy_run_valid },
    { "UpdateDaisyKiss", 0, 0x11D0, t_update_daisy_kiss, 0, 0 },
    { "UpdateQuestComplete", 0, 0x121B, t_update_quest_complete, 0, 0, dialogue_valid },
    { "BoardPlane", 0, 0x1254, t_board_plane, 0, 0, plane_board_valid },
    { "PreparePlaneTakeoff", 0, 0x12A1, t_prepare_plane_takeoff, 0, 0 },
    { "UpdatePlaneTakeoff", 0, 0x12C2, t_update_plane_takeoff, 0, 0, plane_draw_valid },
    { "UpdatePlaneCruise", 0, 0x12F1, t_update_plane_cruise, 0, 0, plane_cruise_valid },
    { "LeaveHangar", 0, 0x138E, t_leave_hangar, 0, 0, plane_draw_valid },
    { "PrepareCreditLines", 0, 0x13F0, t_prepare_credit_lines, 0, 0, credits_valid },
    { "ScrollCreditsIn", 0, 0x1441, t_scroll_credits_in, 0, 0, plane_draw_valid },
    { "HoldCredits", 0, 0x145A, t_hold_credits, 0, 0, plane_draw_valid },
    { "ScrollCreditsOut", 0, 0x1466, t_scroll_credits_out, 0, 0, plane_draw_valid },
    { "PrepareEndingLetters", 0, 0x1488, t_prepare_ending_letters, 0, 0, plane_draw_valid },
    { "UpdateEndingLetters", 0, 0x14DC, t_update_ending_letters, 0, 0, plane_draw_valid },
    { "ResetToMenu", 0, 0x1530, t_reset_to_menu, 0, 0 },
    { "EnterPipeDown", 0, 0x161B, t_enter_pipe_down, 0, 0, exit_walk_valid },
    { "EnterUnderground", 0, 0x162F, t_enter_underground, 0, 0, underground_entry_valid },
    { "ExitPipeSideways", 0, 0x166C, t_exit_pipe_sideways, 0, 0, pipe_side_valid },
    { "ExitPipeUp", 0, 0x16DA, t_exit_pipe_up, 0, 0, exit_walk_valid },
    { "PrepareGameOver", 0, 0x1C7C, t_prepare_game_over, 0, 0 },
    { "UpdateGameOver", 0, 0x1CE8, t_update_game_over, 0, 0 },
    { "PrepareTimeUp", 0, 0x1CF0, t_prepare_time_up, 0, 0 },
    { "FinishTimeUp", 0, 0x1D1D, t_finish_time_up, 0, 0 },
    { "UpdateLevelColumns",  0, 0x2198, t_update_level_columns, 0, 0, level_columns_valid },
    { "LoadNextColumn",      0, 0x21B1, t_load_next_column, 0, 0, level_columns_valid },
    { "DrawColumn",          0, 0x2258, t_draw_column, 0, 1, draw_column_valid },
    { "CheckPipeForWarp",    0, 0x22A9, t_check_pipe_for_warp, 0, 0, level_columns_valid },
    { "PutPipeWarp",         0, 0x22FD, t_put_pipe_warp, 0, 1 },
    { "CheckBlockForItem",   0, 0x2321, t_check_block_for_item, 0, 0, level_columns_valid },
    { "PutBlockItem",        0, 0x2363, t_put_block_item, 0, 1 },
    { "UpdateAutoscrollGameplay", 0, 0x2376, t_update_autoscroll_gameplay, 0, 0, autoscroll_gameplay_valid },
    { "AnimateBackground",   0, 0x2401, t_animate_background, 0, 1 },
    { "InitLevelCounters",   0, 0x3D1A, t_init_level_counters, 0, 0 },
    { "DisplayTimer",        0, 0x3D6A, t_display_timer, 0, 1 },
    { "PrintTimer",          0, 0x3D7E, t_print_timer, 0, 0 },
    { "PrepareBonusGame", 0, 0x3D97, t_prepare_bonus_game, 0, 0 },
    { "DrawBonusRoom", 0, 0x3DD7, t_draw_bonus_room, 0, 0 },
    { "DrawBonusLadder", 0, 0x3EA7, t_draw_bonus_ladder, 0, 0, ladder_valid },
    { "PrepareBonusMario", 2, 0x5832, t_prepare_bonus_mario, 0, 0 },
    { "UpdateBonusSelection", 2, 0x5835, t_update_bonus_selection, 0, 0 },
    { "WalkBonusMario", 2, 0x5838, t_walk_bonus_mario, 0, 0 },
    { "DescendBonusLadder", 2, 0x583B, t_descend_bonus_ladder, 0, 0 },
    { "AscendBonusLadder", 2, 0x583E, t_ascend_bonus_ladder, 0, 0 },
    { "AwardBonusPrize", 2, 0x5841, t_award_bonus_prize, 0, 0 },
    { "UpdateTimerAndFloaties", 2, 0x5844, t_update_timer_and_floaties, 0, 0, floaties_valid },
    { "UpdateGameTimer",     2, 0x584B, t_update_game_timer, 0, 0 },
    { "UpdateFloaties",      2, 0x5892, t_update_floaties, 0, 0, floaties_valid },
    { 0 },
};
