#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"
#include "Socket.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;      // 保存了客户端连接的相关数据, 但是还是打包成Channel给它

// 用于打包成功连接的一条通信链路的

// 之前我们写到的 TcpServer 拿到了一条connection fd，分配subloop，实际上就是封装成了 TcpConnection 这个类别
// 说明这个代表已经建立连接的一条客户端的链路

/*
 * TcpServer 通过 acceptor 对应的 accept函数拿到connfd，然后就可以打包TcpConnection 函数，设置回调，让TcpConnection 给 Channel设置这个回调
 * 然后对应的Poller就能根据Channel 的通知事件去调用Channel对应的 回调函数了，handleEventWithGuard_
*/

class TcpConnection : noncopyable::noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const {return loop_;}
    const std::string &name() const {return name_;}
    const InetAddress& localAddress() const {return localAddr_; }; 
    const InetAddress& peerAddress() const {return peerAddr_; };

    bool connected() const {return state_ == kConnected; }
    
    // 发送数据
    void send(const std::string &buf);
    // 关闭连接
    void shutdown();

    void setConnectionCallback(const ConnectionCallback &cb)
    { connectionCallback_ = cb; }

    void setMessageCallback(const MessageCallback &cb)
    {messageCallback_ = cb;}

    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    {writeCompleteCallback_ = cb;}

    void setHighWaterCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    {highWaterMarkCallback_ = cb, highWaterMark_ = highWaterMark;}

    void setCloseCallback(const CloseCallback &cb)
    {closeCallback_ = cb;}
    // 建立连接
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

private:
    enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting};
    void setState(StateE state) { state_ = state;}

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* data, size_t len);
    void shutDownInLoop();

    EventLoop *loop_; // 这个不是baseloop, 因为TcpConnection 都是在subloop里面管理的
    const std::string name_;

    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;
    MessageCallback    messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    CloseCallback closeCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;

    size_t highWaterMark_;
    Buffer inputBuffer_;    // 接收数据的缓冲区
    Buffer outputBuffer_;   // 发送数据的缓冲区
};