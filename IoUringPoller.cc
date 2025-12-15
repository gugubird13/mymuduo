// #include "IoUringPoller.h"
// #include "Logger.h"
// #include "Channel.h"

// namespace{
//     const int kNew = -1;    // 这个channel 还没有添加到poller
//     // channel 的成员 index_ 初始化也是 -1, 因此我们可以知道 Channel的这个index表示的就是在poller中的状态，是未添加还是已添加等等
//     const int kAdded = 1;   // 这个channel 在poller里面，并且 加入到了对应的 epoll里面
//     const int kDeleted = 2; // 这个channel 在poller里面，但是不在 epoll里面
// }

// IoUringPoller::IoUringPoller(EventLoop* loop)
//     : Poller(loop)
// {
//     // 初始化 io_uring
//     // IORING_SETUP_SQPOLL
//     // 0
//     if(io_uring_queue_init(kQueueDepth, &ring_, 0) != 0)
//     {
//         LOG_FATAL("IoUringPoller ctor queue_init failed");
//     }
// }

// IoUringPoller::~IoUringPoller()
// {
//     io_uring_queue_exit(&ring_);
// }

// // poller 统一接口，poll， 如果是epoll就是 epoll wait
// Timestamp IoUringPoller::poll(int timeoutMs, ChannelList* activateChannels)
// {
//     // LOG_DEBUG("IoUring In Curr Poller");
//     LOG_DEBUG("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());
//     if(activateChannels == nullptr)
//     {
//         return Timestamp::now();
//     }

//     // 根据 timeoutMs 判断是否需要设置 io_uring_wait_cqe 还是 io_uring_wait_cqe_timeout
//     // 1. 提交所有积压的 SQE (来自 updateChannel/removeChannel)
//     // 这一步至关重要，因为我们把 submit 从 updateChannel 移除了
//     int ret = io_uring_submit(&ring_);
//     if (ret < 0) {
//         LOG_ERROR("IoUringPoller::poll submit error: %d", -ret);
//     }

//     // 2. 准备超时结构体
//     struct io_uring_cqe* cqe_ptr = nullptr;
//     __kernel_timespec ts;
//     __kernel_timespec* ts_ptr = nullptr;

//     if (timeoutMs >= 0)
//     {
//         ts.tv_sec = timeoutMs / 1000;
//         ts.tv_nsec = (timeoutMs % 1000) * 1000 * 1000;
//         ts_ptr = &ts;
//     }

//     // 3. 等待 CQE
//     // 如果 timeoutMs < 0，ts_ptr 为 nullptr，表示无限等待
//     // 如果 timeoutMs >= 0，等待指定时间
//     ret = io_uring_wait_cqe_timeout(&ring_, &cqe_ptr, ts_ptr);

//     // 上述操作，如果错误，会返回错误码的负数
//     if(ret == -ETIME)
//     {
//         return Timestamp::now();
//     }
//     if(ret < 0)
//     {
//         LOG_ERROR("IoUringPoller::poll wait_cqe err=%d\n", -ret);
//         return Timestamp::now();
//     }

//     // 否则成功， 遍历所有完成的事件
//     unsigned int head;
//     unsigned int count = 0;
//     struct io_uring_cqe* cqe;

//     // 使用 liburing 提供的宏遍历 CQE
//     io_uring_for_each_cqe(&ring_, head, cqe)
//     {
//         Channel* channel = reinterpret_cast<Channel*>(io_uring_cqe_get_data(cqe));
        
//         if(channel)
//         {
//             int res = cqe->res;
//             // result code for this event
//             if(res == -ECANCELED)
//             {
//                 // 如果是 canceled 也算是 负的情况，所以这里要do nothing，并不是error
//                 // 不过也可以不去处理也行
//                 // Do nothing
//                 LOG_INFO("IO canceled already, regarded as cqe in uring\n");
//             }
//             else if(res >= 0)
//             {
//                 channel->set_revents(res);
//                 activateChannels->push_back(channel);
//             }
//             else{
//                 // 可能是被取消或者错误
//                 LOG_ERROR("IoUringPoller::poll cqe res error: %d fd: %d\n", res, channel->fd());
//             }
//         }
//         count++;
//     }

//     // 拿到这些队列后，就要提供标记，标记这些CQE 已经处理了
//     io_uring_cq_advance(&ring_, count);

//     return Timestamp::now();
// }

// void IoUringPoller::updateChannel(Channel* channel)
// {
//     // index 在 Poller 中通常用于标记这个 Channel在 Poller的 状态： kNew, kAdded, kDeleted
//     // 这里可以简化逻辑：如果是新通道或者已经删除的通道，则添加；如果是已经添加的，则修改（先删后加 或者使用 POLL_ADD_MULTI）

