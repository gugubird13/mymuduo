#pragma once

#include "Poller.h"

#include <vector>
#include <sys/epoll.h>

/*
 * epoll 的使用
 * epoll_create 创建 epollfd
 * epoll_ctl    添加想让epoll监听的fd以及针对这个fd感兴趣的事情  [add, mod, del]
 * epoll_wait   
 * 
 * 那么这里很清楚了： 这里封装的构造函数就是 调用了 epoll_create， 析构函数就是调用相应的close 去close掉对应的 epollfd
 * 也就是说这里就是以  oop 的方式去封装epoll
*/

class EpollPoller : public Poller
{
public:
    EpollPoller(EventLoop *loop);
    ~EpollPoller() override;

    // 重写poller基类的抽象方法
    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override; // 相当于epoll_wait
    void updateChannel(Channel* channel) override;                       // 相当于epoll_ctl 
    void removeChannel(Channel* channel) override;                       // 对应  epoll_ctl

private:
    static const int kInitEventListSize = 16;

    // 填写活跃的连接
    void fillActivateChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道
    void update(int operation, Channel* channel);

    using EventList = std::vector<epoll_event>; // 我们关于这个epoll_event 要扩容，我们不想它定长数组，所以放到 vector里面

    int epollfd_;
    EventList events_;
};