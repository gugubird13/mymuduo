#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <unordered_map>
#include <vector>
class Channel;
class EventLoop;

// muduo 库中的多路事件分发器
class Poller : noncopyable::noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop* loop);
    virtual ~Poller() = default;

    // 给所有IO复用保留统一的接口
    virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0; // EpollPoller中相当于Epoll_wait
    virtual void updateChannel(Channel* channel) = 0;                       // EpollPoller中相当于EpollCtl add等等
    virtual void removeChannel(Channel* channel) = 0;

    // 判断参数channel是否在当前 poller当中
    bool hasChannel(Channel *channel) const; 
    // EventLoop 可以通过该接口获取默认的IO复用的具体实现
    static Poller* newDefaultPoller(EventLoop *loop); // 对于这个函数，其返回的可能是不同子类的实例，所以不能在基类的.cc文件里面去定义,应该另起一个

protected:
    // 这里的int表示 Channel的sockfd，value就是对应的sockfd所属的通道类型，即Channel
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;

private:
    EventLoop* ownerLoop_; //定义poller所属的事件循环 EventLoop

};