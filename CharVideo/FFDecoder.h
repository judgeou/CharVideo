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
}

static AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx;

class FFDecoder {
public:
    AVRational timebase;
    int fps;

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
                timebase = pFormatContext->streams[i]->time_base;
                fps = av_q2d(pFormatContext->streams[i]->r_frame_rate);

                for (int i = 0;; i++) {
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

                pPacket = av_packet_alloc();
                pFrame = av_frame_alloc();
                sw_frame = av_frame_alloc();
            }
        }

	}
	~FFDecoder() {
        av_frame_free(&sw_frame);
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        av_buffer_unref(&pCodecContext->hw_device_ctx);
        avcodec_free_context(&pCodecContext);

		avformat_close_input(&pFormatContext);
		avformat_free_context(pFormatContext);
	}

    AVFrame* RequestFrame() {
        while (av_read_frame(pFormatContext, pPacket) >= 0) {
            avcodec_send_packet(pCodecContext, pPacket);
            auto ret = avcodec_receive_frame(pCodecContext, pFrame);
            if (ret != 0) {
                av_packet_unref(pPacket);
                continue;
            }

            if (pFrame->format == hw_pix_fmt) {
                /* retrieve data from GPU to CPU */
                if ((ret = av_hwframe_transfer_data(sw_frame, pFrame, 0)) < 0) {
                    fprintf(stderr, "Error transferring the data to system memory\n");
                    continue;
                }
                sw_frame->best_effort_timestamp = pFrame->best_effort_timestamp;
            }

            return sw_frame;
        }
        return nullptr;
    }

private:
	AVFormatContext* pFormatContext;
    AVCodecParameters* pLocalCodecParameters;
    AVCodec* pLocalCodec;
    AVCodecContext* pCodecContext = nullptr;
    
    AVPacket* pPacket;
    AVFrame* pFrame;
    AVFrame* sw_frame;

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
