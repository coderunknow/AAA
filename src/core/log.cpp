#include "core/log.h"

#include <cstdio>

namespace aaa {

void logMessage(LogLevel level, const char* fmt, ...) {
  static const char* kTags[] = {"debug", "info", "warn", "error"};
  FILE* out = level >= LogLevel::Warn ? stderr : stdout;
  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  std::fprintf(out, "[%s] %s\n", kTags[static_cast<int>(level)], buffer);
  std::fflush(out);
}

}  // namespace aaa
