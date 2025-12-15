#include "Logger.h"
#include "Timestamp.h"

// static local， magic static
Logger& Logger::getinstance(){
    static Logger logger;
    return logger;
}

// 设置日志级别
void Logger::setLogLevel(int level){
    loglevel_ = level;
}

// 写日志: [级别] time : msg 
void Logger::log(std::string msg){
    switch (loglevel_)
    {
    case INFO:
        std::cout << "[INFO]";
        break;
    case ERROR:
        std::cout << "[INFO]";
        break;      
    case FATAL:
        std::cout << "[FATAL]";
        break;    
    case DEBUG:
        std::cout << "[DEBUG]";
        break;    

    default:
        break;
    }

    // 打印时间和msg
    std::cout << Timestamp::now().toString() << " : " << msg << std::endl;
}   