#pragma once
// bgfx callback that writes requested backbuffer screenshots to PNG files.
// Used by the CI render verification (PROMPT §9.8: --screenshot out.png).
#include <bgfx/bgfx.h>

#include <cstdarg>
#include <cstdint>
#include <functional>
#include <string>

namespace aaa {

class ScreenshotCallback : public bgfx::CallbackI {
 public:
  // Fires (on the render thread) once a screenshot file has been written.
  using Listener = std::function<void(const std::string& path, bool ok)>;
  void setListener(Listener l) { listener_ = std::move(l); }

  void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override;
  void traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) override;
  void profilerBegin(const char* name, uint32_t abgr, const char* filePath, uint16_t line) override;
  void profilerBeginLiteral(const char* name, uint32_t abgr, const char* filePath, uint16_t line) override;
  void profilerEnd() override;
  uint32_t cacheReadSize(uint64_t id) override;
  bool cacheRead(uint64_t id, void* data, uint32_t size) override;
  void cacheWrite(uint64_t id, const void* data, uint32_t size) override;
  void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                  bgfx::TextureFormat::Enum format, const void* data, uint32_t size, bool yflip) override;
  void captureBegin(uint32_t width, uint32_t height, uint32_t pitch, bgfx::TextureFormat::Enum format,
                    bool yflip) override;
  void captureEnd() override;
  void captureFrame(const void* data, uint32_t size) override;

 private:
  Listener listener_;
};

}  // namespace aaa
