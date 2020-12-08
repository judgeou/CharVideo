#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>

using std::array;

template <class T = int, int size = 5>
class RingArray {
public:
	RingArray() {
		empty = true;
		pushCount = 0;
		popCount = 0;
	}

	void push(T value) {
		// std::lock_guard<std::mutex> lock(mutex_);
		if (empty) {
			data[pushCount % size] = value;
			pushCount++;
			empty = false;
		}
		else {
			data[pushCount % size] = value;
			pushCount++;
		}
	}

	const T& front() const {
		return data[popCount % size];
	}

	void pop() {
		// std::lock_guard<std::mutex> lock(mutex_);
		popCount++;
	}

	bool isEmpty() const {
		return empty;
	}
private:
	std::mutex mutex_;
	std::condition_variable cond_;

	bool empty;
	unsigned int pushCount;
	unsigned int popCount;
	T data[size];
};