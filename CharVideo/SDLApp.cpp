#include <stdio.h>

extern "C" {
#include "SDL.h"
#undef main
#include "SDL_thread.h"
#pragma comment(lib, "x64/SDL2.lib")
}

SDL_Window* window;

int main() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("%s", SDL_GetError());
	}

    window = SDL_CreateWindow("My Video Window",  // title
        SDL_WINDOWPOS_CENTERED,     // init window position
        SDL_WINDOWPOS_CENTERED,     // init window position
        800,           // window width
        600,          // window height
        SDL_WINDOW_OPENGL);         // flag

    bool isquit = false;
    SDL_Event event;
    while (!isquit) {
        if (SDL_WaitEvent(&event)) {
            if (event.type == SDL_QUIT) {
                isquit = true;
            }
        }
    }
}