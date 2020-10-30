#include <stdio.h>
#include <thread>
#include <string>
#include <memory>
#include <chrono>

#include "FFDecoder.h"

extern "C" {
#include "SDL.h"
#undef main

#pragma comment(lib, "x64/SDL2.lib")
}

using std::shared_ptr;
using std::make_shared;
using namespace std::chrono;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
uint8_t* nv12buf;
int xwidth;
int xheight;

int dealFrame(char* infile);

void PrintSDLErr() {
    auto err = SDL_GetError();
    printf("%s\n", err);
}

uint8_t* newNv12Plane(uint8_t* buf, uint8_t* data[8], int height, int linesize[8]) {
    uint8_t* y = data[0];
    uint8_t* uv = data[1];
    auto y_size = height * linesize[0];
    auto uv_size = height * linesize[1];
    // auto buf = new uint8_t[y_size + uv_size / 2];
    memcpy(buf, y, y_size);
    memcpy(buf + y_size, uv, uv_size / 2);
    return buf;
}

void SDLPlayFrame(AVFrame* pFrame) {
    if (texture == nullptr) {
        texture = SDL_CreateTexture(renderer,
            SDL_PIXELFORMAT_NV12,
            SDL_TEXTUREACCESS_STREAMING,
            pFrame->width,
            pFrame->height);
    }

    newNv12Plane(nv12buf, pFrame->data, pFrame->height, pFrame->linesize);
    SDL_UpdateTexture(texture, NULL, nv12buf, pFrame->linesize[0]);

    // SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

}

int CreateSDLWindow() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("%s", SDL_GetError());
	}

    double ratio = 16.0 / 9.0;
    xwidth = 1000;
    xheight = xwidth / ratio;

    window = SDL_CreateWindow("Player",  // title
        50,     // init window position
        150,     // init window position
        xwidth,           // window width
        xheight,          // window height
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);         // flag

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    auto startPlayTime = system_clock::now();

    shared_ptr<FFDecoder> ffdecoder = nullptr;

    bool isquit = false;
    SDL_Event event;
    while (!isquit) {
        int ret;
        if (ffdecoder) {
            ret = SDL_PollEvent(&event);
            auto frame = ffdecoder->RequestFrame();
            int microsec = frame->pts * av_q2d(ffdecoder->timebase) * 1000000;
            
            auto playTime = startPlayTime + microseconds(microsec);
            std::this_thread::sleep_until(playTime);
            
            SDLPlayFrame(frame);
        }
        else {
            ret = SDL_WaitEvent(&event);
        }
        if (ret) {
            if (event.type == SDL_QUIT) {
                isquit = true;
            }
            else if (event.type == SDL_MOUSEBUTTONUP) {

            }
            else if (event.type == SDL_DROPFILE) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
                ffdecoder = nullptr;
                ffdecoder = make_shared<FFDecoder>(event.drop.file);
                
                if (nv12buf != 0) {
                    delete[] nv12buf;
                }
                nv12buf = new uint8_t[ffdecoder->width * ffdecoder->height * 2];
                
                startPlayTime = system_clock::now();
            }
            else if (event.type == SDL_WINDOWEVENT) {
                switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                {
                    printf("%d %d\n", event.window.data1, event.window.data2);

                    SDL_DestroyTexture(texture);
                    texture = nullptr;
                    break;
                }
                }
            }
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}

int main() {
    av_log_set_level(AV_LOG_QUIET);
    return CreateSDLWindow();
}