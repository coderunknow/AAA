#include "platform/input_mapper.h"

#include <cmath>

namespace aaa {
namespace {
enum Key { kW, kA, kS, kD, kShift, kCtrl, kSpace };
}

void InputMapper::init() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (ids && count > 0) pad_ = SDL_OpenGamepad(ids[0]);
  SDL_free(ids);
}

void InputMapper::shutdown() {
  if (pad_) SDL_CloseGamepad(pad_);
  pad_ = nullptr;
}

void InputMapper::releaseAll() {
  for (bool& k : keys_) k = false;
}

float InputMapper::deadzone(float v, float dz) {
  const float a = std::fabs(v);
  if (a < dz) return 0.0f;
  return std::copysign((a - dz) / (1.0f - dz), v);
}

void InputMapper::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      const bool down = e.type == SDL_EVENT_KEY_DOWN;
      lastWasPad_ = false;
      switch (e.key.scancode) {  // scancodes = physical layout (WASD works on AZERTY too)
        case SDL_SCANCODE_W: case SDL_SCANCODE_UP: keys_[kW] = down; break;
        case SDL_SCANCODE_A: case SDL_SCANCODE_LEFT: keys_[kA] = down; break;
        case SDL_SCANCODE_S: case SDL_SCANCODE_DOWN: keys_[kS] = down; break;
        case SDL_SCANCODE_D: case SDL_SCANCODE_RIGHT: keys_[kD] = down; break;
        case SDL_SCANCODE_LSHIFT: keys_[kShift] = down; break;
        case SDL_SCANCODE_LCTRL: keys_[kCtrl] = down; break;
        case SDL_SCANCODE_SPACE: if (down && !e.key.repeat) jump_ = true; break;
        case SDL_SCANCODE_C: if (down && !e.key.repeat) crouch_ = true; break;
        case SDL_SCANCODE_E: if (down && !e.key.repeat) interact_ = true; break;
        case SDL_SCANCODE_F3: if (down && !e.key.repeat) debug_ = true; break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_P: if (down && !e.key.repeat) pause_ = true; break;
        default: break;
      }
      break;
    }
    case SDL_EVENT_MOUSE_MOTION:
      // Browsers can report one huge movementX/Y around pointer-lock transitions; a single
      // event this large is never a real hand movement, so drop it instead of spinning the camera.
      if (std::fabs(e.motion.xrel) > 300.0f || std::fabs(e.motion.yrel) > 300.0f) break;
      mouse_.x += e.motion.xrel;
      mouse_.y += e.motion.yrel;
      lastWasPad_ = false;
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      wheel_ += e.wheel.y;
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!pad_) pad_ = SDL_OpenGamepad(e.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if (pad_ && SDL_GetGamepadID(pad_) == e.gdevice.which) {
        SDL_CloseGamepad(pad_);
        pad_ = nullptr;
      }
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      lastWasPad_ = true;
      switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: jump_ = true; break;
        case SDL_GAMEPAD_BUTTON_EAST: crouch_ = true; break;
        case SDL_GAMEPAD_BUTTON_WEST: interact_ = true; break;
        case SDL_GAMEPAD_BUTTON_START: pause_ = true; break;
        case SDL_GAMEPAD_BUTTON_BACK: debug_ = true; break;
        default: break;
      }
      break;
    default:
      break;
  }
}

InputFrame InputMapper::take() {
  InputFrame f;
  Vec2 mv{(keys_[kD] ? 1.0f : 0.0f) - (keys_[kA] ? 1.0f : 0.0f), (keys_[kW] ? 1.0f : 0.0f) - (keys_[kS] ? 1.0f : 0.0f)};
  if (length(mv) > 1.0f) mv = mv * (1.0f / length(mv));
  f.sprint = keys_[kShift];
  f.walk = keys_[kCtrl];
  f.lookDelta = {mouse_.x * settings_.mouseSensitivity, (settings_.invertY ? -1.0f : 1.0f) * mouse_.y * settings_.mouseSensitivity};
  if (pad_) {
    const float lx = deadzone(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f, settings_.stickDeadzone);
    const float ly = deadzone(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f, settings_.stickDeadzone);
    const float rx = deadzone(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f, settings_.stickDeadzone);
    const float ry = deadzone(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f, settings_.stickDeadzone);
    if (std::fabs(lx) + std::fabs(ly) > 0.0f) {
      Vec2 s{lx, -ly};
      if (length(s) > 1.0f) s = s * (1.0f / length(s));
      mv = s;  // analog magnitude = walk speed blend
      lastWasPad_ = true;
    }
    f.lookRate = {rx, (settings_.invertY ? -1.0f : 1.0f) * ry};
    if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_LEFT_STICK)) f.sprint = true;
    const float lt = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    const float rt = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
    f.zoom += (rt - lt) * 0.08f;
  }
  f.move = mv;
  f.zoom += wheel_;
  f.jump = jump_;
  f.crouchToggle = crouch_;
  f.interact = interact_;
  f.toggleDebug = debug_;
  f.pause = pause_;
  mouse_ = {};
  wheel_ = 0.0f;
  jump_ = crouch_ = interact_ = debug_ = pause_ = false;
  return f;
}

}  // namespace aaa
