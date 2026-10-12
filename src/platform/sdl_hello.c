/* Toolchain proof: opens a 640x576 window (4x the Game Boy screen), fills it
 * with a solid colour and exits after one second. No game logic.
 *   sdl_hello                 window for 1 s
 *   sdl_hello --headless-test init SDL video with the dummy driver and quit */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int headless = argc > 1 && strcmp(argv[1], "--headless-test") == 0;

    SDL_SetMainReady();
    if (headless) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_version v;
    SDL_GetVersion(&v);
    printf("SDL %d.%d.%d, video driver: %s\n", v.major, v.minor, v.patch,
           SDL_GetCurrentVideoDriver());
    if (headless) {
        SDL_Quit();
        puts("headless test ok");
        return 0;
    }

    SDL_Window *win = SDL_CreateWindow("SML port toolchain test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 576, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Surface *surf = SDL_GetWindowSurface(win);
    if (!surf) {
        fprintf(stderr, "SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    /* Game Boy "DMG green" */
    SDL_FillRect(surf, NULL, SDL_MapRGB(surf->format, 0x9b, 0xbc, 0x0f));
    SDL_UpdateWindowSurface(win);

    Uint32 end = SDL_GetTicks() + 1000;
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT) end = 0;
        SDL_Delay(10);
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    puts("window test ok");
    return 0;
}
