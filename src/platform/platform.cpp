#include "platform/platform.h"

#include "core/log.h"

namespace aaa {

bool Platform::init(const char* title, int width, int height, bool headless) {
  headless_ = headless;
  SDL_SetHint(SDL_HINT_APP_NAME, title);
#if defined(__EMSCRIPTEN__)
  // Keep keyboard events on the canvas so browser shortcuts (F5, Ctrl+L) still work elsewhere.
  SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
  if (headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_EVENTS)) {
      AAA_LOG_ERROR("SDL_Init(events) failed: %s", SDL_GetError());
      return false;
    }
    return true;
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
    AAA_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  // No SDL_WINDOW_FILL_DOCUMENT: it re-parents every other DOM element into a hidden div,
  // which would hide the HTML loading / error overlay. The page sizes the canvas with CSS.
  const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  window_ = SDL_CreateWindow(title, width, height, flags);
  if (!window_) {
    AAA_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  input_.init();
  return true;
}

void Platform::shutdown() {
  input_.shutdown();
  if (window_) SDL_DestroyWindow(window_);
  window_ = nullptr;
  SDL_Quit();
}

NativeHandles Platform::nativeHandles() const {
  NativeHandles h;
#if defined(__EMSCRIPTEN__)
  h.window = const_cast<char*>("#canvas");
#else
  if (!window_) return h;
  const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
#if defined(_WIN32)
  h.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
  h.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
  if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
    h.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    h.window = reinterpret_cast<void*>(
        static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
  } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
    h.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    h.window = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
  }
#endif
#endif
  return h;
}

void Platform::pixelSize(uint32_t& w, uint32_t& h) const {
  int iw = 1280, ih = 720;
  if (window_) SDL_GetWindowSizeInPixels(window_, &iw, &ih);
  w = static_cast<uint32_t>(iw > 0 ? iw : 1);
  h = static_cast<uint32_t>(ih > 0 ? ih : 1);
}

void Platform::setPointerLock(bool on) {
  wantLock_ = on;
  // On the web the browser only grants pointer lock inside a user gesture; SDL defers the
  // request to the next click on the canvas.
  if (window_) SDL_SetWindowRelativeMouseMode(window_, on);
}

bool Platform::pointerLocked() const { return window_ && SDL_GetWindowRelativeMouseMode(window_); }

bool Platform::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_QUIT:
      return false;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
      resized_ = true;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      focused_ = false;
      input_.releaseAll();
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      focused_ = true;
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (wantLock_ && !pointerLocked() && window_) SDL_SetWindowRelativeMouseMode(window_, true);
      break;
    default:
      break;
  }
  input_.handleEvent(e);
  return true;
}

InputFrame Platform::takeInput() {
  InputFrame f = input_.take();
  f.pointerLocked = pointerLocked();
  if (f.pointerLocked != wasLocked_) {
    wasLocked_ = f.pointerLocked;
    lookSettleFrames_ = 3;
  }
  if (lookSettleFrames_ > 0) {
    --lookSettleFrames_;
    f.lookDelta = {};
  }
  return f;
}

}  // namespace aaa
