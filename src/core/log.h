// scoot would - logging
#pragma once

#include <cstdarg>
#include <string>
#include <vector>
#include <functional>

namespace sw {

enum class LogLevel { Trace = 0, Info, Warning, Error, Critical };

struct LogLine {
    LogLevel level;
    double time;
    std::string text;
};

class Log {
public:
    static void init(const std::string& logFilePath);
    static void shutdown();
    static void write(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    static void writev(LogLevel level, const char* fmt, va_list args);
    static void setMinLevel(LogLevel level);
    // snapshot of the most recent lines (used by the developer console)
    static std::vector<LogLine> recent(size_t maxLines = 256);
    static const std::string& filePath();
    static void flush();
    static void installCrashHandler();
};

}  // namespace sw

#define LOG_TRACE(...) ::sw::Log::write(::sw::LogLevel::Trace, __VA_ARGS__)
#define LOG_INFO(...) ::sw::Log::write(::sw::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::sw::Log::write(::sw::LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::sw::Log::write(::sw::LogLevel::Error, __VA_ARGS__)
#define LOG_CRITICAL(...) ::sw::Log::write(::sw::LogLevel::Critical, __VA_ARGS__)
