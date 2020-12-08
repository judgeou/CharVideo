#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class SharedQueue
{
public:
    SharedQueue(int limit = 5);
    ~SharedQueue();

    T& front();
    void pop_front();
    void clear();

    void push_back(const T& item);

    int size();
    bool empty();

private:
    std::deque<T> queue_;
    int sizeLimit = 5;
    std::mutex mutex_;
    std::condition_variable cond_;

    std::mutex mutex_size;
    std::condition_variable cond_size;
};

template <typename T>
SharedQueue<T>::SharedQueue(int limit) {
    sizeLimit = limit;
}

template <typename T>
SharedQueue<T>::~SharedQueue() {}

template <typename T>
T& SharedQueue<T>::front()
{
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty())
    {
        cond_.wait(mlock);
    }
    return queue_.front();
}

template <typename T>
void SharedQueue<T>::pop_front()
{
    std::unique_lock<std::mutex> mlock(mutex_);
    while (queue_.empty())
    {
        cond_.wait(mlock);
    }
    queue_.pop_front();
    
    cond_size.notify_one();
}

template <typename T>
void SharedQueue<T>::push_back(const T& item)
{
    std::unique_lock<std::mutex> mlock_size(mutex_size);
    while (queue_.size() >= sizeLimit) {
        cond_size.wait(mlock_size);
    }

    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push_back(item);
    mlock.unlock();     // unlock before notificiation to minimize mutex con
    cond_.notify_one(); // notify one waiting thread

}

template <typename T>
int SharedQueue<T>::size()
{
    std::unique_lock<std::mutex> mlock(mutex_);
    int size = queue_.size();
    mlock.unlock();
    return size;
}

template<typename T>
inline bool SharedQueue<T>::empty()
{
    std::unique_lock<std::mutex> mlock(mutex_);
    auto r = queue_.empty();
    mlock.unlock();
    return r;
}

template <typename T>
void SharedQueue<T>::clear()
{
    queue_.clear();

    cond_size.notify_one();
}
