#include "ControlModel.h"
#include <CommCtrl.h>

EditModel::EditModel(DWnd& dwd, int rcid): dwd(dwd)
{
	this->dwd = dwd;
	this->rcid = rcid;

	dwd.AddCommandEventListener(rcid, EN_CHANGE, [this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		auto edit = (HWND)lParam;
		int len = GetWindowTextLength(edit);
		std::wstring text(len, '\0');
		GetWindowText(edit, &text[0], len);
		_value = text;
    });
}

void EditModel::update() {
	SetWindowText(dwd.GetControl(rcid), _value.c_str());
}

StaticModel::StaticModel(HWND hWnd)
{
	this->hWnd = hWnd;
}

void StaticModel::update() {
	SetWindowText(hWnd, _value.c_str());
}

TabModel::TabModel(DWnd& dwd, int rcid): dwd(dwd), rcid(rcid), index(0)
{
	HWND tabWnd = dwd.GetControl(rcid);
	auto tabid = rcid;
	auto& pages = this->pages;

	dwd.AddNotifyListener(tabid, TCN_SELCHANGE, [tabWnd, this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		int iPage = TabCtrl_GetCurSel(tabWnd);
		this->index = iPage;
		for (auto& page : this->pages) {
			if (page.hWnd) {
				ShowWindow(page.hWnd, (iPage == page.index) ? SW_SHOW : SW_HIDE);
			}
		}
	});
}

void TabModel::AddPage(const TabPage& page)
{
	pages.push_back(page);
	auto tabWnd = dwd.GetControl(rcid);
	
	RECT tabRect;
	GetWindowRect(tabWnd, &tabRect);

	const auto& dpiFactor = dwd.dpiFactor;
	auto left = 1 * dpiFactor;
	auto top = 20 * dpiFactor;
	auto sx = tabRect.right - tabRect.left - left * 2;
	auto sy = tabRect.bottom - tabRect.top - top - dpiFactor;

	TCITEM tie = { 0 };
	tie.mask = TCIF_TEXT;
	tie.pszText = (WCHAR*)page.title.c_str();
	TabCtrl_InsertItem(tabWnd, page.index, &tie);

	if (page.hWnd) {
		// 重新设置父窗口
		SetParent(page.hWnd, tabWnd);
		SetWindowPos(page.hWnd, HWND_TOP, left, top, sx, sy, SWP_HIDEWINDOW);
	}

	if (pages.size() == 1) {
		SelectPage(0);
	}
}

void TabModel::RemovePage(int index)
{
	auto tabWnd = dwd.GetControl(rcid);
	TabCtrl_DeleteItem(tabWnd, index);
	pages.erase(pages.begin() + index);
}

void TabModel::SelectPage(int index)
{
	auto tabWnd = dwd.GetControl(rcid);
	auto& tabPages = pages;

	TabCtrl_SetCurSel(tabWnd, index);

	for (auto& page : tabPages) {
		if (page.hWnd) {
			ShowWindow(page.hWnd, (index == page.index) ? SW_SHOW : SW_HIDE);
		}
	}
}

const TabPage& TabModel::operator*() const
{
	return pages[index];
}

const TabPage& TabModel::operator[](int index) const
{
	return pages[index];
}

CheckBoxModel::CheckBoxModel(DWnd& dwd, int rcid): dwd(dwd), rcid(rcid)
{
	dwd.AddCommandEventListener(rcid, BN_CLICKED, [this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		auto state = Button_GetCheck((HWND)lParam);
		this->operator=(state);
	});
}

void CheckBoxModel::SetTitle(const std::wstring& title)
{
	SetWindowText(dwd.GetControl(rcid), title.c_str());
}

void CheckBoxModel::update()
{
	if (_value == 0) {
		Button_SetCheck(dwd.GetControl(rcid), BST_UNCHECKED);
	}
	else if (_value == 1) {
		Button_SetCheck(dwd.GetControl(rcid), BST_CHECKED);
	}
	else {
		Button_SetCheck(dwd.GetControl(rcid), BST_INDETERMINATE);
	}
}
