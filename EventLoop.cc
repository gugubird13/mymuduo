#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
// 防止一个线程创建多个EventLoop, 也就是说
// 当前线程创建完一个eventloop，这个指针会指向这个 loop
// 也就是说当前线程想创建一个loop的时候，就要检查这个是不是空了
// __thread 就是thread local的意思
__thread EventLoop* t_loopInThisThread = nullptr;

// 定义了一个默认的全局的 poller的超时时间
const int kPollTimeMs = 10000;

// 封装了我们所说的这个eventfd() 函数，因此我们要include相应的头文件
// 创建wakeupfd 用来通知subReactor 处理新来的 Channel，也就是唤醒，通过轮询的方式
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0)
    {
        // 如果小于0，说明出错了，就不用继续下去了
        LOG_FATAL("eventfd error:%d\n", errno); // 这里面自带了 exit，所以我们就不写了
    }

    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false) // 刚开始没有需要处理的回调函数
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , currentActiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if(t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else{
        t_loopInThisThread = this;
    }

    // 刚刚我们在初始化列表上面，并没有设置wakeupChannel_相关的events，只是设置了 fd
    // 因此这里还需要设置 wakeupfd 事件类型以及发生后事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的 EPOLLIN 读事件了
    wakeupChannel_->enableReading();

}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    // 实际上读到什么不重要，因为我们需要唤醒即可
    // 唤醒了的话，对于主反应堆，就可以把Channel发到sub loop上面
    if(n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %ld bytes instead of 8", n);
    }
}

// 调度底层的poller, 开始事件分发
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping\n", this);

    while(!quit_)
    {
        activeChannels_.clear();

        // 这里的poll监听的就是两类fd，一种是clientfd ，另外一种是wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for(Channel* channel : activeChannels_)
        {
            // poller 监听哪些channel发生事件了，然后上报给EventLoop, 通知channel处理相应的事件
            currentActiveChannel_ = channel;
            currentActiveChannel_->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        // 为什么当前的EventLoop 还要执行回调操作？
        /*
         * IO线程 mainLoop -> accept 相关工作 -> 拿到fd 注册好 Channel 去给一个 subloop
         * mainLoop 事先注册一个回调cb （需要subloop 来执行） wakeup subloop 之后，就执行了下面的doPendingFunctors()
         * 这里面放的就是mainLoop 注册的cb操作 
         *
        */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping.\n", this);
    looping_ = false;

}

// 退出事件循环 1. loop在自己线程中调用quit 2.在非loop的线程中，调用loop的quit
/*
 *                      mainloop
 *                                          (对于muduo库来说，并没有这样的 队列 实现)
 *                                      no  ==============================  生产者-消费者的线程安全队列, 将 mainLoop 和 子 Loop 隔开
 *      muduo 库是通过 wakeupFd 是实现的
 * subLoop1         subLoop2        subLoop3
 *
*/

void EventLoop::quit()
{
    quit_ = true;
    if(!isInLoopThread())
    {
        wakeup(); // 如果是在其它线程中调用的quit，要先唤醒，然后那个线程才能进 loop循环判断 quit_ 变量
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if(isInLoopThread())  // 在当前的loop线程中，执行 cb
    {
        cb();
    }
    else{ // 在非当前loop线程中执行cb, 那就需要唤醒loop所在线程，执行cb
        queueInLoop(cb);
    }
}

// 这个涉及变量访问冲突，要先上锁
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop线程了
    // 写完 doPendingFunctors，我们就知道这个判断条件的第二个条件
    // 这里的 callingPendingFunctors_ 就是说明那个线程并没有阻塞，而是在执行 相应的 回调函数
    // 所以这里要看，如果是这样的话，还是需要wakeup的，因为我们去看 loop函数，在调用完对应的 doPendingFunctors 
    // 还是会调用 poll 去阻塞的，这样还是会阻塞，所以我们可以进行唤醒
    if( !isInLoopThread() || callingPendingFunctors_)
    {
        wakeup(); // 要唤醒loop所在的线程
    }

}

// 用于唤醒loop所在的线程的，那么我们只需要向这个wakeup写数据即可
// 接着wakeupChannel 就发生读事件，当前loop就被唤醒了
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if(n != sizeof one)
    {
        LOG_ERROR("EventLoop::writes() %lu bytes instead of 8 \n", n);
    }
}
    
// EventLoop 的方法 ==> Poller 的方法
/*
 * 本质上这些方法都是 Channel 向 Poller问的，但是Channel 无法直接向 Poller 沟通
*/
void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::HasChannel(Channel* channel)
{
    return poller_->hasChannel(channel);
}
// ==> Poller 方法的结束

void EventLoop::doPendingFunctors()
{
    // 为了保证 queueInLoop 和 doPendingFunctors 能够并行执行
    // 并且防止 上锁 导致的时延加长
    // 这里做一个 线程局部的 变量，先做交换，然后保证这个是 局部的即可
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    // 出了这个括号，锁就没了

    for (const Functor &functor : functors)
    {
        functor();  // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;

}