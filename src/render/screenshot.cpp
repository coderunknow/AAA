#include "render/screenshot.h"

#include <bimg/bimg.h>
#include <bx/file.h>

#include "core/log.h"

namespace aaa {

void ScreenshotCallback::fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) {
  AAA_LOG_ERROR("bgfx fatal %d at %s:%u: %s", static_cast<int>(code), filePath ? filePath : "?", line,
                str ? str : "(no message)");
}

void ScreenshotCallback::traceVargs(const char*, uint16_t, const char*, va_list) {}
void ScreenshotCallback::profilerBegin(const char*, uint32_t, const char*, uint16_t) {}
void ScreenshotCallback::profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) {}
void ScreenshotCallback::profilerEnd() {}
uint32_t ScreenshotCallback::cacheReadSize(uint64_t) { return 0; }
bool ScreenshotCallback::cacheRead(uint64_t, void*, uint32_t) { return false; }
void ScreenshotCallback::cacheWrite(uint64_t, const void*, uint32_t) {}

// Video capture is not used by the game (unused arguments kept for the interface).
void ScreenshotCallback::captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) {}
void ScreenshotCallback::captureEnd() {}
void ScreenshotCallback::captureFrame(const void*, uint32_t) {}

void ScreenshotCallback::screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                                    bgfx::TextureFormat::Enum format, const void* data, uint32_t /*size*/,
                                    bool yflip) {
  bool ok = false;
  if (filePath && data && width > 0 && height > 0) {
    bx::FileWriter writer;
    bx::Error err;
    if (bx::open(&writer, filePath, false, &err)) {
      // Screenshot data is always 4-byte BGRA (bgfx docs).
      ok = bimg::imageWritePng(&writer, width, height, pitch, data, bimg::TextureFormat::Enum(format), yflip, &err) >
               0 &&
           err.isOk();
      bx::close(&writer);
    }
  }
  AAA_LOG_INFO("screenshot %s: %s", ok ? "written" : "FAILED", filePath ? filePath : "(null)");
  if (listener_) listener_(filePath ? filePath : "", ok);
}

}  // namespace aaa
