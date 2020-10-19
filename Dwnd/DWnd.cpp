#include "DWnd.h"
#include <CommCtrl.h>

std::map<HWND, DWnd*> DWnd::dWndThisMap;

DWnd::DWnd(HMODULE hInstance, int rcid, HWND fatherHwnd) {
	this->dpiFactor = 1;
	this->fatherHwnd = fatherHwnd;
	this->hInstance = hInstance;
	this->rcid = rcid;

	mainHWnd = CreateDialogParam(hInstance, MAKEINTRESOURCE(rcid), fatherHwnd, WindProc, (LPARAM)this);
	
	// 如果有父窗口，则直接进入Run(false)
	if (fatherHwnd) {
		Run(false);
	}
}

DWnd::~DWnd()
{
	if (mainHWnd) {
		dWndThisMap.erase(mainHWnd);
	}
}

INT_PTR DWnd::Run(bool selfMessageLoop)
{
	auto hWnd = mainHWnd;

	// 添加默认事件处理器
	AddMessageListener(WM_CLOSE, [](HWND hWnd, auto...args) { DestroyWindow(hWnd); });
	AddMessageListener(WM_DESTROY, [](auto...args) { PostQuitMessage(0); });

	// 对窗体做必要初始设置
	ShowWindow(hWnd, SW_SHOW);

	// 消息循环
	if (selfMessageLoop) {
		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0) > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return msg.wParam;
	}
	else {
		return 0;
	}
}

std::list<DWnd::MsgHandler>::const_iterator DWnd::AddMessageListener(UINT msg, MsgHandler cb)
{
	auto& handlers = msgHandlerMap[msg];
	handlers.push_back(cb);
	auto index = handlers.cend();
	index--;
	return index;
}

std::list<DWnd::MsgHandler>::const_iterator DWnd::AddNotifyListener(int rcid, UINT msg, MsgHandler cb)
{
	return AddMessageListener(WM_NOTIFY, [msg, rcid, cb](HWND hWnd, UINT msg_, WPARAM wParam, LPARAM lParam) {
		auto lpn = (LPNMHDR)lParam;
		if (lpn->code == msg && lpn->idFrom == rcid) {
			cb(hWnd, msg_, wParam, lParam);
		}
	});
}

std::list<DWnd::MsgHandler>::const_iterator DWnd::AddCommandListener(int command, MsgHandler cb)
{
	return AddMessageListener(WM_COMMAND, [command, cb](HWND hWnd, UINT msg_, WPARAM wParam, LPARAM lParam) {
		auto cmd = LOWORD(wParam);

		if (cmd == command) {
			cb(hWnd, msg_, wParam, lParam);
		}
	});
}

std::list<DWnd::MsgHandler>::const_iterator DWnd::AddCommandEventListener(int rcid, WORD msg, MsgHandler cb)
{
	return AddCommandListener(rcid, [msg, cb](HWND hWnd, UINT msg_, WPARAM wParam, LPARAM lParam) {
		auto code = HIWORD(wParam);
		if (code == msg) {
			cb(hWnd, msg_, wParam, lParam);
		}
    });
}

// 千万不要使用已经删除过的index，一定会出现异常
void DWnd::RemoveMessageListener(UINT msg, std::list<DWnd::MsgHandler>::const_iterator index)
{
	auto& handlers = msgHandlerMap[msg];
	handlers.erase(index);
}

void DWnd::RemoveNotifyListener(std::list<DWnd::MsgHandler>::const_iterator index)
{
	auto& handlers = msgHandlerMap[WM_NOTIFY];
	handlers.erase(index);
}

void DWnd::RemoveCommandListener(std::list<DWnd::MsgHandler>::const_iterator index)
{
	auto& handlers = msgHandlerMap[WM_COMMAND];
	handlers.erase(index);
}

void DWnd::RemoveCommandEventListener(std::list<DWnd::MsgHandler>::const_iterator index)
{
	RemoveCommandListener(index);
}

void DWnd::Hide()
{
	ShowWindow(mainHWnd, SW_HIDE);
}

HWND DWnd::GetControl(int rcid) const
{
	return GetDlgItem(mainHWnd, rcid);
}

INT_PTR WINAPI DWnd::WindProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG) {
		DWnd* dwd = (DWnd*)lParam;
		dWndThisMap[hWnd] = dwd;
		dwd->mainHWnd = hWnd;

		auto hdc = GetDC(hWnd);
		dwd->dpiFactor = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0;
		ReleaseDC(hWnd, hdc);
	}

	DWnd* dwd = dWndThisMap[hWnd];
	if (dwd) {
		return dwd->InternalWindProc(hWnd, msg, wParam, lParam);
	}
	else {
		return FALSE;
	}

}

INT_PTR DWnd::InternalWindProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	auto& cbset = msgHandlerMap[msg];
	for (auto& cb : cbset) {
		cb(hWnd, msg, wParam, lParam);
	}
	return cbset.empty() ? FALSE : TRUE;
}
