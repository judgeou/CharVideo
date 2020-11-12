#include <stdio.h>
#include <vector>
#include <thread>
#include <chrono>
#include <Windows.h>

extern "C" {
#include "SDL.h"
#undef main

#pragma comment(lib, "SDL2.lib")
}

extern "C" {
#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/imgutils.h>
#pragma comment(lib, "avutil.lib")

#include <libswscale/swscale.h>
#pragma comment(lib, "swscale.lib")

#include <libswresample/swresample.h>
#pragma comment(lib, "swresample.lib")
}

using std::vector;
using namespace std::chrono;

struct RGBPixel {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

void PrintAVErr(int ret) {
	static char errstr[256];
	printf("%s\n", av_make_error_string(errstr, sizeof(errstr), ret));
}

int main()
{
	SetProcessDPIAware();

	int width = 1280;
	int height = width / (16.0 / 9);
	auto window = SDL_CreateWindow(
		"SSS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_RESIZABLE
	);

	auto renderer = SDL_CreateRenderer(window, -1, 0); // alloc

	// auto bmp = SDL_LoadBMP("C:\\Users\\ouzian\\Pictures\\Anime\\1.bmp"); // alloc
	SDL_Texture* texture = 0;

	// memcpy(pixels, bmp->pixels, bmp->pitch * bmp->h);

	// 打开视频容器
	AVFormatContext* avfCtx = 0;
	avformat_open_input(&avfCtx, "D:\\Video\\236565548-1-116.flv", NULL, NULL);
	avformat_find_stream_info(avfCtx, NULL);

	AVRational time_base;
	int videoStraemIndex = 0;
	AVCodecContext* vcodecCtx = nullptr;

	for (int i = 0; i < avfCtx->nb_streams; i++) {
		// 获取解码器
		AVCodec* codec = avcodec_find_decoder(avfCtx->streams[i]->codecpar->codec_id);

		// 处理视频
		if (codec->type == AVMEDIA_TYPE_VIDEO) {
			// 获取解码环境
			videoStraemIndex = i;
			time_base = avfCtx->streams[i]->time_base;
			vcodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(vcodecCtx, avfCtx->streams[i]->codecpar);
			avcodec_open2(vcodecCtx, codec, NULL);

			texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, vcodecCtx->width, vcodecCtx->height);
			SDL_SetWindowSize(window, 1280, 720);
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		}
	}

	auto startTime = system_clock::now();
	SDL_Event event;
	while (1) {
		SDL_PollEvent(&event);
		if (event.type == SDL_QUIT) {
			break;
		}

		// 读取数据包
		AVPacket* packet = av_packet_alloc();
		av_read_frame(avfCtx, packet);

		if (packet->stream_index == videoStraemIndex) {
			// 发送给解码器
			int ret = avcodec_send_packet(vcodecCtx, packet);

			if (ret != 0) {
				PrintAVErr(ret);
			}

			// 从解码器获取解码后的帧
			AVFrame* frame = av_frame_alloc();
			ret = avcodec_receive_frame(vcodecCtx, frame);

			if (ret == AVERROR(EAGAIN)) { // 数据包不够，再拿
				continue;
			}
			else if (ret == 0) {
				auto format = (AVPixelFormat)frame->format;

				SDL_UpdateYUVTexture(texture, NULL, 
					frame->data[0], frame->linesize[0],
					frame->data[1], frame->linesize[1],
					frame->data[2], frame->linesize[2]
				);
				
				double ms = frame->pts * ( time_base.num / (double)time_base.den ) * 1000000;
				auto playTime = startTime + microseconds((int)ms);
				std::this_thread::sleep_until(playTime);

				SDL_RenderCopy(renderer, texture, NULL, NULL);
				SDL_RenderPresent(renderer);
			}
			else {
				PrintAVErr(ret);
			}
			av_frame_free(&frame);
		}

		av_packet_unref(packet);
	}

	avcodec_free_context(&vcodecCtx);
	avformat_close_input(&avfCtx);

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
}
