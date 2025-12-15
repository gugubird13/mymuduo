#pragma once 

#include <aio.h>
#include <vector>
#include <string>
#include <algorithm>

class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend) // 初始化这俩指向的是同一个地方
    {}

    size_t readabelBytes() const
    {
        return writerIndex_ - readerIndex_;    
    }

    size_t writeableBytes() const // 底层缓冲区可写的缓冲区大小(从 socket里面拿到的有多少可以放到缓冲区的)
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    const char* peek() const{
        return begin() + readerIndex_; // 返回缓冲区中可读数据的起始地址
    }

    // onMessage 有数据到来的时候，就会把数据从buffer 拿到，变成string类型，经常是一些数据格式
    void retrieve(size_t len)
    {
        if(len < readabelBytes())
        {
            // 应用只读取了可读缓冲区的一部分 就是len长度
            readerIndex_ += len;
        }
        else{ // len == readabelBtyes()
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 把 onMessage 函数上报的Buffer数据，转成string 类型的数据返回
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readabelBytes()); // 应用可读取数据的长度
    }

    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len); // 上面一句已经把缓冲区中可读的数据已经读取出来了，这里肯定要对缓冲区进行复位操作
        return result;
    }

    // buffer_.size() - writerIndex_
    void ensureWriteableBytes(size_t len)
    {
        // 如果我想写的内容长度要大于剩余的可写空间，那么就要扩容，否则就可以直接写
        if(writeableBytes() < len)
        {
            makeSpace(len);
        }
    }

    void append(const char *data, size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data, data+len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const{
        return begin()+writerIndex_;
    }
    
    // 从fd上面读取数据
    ssize_t readFd(int fd, int *savedErrno);
    ssize_t writeFd(int fd, int *savedErrno);

private:
    char* begin()
    {
        return &*buffer_.begin();
    }
    // 拿到vector 底层数组的裸指针

    const char* begin() const
    {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len)
    {
        /*
         kCheapPrepend    |      reader      |      writer      |
         kCheapPrepend    |                  len                        |
         *
        */
        if(writeableBytes() + prependableBytes() < len + kCheapPrepend)
        // 这里说明即使 readerIndex_ 挪动了位置了，但是怎么挤都挤不出位置了
        // 方便理解，可以把 kCheapPrepend 挪到不等式左边
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            // 否则我们就做一次挪动
            size_t readable = readabelBytes();
            // 把从 readerIndex_ 开始这么多长度的内容往前面挪一挪

            std::copy(begin()+readerIndex_, begin()+writerIndex_, begin()+kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};