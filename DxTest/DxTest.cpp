#include <stdio.h>

#include "Graphics.h"
#include "..\Dwnd\DWnd.h"
#include "resource.h"

int main()
{
    auto ret = CoInitialize(NULL);
    
    auto hInstance = GetModuleHandle(NULL);
    DWnd dwnd(hInstance, IDD_DIALOG1);

    Graphics graphics(dwnd.mainHWnd);

    dwnd.AddMessageListener(WM_PAINT, [&](...) {
        // graphics.EndFrame();
     });

    dwnd.AddMessageListener(WM_MOUSEMOVE, [&](DWND_MSGHADNLE_PARAMS) {
        RECT rect;
        GetWindowRect(hWnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        double x = GET_X_LPARAM(lParam);
        double y = GET_Y_LPARAM(lParam);
        graphics.ClearBuffer(x / width, y / height, 255);
        graphics.EndFrame();
        });

    dwnd.Run(true);

    CoUninitialize();
}
