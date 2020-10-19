#include "NotepadLogger.h"
#include <TlHelp32.h>

using namespace std;

bool NotepadLogger::EnsureHwnd()
{
	struct S1 {
		DWORD pid;
		NotepadLogger* nlptr;
	};
	static auto EnumWindowsProcChildMy = [](HWND hwnd, LPARAM lParam) -> BOOL {
		S1* data = (S1*)lParam;
		WCHAR className[200];
		GetClassName(hwnd, className, 200);
		if (wstring(L"Edit") == className) {
			data->nlptr->notepadEditHwnd = hwnd;
			return FALSE;
		}
		return TRUE;
	};
	static auto EnumWindowsProcMy = [](HWND hwnd, LPARAM lParam) -> BOOL
	{
		S1* data = (S1*)lParam;
		DWORD lpdwProcessId;
		GetWindowThreadProcessId(hwnd, &lpdwProcessId);
		if (data->pid == lpdwProcessId) {
			data->nlptr->notepadHwnd = hwnd;
			EnumChildWindows(hwnd, EnumWindowsProcChildMy, (LPARAM)data);
			if (data->nlptr->notepadEditHwnd) {
				// 在这里把记事本移动到左上角
				SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE);
				return FALSE;
			}
			else {
				return TRUE;
			}
		}
		return TRUE;
	};

	if (notepadEditHwnd == NULL) {
		pid = EnumProcessByName(L"notepad.exe");
		if (pid > 0) {
			S1 data = {
				pid,
				this
			};
			EnumWindows(EnumWindowsProcMy, (LPARAM)&data);
			return notepadHwnd && notepadEditHwnd;
		}
		else {
			return false;
		}
	}
	else {
		return true;
	}
}

NotepadLogger::NotepadLogger(const wchar_t * title, CREATE_METHOD cm)
{
	notepadHwnd = NULL;
	notepadEditHwnd = NULL;

	switch (cm)
	{
	case CREATE_METHOD::Create: {
		WCHAR cmd[] = L"notepad";
		PROCESS_INFORMATION pInfo;
		CreateProcess(NULL, cmd, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &startupInfo, &pInfo);
		pid = pInfo.dwProcessId;
		hProcess = pInfo.hProcess;
		CloseHandle(pInfo.hThread);
	}
		break;
	case CREATE_METHOD::System:
		system("start notepad");
		break;
	default:
		break;
	}

	while (EnsureHwnd() != true) {

	}
	SetTitle(title);
}

NotepadLogger::~NotepadLogger()
{
	if (hProcess == INVALID_HANDLE_VALUE) {
		hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	}

	TerminateProcess(hProcess, 0);
	CloseHandle(hProcess);
}

void NotepadLogger::Show()
{
	EnsureHwnd();
	ShowWindow(notepadHwnd, SW_SHOW);
}

void NotepadLogger::Hide()
{
	EnsureHwnd();
	ShowWindow(notepadHwnd, SW_HIDE);
}

void NotepadLogger::SetTitle(const wchar_t* title)
{
	SendMessage(notepadHwnd, WM_SETTEXT, 0, (LPARAM)title);
}

void NotepadLogger::Clear()
{
	SendMessage(notepadEditHwnd, WM_SETTEXT, 0, (LPARAM)L"");
}

void NotepadLogger::Output(const wchar_t* text, bool newline)
{
	SendMessage(notepadEditHwnd, 194, 1, (LPARAM)text);
	if (newline) {
		SendMessage(notepadEditHwnd, 194, 1, (LPARAM)L"\r\n");
	}
}

DWORD NotepadLogger::EnumProcessByName(const std::wstring& name)
{
	std::vector<DWORD> found;
	auto hProcSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (!hProcSnap)
		return 0;

	PROCESSENTRY32W tEntry = { 0 };
	tEntry.dwSize = sizeof(PROCESSENTRY32W);

	// Iterate threads
	for (BOOL success = Process32FirstW(hProcSnap, &tEntry);
		success != FALSE;
		success = Process32NextW(hProcSnap, &tEntry))
	{
		if (name.empty() || _wcsicmp(tEntry.szExeFile, name.c_str()) == 0)
			found.emplace_back(tEntry.th32ProcessID);
	}

	return found.size() > 0 ? found[0] : 0;
}
