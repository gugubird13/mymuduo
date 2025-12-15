#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"

#include "memory"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseloop, const std::string &nameArg)
    : baseLoop_(baseloop)
    , name_(nameArg)
    , started_(false)
    , numThreads_(0)
    , next_(0) // 轮询的索引
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    for(int i=0; i<numThreads_; ++i)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop()); // 底层创建线程，绑定一个新的 EventLoop , 并且返回该loop的地址
    }

    // 如果没有设置 numThreads
    // 说明整个服务端就只有一个线程, 运行着baseloop
    if(numThreads_ == 0 && cb )
    {   
        cb(baseLoop_); // 那么只有拿这个baseLoop 去执行了
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    EventLoop* loop = baseLoop_;

    if(!loops_.empty()) // 通过轮询获取下一个处理事件的loop
    {
        loop = loops_[next_];
        ++next_;
        if(next_ >= loops_.size()) // 循环索引
        {
            next_ = 0;
        }
    }

    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    if( loops_.empty() )
    {
        return std::vector<EventLoop*> (1, baseLoop_);
    }
    else{
        return loops_;
    }
}