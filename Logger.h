#pragma once

#include <string>
#include <iostream>
#include "noncopyable.h"

// 定义日志的级别 INFO ERROR FATAL DEBUG
enum LogLevel
{
    INFO, // 普通信息
    ERROR, // 错误信息
    FATAL, // core 信息
    DEBUG, // 调试信息
};

// LOG_INFO("%s %d", arg1, agr2)
#define LOG_INFO(LogmsgFormat, ...)\
    do \
    { \
        Logger &logger = Logger::getinstance(); \
        logger.setLogLevel(INFO); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.log(buf); \
    } while(0)

#define LOG_ERROR(LogmsgFormat, ...)\
    do \
    { \
        Logger &logger = Logger::getinstance(); \
        logger.setLogLevel(ERROR); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.log(buf); \
    } while(0)

#define LOG_FATAL(LogmsgFormat, ...)\
    do \
    { \
        Logger &logger = Logger::getinstance(); \
        logger.setLogLevel(FATAL); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.log(buf); \
        exit(-1); \
    } while(0)

#ifdef MUDEBUG 
#define LOG_DEBUG(LogmsgFormat, ...)\
    do \
    { \
        Logger &logger = Logger::getinstance(); \
        logger.setLogLevel(DEBUG); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.log(buf); \
    } while(0)
#else 
    #define LOG_DEBUG(LogmsgFormat, ...)
#endif

// 输出一个日志类
class Logger : noncopyable::noncopyable
{
public:
    // 单例模式，获取唯一的实例对象
    static Logger& getinstance();
    
    // 设置日志级别
    void setLogLevel(int level);

    // 写日志
    void log(std::string msg);

private:
    int loglevel_;
    Logger(){}
};