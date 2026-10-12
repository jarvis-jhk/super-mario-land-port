/*
 * Level subsystem: game states, scrolling and drawing the level, the
 * status bar timer, the bonus game, the ending. Original routines are
 * named after the community disassembly where it has names.
 */
#ifndef SML_LEVEL_H
#define SML_LEVEL_H

#include <stdint.h>

/* How far the next level column is: loaded into column_buffer by the main
   program, then drawn into the background map by the vblank handler. */
enum ColumnState {
    COLUMN_IDLE   = 0,
    COLUMN_READY  = 1,      /* loaded, waiting to be drawn */
    COLUMN_DRAWN  = 2,      /* drawn this frame (the score is not redrawn then) */
    COLUMN_WAIT   = 3,      /* wait until the screen has scrolled on */
};

/* level.c */
void animate_background(void);         /* 00:2401 AnimateBackground (vblank) */
void update_level_columns(void);       /* 00:2198 (+ 00:2188) */
void load_next_column(void);           /* 00:21B1 LoadNextColumn */
void draw_column(void);                /* 00:2258 DrawColumn (vblank) */
void check_pipe_for_warp(void);        /* 00:22A9 CheckPipeForWarp */
void put_pipe_warp(uint16_t map_addr); /* 00:22FD */
void check_block_for_item(void);       /* 00:2321 CheckBlockForItem */
void put_block_item(uint16_t map_addr);/* 00:2363 */

void start_level_music(void);          /* 00:07A3 StartLevelMusic */

/* menu.c, level_start.c and level_states.c */
void fill_menu_top_row(uint16_t map, uint8_t tile); /* 00:056F FillStartMenuTopRow */
void prepare_menu(void);               /* 00:0322 GameState_0E */
void update_menu(void);                /* 00:04C3 GameState_0F.entryPoint */
void start_level(void);                /* 00:0576 GameState_11 */
void restart_checkpoint(void);         /* 00:06DC GameState_02 */
void load_world_tiles(uint8_t world_level); /* 00:0D6D GameState_08.loadWorldTiles */
void advance_level(void);              /* 00:0D49 GameState_08 */
void finish_death(void);               /* 00:06BC GameState_01 */
void prepare_death_animation(void);    /* 00:0B8D GameState_03 */
void update_death_animation(void);     /* 00:0BD6 GameState_04 */
void update_level_exit(void);          /* 00:0C40 GameState_07 */
void count_down_level_score(void);     /* 00:0C73 GameState_05 */
void finish_level(void);               /* 00:0CCB GameState_06 */
void leave_bonus_game(void);           /* 00:0DF9 GameState_1B */
void prepare_boss_gate(void);          /* 00:0E15 GameState_1C */
void locate_boss_gate(void);           /* 00:0E31 GameState_1D */
void open_boss_gate(void);             /* 00:0E5D GameState_1E */
void finish_boss_gate(void);           /* 00:0E96 GameState_1F */
void prepare_game_over(void);          /* 00:1C7C GameState_39 */
void update_game_over(void);           /* 00:1CE8 GameState_3A */
void prepare_time_up(void);            /* 00:1CF0 GameState_3B */
void finish_time_up(void);             /* 00:1D1D GameState_3C */
void init_level_counters(void);        /* 00:3D1A Call_3D1A */

void draw_first_level_screen(void);    /* 00:0807 Call_807 */
void draw_initial_columns(void);       /* 00:081F Call_807.drawLevel */
void reset_to_menu(void);              /* 00:1530 GameState_38.resetToMenu */

/* bonus.c */
void prepare_bonus_game(void);         /* 00:3D97 GameState_12 */
void draw_bonus_room(void);            /* 00:3DD7 GameState_13 */
void draw_bonus_ladder(void);          /* 00:3EA7 GameState_16 */
void prepare_bonus_mario(void);        /* 02:5832 GameState_14 */
void update_bonus_selection(void);     /* 02:5835 GameState_15 */
void walk_bonus_mario(void);           /* 02:5838 GameState_17 */
void descend_bonus_ladder(void);       /* 02:583B GameState_18 */
void ascend_bonus_ladder(void);        /* 02:583E GameState_19 */
void award_bonus_prize(void);          /* 02:5841 GameState_1A */

