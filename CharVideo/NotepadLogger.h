#pragma once
#include <Windows.h>
#include <vector>
#include <string>

enum class CREATE_METHOD {
	Create,
	System
};

class NotepadLogger
{
private:

	STARTUPINFO startupInfo;
	DWORD pid;
	HANDLE hProcess = INVALID_HANDLE_VALUE;

	bool EnsureHwnd();
	DWORD EnumProcessByName(const std::wstring& name);
public:

	HWND notepadHwnd;
	HWND notepadEditHwnd;
	NotepadLogger(const wchar_t * title = L"记事本", CREATE_METHOD cm = CREATE_METHOD::Create);
	~NotepadLogger();
	
	// 显示记事本窗口
	void Show();

	// 隐藏记事本窗口
	void Hide();

	// 设置窗口的标题
	void SetTitle(const wchar_t* title);

	// 清除记事本内容
	void Clear();

	// 添加显示内容
	void Output(const wchar_t* text, bool newline = true);
};

