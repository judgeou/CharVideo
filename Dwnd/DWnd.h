#ifndef DWND_20200816
#define DWND_20200816

#include <map>
#include <list>
#include <vector>
#include <functional>
#include <string>
#include <Windows.h>
#include <windowsx.h>

#define DWND_MSGHADNLE_PARAMS HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam

struct TabPage
{
	std::wstring title;
	int index;
	HWND hWnd;
};

class DWnd {
public:
	typedef std::function<void(DWND_MSGHADNLE_PARAMS)> MsgHandler;

	HWND mainHWnd;
	double dpiFactor;

	DWnd(HMODULE hInstance, int rcid, HWND fatherHwnd = NULL);
	~DWnd();

	INT_PTR Run(bool selfMessageLoop = true, MsgHandler customLoopHandler = nullptr);
	std::list<DWnd::MsgHandler>::const_iterator AddMessageListener(UINT msg, MsgHandler cb);
	std::list<DWnd::MsgHandler>::const_iterator AddNotifyListener(int rcid, UINT msg, MsgHandler cb);
	std::list<DWnd::MsgHandler>::const_iterator AddCommandListener(int command, MsgHandler cb);
	std::list<DWnd::MsgHandler>::const_iterator AddCommandEventListener(int rcid, WORD msg, MsgHandler cb);
	
	void RemoveMessageListener(UINT msg, std::list<DWnd::MsgHandler>::const_iterator index);
	void RemoveNotifyListener(std::list<DWnd::MsgHandler>::const_iterator index);
	void RemoveCommandListener(std::list<DWnd::MsgHandler>::const_iterator index);
	void RemoveCommandEventListener(std::list<DWnd::MsgHandler>::const_iterator index);

	void Hide();
	HWND GetControl(int rcid) const;
private:
	static INT_PTR WINAPI WindProc(DWND_MSGHADNLE_PARAMS);
	INT_PTR InternalWindProc(DWND_MSGHADNLE_PARAMS);

	static std::map<HWND, DWnd*> dWndThisMap;
	HMODULE hInstance;
	int rcid;
	HWND fatherHwnd;

	std::map<UINT, std::list<MsgHandler>> msgHandlerMap;
};

#endif
