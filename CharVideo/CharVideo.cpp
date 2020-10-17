#include <stdio.h>
#include <string>
#include <thread>
#include <vector>
#include <list>
#include <chrono>
#include <mutex>
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
#include "..\..\DWnd\Dwnd\DWnd.h"

using std::string;
using std::wstring;
using std::vector;
using std::list;

using namespace std::chrono;

int width;
int height;
std::mutex mut01;
HDC hdc;

struct FrameData
{
    vector<uint8_t> bit;
    system_clock::time_point playTime;
};
list<FrameData> bitmapList;

void addRgbbuffer(AVFrame* frame) {

}

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

void DrawFrame(const FrameData& bitData) {
    BITMAPINFO info = { 0 };
    auto& bgr_buffer = bitData.bit;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biSizeImage = bgr_buffer.size();
    info.bmiHeader.biCompression = BI_RGB;

    std::this_thread::sleep_until(bitData.playTime);
    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, &bgr_buffer[0], &info, DIB_RGB_COLORS, SRCCOPY);

    {
        COORD topLeft = { 0, 0 };
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO screen;
        DWORD written;

        GetConsoleScreenBufferInfo(console, &screen);

        FillConsoleOutputAttribute(
            console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE,
            screen.dwSize.X * screen.dwSize.Y, topLeft, &written
        );
        SetConsoleCursorPosition(console, topLeft);

        printf("Hello World\nHello World\nHello World\nHello World\n");
    }
}

int dealFrame(char * infile, HWND hwnd) {
    AVFormatContext* pFormatContext = avformat_alloc_context();
    avformat_open_input(&pFormatContext, infile, NULL, NULL);

    avformat_find_stream_info(pFormatContext, NULL);

    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters* pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        AVCodec* pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        // 只要视频和音频
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            width = pLocalCodecParameters->width;
            height = pLocalCodecParameters->height;

            AVCodecContext* pCodecContext = avcodec_alloc_context3(pLocalCodec);
            avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);
            avcodec_open2(pCodecContext, pLocalCodec, NULL);

            SwsContext* swsContext = sws_getContext(width, height, pCodecContext->pix_fmt, width, height, AV_PIX_FMT_BGR24,
                NULL, NULL, NULL, NULL);

            AVPacket* pPacket = av_packet_alloc();
            AVFrame* pFrame = av_frame_alloc();
            AVFrame* pRgbFrame = av_frame_alloc();
            int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, width, height, 1);

            auto startTime = system_clock::now();

            while (av_read_frame(pFormatContext, pPacket) >= 0) {
                avcodec_send_packet(pCodecContext, pPacket);
                avcodec_receive_frame(pCodecContext, pFrame);

                if (pFrame->best_effort_timestamp >= 0) {
                    vector<uint8_t> bgr_buffer(num_bytes);
                    auto p_global_bgr_buffer = &bgr_buffer[0];

                    av_image_fill_arrays(pRgbFrame->data, pRgbFrame->linesize, p_global_bgr_buffer,
                        AV_PIX_FMT_BGR24, width, height, 1);

                    int64_t pts = av_rescale_q(pFrame->best_effort_timestamp, pFormatContext->streams[i]->time_base, AV_TIME_BASE_Q);
                    auto playTime = startTime + microseconds(pts);
                    
                    sws_scale(swsContext, pFrame->data, pFrame->linesize, 0, height, pRgbFrame->data, pRgbFrame->linesize);

                    DrawFrame({ bgr_buffer, playTime });
                    /*if (bitmapList.size() >= 5) {
                        mut01.lock();
                        bitmapList.push_back({ bgr_buffer, playTime });
                        mut01.unlock();
                    }
                    else {
                        bitmapList.push_back({ bgr_buffer, playTime });
                    }*/
                }


            }

            sws_freeContext(swsContext);
        }
    }

    return 0;
}

void DrawVideoLoop() {
    return;
    while (1) {
        if (bitmapList.size() >= 5) {
            mut01.lock();
            auto& bitData = bitmapList.front();
            DrawFrame(bitData);

            bitmapList.pop_front();
            mut01.unlock();
        }
    }
}

int main(int argc, char * argv[])
{
    av_log_set_level(AV_LOG_QUIET);

    auto hInstance = GetModuleHandle(NULL);
    DWnd mainWind(hInstance, IDD_DIALOG1);

    hdc = GetDC(mainWind.mainHWnd);

    mainWind.AddMessageListener(WM_DESTROY, [](HWND hWnd, ...) {
        // exit thread
        ReleaseDC(hWnd, hdc);
    });

    mainWind.AddCommandListener(IDCANCEL, [](HWND hWnd, ...) {
        DestroyWindow(hWnd);
    });

    mainWind.AddCommandListener(IDOK, [argv](HWND hWnd, ...) {
        std::thread([hWnd, argv]() {
            dealFrame(argv[1], hWnd);
        }).detach();

        std::thread(DrawVideoLoop).detach();
    });

    mainWind.AddMessageListener(WM_DROPFILES, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto hdrop = (HDROP)wParam;
        WCHAR filepath[MAX_PATH];

        DragQueryFile(hdrop, 0, filepath, MAX_PATH);
        DragFinish(hdrop);

        wstring wfilepath = filepath;
        std::thread([hWnd, wfilepath]() {
            dealFrame((char*)(w2s(wfilepath).c_str()), hWnd);
        }).detach();

        std::thread(DrawVideoLoop).detach();
    });

    return mainWind.Run();

}
