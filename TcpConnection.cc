#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <memory>
#include <errno.h>
#include <string>

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection is null! \n",__FILE__, __FUNCTION__, __LINE__ );
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop* loop, 
                            const std::string &nameArg,
                            int sockfd, 
                            const InetAddress &localAddr, const InetAddress &peerAddr)
                            : loop_(CheckLoopNotNull(loop))
                            , name_(nameArg)
                            , state_(kConnecting)
                            , reading_(true)
                            , socket_(new Socket(sockfd))
                            , channel_(new Channel(loop, sockfd))
                            , localAddr_(localAddr)
                            , peerAddr_(peerAddr)
                            , highWaterMark_(64 * 1024 * 1024)  // 64M
{
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true); // 启动 socket的保活机制
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::ctor[%s] at fd=%d state=%d \n", name_.c_str(), channel_->fd(), (int)state_);
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    // Channel 上fd有数据可读了, Connection 想让 channel做的事情
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if(n > 0)
    {
        // 说明有数据了
        // 调用用户传入的回调操作 onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if(n == 0)
    {
        // 断开了，客户端断开了
        handleClose();
    }
    else{
        // 出错了
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if(channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if(n > 0)
        {
            outputBuffer_.retrieve(n); // 说明n个数组已经处理过了
            if(outputBuffer_.readabelBytes() == 0)
            {
                channel_->disableWriting(); // 数据写完了
                if(writeCompleteCallback_)
                {
                    // 说明当前这个事情不紧急，可以放到队列里面，仅此而已
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                } 
                if( state_ == kDisconnecting)
                {
                    shutDownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing\n", channel_->fd());
    }
}

void TcpConnection::handleClose()
{   
    LOG_INFO("fd=%d state=%d \n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll(); // channel对所有事件都不感兴趣了

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);
    closeCallback_(connPtr);
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int error = 0;
    if(::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0){
        error = errno;
    }
    else{
        error = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), error);
}

void TcpConnection::send(const std::string &buf)
{
    if(state_ == kConnected)
    {
        if(loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else{
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop,
                            this, buf.c_str(), buf.size()));
        }
    }
}


// 这个函数我的理解就是： 先看看能不能直接同步写，不能同步写，就放到 应用缓冲区里面，等事件驱动（内核通知有缓冲区可用）
// 再调用 给channel 设定好的 handlewrite函数，handlewrite函数实现是从 用户缓冲区写到 fd对应的缓冲区里面，也就是内核缓冲区 
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if(state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }

    // 说明 channel  第一次开始写数据，输出缓冲区是空的
    // 那么尝试直接同步写，而不是放到 queueInLoop()
    if(!channel_->isWriting() && outputBuffer_.readabelBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if(nwrote > 0)
        {
            remaining = len - nwrote;
            if(remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
            // 既然在这里一次性数据发送完成，就不用再给Channel设置 epollout事件了
        }
        else{ // nwrote < 0
            nwrote = 0;
            if(errno != EWOULDBLOCK) // 由于非阻塞，没有数据写入，这个是正常现象，缓冲区准备好了但是应用的缓冲区没有数据
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if(errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中
    // 然后再给channel 注册 epollout 事件，poller（LT模式）发现Tcp发送缓冲区有空间，会通知相应的sock->channel 调用 writeCallback_
    // 也就是调用 TcpConnection::handleWrite方法，把发送缓冲区的数据全部发送完成
    if(!faultError && remaining > 0)
    {
        // 这里就要把没有发送完的数据放到缓冲区里面
        size_t oldLen = outputBuffer_.readabelBytes();
        if( oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {     
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        // 把待发送数据添加到缓冲区
        outputBuffer_.append((char *)data + nwrote, remaining);
        if(!channel_->isWriting())
        {
            channel_->enableWriting(); // 一定要注意channel的写事件，否则poller不会给channel 通知 这个 epollout信号
        }
    }
}

// 很明显下面这俩方法就是回调方法
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();  // 向poller注册channel的 epollin 事件
    
    // 新连接建立，执行回调
    // 这种回调立即调用，说明已经在当前的IO线程里面了
    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{   
    if(state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 从poller中delete掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove();
}

////////////////////////////////////////////

// 考虑当前loop 关闭连接
void TcpConnection::shutdown()
{
    if(state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutDownInLoop, this));
    }
}

void TcpConnection::shutDownInLoop()
{
    if( !channel_->isWriting()) // 说明outputBuffer中的数据已经全部发送完成
    {
        socket_->shutdownWrite();
        // 设置完 shutdownWrite()  之后，对应的 channel就会执行 closeCallback_ 函数
        // 这里 并不需要 设置 poller对 EPOLLHUP，应为其底层本身就会关注这个事情
        // 所以只需要执行socket对应的  shutdown 方法就行了
    }
}