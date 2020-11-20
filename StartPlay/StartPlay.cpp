#include <stdio.h>
#include <vector>
#include <string>
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

	RECT rect;
	GetClientRect(hwnd, &rect);

	if (params.BackBufferWidth != rect.right || params.BackBufferHeight != rect.bottom) {
		D3DPRESENT_PARAMETERS presentParams = {};
		presentParams.BackBufferWidth = rect.right;
		presentParams.BackBufferHeight = rect.bottom;
		presentParams.SwapEffect = D3DSWAPEFFECT_FLIP;
		presentParams.Windowed = TRUE;
		d3d9device->Reset(&presentParams);
	}

	IDirect3DSurface9* backSurface = 0;
	d3d9device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backSurface);

	double srcRatio = (double)frame->width / frame->height;
	double destRatio = (double)rect.right / rect.bottom;

	RECT backRect = { 0, 0 };
	if (srcRatio < destRatio) {
		backRect.bottom = rect.bottom;
		auto newWidth = rect.bottom * srcRatio;
		auto offset = rect.right - newWidth;
		backRect.right = newWidth + offset / 2;
		backRect.left = offset / 2;
	}
	else if (srcRatio > destRatio) {
		backRect.right = rect.right;
		auto newHeight = rect.right / srcRatio;
		auto offset = rect.bottom - newHeight;
		backRect.bottom = newHeight + offset / 2;
		backRect.top = offset / 2;
	}
	else {
		backRect.bottom = rect.bottom;
		backRect.right = rect.right;
	}
	
	d3d9device->StretchRect(surface, NULL, backSurface, &backRect, D3DTEXF_LINEAR);
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
	SDL_Init(SDL_INIT_AUDIO);

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
	int audioStreamIndex = 0;
	AVCodecContext* vcodecCtx = nullptr;
	AVCodecContext* acodecCtx = nullptr;
	AVBufferRef* hw_device = nullptr;
	SDL_AudioDeviceID audioDeviceId;
	SwrContext* audioSwrCtx = nullptr;

	for (int i = 0; i < avfCtx->nb_streams; i++) {
		// 获取解码器
		AVCodec* codec = avcodec_find_decoder(avfCtx->streams[i]->codecpar->codec_id);
		if (codec == 0) {
			continue;
		}
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
		// 处理音频
		else if (codec->type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			acodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(acodecCtx, avfCtx->streams[i]->codecpar);
			avcodec_open2(acodecCtx, codec, NULL);

			audioSwrCtx = swr_alloc_set_opts(
				nullptr,
				acodecCtx->channel_layout,
				AV_SAMPLE_FMT_FLT,
				acodecCtx->sample_rate,
				acodecCtx->channel_layout,
				acodecCtx->sample_fmt,
				acodecCtx->sample_rate,
				0,
				0
			);
			swr_init(audioSwrCtx);

			SDL_AudioSpec spec = {};
			spec.freq = acodecCtx->sample_rate;
			spec.format = AUDIO_F32SYS;
			spec.channels = acodecCtx->channels;
			spec.silence = 0;
			spec.samples = 4096;
			SDL_AudioSpec obt = {};
			audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &spec, &obt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

			SDL_PauseAudioDevice(audioDeviceId, 0);
		}
	}

	SDL_DisplayMode displayMode;
	SDL_GetDisplayMode(0, 0, &displayMode);
	double frameRate = displayMode.refresh_rate;
	Uint32 fpsTimer = 0;

	int audioVolume = SDL_MIX_MAXVOLUME / 2;

	SDL_Event event;
	while (1) {
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				break;
			}

			if (event.type == SDL_KEYUP) {
				if (event.key.keysym.sym == 13) {
					static int flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
					SDL_SetWindowFullscreen(window, flag);
					flag = flag == 0 ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
				}
			}

			if (event.type == SDL_MOUSEWHEEL) {
				if (event.wheel.y > 0) {
					audioVolume += 4;
				}
				else if (event.wheel.y < 0) {
					audioVolume -= 4;
				}

				if (audioVolume > SDL_MIX_MAXVOLUME) {
					audioVolume = SDL_MIX_MAXVOLUME;
				}
				else if (audioVolume < 0) {
					audioVolume = 0;
				}
			}

			SDL_SetWindowTitle(window, std::to_string(audioVolume).c_str());
		}

		// 读取数据包
		AVPacket* packet = av_packet_alloc();
		av_read_frame(avfCtx, packet);

		if (packet->stream_index == videoStraemIndex) {
			// 发送给解码器
			int ret = avcodec_send_packet(vcodecCtx, packet);
			frameRate = (double)vcodecCtx->framerate.num / vcodecCtx->framerate.den;
			auto timebase = (double)vcodecCtx->time_base.num / vcodecCtx->time_base.den;

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
		else if (packet->stream_index == audioStreamIndex) {
			int ret = avcodec_send_packet(acodecCtx, packet);
			if (ret != 0) {
				PrintAVErr(ret);
			}

			AVFrame* frame = av_frame_alloc();
			ret = avcodec_receive_frame(acodecCtx, frame);
			
			if (ret == AVERROR(EAGAIN)) { // 数据包不够，再拿
				continue;
			}
			else if (ret == 0) {
				constexpr int SAMPLE_SIZE = 4;
				int SAMPLE_NB = frame->nb_samples * frame->channels;
				int SAMPLE_BYTES = SAMPLE_NB * SAMPLE_SIZE;
				uint8_t* buffer = new uint8_t[SAMPLE_BYTES];
				uint8_t* destBuffer = new uint8_t[SAMPLE_BYTES];
				memset(destBuffer, 0, SAMPLE_BYTES);

				auto sampleNum = swr_convert(audioSwrCtx, &buffer, frame->nb_samples, (const uint8_t**)frame->extended_data, frame->nb_samples);
				
				SDL_MixAudioFormat(destBuffer, buffer, AUDIO_F32SYS, SAMPLE_BYTES, audioVolume);
				SDL_QueueAudio(audioDeviceId, destBuffer, SAMPLE_BYTES);
				delete[] buffer;
				delete[] destBuffer;
			}
			
			av_frame_free(&frame);
		}

		av_packet_unref(packet);
	}

	swr_free(&audioSwrCtx);
	avcodec_free_context(&vcodecCtx);
	avcodec_free_context(&acodecCtx);
	avformat_close_input(&avfCtx);

	SDL_CloseAudioDevice(audioDeviceId);

	// SDL_DestroyTexture(texture);
	// SDL_DestroyRenderer(renderer);
}
