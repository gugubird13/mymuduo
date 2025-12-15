#include "Buffer.h"

#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

/*
 * 从fd上面读取数据, Poller工作在LT模式, 也就是 fd上的数据没有读完，底层的poller会不停上报，好处就是数据不会丢失
 * Buffer 缓冲区是有大小的，但是从fd上面读数据的时候，却不知道Tcp数据最终的 大小的（流数据）
 */

ssize_t Buffer::readFd(int fd, int *savedErrno)
{
    char extrabuf[65536] = {0}; // 栈上的内存空间
    struct iovec vec[2];
    const size_t writeable = writeableBytes(); //剩余可写空间大小
    // 不一定够存储

    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len  = writeable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len  = sizeof extrabuf;

    // 当有足够空间的时候，我们就不往第二个缓冲区写东西了
    // 为什么要这样设计，因为如果空间不够，我们先往extrabuf里面写，然后整个移过去，不浪费一点buffer内部空间
    const int ivocnt = (writeable < sizeof extrabuf)? 2 : 1;
    const ssize_t n = ::readv(fd, vec, ivocnt);  // 这个函数能往不同的地址直接写入数据
    
    if(n < 0)
    {
        *savedErrno = errno;
    }
    else if(n <= writeable)
    {
        // 说明buffer本身的空间足够存
        writerIndex_ += n;
    }
    else{ // 说明extrabuffer也写入了数据
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writeable);
    }

    return n;
}  

ssize_t Buffer::writeFd(int fd, int *savedErrno)
{
    ssize_t n = ::write(fd, peek(), readabelBytes());
    if(n < 0)
    {
        *savedErrno = errno;
    }
    return n;
}