#include <stdio.h>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>

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

int main(int argc, char** argv)
{
	static bool isPlay = true;
	static mutex mutex_;
	static condition_variable cond_;
	static unique_lock<mutex> mlock(mutex_);

	static int audioStreamIndex = -1;
	static int videoStraemIndex = -1;
	static AVCodecContext* acodecCtx = nullptr;
	static AVCodecContext* vcodecCtx = nullptr;
	AVBufferRef* hw_device = nullptr;

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
	audioDataBuffer.read = 0;

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
					cond_.notify_all();
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

	while (isPlay) {
		cond_.wait(mlock);
	}

	avcodec_free_context(&vcodecCtx);
	avcodec_free_context(&acodecCtx);
	avformat_close_input(&avfCtx);

    return 0;
}