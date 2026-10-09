#pragma once
// Window + event pump (SDL3). The only module that talks to the OS windowing system.
#include <SDL3/SDL.h>

#include <cstdint>

#include "platform/input_mapper.h"

namespace aaa {

struct NativeHandles {
  void* window = nullptr;
  void* display = nullptr;
};

class Platform {
 public:
  bool init(const char* title, int width, int height, bool headless);
  void shutdown();

  // Feed one SDL event (called from SDL_AppEvent). Returns false on quit request.
  bool handleEvent(const SDL_Event& e);
  // Builds this frame's InputFrame (and clears edge-triggered state).
  InputFrame takeInput();

  NativeHandles nativeHandles() const;
  void pixelSize(uint32_t& w, uint32_t& h) const;
  bool resized() { const bool r = resized_; resized_ = false; return r; }
  bool focused() const { return focused_; }
  void setPointerLock(bool on);
  bool pointerLocked() const;
  SDL_Window* window() const { return window_; }
  bool headless() const { return headless_; }
  // In-engine UI support: absolute mouse position in framebuffer pixels, the latched
  // left-click edge, the user input settings, and fullscreen toggling (native).
  Vec2 mousePositionPixels() const;
  bool takeMouseClick();
  InputSettings& inputSettings() { return input_.settings(); }
  void setFullscreen(bool on);
  bool fullscreen() const;

 private:
  SDL_Window* window_ = nullptr;
  bool headless_ = false;
  bool resized_ = false;
  bool focused_ = true;
  bool wantLock_ = false;
  bool wasLocked_ = false;
  int lookSettleFrames_ = 0;  // ignore look input briefly after pointer-lock changes
  InputMapper input_;
};

}  // namespace aaa
