#ifndef HANDLEWRAPPER_20200816
#define HANDLEWRAPPER_20200816

#include <Windows.h>

class HandleWrapper {
public:
	HANDLE operator*() {
		return handle;
	}
	HandleWrapper(HANDLE h) : handle(h) {

	}
	~HandleWrapper() {
		CloseHandle(handle);
	}
private:
	HANDLE handle;
};

#endif // !HANDLEWRAPPER_20200816
