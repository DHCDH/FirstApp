#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <cstdio>
#include <ctime>
#include <iostream>

// 定义 ANSI 颜色转义码
#define LOG_CLR_RESET "\033[0m"
#define LOG_CLR_GREEN "\033[32m"
#define LOG_CLR_CYAN "\033[36m"
#define LOG_CLR_RED "\033[31m"

class SimpleLogger
{
public:
    template <typename... Args>
    static void log(const char* level, const char* color, const char* func, int line,
                    const char* format, Args... args)
    {
        // 1. 获取当前时间
        std::time_t now = std::time(nullptr);
        char time_str[20];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&now));

        // 2. 打印 [时间] [颜色级别] [函数:行数] 用户内容
        // %s%s%s 分别对应：颜色代码、级别文字、重置颜色
        std::printf("[%s] [%s%s%s] [%s:%d] ",
                    time_str,
                    color,
                    level,
                    LOG_CLR_RESET,
                    func,
                    line);

        // 3. 打印消息主体
        std::printf(format, args...);
        std::printf("\n");
        std::fflush(stdout);
    }
};

// 核心宏：通过参数将对应的颜色传给 log 函数
#define DEBUG(format, ...)           \
    SimpleLogger::log("DEBUG",       \
                      LOG_CLR_GREEN, \
                      __FUNCTION__,  \
                      __LINE__,      \
                      format,        \
                      ##__VA_ARGS__)

#define INFO(format, ...)           \
    SimpleLogger::log("INFO ",      \
                      LOG_CLR_CYAN, \
                      __FUNCTION__, \
                      __LINE__,     \
                      format,       \
                      ##__VA_ARGS__)

#define ERROR(format, ...) \
    SimpleLogger::log("ERROR", LOG_CLR_RED, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#endif