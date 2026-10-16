# Custom levels and the level editor

The level editor builds levels from the pieces Super Mario Land has: the
tiles, enemies and block items of one of its four worlds. Levels are small
text files of numbers (below); they hold no ROM bytes, graphics or music, so
they can be shared. Everything they show comes from the player's own ROM.

## Custom mode

A custom level is played in **Custom mode**, separate from the stock game:

- The frontend writes the level, in the game's own level format, into a
  *copy* of the ROM in memory (src/platform/custom_level.c), in place of the
  first level of its world (1-1, 2-1, 3-1 or 4-1) and in the space of that
  world's stock level data. The ROM file is never changed; the stock game
  always runs on the unchanged ROM, so every TAS plays back as before.
- The game is started from the title screen like a continue into that world
  (the frontend sets the continue counter and presses Start). The translated
  game code is the same as in the stock game; only the level data differs.
- Clearing the level (walking off its right end) or a game over returns to
  the editor. Lives, score and hard mode are the game's own.
- The speedrun timer and RetroAchievements are off in Custom mode; nothing
  is saved to the timer's files. A `--record` movie of a custom level starts
  with `# custom level <sha256> (<name>)`, the SHA-256 of the level file's
  canonical text: its replay identity. Its first input is the first poll of
  the level, not of the title screen (`--play-level FILE --movie M` replays
  it).

Limits of this first version: no warp pipes into underground rooms (pipes
can be placed as scenery; the pipe table is empty), no autoscroll/shooter
levels, no checkpoint choice (the game restarts at its own checkpoints every
four screens), at most 24 screens (the enemy list counts columns in a
byte), and the level must fit into its world's stock level space (about
3.7 KiB in world 1, 4.3 KiB in world 3, 10 KiB in worlds 2 and 4;
identical screens are stored once).

## File format (version 1)

UTF-8 text, one item per line, `#` starts a comment line:

    SML-LEVEL 1
    name My first level
    world 1
    columns 160
    row  0 2C2C2C...          16 lines "row R", R = 0..15 from the top,
    ...                       two hex digits (tile number) per column
    row 15 616161...
    enemy 30 12 04            enemy at column 30, row 12, enemy number 04
    item 26 28                item in the item block of column 26

- `world` 1-4 selects graphics, music and the enemy set.
- `columns` is a multiple of 20 (one screen), 40 to 480.
- Rows are the 16 level rows under the status bar. Tile `2C` is empty; tiles
  from `60` on are solid. `FD`-`FF` are reserved.
- Mario starts in columns 4-5, rows 12-13, standing on row 14 (rows 14-15 are the floor of a
  new level). The level ends after its last column: walking off the right
  end clears it.
- `enemy COLUMN ROW ID`: the enemy's top-left tile and the game's enemy
  number (00-7F). Enemies start from column 26: the game spawns them at the
  right edge of the screen while it scrolls, so earlier ones could not
  appear in place.
- `item COLUMN ID`: what the first item block (`80`), coin block (`81`) or
  hidden block (`5F`) of a column releases: `28` mushroom (a flower for
  Super Mario), `2A` heart (1-Up), `2C` star, `C0` coins.

`cl_identity()` hashes the canonical text (what the editor writes: enemies
sorted by column and row), so a level has the same identity however its
file was written.

## Editor

Open it from the menu (*Level editor*) or with `sml --editor [FILE]`.
Levels are saved in the data directory under `levels/` as
`<name>.level.txt`; the editor opens the newest one, or a new level.

| Action | Keyboard / mouse | Controller |
|--------|------------------|------------|
| move the cursor | arrows (Ctrl: a screen), PgUp/PgDn, Home/End, mouse | D-pad, left stick |
| put the brush | Space, Enter, left button (drag paints tiles) | A |
| erase (tile or enemy) | Delete, Backspace, right button | B |
| brush kind: tile / enemy / item | Tab | Select (Back) |
| previous / next brush | Q / E | L / R |
| list of all tiles / enemies / items | T | X |
| take the brush from the cursor | I, middle button | Y |
| play the level | P, F5 | menu |
| save | Ctrl+S | menu |
| menu (play, save, name, world, length, open, new, back, quit) | Esc | Start, Guide |
| picture of the editor (`editor.png`) | F12 | |

The brushes of a world are the tiles and enemies its stock levels use, most
used first, shown with the world's own graphics. The grey line marks
column 26, the first one enemies can start in; the red box at the left is
Mario's start (columns 4-5). Changes are saved when the level is played, another level is
opened or the editor is left. Opening the editor from a running game ends
that game.
