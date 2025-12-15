#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb, const std::string &name)
    : loop_(nullptr)
    , exiting_(false)
    , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
    , mutex_()
    , cond_()
    , callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if(loop_ != nullptr)
    {
        loop_->quit();
        thread_.join(); // 等待子线程结束
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_.start(); //启动底层线程，开启底层的一个新的线程，并执行相关的函数
    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while( loop_ == nullptr)
        {
            // 说明回调函数那里，还没有把 loop赋值完成, 还没有 notify
            cond_.wait(lock);
        }
        // 唤醒出来之后
        loop = loop_;
    }
    return loop;
}

// 下面这个方法，是在单独的新的线程里面运行的，每一次start 都是启动新线程
void EventLoopThread::threadFunc()
{
    // 这里要做的，首先创建一个独立的 EventLoop
    // 和上面的线程是一一对应的
    // 这里就是 one loop per thread!!!!!!!!!!!!!!!
    EventLoop loop;

    if(callback_) // 看看有没有什么其它的相关回调函数
    {
        callback_(&loop); // 针对这个 loop 做你想做的事情
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop(); // EventLoop loop => 开启底层的 poller.poll()
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}