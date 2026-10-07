#pragma once
// Minimal levelled logging. On the web build stdout/stderr go to the browser console.
#include <cstdarg>

namespace aaa {

enum class LogLevel { Debug, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace aaa

#define AAA_LOG_DEBUG(...) ::aaa::logMessage(::aaa::LogLevel::Debug, __VA_ARGS__)
#define AAA_LOG_INFO(...)  ::aaa::logMessage(::aaa::LogLevel::Info, __VA_ARGS__)
#define AAA_LOG_WARN(...)  ::aaa::logMessage(::aaa::LogLevel::Warn, __VA_ARGS__)
#define AAA_LOG_ERROR(...) ::aaa::logMessage(::aaa::LogLevel::Error, __VA_ARGS__)
