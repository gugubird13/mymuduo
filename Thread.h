#pragma once 

#include "noncopyable.h"

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

class Thread : noncopyable::noncopyable
{
public:
    using ThreadFunc = std::function<void ()>;

    explicit Thread(ThreadFunc ,const std::string &name = std::string());
    ~Thread();

    void start();
    void join();    // 我们调用 C++ 提供的相关函数

    bool started() const {return started_; }

    pid_t tid() const {return tid_;} 
    // 这个muduo库返回的 线程的 tid是linux上面用top 查看出来的进程里面的线程id，而不是 pthread_create那个id
    // pthread self 这个id 不是真正打印出来的 id

    const std::string& name() const{return name_;}

    static int numCreated() {return numCreated_;}

private:
    void setDefaultName();
    bool started_;
    bool joined_;  // 当前线程运行完了等待其它线程再继续往下运行
    // std::thread pthreadId_; // 由于这个 一旦声明，thread就开始执行了，因此我们需要一个智能指针来控制它的产生
    std::shared_ptr<std::thread> thread_;
    pid_t tid_;
    ThreadFunc func_; // 存储线程函数的
    std::string name_;

    static std::atomic_int32_t numCreated_;
};