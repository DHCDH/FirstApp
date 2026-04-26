#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <cstdio>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

// 颜色定义
#define LOG_CLR_RESET "\033[0m"
#define LOG_CLR_GREEN "\033[32m"
#define LOG_CLR_BLUE "\033[34m"
#define LOG_CLR_RED "\033[31m"

class SimpleLogger
{
public:
    template <typename... Args>
    static void log(const char* level, const char* color, const char* func, int line,
                    const char* format, Args... args)
    {
        // 1. 获取时间
        std::time_t now = std::time(nullptr);
        char time_str[20];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&now));

        // 2. 格式化消息内容
        char content_buf[1024];
        std::snprintf(content_buf, sizeof(content_buf), format, args...);

        // 3. 打印到控制台 (级别放行首，带颜色)
        // 格式：[DEBUG] [时间] [函数:行号] 内容
        std::printf("[%s%s%s] [%s] [%s:%d] %s\n",
                    color,
                    level,
                    LOG_CLR_RESET,
                    time_str,
                    func,
                    line,
                    content_buf);
        std::fflush(stdout);

#ifdef _WIN32
        // 4. 打印到 Visual Studio 输出窗口 (级别放行首，纯文本)
        char vs_output_buf[2048];
        std::snprintf(vs_output_buf,
                      sizeof(vs_output_buf),
                      "[%s] [%s] [%s:%d] %s\n",
                      level,
                      time_str,
                      func,
                      line,
                      content_buf);
        OutputDebugStringA(vs_output_buf);
#endif
    }
};

#define DEBUG(format, ...)           \
    SimpleLogger::log("DEBUG",       \
                      LOG_CLR_GREEN, \
                      __FUNCTION__,  \
                      __LINE__,      \
                      format,        \
                      ##__VA_ARGS__)

#define INFO(format, ...)           \
    SimpleLogger::log("INFO",      \
                      LOG_CLR_BLUE, \
                      __FUNCTION__, \
                      __LINE__,     \
                      format,       \
                      ##__VA_ARGS__)

#define ERROR(format, ...) \
    SimpleLogger::log("ERROR", LOG_CLR_RED, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)

#endif