#include <stdio.h>
#include <string>
#include <thread>
#include <vector>
#include <list>
#include <chrono>
#include <future>
#include <condition_variable>

#include <Windows.h>


extern "C" {
#include <libavcodec/avcodec.h>
#pragma comment(lib, "avcodec.lib")

#include <libavformat/avformat.h>
#pragma comment(lib, "avformat.lib")

#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#pragma comment(lib, "libavutil.dll.a")

#include <libswscale/swscale.h>
#pragma comment(lib, "swscale.lib")
}

#include "resource.h"
#include "..\Dwnd\DWnd.h"
#include "..\Dwnd\ControlModel.h"
#include "NotepadLogger.h"

using std::string;
using std::wstring;
using std::vector;
using std::list;

using namespace std::chrono;

int width;
int height;
std::mutex mut01;
HDC hdc;
EditModel* pEdit01;
DWnd* pMainWind;
HWND hEdit01;
bool stop = false;
NotepadLogger* nlogger;
static enum AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx = NULL;

struct FrameData
{
    vector<uint8_t>& bit;
    system_clock::time_point playTime;
    int width;
    int height;
};

string w2s(const wstring& wstr) {
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
    string str(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), &str[0], str.size(), NULL, NULL);
    return str;
}

void DrawFrame(const FrameData& bitData) {
    BITMAPINFO info = { 0 };
    auto& bgr_buffer = bitData.bit;
    int width = bitData.width;
    int height = bitData.height;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biSizeImage = bgr_buffer.size();
    info.bmiHeader.biCompression = BI_RGB;

    std::this_thread::sleep_until(bitData.playTime);
    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, &bgr_buffer[0], &info, DIB_RGB_COLORS, SRCCOPY);

}

