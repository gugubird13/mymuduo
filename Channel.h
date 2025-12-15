#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>
class EventLoop;


/*
    封装了sockfd和其感兴趣的 event，如EPOLLIN、EPOLLOUT 事件，还绑定了poller返回的具体事件
    // 搞清楚 eventloop 和 channel 的关系，一个 eventloop 可以监听很多个fd，那么就对应很多个 channel，所以每一个channel 就只有一个 loop
    // 这也是遵循当下比较好的规范： one loop per thread
    // 那么一个线程可以监听多个fd ，这就是 “ 多路 ” 的来源
    // Reactor 模型上面对应 Demultiplex
*/
class Channel:noncopyable::noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // fd 得到 poller通知以后， 处理事件。 调用相应的回应方法
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    // 这里的参数就是个副本， 使用 std::move() ，如果不再需要可以直接移动
    void setReadCallback(ReadEventCallback cb)  {readCallback_  = std::move(cb); }
    void setWriteCallback(EventCallback cb)     {writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb)     {closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb)     {errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void>&);

    int fd() const {return fd_;}
    int events() const {return events_;}
    int set_revents(int revt) {revents_ = revt; return 0;} // 这个revents_ 是 poller 返回的具体发生的事件

    // 设置 fd 相应的 事件状态
    void enableReading() {events_ |= kReadEvent; update();}
    void disableReading() {events_ &= ~kReadEvent; update();}
    void enableWriting() {events_ |= kWriteEvent; update();}
    void disableWriting() {events_ &= ~kWriteEvent; update();}
    void disableAll() {events_ = kNoneEvent; update();}
    // 实际上这个update 还是在调用底层的 epoll_ctl，这里只不过是做了一层封装

    bool isNoneEvent() const {return events_ == kNoneEvent;}
    bool isWriting() const {return events_ & kWriteEvent;}
    bool isReading() const {return events_ & kReadEvent;}

    // for poller
    int index() {return index_;}
    void set_index(int idx) {index_ = idx;}

    // 搞清楚 eventloop 和 channel 的关系，一个 eventloop 可以监听很多个fd，那么就对应很多个 channel，所以每一个channel 就只有一个 loop
    // 这也是遵循当下比较好的规范： one loop per thread
    // 那么一个线程可以监听多个fd ，这就是 “ 多路 ” 的来源

    EventLoop* ownerLoop(){ return loop_; }
    void remove(); // 这个函数是用来删除channel的

private:

    // 内部私有函数接口，给内部函数调用的
    void update();
    void handleEventWithGurad(Timestamp receiveTime);

    static const int kNoneEvent;        // 对任何事件都不感兴趣
    static const int kReadEvent;        // 对读事件感兴趣
    static const int kWriteEvent;       // 对写事件感兴趣

    EventLoop* loop_; // 事件循环
    const int fd_;    // fd, Poller 监听的对象
    int events_;      // 注册fd感兴趣的事件
    int revents_;     // Poller 返回的具体发生的事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为channel 通道可以获知 fd最终发生的具体的事件 revents 所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback     writeCallback_;
    EventCallback     closeCallback_;
    EventCallback     errorCallback_;
};