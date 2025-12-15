#pragma once

#include "noncopyable.h"
#include "Thread.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>
// 之前写了 EventLoop 和 Thread 两个类别
// 现在这个类是把这俩组合一起， 让 每一个EventLoop 都运行在一个 Thread里面

class EventLoop;

class EventLoopThread : noncopyable::noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    
    EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(), const std::string &name = std::string());
    ~EventLoopThread();

    EventLoop* startLoop();

private:
    void threadFunc();

    EventLoop* loop_;
    bool exiting_; 
    Thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    ThreadInitCallback callback_;
};