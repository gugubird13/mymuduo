#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

// 创建一个非阻塞IO
static int createNonblockingOrDie()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create error: %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblockingOrDie())   // 1. 这里相当于在做 socket
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    // 先设置一些属性
    // 然后开始绑定
    acceptSocket_.bindAddress(listenAddr);      // 2. 这里相当于bind 
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this)); // 这个baseloop监听到有事件发生了，这个Channel就调用这个回调
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll(); // 不再去向poller里面注册读写事件了，自己本身再从poller里面删除掉
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading(); // 向poller里面注册
}

void Acceptor::handleRead()
{
    // 在channel 收到 listenfd 有事件发生了时候（即新用户连接）， 就会用这个函数
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if(connfd >= 0)
    {
        // 说明就成功了，需要执行回调了
        if(newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr); // 轮询找到subLoop, 唤醒，分发当前的新客户端的Channel
        }
        else{ // 说明出错了
            ::close(connfd);
        }
    }
    else{
        LOG_ERROR("%s:%s:%d accept error: %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        if( errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }

}