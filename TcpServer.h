#pragma once

/*
 * 用户使用 muduo 编写服务器程序
*/
#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

// 对外的服务器编程使用的类别
class TcpServer : noncopyable::noncopyable
{
public:
    // 想一想这个类别声明这个回调函数，具体是给谁设置的？
    using ThreadInitCallback  = std::function<void(EventLoop*)>;

    enum Option{
        kNoReusePort,
        kReusePort,
    };

    TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string& nameArg, Option option = kNoReusePort);
    ~TcpServer();

    void setThreadInitCallback(const ThreadInitCallback& cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // 设置底层的 subloop 的个数
    void setThreadNum(int numThreads);
    
    // 开启服务器监听, 就是开启底层的 mainloop 的Acceptor的 listen的
    void start();

private:
    void newConnection(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn); // 肯定是从 map 里面移除
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_; // baseLoop 用户定义的loop, 用户自己传给这个TCP server
    const std::string ipPort_; // 服务器相关名称等等
    const std::string name_;

    std::unique_ptr<Acceptor> acceptor_; // main loop 的 acceptor
    std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread
    
    // 用户使用 TCP 的一些 callback, 具体的 callback 需要用户去定义
    ConnectionCallback connectionCallback_; // 有新连接时候的回调
    MessageCallback messageCallback_;       // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调

    ThreadInitCallback threadInitCallback_; // loop 线程初始化的回调, 给 EventLoopThread 的callback_ 赋值的
    std::atomic_int started_;

    int nextConnId_;
    ConnectionMap connections_;
};