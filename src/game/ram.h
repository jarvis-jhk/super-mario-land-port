/*
 * The game's variables, at the addresses the original uses.
 *
 * Work RAM is C000-DFFF, high RAM FF80-FFFE. Names follow what the
 * variables mean; the comment gives the label used in the community
 * disassembly (reference/disasm), if any, so both can be cross-referenced.
 * Keep this file sorted by address.
 */
#ifndef SML_RAM_H
#define SML_RAM_H

#include "rt.h"

/* ---- work RAM ---- */
#define oam_buffer           WRAM_PTR(0xC000)  /* wOAMBuffer: shadow OAM, 4 bytes per sprite (y, x, tile, attributes); copied to OAM by DMA every frame */
#define score_bcd            WRAM_PTR(0xC0A0)  /* wScore: 3 bytes, BCD, least significant first */
#define lives_change         WRAM(0xC0A3)      /* wLivesEarnedLost: $FF lose a life, other nonzero gain one */
#define game_over_state_copy WRAM(0xC0A4)      /* gets the game over state number too */
#define num_continues        WRAM(0xC0A6)      /* wNumContinues */
#define continue_world_level WRAM(0xC0A8)      /* wContinueWorldAndLevel */
#define superball_ttl        WRAM(0xC0A9)  /* wSuperballTTL: frames left before the superball disappears */
#define drawn_scroll_x       WRAM(0xC0AA)  /* scroll position when the last column was drawn (see scroll_pending) */
#define level_progress       WRAM(0xC0AB)  /* counts level columns (two columns per step); drives enemy spawning */
#define death_step           WRAM(0xC0AC)  /* index into the dying Mario arc */
#define shot_cooldown          WRAM(0xC0AE)  /* frames until the next shot may be fired (the shooter levels: while A is held) */
#define column_buffer        WRAM_PTR(0xC0B0)  /* the next level column (16 tiles, top to bottom), filled by load_next_column */
#define block_item           WRAM(0xC0CD)    /* item hidden in the block being loaded/drawn (0 none), see check_block_for_item */
#define coin_block_timer     WRAM(0xC0CE)  /* frames left in which a coin block can still pay out coins */
#define level_end_counter    WRAM(0xC0D2)  /* starts counting when the end of the level is reached */
#define invincibility_timer  WRAM(0xC0D3)  /* wInvincibilityTimer: star power, counts down every 4th frame */
#define demo_timer          WRAM(0xC0D7)  /* title/demo countdown, decremented every 16 frames */
#define demo_wait              WRAM(0xC0D8)  /* frames left before the attract-mode demo reads its next input */
#define demo_pos               WRAM(0xC0D9)  /* position in the current demo input list */
#define demo_input             WRAM(0xC0DA)  /* the demo's current input, fed to the game as the joypad */
#define demo_joypad          WRAM(0xC0DB)  /* joypad state the attract-mode demo feeds to the game */
#define demo_part              WRAM(0xC0DC)  /* which of the three demo input lists plays */
#define death_y              WRAM(0xC0DD)  /* Mario's Y position when he died */
#define hud_coins_drawn      WRAM(0xC0E2)      /* set when the coin counter was redrawn; blocks the score redraw */
#define mario_flags          WRAM(0xC200)  /* mario object (C200-C20F): $80 = not drawn (blinking, dying), 0 = drawn */
#define mario_y              WRAM(0xC201)  /* Mario's Y position on screen (+16) */
#define mario_x              WRAM(0xC202)  /* Mario's X position on screen (+8) */
#define mario_anim           WRAM(0xC203)  /* animation frame (low nibble) and size look (high nibble: 0 small, 1 big, ...) */
#define mario_scripted       WRAM(0xC204)  /* $80 while a pipe animation moves Mario */
#define mario_facing         WRAM(0xC205)  /* sprite attribute for the facing direction: 0 right, $20 left */
#define mario_jump_state     WRAM(0xC207)  /* 0 on the ground, 1 rising, 2 falling */
#define mario_jump_step      WRAM(0xC208)  /* index into the jump arc table */
#define mario_c209           WRAM(0xC209)  /* cleared together with the jump state; purpose unknown */
#define mario_on_ground      WRAM(0xC20A)  /* 1 while standing on something */
#define mario_walk_counter   WRAM(0xC20B)  /* frames walked in one direction; drives the walk animation */
#define mario_momentum       WRAM(0xC20C)  /* 0-6: how far the walking speed has built up */
#define mario_walk_dir       WRAM(0xC20D)  /* $10 walking right, $20 walking left, 1 turning around, 0 standing */
#define mario_speed_mode     WRAM(0xC20E)  /* 0 standing, 2 walking, 4 running (fast walk) */
#define mario_step_toggle    WRAM(0xC20F)  /* alternates 0/1 each step: half-pixel walking speed */
#define object_attributes      WRAM(0xD000)  /* sprite attributes for the tile list being drawn by draw_enemy */
#define script_command         WRAM(0xD002)  /* wCurrentCommand: the enemy script command being executed */
#define script_argument        WRAM(0xD003)  /* wCommandArgument: the byte after a two-byte script command */
#define boss_defeated          WRAM(0xD007)  /* set to $60 when Tatanga is killed; cleared when a level starts */
#define next_enemy_lo          WRAM(0xD010)  /* address (low) of the next entry of the level's enemy list to spawn */
#define next_enemy_hi          WRAM(0xD011)  /* (high) */
#define objects_drawn          WRAM(0xD013)  /* wObjectsDrawn: sprites used so far by the enemies being drawn (max 20) */
#define background_animated  WRAM(0xD014)    /* wBackgroundAnimated: the level has an animated background tile */
#define game_timer           WRAM_PTR(0xDA00)  /* wGameTimer: [0] frames in the current time unit, [1] ones+tens (BCD), [2] hundreds */
#define floaty_ttl           WRAM_PTR(0xDA03)  /* wFloaty0_TTL..wFloaty3_TTL: per floaty, moves left before it disappears */
#define floaty_coin_tile     WRAM_PTR(0xDA07)  /* wFloaty0_SpriteIfCoin..: per floaty, current tile of a spinning coin (F6-F8) */
#define next_floaty_oam      WRAM(0xDA0B)      /* wNextFloatyOAMIndex: OAM buffer offset of the next floaty (30, 38, 40, 48) */
#define floaty_is_coin       WRAM_PTR(0xDA0C)  /* wFloaty0_IsCoin..: per floaty, C0 if it is a coin (moves every frame) */
#define floaty_step          WRAM_PTR(0xDA10)  /* per floaty, frame toggle: numbers move up every second frame */
#define bonus_anim           WRAM(0xDA14)      /* walking sprite-list offset */
#define lives_bcd            WRAM(0xDA15)      /* wLives: lives left, BCD */
#define bonus_probe_timer    WRAM(0xDA16)      /* ladder collision cooldown */
#define bonus_prize          WRAM(0xDA17)      /* prize/growth counter */
#define ladder_map_hi        WRAM(0xDA18)      /* wLadderLocationHi */
#define ladder_map_lo        WRAM(0xDA19)      /* wLadderLocationLo */
#define bonus_spare          WRAM(0xDA1A)      /* cleared when the ladder is selected */
#define bonus_end_timer      WRAM(0xDA1B)      /* wBonusGameEndTimer */
#define bonus_walking        WRAM(0xDA1C)      /* walking/growth started */
#define timer_expiring_flag  WRAM(0xDA1D)  /* wGameTimerExpiringFlag */
#define bonus_grow           WRAM(0xDA1E)      /* wBonusGameGrowAnimationFlag */
#define bonus_anim_timer     WRAM(0xDA1F)      /* wBonusGameAnimationTimer */
#define bonus_jump           WRAM(0xDA20)      /* reward jump step */
#define bonus_jump_phase     WRAM(0xDA21)      /* rising, falling, landing */
#define bonus_frame          WRAM(0xDA22)      /* wBonusGameFrameCounter */
#define ladder_tiles         WRAM_PTR(0xDA23)  /* wLadderTiles: four tiles */
#define ladder_phase         WRAM(0xDA27)      /* alternates erasing/drawing at each floor */
#define ladder_tiles_left    WRAM(0xDA28)      /* tiles left in a ladder pass */
#define ladder_passes        WRAM(0xDA29)      /* repeated writes during the ladder state */

