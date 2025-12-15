#pragma once 

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

#include <functional>
#include <vector>
#include <atomic>
#include <unistd.h>
#include <memory>
#include <mutex>

class Channel;
class Poller;

// 事件循环类， 包含两个模块
// 1. Channel 
// 2. Poller (Epoll 的抽象)
// 类似一个是调度，另外一个是具体的事务
class EventLoop : noncopyable::noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 开启事件循环
    void loop();
    // 退出事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    // 在当前loop中执行 cb
    void runInLoop(Functor cb);
    // 把cb放入队列中, 唤醒loop所在的线程并执行cb
    void queueInLoop(Functor cb);

    // 用来唤醒loop所在的线程的
    void wakeup();
    
    // EventLoop 的方法 ==> Poller 的方法
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool HasChannel(Channel* channel);

    // 判断eventloop这个对象是否在自己的线程里面
    // 左边是创建这个eventloop的线程id, 右边是当前线程的id
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid();}

private:
    void handleRead(); // wake up
    void doPendingFunctors(); // 执行回调的

    using ChannelList = std::vector<Channel*>;
    
    std::atomic_bool looping_; // 原子操作，通过CAS实现的
    std::atomic_bool quit_;    // 标识退出 loop循环

    const pid_t threadId_;  // 记录当前loop所在线程的id
    // 这个 threadId 在使用的时候，就是看当前这个线程ID和对应的EventLoop 线程id是不是一致的
    // 因为 muti-Reactor模型中，这种多reactor有一个是处理accept的，剩下的全是 Worker Eventloop
    Timestamp pollReturnTime_; // 因为我们知道， eventloop还是调用 相关poller 来使用 epoll_wait的，所以这里
    // 返回的是 发生事件的 activeChannels的时间点

    std::unique_ptr<Poller> poller_; // 动态管理资源

    int wakeupFd_;  // 实际上这个就是 当主 Reactor 拿到了一个用户连接，有一些感兴趣的事情来了的时候，要唤醒一些
                    // worker EventLoop 使得其从阻塞被唤醒去处理这些事件，这个变量很重要，
                    // 其内部就是用的 eventfd() 这个函数来创建的
                    // 当mainloop 获取了一个新用户的 channel， 就通过轮询算法选择一个subloop
                    // 通过该成员来通知、唤醒subloop
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;
    Channel *currentActiveChannel_;

    std::mutex mutex_; // 互斥锁, 用来保护下面容器的线程安全操作
    std::atomic_bool callingPendingFunctors_; // 标识当前 loop 是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_ ; // 存储loop所需要执行的所有回调操作

};