/* ending.c */
void walk_right_at_exit(void);         /* 00:0EBB GameState_20.walkRight */
void update_exit_walk(void);           /* 00:0EA9 GameState_20 / GameState_28 */
void prepare_mario_and_daisy(void);     /* 00:0EE7 GameState_21.prepareMarioAndDaisy */
void update_daisy_meeting(void);        /* 00:0ECD GameState_21 */
uint8_t print_victory_message(uint16_t text); /* 00:0F8A PrintVictoryMessage */
void update_daisy_dialogue(void);       /* 00:0F6A GameState_24 */
void write_daisy_sprites(uint16_t src); /* 00:1029 GameState_25.writeSprite */
void update_daisy_morph(void);          /* 00:0FFD GameState_25 */
void scroll_to_daisy(void);            /* 00:0F12 GameState_22 */
void approach_daisy(void);             /* 00:0F33 GameState_23 */
void update_daisy_escape(void);        /* 00:1055 GameState_26 */
void update_tatanga_defeat(void);      /* 00:1099 GameState_27 */
void prepare_daisy_reunion(void);      /* 00:1116 GameState_29 */
void update_reunion_dialogue(void);    /* 00:1165 GameState_2A */
void update_daisy_run(void);           /* 00:1194 GameState_2B */
void update_daisy_kiss(void);          /* 00:11D0 GameState_2C */
void update_quest_complete(void);       /* 00:121B GameState_2D */

/* credits.c */
void animate_plane_exhaust(uint16_t x_addr); /* 00:12E6 */
void animate_plane(void);              /* 00:130E */
void animate_clouds(void);             /* 00:13C4 */
void animate_plane_and_clouds(void);   /* 00:1550 */
void board_plane(void);                /* 00:1254 GameState_2E */
void prepare_plane_takeoff(void);      /* 00:12A1 GameState_2F */
void update_plane_takeoff(void);       /* 00:12C2 GameState_30 */
void clear_flight_column(void);        /* 00:134E */
void replace_cloud(uint16_t obj, uint16_t src); /* 00:1376 */
void count_plane_scroll_wrap(void);    /* 00:1318 */
void update_plane_cruise(void);        /* 00:12F1 GameState_31 */
void leave_hangar(void);               /* 00:138E GameState_32 */
void prepare_credit_lines(void);       /* 00:13F0 GameState_33 */
void scroll_credits_in(void);          /* 00:1441 GameState_34 */
void hold_credits(void);               /* 00:145A GameState_35 */
void scroll_credits_out(void);         /* 00:1466 GameState_36 */
void prepare_ending_letters(void);     /* 00:1488 GameState_37 */
void ending_button_press(void);        /* 00:1529 */
void update_ending_letters(void);      /* 00:14DC GameState_38 */

/* pipes.c */
void enter_pipe_down(void);            /* 00:161B GameState_09 */
void enter_underground(void);          /* 00:162F GameState_0A */
void exit_pipe_sideways(void);         /* 00:166C GameState_0B */
void exit_pipe_up(void);               /* 00:16DA GameState_0C */

/* level_play.c */
void update_normal_gameplay(void);     /* 00:0627 GameState_00 */
void update_autoscroll_gameplay(void);  /* 00:2376 GameState_0D */

/* level_floaties.c */
void update_timer_and_floaties(void);  /* 02:5844 UpdateTimerAndFloaties */
void update_game_timer(void);          /* 02:584B UpdateGameTimer */
void update_floaties(void);            /* 02:5892 UpdateFloaties */

/* hud.c (level part) */
void display_timer(void);              /* 00:3D6A DisplayTimer (vblank) */
void print_timer(void);                /* 00:3D7E DisplayTimer.printTimer */

#endif
