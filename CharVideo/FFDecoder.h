#ifndef FFDECODER_20201022
#define FFDECODER_20201022

extern "C" {
#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/imgutils.h>
#pragma comment(lib, "libavutil.dll.a")

#include <libswscale/swscale.h>
#pragma comment(lib, "swscale.lib")

#include <libswresample/swresample.h>
#pragma comment(lib, "swresample.lib")
}

static AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx;

class FFDecoder {
public:
    AVRational timebase;
    double fps;
    int height;
    int width;
    int channels;
    int sample_rate;
    uint8_t* audioBuffer;
    int audioBufferSize;

	FFDecoder(char * infile) {
		auto hw_type = AV_HWDEVICE_TYPE_DXVA2;

		pFormatContext = avformat_alloc_context();
		avformat_open_input(&pFormatContext, infile, NULL, NULL);
		avformat_find_stream_info(pFormatContext, NULL);

        for (unsigned int i = 0; i < pFormatContext->nb_streams; i++)
        {
            pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
            pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

            if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex = i;
                timebase = pFormatContext->streams[i]->time_base;

                for (int i = 0; i < 100; i++) {
                    const AVCodecHWConfig* config = avcodec_get_hw_config(pLocalCodec, i);
                    if (!config) {
                        fprintf(stderr, "Decoder %s does not support device type %s.\n",
                            pLocalCodec->name, av_hwdevice_get_type_name(hw_type));

                    }
                    else {
                        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                            config->device_type == hw_type) {
                            hw_pix_fmt = config->pix_fmt;
                            break;
                        }
                    }
                }

                pCodecContext = avcodec_alloc_context3(pLocalCodec);
                avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);

                pCodecContext->get_format = get_hw_format;
                if (hw_decoder_init(pCodecContext, hw_type) < 0) {
                    // error
                }

                avcodec_open2(pCodecContext, pLocalCodec, NULL);

                fps = av_q2d(pCodecContext->framerate);
                width = pCodecContext->width;
                height = pCodecContext->height;
            }
            if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStreamIndex = i;
                pACodecContext = avcodec_alloc_context3(pLocalCodec);
                avcodec_parameters_to_context(pACodecContext, pLocalCodecParameters);
                avcodec_open2(pACodecContext, pLocalCodec, NULL);
                channels = pACodecContext->channels;
                sample_rate = pACodecContext->sample_rate;

                audioSwrCtx = swr_alloc_set_opts(
                    nullptr,
                    pACodecContext->channel_layout,
                    AV_SAMPLE_FMT_FLT,
                    pACodecContext->sample_rate,
                    pACodecContext->channel_layout,
                    pACodecContext->sample_fmt,
                    pACodecContext->sample_rate,
                    0,
                    0
                );
                swr_init(audioSwrCtx);
                audioBuffer = new uint8_t[0x1000 * pACodecContext->channels * 4];
            }
        }

        pPacket = av_packet_alloc();
        pFrame = av_frame_alloc();
        sw_frame = av_frame_alloc();
	}
	~FFDecoder() {
        delete[] audioBuffer;
        av_frame_free(&sw_frame);
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        swr_free(&audioSwrCtx);

        av_buffer_unref(&pCodecContext->hw_device_ctx);
        avcodec_free_context(&pCodecContext);
        avcodec_free_context(&pACodecContext);
		avformat_close_input(&pFormatContext);
		avformat_free_context(pFormatContext);
	}

    AVFrame* RequestFrame() {
        while (av_read_frame(pFormatContext, pPacket) >= 0) {
            AVFrame* ret = 0;
            if (pPacket->stream_index == videoStreamIndex) {
                ret = DecodePacket(pCodecContext, pPacket);
                if (ret) {
                    if (ret->format == hw_pix_fmt) {
                        av_hwframe_transfer_data(sw_frame, pFrame, 0);

                        sw_frame->best_effort_timestamp = pFrame->best_effort_timestamp;
                        sw_frame->pts = pFrame->pts;
                        return sw_frame;
                    }
                    else {
                        return pFrame;
                    }
                }
            }
            else if (pPacket->stream_index == audioStreamIndex) {
                ret = DecodePacket(pACodecContext, pPacket);
                if (ret) {
                    auto sampleNum = swr_convert(audioSwrCtx, &audioBuffer, pFrame->nb_samples, (const uint8_t**)pFrame->extended_data, pFrame->nb_samples);
                    audioBufferSize = sampleNum * pFrame->channels * (32 / 8);
                    return ret;
                }
            }
        }
        return nullptr;
    }

private:
	AVFormatContext* pFormatContext;
    AVCodecParameters* pLocalCodecParameters;
    AVCodec* pLocalCodec;
    AVCodecContext* pCodecContext = nullptr;
    AVCodecContext* pACodecContext = nullptr;
    SwrContext* audioSwrCtx;
    
    AVPacket* pPacket;
    AVFrame* pFrame;
    AVFrame* sw_frame;
    int videoStreamIndex;
    int audioStreamIndex;

    AVFrame* DecodePacket(AVCodecContext* dec, const AVPacket* pkt) {
        int ret = 0;

        // submit the packet to the decoder
        ret = avcodec_send_packet(dec, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
            return nullptr;
        }

        // get all the available frames from the decoder
        while (ret >= 0) {
            ret = avcodec_receive_frame(dec, pFrame);
            if (ret < 0) {
                // those two return values are special and mean there is no output
                // frame available, but there were no errors during decoding
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                    return 0;

                fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
                return nullptr;
            }

            // write the frame data to output file
            if (dec->codec->type == AVMEDIA_TYPE_VIDEO) {
                return pFrame;
            }
            else if (dec->codec->type == AVMEDIA_TYPE_AUDIO) {
                return pFrame;
            }

            if (ret < 0)
                return nullptr;
        }

        return 0;
    }

    static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
        const enum AVPixelFormat* p;

        for (p = pix_fmts; *p != -1; p++) {
            if (*p == hw_pix_fmt)
                return *p;
        }

        fprintf(stderr, "Failed to get HW surface format.\n");
        return AV_PIX_FMT_NONE;
    }

    static int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type)
    {
        int err = 0;

        if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
            NULL, NULL, 0)) < 0) {
            fprintf(stderr, "Failed to create specified HW device.\n");
            return err;
        }
        ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

        return err;
    }
};

#endif // !FFDECODER_20201022
