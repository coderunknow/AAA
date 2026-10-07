#pragma once
// Device-independent per-frame input. The platform layer maps keyboard/mouse and
// gamepads into this structure; gameplay never sees raw devices.
#include "core/math.h"

namespace aaa {

struct InputFrame {
  Vec2 move;              // x = strafe right, y = forward; magnitude <= 1
  Vec2 lookDelta;         // mouse delta in pixels (already scaled by sensitivity)
  Vec2 lookRate;          // analog stick look, [-1,1] (rotation rate)
  float zoom = 0.0f;      // camera distance change request (wheel)
  bool sprint = false;
  bool walk = false;
  bool crouchToggle = false;  // edge
  bool jump = false;          // edge
  bool interact = false;      // edge
  bool buildFire = false;     // edge
  bool eat = false;           // edge
  bool toggleDebug = false;   // edge
  bool pause = false;         // edge
  bool pointerLocked = false;
};

}  // namespace aaa
