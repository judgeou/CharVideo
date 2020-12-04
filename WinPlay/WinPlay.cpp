﻿#include <stdio.h>
#include <vector>
#include <map>
#include "SharedQueue.h"

using std::map;
using std::vector;

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
#include "miniaudio.h"

void PrintAVErr(int ret) {
	static char errstr[256];
	printf("%s\n", av_make_error_string(errstr, sizeof(errstr), ret));
}

struct AudioDataBuffer {
	vector<uint8_t> data;
	int read;
};

int main(int argc, char** argv)
{
	static int audioStreamIndex = -1;
	static AVCodecContext* acodecCtx = nullptr;

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
	}

	AudioDataBuffer audioDataBuffer;
	audioDataBuffer.read = 0;

	static map<AVSampleFormat, ma_format> ma_format_map = {
	{ AV_SAMPLE_FMT_S16, ma_format_s16 },
	{ AV_SAMPLE_FMT_S32, ma_format_s32 }
	};
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_map[acodecCtx->sample_fmt];   // Set to ma_format_unknown to use the device's native format.
	config.playback.channels = acodecCtx->channels;               // Set to 0 to use the device's native channel count.
	config.sampleRate = acodecCtx->sample_rate;           // Set to 0 to use the device's native sample rate.
	config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
		auto size = frameCount * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
		auto audioDataBuffer = (AudioDataBuffer*)pDevice->pUserData;
		auto& data = audioDataBuffer->data;

		if (size > data.size()) {
			while (1) {
				AVPacket* packet = av_packet_alloc();
				int ret = av_read_frame(avfCtx, packet);

				if (ret != 0) {
					// 结束播放
					ma_device_uninit(pDevice);    // This will stop the device so no need to do that manually.
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
						data.insert(data.end(), frame->data[0], frame->data[0] + frame->linesize[0]);

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

	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		return -1;  // Failed to initialize the device.
	}

	ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.

	getchar();



	avcodec_free_context(&acodecCtx);
	avformat_close_input(&avfCtx);

    return 0;
}