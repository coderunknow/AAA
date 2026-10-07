#pragma once
// Maps SDL keyboard/mouse/gamepad events to the device-independent InputFrame.
#include <SDL3/SDL.h>

#include "game/input.h"

namespace aaa {

struct InputSettings {
  float mouseSensitivity = 1.0f;  // user multiplier on top of CameraTuning::mouseSensitivity
  bool invertY = false;
  float stickDeadzone = 0.18f;
};

class InputMapper {
 public:
  void init();
  void shutdown();
  void handleEvent(const SDL_Event& e);
  InputFrame take();
  void releaseAll();
  InputSettings& settings() { return settings_; }
  bool usingGamepad() const { return lastWasPad_; }

 private:
  static float deadzone(float v, float dz);
  InputSettings settings_;
  SDL_Gamepad* pad_ = nullptr;
  bool keys_[8] = {};  // W A S D Shift Ctrl Space(held) unused
  Vec2 mouse_{};
  float wheel_ = 0.0f;
  bool crouch_ = false, jump_ = false, interact_ = false, debug_ = false, pause_ = false;
  bool lastWasPad_ = false;
};

}  // namespace aaa