void DrawFrameText(const FrameData& bitData) {
    int width = bitData.width;
    int height = bitData.height;
    auto& bitArray = bitData.bit;
    const wchar_t charmap[] = L" .:;ox%#@";
    const auto charmapNum = sizeof(charmap) / sizeof(charmap[0]) - 1;

    vector<WCHAR> screenChars;
    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            auto offset = h * (width) + w;
            auto gray = bitArray[offset];
            auto c = charmap[(255 - gray) * charmapNum / 256];

            screenChars.push_back(c);
        }
        screenChars.push_back(L'\r');
        screenChars.push_back(L'\n');
    }
    screenChars.push_back(L'\0');
    
    WCHAR* text = &screenChars[0];
    std::this_thread::sleep_until(bitData.playTime);
    
    nlogger->Clear();
    nlogger->Output(text, false);
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
    const enum AVPixelFormat* pix_fmts)
{
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

int dealFrame(char * infile, HWND hwnd) {
    stop = false;
    
    auto hw_type = AV_HWDEVICE_TYPE_D3D11VA;

    AVFormatContext* pFormatContext = avformat_alloc_context();
    avformat_open_input(&pFormatContext, infile, NULL, NULL);
    avformat_find_stream_info(pFormatContext, NULL);

    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters* pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        AVCodec* pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            for (int i = 0;; i++) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(pLocalCodec, i);
                if (!config) {
                    fprintf(stderr, "Decoder %s does not support device type %s.\n",
                        pLocalCodec->name, av_hwdevice_get_type_name(hw_type));
                    return -1;
                }
                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == hw_type) {
                    hw_pix_fmt = config->pix_fmt;
                    break;
                }
            }

            width = pLocalCodecParameters->width;
            height = pLocalCodecParameters->height;
            double ratio = (double)width / (double)height;
            int widthPlay = width;
            int heightPlay = widthPlay / ratio;
            int widthCon = 200;
            int heightCon = widthCon / ratio / 2;

            RECT clientSize = { 0, 0, width, height };
            AdjustWindowRect(&clientSize, WS_CAPTION, FALSE);
            MoveWindow(pMainWind->mainHWnd, 0, 0, clientSize.right - clientSize.left, clientSize.bottom - clientSize.top, true);

            AVCodecContext* pCodecContext = avcodec_alloc_context3(pLocalCodec);
            avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);

            pCodecContext->get_format = get_hw_format;
            if (hw_decoder_init(pCodecContext, hw_type) < 0) {
                return -1;
            }

            avcodec_open2(pCodecContext, pLocalCodec, NULL);

            SwsContext* swsContextCon = sws_getContext(width, height, pCodecContext->pix_fmt, widthCon, heightCon, AV_PIX_FMT_GRAY8,
                NULL, NULL, NULL, NULL);

            AVPacket* pPacket = av_packet_alloc();
            AVFrame* pFrame = av_frame_alloc();
            AVFrame* pRgbFrame = av_frame_alloc();
            AVFrame* pRgbFrameCon = av_frame_alloc();
            AVFrame* sw_frame = av_frame_alloc();
            
            int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, widthPlay, heightPlay, 1);
            int num_bytes_con = av_image_get_buffer_size(AV_PIX_FMT_GRAY8, widthCon, heightCon, 1);

            vector<uint8_t> bgr_buffer(num_bytes);
            vector<uint8_t> bgr_buffer_con(num_bytes_con);

            auto p_global_bgr_buffer = &bgr_buffer[0];
            av_image_fill_arrays(pRgbFrame->data, pRgbFrame->linesize, p_global_bgr_buffer,
                AV_PIX_FMT_BGR24, widthPlay, heightPlay, 1);

            auto p_global_bgr_buffer_con = &bgr_buffer_con[0];
            av_image_fill_arrays(pRgbFrameCon->data, pRgbFrameCon->linesize, p_global_bgr_buffer_con,
                AV_PIX_FMT_GRAY8, widthCon, heightCon, 1);

            auto startTime = system_clock::now();

            while (av_read_frame(pFormatContext, pPacket) >= 0 && (stop == false)) {
                avcodec_send_packet(pCodecContext, pPacket);
                auto ret = avcodec_receive_frame(pCodecContext, pFrame);
                if (ret != 0) {
                    continue;
                }

                if (pFrame->format == hw_pix_fmt) {
                    /* retrieve data from GPU to CPU */
                    if ((ret = av_hwframe_transfer_data(sw_frame, pFrame, 0)) < 0) {
                        fprintf(stderr, "Error transferring the data to system memory\n");
                        break;
                    }
                }

                int64_t pts = av_rescale_q(pFrame->best_effort_timestamp, pFormatContext->streams[i]->time_base, AV_TIME_BASE_Q);
                auto playTime = startTime + microseconds(pts);

                {
                    SwsContext* swsContext = sws_getContext(width, height, (AVPixelFormat)sw_frame->format, widthPlay, heightPlay, AV_PIX_FMT_BGR24,
                        NULL, NULL, NULL, NULL);
                    sws_scale(swsContext, sw_frame->data, sw_frame->linesize, 0, height, pRgbFrame->data, pRgbFrame->linesize);
                    FrameData p1 = { bgr_buffer, playTime, widthPlay, heightPlay };
                    DrawFrame(p1);
                    sws_freeContext(swsContext);
                }
                {
                    /*sws_scale(swsContextCon, pFrame->data, pFrame->linesize, 0, height, pRgbFrameCon->data, pRgbFrameCon->linesize);
                    FrameData p2 = { bgr_buffer_con, playTime, widthCon, heightCon };
                    DrawFrameText(p2);*/
                }

                // av_packet_unref(pPacket);
            }


            sws_freeContext(swsContextCon);
            av_frame_free(&pRgbFrameCon);
            av_frame_free(&pRgbFrame);
            av_frame_free(&pFrame);
            av_packet_free(&pPacket);
            
            avcodec_free_context(&pCodecContext);
        }
    }

    avformat_close_input(&pFormatContext);
    avformat_free_context(pFormatContext);

    return 0;
}

int main()
{
    av_log_set_level(AV_LOG_QUIET);

    auto hInstance = GetModuleHandle(NULL);
    DWnd mainWind(hInstance, IDD_DIALOG1);
    pMainWind = &mainWind;
    EditModel edit01(mainWind, IDC_EDIT1);
    pEdit01 = &edit01;
    hEdit01 = mainWind.GetControl(IDC_EDIT1);

    HFONT hFont = CreateFont(13, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, TEXT("Consolas"));
    
    SendMessage(hEdit01, WM_SETFONT, (WPARAM)hFont, TRUE);

    hdc = GetDC(mainWind.mainHWnd);

    mainWind.AddMessageListener(WM_DESTROY, [](HWND hWnd, ...) {
        // exit thread
        ReleaseDC(hWnd, hdc);
    });

    mainWind.AddMessageListener(WM_DROPFILES, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        stop = true;

        Sleep(200); // 稍微等下原来的线程结束
        auto hdrop = (HDROP)wParam;
        WCHAR filepath[MAX_PATH];

        DragQueryFile(hdrop, 0, filepath, MAX_PATH);
        DragFinish(hdrop);

        wstring wfilepath = filepath;
        std::thread([hWnd, wfilepath]() {
            dealFrame((char*)(w2s(wfilepath).c_str()), hWnd);
        }).detach();

    });

    return mainWind.Run();

}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
) {
    return main();
}