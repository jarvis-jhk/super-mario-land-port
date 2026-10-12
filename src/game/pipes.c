/* Scripted pipe travel (00:161B-16F4). */
#include "game.h"
#include "ram.h"

/* Sink Mario into the pipe, one pixel per frame (00:161B). */
void enter_pipe_down(void)
{
    if (taken(pipe_goal == mario_y)) {
        shared_write(0xFFB3, STATE_PIPE_UNDERGROUND); underground = STATE_PIPE_UNDERGROUND;
    } else { mario_y++; mario_animate_and_walk(); }
}

/* Rebuild the underground room and place Mario at its entrance (00:162F). */
void enter_underground(void)
{
    io_write(rLCDC, 0); column_index = 0;
    clear_object_sprites(); clear_block_overlay();
    screen_index = pipe_data;
    draw_first_level_screen(); init_enemy_slots();
    mario_y = 0x20; mario_x = 0x1D; mario_scripted = 0;
    io_write(rIF, 0); shared_write(0xFFB3, STATE_PLAYING); shared_write(0xFFA4, 0);
    io_write(rLCDC, 0xC3);
}

/* Walk through the underground exit and rebuild the overworld (00:166C).
   The original exchanges the saved X/Y pipe coordinates on return. */
void exit_pipe_sideways(void)
{
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 1) == 0)) return;
    if (!taken(pipe_goal < mario_x)) {
        mario_x++; mario_walk_counter++; mario_animate_and_walk(); return;
    }
    screen_index = ram_read(0xFFF5);
    io_write(rLCDC, 0); column_index = 0; clear_block_overlay();
    pipe_data = 0; ram_write(0xFFF5, 0);
    uint8_t y = ram_read(0xFFF7), x = ram_read(0xFFF6);
    draw_first_level_screen();
    mario_scripted = 0x80; mario_y = y; pipe_goal = y - 0x12; mario_x = x;
    uint8_t screen = reg_b(screen_index - 4);
    uint8_t rotated = (uint8_t)((screen << 3) | (screen >> 5));
    level_progress = rotated + screen + screen + 0x0C;
    io_write(rIF, 0); shared_write(0xFFA4, 0); shared_write(ADDR_DRAW_COLUMN_LO, 0x5B);
    init_enemy_slots(); clear_object_sprites();
    io_write(rLCDC, 0xC3); shared_write(0xFFB3, 0x0C); start_level_music();
}

/* Rise out of the pipe on every other frame (00:16DA). */
void exit_pipe_up(void)
{
    if (taken((shared_read(ADDR_FRAME_COUNTER) & 1) == 0)) return;
    if (taken(pipe_goal == mario_y)) {
        shared_write(0xFFB3, STATE_PLAYING); mario_scripted = underground = 0;
    } else { mario_y--; mario_animate_and_walk(); }
}