/* ---- high RAM ---- */
#define joy_held             HRAM(0xFF80)  /* hJoyHeld: A=01 B=02 Select=04 Start=08 Right=10 Left=20 Up=40 Down=80 */
#define joy_pressed          HRAM(0xFF81)  /* hJoyPressed: buttons that went down since the last read */
#define draw_oam_hi          HRAM(0xFF8D)  /* argument of the object drawer: OAM buffer address of the first sprite (high) */
#define draw_oam_lo          HRAM(0xFF8E)  /* (low) */
#define hitbox_right         HRAM(0xFF8F)  /* right edge of the box being tested for collisions; also a counter for the object drawer */
#define super_status         HRAM(0xFF99)  /* hSuperStatus: 0 small, 1 growing, 2 super, 3 shrinking (blinks), 4 invulnerable after a hit */
#define win_count              HRAM(0xFF9A)  /* hWinCount: number of times the game was finished; hard-mode enemies appear when nonzero */
#define hit_enemy_flags      HRAM(0xFF9B)  /* health byte (D1xC) of the enemy being hit */
#define stomp_chain_timer    HRAM(0xFF9C)  /* frames left to continue a chain of stomps */
#define stomp_chain          HRAM(0xFF9D)  /* number of stomps in the current chain (0-3) */
#define hit_reward           HRAM(0xFF9E)  /* floaty code for what hitting the enemy is worth (01=100 .. 50=coin/1up) */
#define demo_mode            HRAM(0xFF9F)  /* nonzero while the attract-mode demo plays */
#define hitbox_top           HRAM(0xFFA0)  /* top edge of the box being tested; also a scratch byte of block code */
#define hitbox_bottom        HRAM(0xFFA1)  /* bottom edge of the box being tested */
#define hitbox_left          HRAM(0xFFA2)  /* left edge of the box being tested */
#define column_phase         HRAM(0xFFA3)  /* 0 or 8: toggles each time a new level column has to be loaded */
#define scroll_x             HRAM(0xFFA4)  /* hScrollX (shared with the STAT handler: main code uses shared_read/shared_write) */
#define frame_timer          HRAM(0xFFA6)  /* hTimer: generic countdown, decremented every frame */
#define superball_state      HRAM(0xFFA9)  /* hSuperball[3]: 0 = unused, bit 0 flying right/left, bits 2-3 up/down */
#define frame_counter        HRAM(0xFFAC)  /* hFrameCounter */
#define tile_y_px            HRAM(0xFFAD)  /* pixel position for tile lookups (y + 16) */
#define tile_x_px            HRAM(0xFFAE)  /* (x + 8) */
#define tile_addr_lo         HRAM(0xFFAF)  /* background map address of the looked-up tile */
#define tile_addr_hi         HRAM(0xFFB0)
#define score_changed        HRAM(0xFFB1)  /* the HUD score needs to be redrawn; while printing a
                                              number it means "a nonzero digit came already" */
