#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <strings.h>
#include <functional>

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainloop is null! \n",__FILE__, __FUNCTION__, __LINE__ );
    }
    return loop;
}
    

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string& nameArg, Option option)
    : loop_(CheckLoopNotNull(loop))
    , ipPort_(listenAddr.toIpPort())
    , name_(nameArg)
    , acceptor_(new Acceptor(loop, listenAddr, option==kReusePort))
    , threadPool_(new EventLoopThreadPool(loop, name_))
    , connectionCallback_()
    , messageCallback_()
    , nextConnId_(1) 
    , started_(0)
{
    // 当有新用户连接时，会执行TcpServer::newConnection 执行回调
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this
                                    , std::placeholders::_1, std::placeholders::_2));

}  

TcpServer::~TcpServer()
{
    for(auto& item : connections_)
    {
        TcpConnectionPtr conn(item.second);
        item.second.reset();
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
    // 出了上面这个 } conn就被自动析构了，很好的设计
    // 这里一定是conn是最后一个引用计数了，因为下面一行使用 reset 了
}

void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
    if(started_++ == 0)
    {
        // 防止一个TcpServer对象被start多次
        threadPool_->start(threadInitCallback_);
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get())); // 主loop直接执行这个回调
    }
}

// 这里是 acceptor 拿到一个新的连接了，处理 TcpServer给 acceptor的回调函数
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // mainloop 干的就是根据轮询算法来选择、唤醒subloop，把当前connfd封装成相应的Channel 分发给subloop(在此之前一定要处理回调)
    EventLoop *ioloop = threadPool_->getNextLoop();
    // 这里每一个loop里面都在阻塞着 都在对应的poller里面 poll着
    
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_; // 该变量只在mainloop里面才处理，所以不用 原子变量

    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConnection [%s] - new Connection [%s] from %s \n",
        name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口号
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    if(::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }

    InetAddress localAddr(local);
 
    // 根据连接成功
    TcpConnectionPtr conn(new TcpConnection(
                        ioloop,
                        connName,
                        sockfd,
                        localAddr,
                        peerAddr));
    connections_[connName] = conn;
    
    // 下面的回调都是用户设置给TcpServer-> TcpConnection ->Channel->Poller->notify Channel 调用回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
    
    // 直接调用
    ioloop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn)); 
    
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s \n", name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());
    EventLoop *ioloop = conn->getLoop();
    ioloop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}