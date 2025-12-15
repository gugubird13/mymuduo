#pragma once

#include <cstdint>
#include <string>

class Timestamp
{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);

    static Timestamp now();  // 获取当前时间
    std::string toString() const; //  获取当前以年月日形式的日期，返回类型string
private:
    int64_t microSecondsSinceEpoch_;

};