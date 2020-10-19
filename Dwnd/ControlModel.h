#ifndef CONTROLMODEL_20200816
#define CONTROLMODEL_20200816

#include <string>
#include <list>
#include <Windows.h>
#include "DWnd.h"

template<typename T>
struct ComboboxItem {
	std::wstring title;
	T value;
};

template<typename T>
class ControlModel {
public:
	typedef std::function<void(const T& value)> ValueChangeHandler;

	void operator=(const T& newval) {
		if (_value != newval) {
			_value = newval;
			update();
			invokeHandlers();
		}
	}

	const T& operator*() const {
		return _value;
	}

	auto OnChange(ValueChangeHandler cb) {
		changeCbList.push_back(cb);
		auto index = changeCbList.cend();
		index--;
		return index;
	}

protected:
	T _value;
	std::list<ValueChangeHandler> changeCbList;

	virtual void update() = 0;
	void invokeHandlers() {
		for (auto& cb : changeCbList) {
			cb(_value);
		}
	}
};

class EditModel : public ControlModel<std::wstring> {
public:
	using ControlModel::operator=;
	using ControlModel::operator*;
	EditModel(DWnd& dwd, int rcid);
private:
	DWnd& dwd;
	int rcid;
	void update();
};

class StaticModel : public ControlModel<std::wstring> {
public:
	using ControlModel::operator=;
	using ControlModel::operator*;
	StaticModel(HWND hWnd);
private:
	HWND hWnd;
	void update();
};

template<typename T = std::wstring>
class ComboboxModel {
public:
	int index;

	ComboboxModel(DWnd& dwd, int rcid): dwd(dwd), rcid(rcid) {
		index = 0;

		dwd.AddCommandEventListener(rcid, CBN_SELCHANGE, [this](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			auto combobox = (HWND)lParam;
			index = SendMessage(combobox, CB_GETCURSEL, 0, 0);
		});
	}

	void AddItem(const ComboboxItem<T>& item) {
		items.push_back(item);
		SendMessage(dwd.GetControl(rcid), (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)item.title.c_str());

		if (items.size() == 1) {
			Select(0);
		}
	}

	void RemoveItem(int index) {
		SendMessage(dwd.GetControl(rcid), CB_DELETESTRING, index, NULL);
		items.erase(items.begin() + index);

	}

	void ClearItem() {
		for (auto i = items.size(); i > 0; i--) {
			RemoveItem(0);
		}
	}

	void Select(int index) {
		this->index = index;
		SendMessage(dwd.GetControl(rcid), CB_SETCURSEL, index, NULL);
	}

	const ComboboxItem<T>& operator*() const {
		return items[index];
	}
private:
	DWnd& dwd;
	int rcid;

	std::vector<ComboboxItem<T>> items;
};

class TabModel {
public:
	int index;

	TabModel(DWnd& dwd, int rcid);
	void AddPage(const TabPage& page);
	void RemovePage(int index);
	void SelectPage(int index);

	const TabPage& operator*() const;
	const TabPage& operator[](int index) const;

private:
	DWnd& dwd;
	int rcid;

	std::vector<TabPage> pages;
};

class CheckBoxModel : public ControlModel<char> {
public:
	using ControlModel::operator=;
	using ControlModel::operator*;
	CheckBoxModel(DWnd& dwd, int rcid);
	void SetTitle(const std::wstring& title);
private:
	DWnd& dwd;
	int rcid;
	void update();
};

template<typename T = std::wstring>
class RadioButtonModel : public ControlModel<T> {
public:
	void operator=(const int& rcid) {
		selectedRcid = rcid;
		update();
		this->invokeHandlers();
	}
	const T& operator*() {
		return rcidmap[selectedRcid];
	}

	RadioButtonModel(DWnd& dwd, const std::map<int, T>& rcidmap): dwd(dwd), rcidmap(rcidmap) {
		for (auto& rciditem : rcidmap) {
			auto rcid = rciditem.first;
			dwd.AddCommandEventListener(rcid, BN_CLICKED, [this, rcid](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
				auto state = Button_GetCheck((HWND)lParam);
				if (state == 1) {
					this->selectedRcid = rcid;
					this->invokeHandlers();
				}
			});
		}
	}
private:
	DWnd& dwd;
	std::map<int, T> rcidmap;
	int selectedRcid = 0;

	void update() {
		int minrcid = 0;
		int maxrcid = 0;

		for (auto& item : rcidmap) {
			if (minrcid == 0 || item.first < minrcid) { minrcid = item.first; }
			if (maxrcid == 0 || item.first > maxrcid) { maxrcid = item.first; }
		}

		CheckRadioButton(dwd.mainHWnd, minrcid, maxrcid, selectedRcid);
	}

	void invokeHandlers() {
		for (auto& cb : this->changeCbList) {
			cb(rcidmap[selectedRcid]);
		}
	}
};

#endif