#define game_paused          HRAM(0xFFB2)  /* hGamePaused: nonzero while gameplay is paused */
#define game_state_isr       HRAM(0xFFB3)  /* hGameState as seen by interrupt handlers (see shared.h) */
#define world_and_level      HRAM(0xFFB4)  /* hWorldAndLevel: world in the high nibble, level in the low one (BCD) */
#define superball_mario      HRAM(0xFFB5)  /* hSuperballMario: Mario carries the flower and can throw superballs */
#define saved_rom_bank       HRAM(0xFFE1)  /* hSavedRomBank */
#define text_cursor_hi       HRAM(0xFFE2)  /* hTextCursorHi: dialogue map cursor */
#define text_cursor_lo       HRAM(0xFFE3)  /* hTextCursorLo */
#define level_index          HRAM(0xFFE4)    /* hLevelIndex: 0-11, world*3 + level */
#define screen_index         HRAM(0xFFE5)    /* hScreenIndex: screen of the level being loaded */
#define column_index         HRAM(0xFFE6)    /* hColumnIndex: column within that screen (0-19) */
#define column_ptr_hi        HRAM(0xFFE7)    /* hColumnPointerHi: where the next column's data starts in ROM (big endian) */
#define column_ptr_lo        HRAM(0xFFE8)    /* hColumnPointerLo */
#define draw_column_lo       HRAM(0xFFE9)    /* low byte of the background map address the next column is drawn at (98xx, wraps 40-5F) */
#define column_state         HRAM(0xFFEA)  /* drawing of new level columns, see enum ColumnState in level.h */
#define floaty_x             HRAM(0xFFEB)  /* position and kind of the next score/coin number that floats up */
#define floaty_y             HRAM(0xFFEC)  /* (Y) */
#define floaty_control       HRAM(0xFFED)  /* (score code: 01=100, 04=400, 08=800, 10=1000, 50=1UP/coin, C0=coin, FF=1UP) */
#define block_event          HRAM(0xFFEE)  /* what a hit block needs from the vblank handler: 1 break, 2 bounce start, 3 bouncing, 4 bounce over, C0 collect coin */
#define block_event_hi       HRAM(0xFFEF)  /* background map address of the affected block (high) */
#define block_event_lo       HRAM(0xFFF0)  /* (low) */
#define block_sprite_y       HRAM(0xFFF1)  /* Y position of the bouncing block sprite */
#define block_sprite_x       HRAM(0xFFF2)  /* X position (in level coordinates) of the bouncing block sprite */
#define broken_block_scroll  HRAM(0xFFF3)  /* scroll position when a block was broken */
#define pipe_data            HRAM(0xFFF4)  /* 4 bytes (FFF4-FFF7) describing the pipe Mario entered: underground flag, screen, exit Y and X */
#define pipe_goal            HRAM(0xFFF8)  /* target position of the pipe animation */
#define underground          HRAM(0xFFF9)  /* nonzero while Mario is in an underground bonus area */
#define enemy_slot_lo        HRAM(0xFFFC)  /* low byte of the enemy slot being checked by the Mario-vs-objects loop ($90 = the powerup slot) */
#define active_rom_bank      HRAM(0xFFFD)    /* hActiveRomBank: the bank the code believes is mapped (the timer interrupt restores it) */
#define hud_coin_flag        HRAM(0xFFFE)
#define coins_bcd            HRAM(0xFFFA)  /* hCoins: BCD */

