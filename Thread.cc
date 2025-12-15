#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

std::atomic_int32_t Thread::numCreated_(0);

Thread::Thread(ThreadFunc func,const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    if(started_ && !joined_)
    {
        // 线程只有在 运行起来的时候 或者 是非 joined 状态，joined 的状态是不允许被 detach的
        // 也就是说，这个线程 要么就是 守护线程（主线程结束后其自动结束，内核资源自动回收，不会成为孤儿线程）； 要么就是工作线程        
        thread_->detach(); //thread 类 提供了 设置分离线程的方法，底层还是 pthread_detach()
    }
}

void Thread::start()
{
    sem_t sem;
    sem_init(&sem, false, 0);
    started_ = true;

    // 开启线程
    thread_ = std::shared_ptr<std::thread> (new std::thread([&](){
        //获取线程的tid
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        // 开启一个线程，专门执行该线程函数
        func_(); 
    })); 

    // 实际上上述代码， 是某个线程执行，然后要开启一个新的线程，新的线程是在 lambda函数体里面
    // 这里必须要等待获取上面新创建的线程tid值

    sem_wait(&sem);

}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if(name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof buf, "Thread %d", num);
        name_ = buf;
    }
}