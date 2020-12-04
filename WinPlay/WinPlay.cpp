﻿#include <stdio.h>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <Windows.h>
#include <d3d9.h>

using std::map;
using std::vector;
using std::mutex;
using std::condition_variable;
using std::unique_lock;

extern "C" {
#include <libavcodec/avcodec.h>
#pragma comment(lib, "libavcodec.a")

#include <libavformat/avformat.h>
#pragma comment(lib, "libavformat.a")

#include <libavutil/imgutils.h>
#pragma comment(lib, "libavutil.a")

#pragma comment(lib, "Bcrypt.lib")
}

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

void PrintAVErr(int ret) {
	static char errstr[256];
	printf("%s\n", av_make_error_string(errstr, sizeof(errstr), ret));
}

struct AudioDataBuffer {
	vector<uint8_t> data;
	int read;
};

void UpdateScreen(HWND hwnd, AVFrame* frame) {
	IDirect3DSurface9* surface = (IDirect3DSurface9*)frame->data[3];
	if (surface == 0) {
		return;
	}
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
	if (backSurface) {
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

		auto ret = d3d9device->StretchRect(surface, NULL, backSurface, &backRect, D3DTEXF_LINEAR);
		backSurface->Release();

		ret = d3d9device->Present(NULL, NULL, hwnd, NULL);
	}
}

int main(int argc, char** argv)
{
	static bool isPlay = true;

	static mutex mutex_update;
	static condition_variable cond_update;
	// static unique_lock<mutex> mlock_update(mutex_update);

	static mutex mutex_decode;
	static condition_variable cond_decode;
	// static unique_lock<mutex> mlock_decode(mutex_decode);

	static int audioStreamIndex = -1;
	static int videoStraemIndex = -1;
	static AVCodecContext* acodecCtx = nullptr;
	static AVCodecContext* vcodecCtx = nullptr;
	AVBufferRef* hw_device = nullptr;
	static AVFrame* videoFrame = 0;

	// 打开视频容器
	static AVFormatContext* avfCtx = 0;
	avformat_open_input(&avfCtx, argv[1], NULL, NULL);
	avformat_find_stream_info(avfCtx, NULL);

	for (int i = 0; i < avfCtx->nb_streams; i++) {
		AVCodec* codec = avcodec_find_decoder(avfCtx->streams[i]->codecpar->codec_id);
		if (codec == 0) {
			continue;
		}
		if (codec->type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			acodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(acodecCtx, avfCtx->streams[i]->codecpar);
			avcodec_open2(acodecCtx, codec, NULL);
		}
		else if (codec->type == AVMEDIA_TYPE_VIDEO) {
			videoStraemIndex = i;
			vcodecCtx = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(vcodecCtx, avfCtx->streams[i]->codecpar);
			avcodec_open2(vcodecCtx, codec, NULL);

			// hw
			hw_device = 0;
			av_hwdevice_ctx_create(&hw_device, AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2, NULL, NULL, NULL);
			vcodecCtx->hw_device_ctx = hw_device;
		}
	}

	AudioDataBuffer audioDataBuffer;

	static map<AVSampleFormat, ma_format> ma_format_map = {
		{ AV_SAMPLE_FMT_S16, ma_format_s16 },
		{ AV_SAMPLE_FMT_S32, ma_format_s32 },
		{ AV_SAMPLE_FMT_FLT, ma_format_f32 },
		{ AV_SAMPLE_FMT_FLTP, ma_format_f32 }
	};
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_map[acodecCtx->sample_fmt];   // Set to ma_format_unknown to use the device's native format.
	config.playback.channels = acodecCtx->channels;               // Set to 0 to use the device's native channel count.
	config.sampleRate = acodecCtx->sample_rate;           // Set to 0 to use the device's native sample rate.
	config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
		if (isPlay == false) {
			return;
		}

		auto size = frameCount * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
		auto audioDataBuffer = (AudioDataBuffer*)pDevice->pUserData;
		auto& data = audioDataBuffer->data;

		while (size > data.size()) {
			while (1) {
				AVPacket* packet = av_packet_alloc();
				int ret = av_read_frame(avfCtx, packet);

				if (ret != 0) {
					// 结束播放
					isPlay = false;
					return;
				}

				if (packet->stream_index == audioStreamIndex) {
					int ret = avcodec_send_packet(acodecCtx, packet);
					if (ret != 0) {
						PrintAVErr(ret);
					}

					AVFrame* frame = av_frame_alloc();
					ret = avcodec_receive_frame(acodecCtx, frame);

					if (ret == AVERROR(EAGAIN)) {
						// 数据包不够，再拿
						av_frame_free(&frame);
					}
					else if (ret == 0) {
						// 解码成功
						if (frame->data[1] && pDevice->playback.format == ma_format_f32) {
							// Planer
							auto nb = frame->nb_samples * frame->channels;
							auto newData = new float[nb];
							auto left = (float*)frame->data[0];
							auto right = (float*)frame->data[1];
							for (int i = 0; i < frame->nb_samples; i++) {
								int p = i * 2;
								newData[p] = left[i];
								newData[p + 1] = right[i];
							}
							data.insert(data.end(), (uint8_t*)newData, (uint8_t*)newData + nb * 4);
							delete[] newData;
						}
						else {
							data.insert(data.end(), frame->data[0], frame->data[0] + frame->linesize[0]);
						}

						av_frame_free(&frame);
						av_packet_free(&packet);
						break;
					}
				}
				else if (packet->stream_index == videoStraemIndex) {
					int ret = avcodec_send_packet(vcodecCtx, packet);
					if (ret != 0) {
						PrintAVErr(ret);
					}

					AVFrame* frame = av_frame_alloc();
					ret = avcodec_receive_frame(vcodecCtx, frame);

					if (ret == AVERROR(EAGAIN)) {
						// 数据包不够，再拿
						av_frame_free(&frame);
					}
					else if (ret == 0) {
						videoFrame = frame;
						cond_update.notify_all();
						unique_lock<mutex> mlock_decode(mutex_decode);
						cond_decode.wait(mlock_decode);

						av_frame_free(&frame);
						av_packet_free(&packet);
						break;
					}
				}

				av_packet_free(&packet);
			}
		}

		memcpy(pOutput, &data[0], size);
		
		vector<uint8_t> newData(data.cbegin() + size, data.cend());
		data = newData;
	};   // This function will be called when miniaudio needs more data.
	config.pUserData = &audioDataBuffer;   // Can be accessed from the device object (device.pUserData).

	static ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		return -1;  // Failed to initialize the device.
	}

	// 创建窗口
	SetProcessDPIAware();
	auto hInstance = GetModuleHandle(NULL);
	auto className = L"winplay";

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(wc);
	wc.hInstance = hInstance;
	wc.lpszClassName = className;
	wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch (msg) {
		case WM_DESTROY:
			isPlay = false;
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
		return 0;
	};
	RegisterClassEx(&wc);

	static auto hwnd = CreateWindowEx(
		NULL,
		className,
		L"title",
		WS_OVERLAPPEDWINDOW,
		1, 1, 1920, 1080,
		NULL, NULL, hInstance, NULL
	);
	ShowWindow(hwnd, SW_SHOW);

	ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.

	MSG msg;
	while (isPlay) {
		unique_lock<mutex> mlock_update(mutex_update);
		cond_update.wait(mlock_update);

		while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		UpdateScreen(hwnd, videoFrame);

		cond_decode.notify_all();
	}

	ma_device_uninit(&device);

	avcodec_free_context(&vcodecCtx);
	avcodec_free_context(&acodecCtx);
	avformat_close_input(&avfCtx);

    return 0;
}