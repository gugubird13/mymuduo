#pragma once

/*
 * 让派生类对象无法进行拷贝构造和赋值；
 * 可以进行正常的构造和析构
*/

namespace noncopyable{

class noncopyable{
public:
    noncopyable(const noncopyable &) = delete;  
    noncopyable& operator= (const noncopyable &) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};

}