/*
 * Constants shared by player.c and level.c (values from the original ROM).
 */
#ifndef SML_CONSTANTS_H
#define SML_CONSTANTS_H

/* Game states (hGameState) that the player code tests or sets */
enum {
    STATE_PLAYING = 0x00,
    STATE_DEAD = 0x01,
    STATE_PRE_DYING = 0x03,
    STATE_LEVEL_END = 0x07,
    STATE_PIPE_DOWN = 0x09,
    STATE_PIPE_UNDERGROUND = 0x0A,
    STATE_PIPE_SIDEWAYS = 0x0B,
    STATE_AUTOSCROLL = 0x0D,
    STATE_MENU = 0x0E,         /* states from here on are not "normal gameplay" */
};

/* Background tiles with a meaning for the collision code */
enum {
    TILE_BLANK = 0x2C,
    TILE_HIDDEN_BLOCK = 0x5F,       /* looks empty, solid from below */
    TILE_FIRST_SOLID = 0x60,        /* tiles from here on are solid ... */
    TILE_PIPE_TOP = 0x70,           /* ... a pipe Mario can enter from above */
    TILE_PIPE_SIDE = 0x77,          /* the entrance of a sideways pipe */
    TILE_USED_BLOCK = 0x7F,
    TILE_SOLID_BLOCK_SPRITE = 0x80, /* in the block sprite: grey block with a hidden item */
    TILE_COIN_BLOCK = 0x81,
    TILE_BREAKABLE_BLOCK = 0x82,
    TILE_SPIKES = 0xED,
    TILE_BOSS_SWITCH = 0xE1,
    TILE_FIST = 0xF2,
    TILE_COIN = 0xF4,
    TILE_NOT_A_BLOCK = 0xF0,
};

#endif
