#pragma once

#include "Poller.h"

#include <liburing.h>
#include <unordered_map>
#include <vector>

class IoUringPoller : public Poller
{
public:
    IoUringPoller(EventLoop* loop);
    ~IoUringPoller();

    // 重写 poller 基类抽象方法
    Timestamp poll(int timeoutMs, ChannelList* activateChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

private:
    struct io_uring ring_;
    static const uint32_t kQueueDepth = 256;

    void submitNoop();
    void fillActiveChannels(int count, ChannelList* activeChannels);

    std::unordered_map<int, Channel*> channels_;
};