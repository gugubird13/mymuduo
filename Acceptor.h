#pragma once

#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

#include <functional>

class EventLoop;
class InetAddress;

// 这个主要就是封装了 listenfd
// 那么自然这个对应的成员属性有： 对应的 Channel、Poller、Loop； Poller 的操作单位是Channel，所以需要Channel 封装，Poller处理
// 新的事物的到达等等

// 所以对于一个 新的 连接连接成功了， 这里要先打包成一个 Channel，然后用 getNextLoop 唤醒 subloop 把这个Channel分发给这个Loop，去监听即可
// 那很明显，既然我Acceptor知道要在这个时候去让TCP server去唤醒loop，但是我Acceptor并不知道对应的唤醒的函数，所以这里要设定回调函数
class Acceptor : noncopyable::noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback& cb) { newConnectionCallback_ = cb; }

    bool listenning() const {return listenning_;}

    void listen();

private:
    void handleRead();

    EventLoop *loop_; // Acceptor 用的就是用户定义的那个baseloop ，也称为 mainloop
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listenning_;

};