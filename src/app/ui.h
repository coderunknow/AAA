#pragma once
// In-engine UI: title / pause / settings menus, HUD, prompts and toasts, drawn with
// the UiRenderer (SDF text + quads). The same UI runs on web and native; the DOM shell
// keeps only the loading and fatal-error overlays (PROMPT §8.5).
#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"
#include "game/game.h"
#include "render/renderer.h"  // QualityPreset
#include "render/ui_renderer.h"

namespace aaa {

// Mouse state for UI hit-testing (framebuffer pixels, origin top-left).
struct UiMouse {
  Vec2 pos;
  bool click = false;  // left button edge
};

// What the UI wants the application to do this frame.
struct UiActions {
  bool startOrResume = false;  // Begin / Continue: start or resume play + lock the pointer
  bool startOver = false;      // "Start a new journey" confirmed (two clicks)
  bool openSettings = false;
  bool closeSettings = false;
  bool settingsChanged = false;  // quality / volume / sensitivity / invertY / fullscreen
};

class Ui {
 public:
  enum class Menu : uint8_t { Loading, Title, Playing, Pause, Settings };

  void beginFrame(uint32_t fbW, uint32_t fbH);
  void setMenu(Menu m) { menu_ = m; }
  Menu menu() const { return menu_; }
  const char* menuName() const;
  // The renderer is used for text measurement (layout). May be null in tests, where a
  // fixed-advance estimate is used instead.
  void setRenderer(UiRenderer* r) { renderer_ = r; }
  void setNative(bool v) { native_ = v; }
  bool isNative() const { return native_; }
  // Test hook: find a laid-out button by id and return its rect (false if absent).
  bool buttonRect(const char* id, float& x, float& y, float& w, float& h) const;

  void notify(const std::string& text);  // toast (italic, self-dismissing)
  // Advances toasts/hud and hit-tests the current layout. `game` supplies HUD data.
  void update(float dt, const Game& game, const UiMouse& mouse, UiActions& out);
  // Renders the current screen (menus or HUD) through the bound UiRenderer.
  void draw(const Game& game);

  // Persisted settings (mirrored into the save).
  int quality() const { return quality_; }
  float volume() const { return volume_; }
  float sensitivity() const { return sens_; }
  bool invertY() const { return invertY_; }
  bool fullscreen() const { return fullscreen_; }
  void setSettings(int quality, float volume, float sens, bool invertY, bool fullscreen);

  bool hasSave() const { return hasSave_; }
  void setHasSave(bool v) { hasSave_ = v; }
  bool started() const { return started_; }
  void setStarted(bool v) { started_ = v; }
  float hudAlpha() const { return hudAlpha_; }

  // Loading screen data (drives the in-engine loading screen on native and web).
  void setLoading(const char* stage, float fraction) {
    loadStage_ = stage ? stage : "";
    loadFrac_ = fraction;
  }

  // Buttons whose click must NOT request pointer lock on the web (restart / settings
  // navigation), exposed to the DOM shell via window.__mistpineState (thin glue).
  struct Rect { float x, y, w, h; };
  const std::vector<Rect>& noLockRects() const { return noLock_; }

 private:
  struct Toast { std::string text; float age = 0.0f; };
  struct Btn {
    float x, y, w, h;
    std::string id;
    std::string label;
    int style = 0;  // 0 primary, 1 quiet, 2 toggle-off, 3 toggle-on, 4 current (highlighted)
  };

  void layout(const Game& game);
  void drawVeil(UiRenderer& r);
  void drawMenuCommon(UiRenderer& r, const char* title, const char* lead, const Game& game);
  void drawHud(UiRenderer& r, const Game& game);
  bool hit(const Btn& b, Vec2 p) const { return p.x >= b.x && p.x < b.x + b.w && p.y >= b.y && p.y < b.y + b.h; }
  void wrap(const char* text, float size, float maxWidth, std::vector<std::string>& out) const;

  Menu menu_ = Menu::Loading;
  bool hasSave_ = false;
  bool started_ = false;
  int quality_ = 2;
  float volume_ = 1.0f;
  float sens_ = 1.0f;
  bool invertY_ = false;
  bool fullscreen_ = false;

  std::vector<Toast> toasts_;
  float hudAlpha_ = 0.0f;
  bool restartArmed_ = false;
  float restartArmedAt_ = 0.0f;

  uint32_t fbW_ = 1280, fbH_ = 720;
  std::vector<Btn> buttons_;   // current layout (hit-tested + drawn)
  std::vector<Rect> noLock_;   // subset of buttons_ that must not lock the pointer
  int hover_ = -1;
  float loadFrac_ = 0.0f;
  std::string loadStage_;
  UiRenderer* renderer_ = nullptr;
  bool native_ = true;
};

}  // namespace aaa