/* ---- variables shared with interrupt handlers (shared.h) ----
   Main-program code reaches these with shared_read/shared_write (or bus_*
   for pointer accesses that also hit plain RAM); the names above are for
   handler-only code. */
enum {
    ADDR_OAM_BUFFER = 0xC000,
    ADDR_SCORE = 0xC0A0,                 /* 3 bytes */
    ADDR_LIVES_CHANGE = 0xC0A3,
    ADDR_GAME_OVER_STATE_COPY = 0xC0A4,
    ADDR_COLUMN_BUFFER = 0xC0B0,         /* 16 bytes */
    ADDR_OVERLAY = 0xC800,               /* C800-CA3F */
    ADDR_BACKGROUND_ANIMATED = 0xD014,
    ADDR_GAME_TIMER = 0xDA00,            /* 3 bytes */
    ADDR_DEMO_MODE = 0xFF9F,
    ADDR_FRAME_COUNTER = 0xFFAC,
    ADDR_SCORE_CHANGED = 0xFFB1,
    ADDR_WORLD_AND_LEVEL = 0xFFB4,
    ADDR_DRAW_COLUMN_LO = 0xFFE9,
    ADDR_COLUMN_STATE = 0xFFEA,
    ADDR_BLOCK_EVENT = 0xFFEE,
    ADDR_BLOCK_EVENT_HI = 0xFFEF,
    ADDR_BLOCK_EVENT_LO = 0xFFF0,
    ADDR_ACTIVE_ROM_BANK = 0xFFFD,
    ADDR_HUD_COIN_FLAG = 0xFFFE,
};

#endif
