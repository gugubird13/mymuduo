#include "EpollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <memory.h>
namespace{
    const int kNew = -1;    // 这个channel 还没有添加到poller
    // channel 的成员 index_ 初始化也是 -1, 因此我们可以知道 Channel的这个index表示的就是在poller中的状态，是未添加还是已添加等等
    const int kAdded = 1;   // 这个channel 在poller里面，并且 加入到了对应的 epoll里面
    const int kDeleted = 2; // 这个channel 在poller里面，但是不在 epoll里面
}

EpollPoller::EpollPoller(EventLoop *loop)
    : Poller(loop) 
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC)) // 这个作用就是及时关闭父进程的fd（当父进程fork子进程的时候，子进程和父进程享有同样的东西）
    // 这里调用 epoll_create1 前面加冒号就是为了声明我用的是 全局的命名空间，也就是优先用系统的而不是我自己写得 epoll_create1函数, 
    // 实际上这里用的是.h 库，也就是C库，没有C++那样的std命名空间，所以就是全局命名空间了
    , events_(kInitEventListSize)
{
    if(epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EpollPoller::~EpollPoller()
{
    ::close(epollfd_);
}

// 重写poller基类的抽象方法

/*
 * updateChannel 和 removeChannel 都对应的 epoll_ctl
 *                      EventLoop(eventloop 里面有一整个 ChannelList,所以 eventloop的 List要大于等于 Poller里面的,Poller里面仅仅是注册了的 Channel)
 *         ChannelList              Poller
 *                                  ChannelMap    <fd, Channel*>
 *          下面的关于Channel的更新和删除，首先这里对于Poller来说，Channel无非就是在这个ChannelMap里面
 *          所以对应remove函数。
 *          除此之外，还有一个要注意的就是底层的这个  epoll_ctl 这个，看看它要不要也删除, 主要看 其index状态 added就是在 epoll里面
 *          deleted就是不在epoll里面
*/

void EpollPoller::updateChannel(Channel* channel) 
{
    // 获取这个Channel 在 poller中的状态
    const int index = channel->index();

    LOG_INFO("func=%s => fd=%d events=%d index=%d\n",__FUNCTION__, channel->fd(), channel->events(), channel->index());
    if(index == kNew || index == kDeleted)
    {
        if(index == kNew)
        {
            // 先增加到poller的成员容器里面
            int fd = channel->fd();
            channels_[channel->fd()] = channel;
        }

        // 如果状态时 kDeleted 不用再往里加了，因为已经在了，只需要设置状态就行了
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel); // 这个就是往epoll里面进行注册了
    }
    else{// == kAdded ,表示channel已经在poller上面注册过了
        int fd = channel->fd();
        if(channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else{
            // 这种情况就说明事件变了，update在 设置完相应状态之后发生了变更
            update(EPOLL_CTL_MOD, channel);
        }

    }
}

void EpollPoller::removeChannel(Channel* channel) 
{
    // 自然就是把 Channel 从 Poller里面删掉
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd=%d events=%d index=%d\n",__FUNCTION__, channel->fd(), channel->events(), channel->index());
    
    int index = channel->index();
    if(index == kAdded)
    {
        // 说明被加入到了 epoll里面，要在epoll里面也删除
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew); // 返回之前的状态，没有往 Poller里面加入过
}

Timestamp EpollPoller::poll(int timeoutMs, Poller::ChannelList* activeChannels)
{  
    // 由于这个poll函数会被经常调用，我们不希望用log_INFO 做太多的输出
    // 因此这里用 LOG_DEBUG() 更为合理
    LOG_DEBUG("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());

    if(numEvents > 0)
    {
        LOG_INFO("%d events happened\n", numEvents);
        fillActivateChannels(numEvents, activeChannels);
        if(numEvents == events_.size())
        {
            events_.resize(events_.size() * 2); // 扩容
        }
    }
    else if(numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        if(savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_ERROR("EpollPoller::poll() error\n");
        }
    }
    return now;
}

void EpollPoller::fillActivateChannels(int numEvents, Poller::ChannelList *activeChannels) const
{
    for(int i = 0; i < numEvents; ++i)
    {
        // 因为我们update的时候把 channel 指针放到了 event.data.ptr 里面
        // 因此我们可以直接拿到就行了
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events); // 设置这个channel 具体发生的事件
        activeChannels->push_back(channel); // 把这个活跃的channel 塞到外面的 activeChannels 里面去
        // EventLoop 就能拿到 对应的 poller 给他的活跃的channel list
    }
}

// 这个update 就是包装了 epoll_ctl add/mod/del
void EpollPoller::update(int operation, Channel* channel)
{
    epoll_event event;
    memset(&event, 0, sizeof event);
    int fd = channel->fd();
    event.events = channel->events();
    event.data.ptr = channel;

    if(::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if(operation == EPOLL_CTL_DEL)
        {  
            // 如果没删掉，不至于 fatal
            LOG_ERROR("epoll_ctl delete error:%d\n", errno);
        }
        else{
            LOG_FATAL("epoll_ctl delete fatal:%d\n", errno);
        }
    }
}