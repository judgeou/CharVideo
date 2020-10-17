#include <stdio.h>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include <Windows.h>
#include <gdiplus.h>
#include <gdiplusgraphics.h>
#pragma comment(lib, "Gdiplus.lib")

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
#include "..\..\DWnd\Dwnd\DWnd.h"

using std::string;
using std::wstring;

string w2s(const wstring& wstr) {
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
    string str(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), &str[0], str.size(), NULL, NULL);
    return str;
}

void save_gray_frame(unsigned char* buf, int wrap, int xsize, int ysize, char* filename)
{
    FILE* f;
    int i;
    f = fopen(filename, "w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

int dealFrame(HDC hdc, char * infile, HWND hwnd) {
    AVFormatContext* pFormatContext = avformat_alloc_context();
    avformat_open_input(&pFormatContext, infile, NULL, NULL);

    avformat_find_stream_info(pFormatContext, NULL);
    printf("Format %s, duration %lld us\n", pFormatContext->iformat->long_name, pFormatContext->duration);

    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters* pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        AVCodec* pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        // 只要视频和音频
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            int width = pLocalCodecParameters->width;
            int height = pLocalCodecParameters->height;
            printf("Video Codec: resolution %d x %d\n", width, height);

            AVCodecContext* pCodecContext = avcodec_alloc_context3(pLocalCodec);
            avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);
            avcodec_open2(pCodecContext, pLocalCodec, NULL);

            SwsContext* swsContext = sws_getContext(width, height, pCodecContext->pix_fmt, width, height, AV_PIX_FMT_BGR24,
                NULL, NULL, NULL, NULL);

            AVPacket* pPacket = av_packet_alloc();
            AVFrame* pFrame = av_frame_alloc();
            AVFrame* pRgbFrame = av_frame_alloc();
            int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, width, height, 1);
            uint8_t* p_global_bgr_buffer = (uint8_t*)av_malloc(sizeof(uint8_t) * num_bytes);

            av_image_fill_arrays(pRgbFrame->data, pRgbFrame->linesize, p_global_bgr_buffer,
                AV_PIX_FMT_BGR24, width, height, 1);

            auto startTime = std::chrono::system_clock::now();

            while (av_read_frame(pFormatContext, pPacket) >= 0) {
                avcodec_send_packet(pCodecContext, pPacket);
                avcodec_receive_frame(pCodecContext, pFrame);

                if (pFrame->best_effort_timestamp >= 0) {
                    auto pts = av_rescale_q(pFrame->best_effort_timestamp, pFormatContext->streams[i]->time_base, AV_TIME_BASE_Q);
                    auto playTime = startTime + std::chrono::microseconds(pts);

                    std::this_thread::sleep_until(playTime);

                    sws_scale(swsContext, pFrame->data, pFrame->linesize, 0, height, pRgbFrame->data, pRgbFrame->linesize);

                    BITMAPINFO info = { 0 };
                    info.bmiHeader.biBitCount = 24;
                    info.bmiHeader.biWidth = pFrame->width;
                    info.bmiHeader.biHeight = -pFrame->height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biSizeImage = num_bytes;
                    info.bmiHeader.biCompression = BI_RGB;

                    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, p_global_bgr_buffer, &info, DIB_RGB_COLORS, SRCCOPY);

                }


            }

            av_free(p_global_bgr_buffer);
            sws_freeContext(swsContext);
        }
    }

    return 0;
}

int main(int argc, char * argv[])
{
    auto hInstance = GetModuleHandle(NULL);
    DWnd mainWind(hInstance, IDD_DIALOG1);

    mainWind.AddCommandListener(IDCANCEL, [](HWND hWnd, ...) {
        DestroyWindow(hWnd);
    });

    mainWind.AddCommandListener(IDOK, [argv](HWND hWnd, ...) {
        std::thread([hWnd, argv]() {
            auto hdc = GetDC(hWnd);

            dealFrame(hdc, argv[1], hWnd);

            ReleaseDC(hWnd, hdc);
        }).detach();
    });

    mainWind.AddMessageListener(WM_DROPFILES, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto hdrop = (HDROP)wParam;
        WCHAR filepath[MAX_PATH];

        DragQueryFile(hdrop, 0, filepath, MAX_PATH);
        DragFinish(hdrop);

        wstring wfilepath = filepath;
        std::thread([hWnd, wfilepath]() {
            auto hdc = GetDC(hWnd);

            dealFrame(hdc, (char*)(w2s(wfilepath).c_str()), hWnd);

            ReleaseDC(hWnd, hdc);
        }).detach();
    });

    return mainWind.Run();

}
