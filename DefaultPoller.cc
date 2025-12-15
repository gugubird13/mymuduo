#include "Poller.h"
#include "EpollPoller.h"
#include "IoUringPoller.h"
#include <stdlib.h>

// 这个 EventLoop *loop 参数是构造函数参数

// 这种设计能够有效避免循环依赖
Poller* Poller::newDefaultPoller(EventLoop *loop)
{
    if(getenv("MUDUO_USE_POLL"))
    {
        return nullptr; // 生成poll的实例
    }
    else if(getenv("MUDUO_USE_IOURING"))
    {
        return new IoUringPoller(loop); // 生成 io_uring 的实例
    }
    else{
        return new EpollPoller(loop); // 生成epoll的实例
    }
}