//     // io_uring 的 poll 通常是 oneshot 的，也就是 触发后，自动移除这个
//     // 但是muduo 的语义是持续监听
//     // 策略：
//     // 1. 每次poll成功触发了，需要重新提交poll请求 (muduo 架构在 Poller::poll 之后 处理事件) 
//     //  io_uring 的  POLL_ADD 触发一次后，就失效了，所以这里必须 确保 下一次poll还能收到事件
//     // 一种使用 IORING_POLL_ADD_MULTI  
//     // 如果不支持，就需要在每次事件触发后重新 arm

//     // 这里我们使用的是 io_uring 的新函数 io_uring_prep_poll_mulishot 来支持多次监听
//     const int index = channel->index();
//     struct io_uring_sqe* sqe;

//     LOG_INFO("func=%s => fd=%d events=%d index=%d\n",__FUNCTION__, channel->fd(), channel->events(), channel->index());
//     if(index == kNew || index == kDeleted)
//     {
//         int fd = channel->fd();
//         int events = channel->events();
//         channels_[channel->fd()] = channel;
//         sqe = io_uring_get_sqe(&ring_);
//         if(sqe)
//         {
//             // 使用multishot 支持持续触发 不需要每次 poll后重新 arm
//             io_uring_prep_poll_multishot(sqe, fd, events);
//             // 设置user_data 为channel 指针，以便在 poll 中找回
//             io_uring_sqe_set_data(sqe, channel);
//             // io_uring_submit(&ring_);

//             channel->set_index(kAdded);
//         }
//         else
//         {
//             LOG_ERROR("IoUringPoller::updateChannel get sqe failed");
//         }
//     }
//     else{
//         // == kAdded ,表示channel已经在poller上面注册过了
//         int fd = channel->fd();

//         if(channel->isNoneEvent())
//         {
//             sqe = io_uring_get_sqe(&ring_);
//             // 通过 user_data 的 channel指针来找到并移除之前的poll请求
//             io_uring_prep_poll_remove(sqe, channel);
//             // 移除操作本身的 completion 我们不关系 ，设为 nullptr
//             io_uring_sqe_set_data(sqe, nullptr);
//             channel->set_index(kDeleted);
//         }
//         else{
//             sqe = io_uring_get_sqe(&ring_);
//             if(sqe)
//             {
//                 // 第四个标识标识仅仅更新事件掩码
//                 io_uring_prep_poll_update(sqe, channel, channel, channel->events(), IORING_POLL_UPDATE_EVENTS);

//                 // 由于 update 操作本身也会产生一个cqe，标识是否成功
//                 // 我们将其user_data 也设定为 nullptr 这样在 poll 循环中就会被忽略
//                 // 避免把 update 的结果误判为 IO事件
//                 io_uring_sqe_set_data(sqe, nullptr);

//                 // io_uring_submit(&ring_);
//             }
//         }
//     }
// }

// void IoUringPoller::removeChannel(Channel* channel)
// {
//     int index = channel->index();
//     int fd = channel->fd();
//     channels_.erase(fd);
//     LOG_INFO("func=%s => fd=%d events=%d index=%d\n",__FUNCTION__, channel->fd(), channel->events(), channel->index());
    
//     if(index == kAdded)
//     {
//         struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
//         if(sqe)
//         {
//             // 移除 poll
//             io_uring_prep_poll_remove(sqe, channel);
//             io_uring_sqe_set_data(sqe, nullptr);
//             io_uring_submit(&ring_);
//         }
//         channel->set_index(kNew);
//     }
// }

// void IoUringPoller::submitNoop()
// {
//     // 提交一个空操作，通常用于唤醒 poller
//     struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
//     if(sqe)
//     {
//         io_uring_prep_nop(sqe);
//         io_uring_submit(&ring_);
//     }
// }

// void IoUringPoller::fillActiveChannels(int count, ChannelList* activeChannels)
// {
//     // 这个函数在 EpollPoller 中用于将 epoll_event 转换为 Channel
//     // 在上面的 poll() 实现中，我们已经直接在遍历 CQE 时填充了 activeChannels
//     // 所以这里可以留空，或者将 poll 中的逻辑移动到这里。
//     // 为了符合 muduo 接口习惯，通常 poll() 负责获取事件，fillActiveChannels 负责转换。
//     // 但 io_uring 的遍历逻辑紧耦合在 poll 中比较方便。
// }

#include "IoUringPoller.h"
#include "Logger.h"
#include "Channel.h"
#include <unordered_map>
#include <memory>

// ==========================================
// 核心修复：引入 Token 机制解决生命周期竞态
// ==========================================
struct PollToken {
    Channel* channel; // 原始 Channel 指针
    int fd;           // 【新增】保存 fd，因为 channel 可能被置空
    bool removed;     // 标记是否已请求移除
    PollToken(Channel* c) : channel(c), fd(c->fd()), removed(false) {}
};

// 全局 Token 映射表
static std::unordered_map<int, std::shared_ptr<PollToken>> g_tokens;

namespace{
    const int kNew = -1;
    const int kAdded = 1;
    const int kDeleted = 2;
}

