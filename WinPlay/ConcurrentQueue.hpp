#pragma once
#include <atomic>
#include <memory>

using std::make_shared;
using std::atomic_compare_exchange_weak;

typedef void* T;
class ConcurrentQueue {
public:
	ConcurrentQueue() {
		Node* node = new Node();
		this->head = this->tail = node;
	}

	void push_back(T value) {
		Node* n = new Node();
		n->value = value;
		n->next = nullptr;

		Node* tail;
		Node* next;
		while (1) {
			tail = this->tail.load();
			next = tail->next;

			if (tail != this->tail) continue;

			if (next != NULL) {
				this->tail.exchange(tail);
				// atomic_compare_exchange_weak(this->tail, tail, next);
				continue;
			}

			// if (atomic_compare_exchange_weak<Node*>(tail->next, next, n) == true) break;
		}

		// atomic_compare_exchange_weak<Node*>(this->tail, tail, n);
	}
	void pop_front() {}
	const T& front() const {}
	
private:
	struct Node {
		T value;
		std::atomic<Node*> next;
	};

	std::atomic<Node*> head;
	std::atomic<Node*> tail;
};