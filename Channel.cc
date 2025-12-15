#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd) 
    : loop_(loop)
    , fd_(fd) 
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{
}

Channel::~Channel()
{
    
}

// TcpConnection 底层绑定了一个channel, 
// TcpConnection 在底层对于这个 channel 设定了回调，但是比如channel这些回调 要用到对应的TcpConnection 对象
// 但是如果这个时候 TcpConnection 对象已经被删掉了怎么办？ 这个时候这个 tie 方法存在的意义就出现了
// 实际上我们去看 handle event 时候，会对于其提升锁，如果没有提升，就不执行了
// 这个tie方法 什么时候被调用过？   --------->    当一个TcpConnection 新连接创建的时候就会被调用了
void Channel::tie(const std::shared_ptr<void>& obj)
{
    tie_ = obj;
    tied_ = true;
}

/*
 * update 在相应的事件状态被更新后，去通知 poller也去更新
 * 因此这个是本质上去让 poller 去负责 做 epoll_ctl的
 * 但是channel 和 poller 都是归于 EventLoop 管的，所以这里的 update应该要让 loop去调用 poller相关的东西
*/
void Channel::update()
{
    // 通过channel所属的EventLoop调用 poller的相应方法，注册fd相关事件
    // 实际上 转到 loop那边， loop那边就是把这个活给 poller干了
    loop_->updateChannel(this);
}

// 同理，这里的Channel 也不能remove自己，所以也要通过自己所属的EventLoop 中去删除
void Channel::remove()
{
    loop_->removeChannel(this);
}

// 现在还不清楚这个tied 是怎么触发的？
void Channel::handleEvent(Timestamp receiveTime)
{
    std::shared_ptr<void> Gurad;
    if(tied_){
        Gurad = tie_.lock();
        if(Gurad){
            handleEventWithGurad(receiveTime);
        }
    }
    else{
        handleEventWithGurad(receiveTime);
    }


}

// 根据poller 通知Channel发生的具体事件来执行回调操作
void Channel::handleEventWithGurad(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents: %d\n", revents_);

    if(revents_ & EPOLLHUP && !(revents_ & EPOLLIN))
    // 虽然挂起了，但是还要检查缓冲区是否有数据还没有读完 
    {   
        if(closeCallback_){
            closeCallback_();
        }
    }

    if(revents_ & EPOLLERR)
    {
        if(errorCallback_)
        {
            errorCallback_();
        }
    }

    if(revents_ & (EPOLLIN | EPOLLPRI))
    {
        if(readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if(revents_ & EPOLLOUT)
    {
        if(writeCallback_)
        {
            writeCallback_();
        }
    }

}