﻿#include <stdio.h>
#include <vector>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <d3d9.h>

extern "C" {
#include <SDL.h>
#include <SDL_syswm.h>
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

HWND GetWindowHwnd(SDL_Window* window) {
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);
	HWND hwnd = wmInfo.info.win.window;
	return hwnd;
}

void UpdateScreen(SDL_Renderer * renderer, SDL_Texture* texture) {
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void UpdateScreen(HWND hwnd, AVFrame* frame) {
	IDirect3DSurface9* surface = (IDirect3DSurface9*)frame->data[3];
	IDirect3DDevice9* d3d9device = 0;
	surface->GetDevice(&d3d9device);

	IDirect3DSwapChain9* swap;
	d3d9device->GetSwapChain(0, &swap);
	D3DPRESENT_PARAMETERS params;
	swap->GetPresentParameters(&params);

	if (params.hDeviceWindow != hwnd) {
		D3DPRESENT_PARAMETERS presentParams = {};
		presentParams.hDeviceWindow = hwnd;
		presentParams.SwapEffect = D3DSWAPEFFECT_FLIP;
		presentParams.Windowed = TRUE;
		d3d9device->Reset(&presentParams);
	}

	IDirect3DSurface9* backSurface = 0;
	d3d9device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backSurface);
	
	d3d9device->StretchRect(surface, NULL, backSurface, NULL, D3DTEXF_LINEAR);
	backSurface->Release();

	d3d9device->Present(NULL, NULL, hwnd, NULL);
}

void UpdateTexture(AVFrame* frame, SDL_Texture* texture) {
	// 把解码后的数据从 GPU 复制到 CPU
	AVFrame* cpuFrame = av_frame_alloc();
	av_hwframe_transfer_data(cpuFrame, frame, 0);
	auto cpuFormat = (AVPixelFormat)cpuFrame->format;

	uint8_t* pixels;
	int pitch;
	SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch);

	size_t ysize = pitch * cpuFrame->height;
	size_t uvsize = pitch * cpuFrame->height / 2;

	for (int i = 0; i < cpuFrame->height; i++) {
		void* srcPtr = cpuFrame->data[0] + cpuFrame->linesize[0] * i;
		void* destPtr = pixels + pitch * i;
		memcpy(destPtr, srcPtr, pitch);
	}
	pixels += pitch * cpuFrame->height;
	for (int i = 0; i < cpuFrame->height; i++) {
		void* srcPtr = cpuFrame->data[1] + cpuFrame->linesize[1] / 2 * i;
		void* destPtr = pixels + pitch / 2 * i;
		memcpy(destPtr, srcPtr, pitch / 2);
	}

	SDL_UnlockTexture(texture);
	av_frame_free(&cpuFrame);
}

int main(int argc, char** argv)
{
	SetProcessDPIAware();

	int width = 1280;
	int height = width / (16.0 / 9);
	auto window = SDL_CreateWindow(
		"SSS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_RESIZABLE
	);

	HWND hwnd = GetWindowHwnd(window);

	// auto renderer = SDL_CreateRenderer(window, -1, SDL_RendererFlags::SDL_RENDERER_PRESENTVSYNC); // alloc

	// SDL_Texture* texture = 0;

	// 打开视频容器
	AVFormatContext* avfCtx = 0;
	avformat_open_input(&avfCtx, argv[1], NULL, NULL);
	avformat_find_stream_info(avfCtx, NULL);

	AVRational time_base;
	int videoStraemIndex = 0;
	AVCodecContext* vcodecCtx = nullptr;
	AVBufferRef* hw_device = nullptr;

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

			// 设置硬件解码设备
			hw_device = 0;
			av_hwdevice_ctx_create(&hw_device, AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2, NULL, NULL, NULL);
			vcodecCtx->hw_device_ctx = hw_device;

			// 创建对应的纹理
			// texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING, vcodecCtx->width, vcodecCtx->height);
			SDL_SetWindowSize(window, 1280, 720);
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		}
	}

	SDL_DisplayMode displayMode;
	SDL_GetDisplayMode(0, 0, &displayMode);
	double refreshRate = displayMode.refresh_rate;
	double frameRate = refreshRate;
	uint64_t presentCount = 1;
	uint64_t decodeCount = 0;

	Uint32 fpsTimer = 0;
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
			frameRate = (double)vcodecCtx->framerate.num / vcodecCtx->framerate.den;

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

				auto currentTicks = SDL_GetTicks();
				int fps = 1000 / ((double)vcodecCtx->framerate.num / vcodecCtx->framerate.den);
				if (currentTicks - fpsTimer < fps) {
					SDL_Delay(fps - currentTicks + fpsTimer);
				}
				fpsTimer = SDL_GetTicks();

				// UpdateTexture(frame, texture);
				// UpdateScreen(renderer, texture);
				UpdateScreen(hwnd, frame);
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

	// SDL_DestroyTexture(texture);
	// SDL_DestroyRenderer(renderer);
}
