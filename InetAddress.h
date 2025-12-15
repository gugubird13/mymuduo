#pragma once

#include <netinet/in.h>
#include <string>
#include <arpa/inet.h>

// 封装socket 地址类型
class InetAddress
{
public:
    // 构造函数定义 explicit 让其防止进行隐式转换，要求显示调用
    // 给这个两个参数都设置一个默认参数，以达到默认构造的效果
    explicit InetAddress(uint16_t port=0, std::string ip = "127.0.0.1");
    explicit InetAddress(const sockaddr_in &addr): addr_(addr) {}

    void setSockAddr(const sockaddr_in &addr) { addr_ = addr; }
    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const;

    const sockaddr_in* getSockAddr() const {return &addr_;}
private:
    sockaddr_in addr_;
};