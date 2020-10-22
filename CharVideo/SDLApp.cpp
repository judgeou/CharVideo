#include <stdio.h>
#include <thread>
#include <string>
#include <memory>

#include "FFDecoder.h"

extern "C" {
#include "SDL.h"
#undef main

#pragma comment(lib, "x64/SDL2.lib")
}

using std::shared_ptr;
using std::make_shared;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
int xwidth;
int xheight;

int dealFrame(char* infile);

void PrintSDLErr() {
    auto err = SDL_GetError();
    printf("%s\n", err);
}

void SDLPlayFrame(AVFrame* pFrame) {
    if (texture == nullptr) {
        texture = SDL_CreateTexture(renderer,
            SDL_PIXELFORMAT_YV12,
            SDL_TEXTUREACCESS_STREAMING,
            pFrame->width,
            pFrame->height);
    }

    auto sws_ctx = sws_getContext(pFrame->width,
        pFrame->height,
        (AVPixelFormat)pFrame->format,
        pFrame->width,
        pFrame->height,
        AV_PIX_FMT_YUV420P,
        NULL,
        NULL,
        NULL,
        NULL);

    AVFrame* pFrameYUV = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pFrame->width,
        pFrame->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
        pFrame->width, pFrame->height, 1);
    sws_scale(sws_ctx, pFrame->data,
        pFrame->linesize, 0, pFrame->height,
        pFrameYUV->data, pFrameYUV->linesize);

    SDL_UpdateYUVTexture(texture,
        nullptr,
        pFrameYUV->data[0],
        pFrameYUV->linesize[0],
        pFrameYUV->data[1],
        pFrameYUV->linesize[1],
        pFrameYUV->data[2],
        pFrameYUV->linesize[2]);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    av_free(buffer);
    av_frame_free(&pFrameYUV);
    sws_freeContext(sws_ctx);

}

int CreateSDLWindow() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("%s", SDL_GetError());
	}

    double ratio = 16.0 / 9.0;
    xwidth = 1000;
    xheight = xwidth / ratio;

    window = SDL_CreateWindow("Player",  // title
        11,     // init window position
        11,     // init window position
        xwidth,           // window width
        xheight,          // window height
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);         // flag

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    shared_ptr<FFDecoder> ffdecoder;

    bool isquit = false;
    SDL_Event event;
    while (!isquit) {
        int ret;
        if (ffdecoder) {
            ret = SDL_PollEvent(&event);
            auto frame = ffdecoder->RequestFrame();
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
                ffdecoder = make_shared<FFDecoder>(event.drop.file);
            }
            else if (event.type == SDL_WINDOWEVENT) {
                switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                {
                    printf("%d %d\n", event.window.data1, event.window.data2);
                    xwidth = event.window.data1;
                    xheight = event.window.data2;
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
    return CreateSDLWindow();
}