IoUringPoller::IoUringPoller(EventLoop* loop)
    : Poller(loop)
{
    if(io_uring_queue_init(kQueueDepth, &ring_, 0) != 0)
    {
        LOG_FATAL("IoUringPoller ctor queue_init failed");
    }
}

IoUringPoller::~IoUringPoller()
{
    io_uring_queue_exit(&ring_);
    g_tokens.clear();
}

Timestamp IoUringPoller::poll(int timeoutMs, ChannelList* activateChannels)
{
    if(activateChannels == nullptr) return Timestamp::now();

    int ret = io_uring_submit(&ring_);
    if (ret < 0) {
        LOG_ERROR("IoUringPoller::poll submit error: %d", -ret);
    }

    struct io_uring_cqe* cqe_ptr = nullptr;
    __kernel_timespec ts;
    __kernel_timespec* ts_ptr = nullptr;

    if (timeoutMs >= 0)
    {
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000 * 1000;
        ts_ptr = &ts;
    }

    ret = io_uring_wait_cqe_timeout(&ring_, &cqe_ptr, ts_ptr);

    if(ret == -ETIME) return Timestamp::now();
    if(ret < 0 && ret != -EINTR)
    {
        LOG_ERROR("IoUringPoller::poll wait_cqe err=%d\n", -ret);
        return Timestamp::now();
    }

    unsigned int head;
    unsigned int count = 0;
    struct io_uring_cqe* cqe;

    io_uring_for_each_cqe(&ring_, head, cqe)
    {
        count++;
        PollToken* token = reinterpret_cast<PollToken*>(io_uring_cqe_get_data(cqe));
        
        if(!token) continue;

        int res = cqe->res;

        // 【关键修复】
        if (token->removed)
        {
            // 使用 token->fd 而不是 token->channel->fd()
            // 因为此时 token->channel 已经是 nullptr 了
            g_tokens.erase(token->fd);
            continue; 
        }

        if(res == -ECANCELED)
        {
            // 忽略
        }
        else if(res >= 0)
        {
            if (token->channel) {
                token->channel->set_revents(res);
                activateChannels->push_back(token->channel);
            }
        }
        else{
            LOG_ERROR("IoUringPoller cqe error: %d", res);
        }
    }

    io_uring_cq_advance(&ring_, count);
    return Timestamp::now();
}

void IoUringPoller::updateChannel(Channel* channel)
{
    const int index = channel->index();
    struct io_uring_sqe* sqe;
    int fd = channel->fd();

    if(index == kNew || index == kDeleted)
    {
        auto token = std::make_shared<PollToken>(channel);
        g_tokens[fd] = token;

        channels_[fd] = channel;
        sqe = io_uring_get_sqe(&ring_);
        if(sqe)
        {
            io_uring_prep_poll_multishot(sqe, fd, channel->events());
            io_uring_sqe_set_data(sqe, token.get());
            channel->set_index(kAdded);
        }
        else
        {
            io_uring_submit(&ring_);
            sqe = io_uring_get_sqe(&ring_);
            if(sqe) {
                io_uring_prep_poll_multishot(sqe, fd, channel->events());
                io_uring_sqe_set_data(sqe, token.get());
                channel->set_index(kAdded);
            }
        }
    }
    else
    {
        auto it = g_tokens.find(fd);
        PollToken* token = (it != g_tokens.end()) ? it->second.get() : nullptr;

        if(channel->isNoneEvent())
        {
            sqe = io_uring_get_sqe(&ring_);
            if(sqe)
            {
                // 【关键】显式转换为 __u64
                io_uring_prep_poll_remove(sqe, token);
                io_uring_sqe_set_data(sqe, token);
            }
            if(token) token->removed = true;
            channel->set_index(kDeleted);
        }
        else
        {
            sqe = io_uring_get_sqe(&ring_);
            if(sqe)
            {
                // 【关键】显式转换为 __u64
                io_uring_prep_poll_update(sqe, token, token, channel->events(), IORING_POLL_UPDATE_EVENTS);
                io_uring_sqe_set_data(sqe, nullptr);
            }
        }
    }
}

void IoUringPoller::removeChannel(Channel* channel)
{
    int fd = channel->fd();
    
    auto it = g_tokens.find(fd);
    PollToken* token = (it != g_tokens.end()) ? it->second.get() : nullptr;
    
    if(token) {
        token->removed = true;
        token->channel = nullptr; // 断开连接
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if(sqe)
    {
        // 【关键】显式转换为 __u64
        io_uring_prep_poll_remove(sqe, token);
        io_uring_sqe_set_data(sqe, token);
    }
    
    channels_.erase(fd);
    channel->set_index(kNew);
}

void IoUringPoller::submitNoop()
{
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if(sqe)
    {
        io_uring_prep_nop(sqe);
        io_uring_submit(&ring_);
    }
}

void IoUringPoller::fillActiveChannels(int count, ChannelList* activeChannels)
{
}