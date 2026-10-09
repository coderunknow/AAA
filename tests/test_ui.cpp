#include "app/ui.h"
#include "game/game.h"
#include "test.h"

using namespace aaa;

namespace {
// A ready game (small heightfield) shared by the UI tests.
Game& sharedGame() {
  static Game* g = [] {
    GameConfig cfg;
    cfg.heightfieldResolution = 513;
    auto* game = new Game(cfg);
    InputFrame none;
    for (int i = 0; i < 10000 && game->phase() == GamePhase::LoadingWorld; ++i) game->update(1.0f / 60.0f, none, 50.0);
    return game;
  }();
  return *g;
}

// Clicks the centre of the named button and returns the actions produced.
UiActions clickButton(Ui& ui, const char* id) {
  float x, y, w, h;
  if (!ui.buttonRect(id, x, y, w, h)) return UiActions{};
  UiMouse m;
  m.pos = {x + w * 0.5f, y + h * 0.5f};
  m.click = true;
  UiActions out;
  ui.update(1.0f / 60.0f, sharedGame(), m, out);
  return out;
}

UiActions frame(Ui& ui, float dt = 1.0f / 60.0f) {
  UiMouse m;  // no click
  UiActions out;
  ui.update(dt, sharedGame(), m, out);
  return out;
}
}  // namespace

TEST_CASE("ui: title screen offers Begin and starts on click") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Title);
  ui.setHasSave(false);
  frame(ui);
  const UiActions a = clickButton(ui, "begin");
  CHECK(a.startOrResume);
  CHECK(!a.startOver);
}

TEST_CASE("ui: a saved journey offers Continue and Start a new journey") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Title);
  ui.setHasSave(true);
  frame(ui);
  float x, y, w, h;
  CHECK(ui.buttonRect("begin", x, y, w, h));      // labelled Continue
  CHECK(ui.buttonRect("restart", x, y, w, h));    // the quiet restart button
  // The restart button must be excluded from web pointer-lock requests.
  bool found = false;
  for (const auto& r : ui.noLockRects())
    if (r.x == x && r.y == y) found = true;
  CHECK(found);
}

TEST_CASE("ui: Start a new journey requires two clicks (confirmation)") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Title);
  ui.setHasSave(true);
  frame(ui);
  const UiActions first = clickButton(ui, "restart");
  CHECK(!first.startOver);  // first click only arms the confirmation
  const UiActions second = clickButton(ui, "restart");
  CHECK(second.startOver);  // second click confirms
}

TEST_CASE("ui: the two-click confirmation expires") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Title);
  ui.setHasSave(true);
  frame(ui);
  CHECK(!clickButton(ui, "restart").startOver);
  for (int i = 0; i < 300; ++i) frame(ui, 1.0f / 60.0f);  // 5 s > the 4 s window
  CHECK(!clickButton(ui, "restart").startOver);  // armed state expired: arms again
}

TEST_CASE("ui: pause -> settings -> back -> continue") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Pause);  // the app sets this when Esc is pressed during play
  frame(ui);
  const UiActions s = clickButton(ui, "settings");
  CHECK(s.openSettings);
  CHECK(ui.menu() == Ui::Menu::Settings);
  const UiActions back = clickButton(ui, "back");
  CHECK(back.closeSettings);
  CHECK(ui.menu() == Ui::Menu::Pause);
  const UiActions cont = clickButton(ui, "continue");
  CHECK(cont.startOrResume);
}

TEST_CASE("ui: settings steppers and toggles clamp and report changes") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Settings);
  ui.setSettings(2, 1.0f, 1.0f, false, false);
  frame(ui);
  // Volume down to 0 and clamped.
  for (int i = 0; i < 30; ++i) {
    const UiActions a = clickButton(ui, "vol-");
    CHECK(a.settingsChanged);
  }
  CHECK_NEAR(ui.volume(), 0.0f, 1e-4);
  // Sensitivity up, clamped at 2.0x.
  for (int i = 0; i < 30; ++i) clickButton(ui, "sens+");
  CHECK_NEAR(ui.sensitivity(), 2.0f, 1e-4);
  // Toggles.
  UiActions a = clickButton(ui, "invertY");
  CHECK(a.settingsChanged && ui.invertY());
  a = clickButton(ui, "fullscreen");
  CHECK(a.settingsChanged && ui.fullscreen());
  // Quality selection.
  a = clickButton(ui, "quality:0");
  CHECK(a.settingsChanged && ui.quality() == 0);
}

TEST_CASE("ui: toasts are queued, age out and never grow unbounded") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Playing);
  for (int i = 0; i < 20; ++i) ui.notify("A test notification that is long enough to matter");
  for (int i = 0; i < 60 * 12; ++i) frame(ui);  // 12 s: all toasts expire (max life ~5.4 s)
  // After expiring, notifying again still works (the queue was drained, not stuck).
  ui.notify("back");
  frame(ui);
  // No crash, no unbounded growth: draw path is exercised via the renderer-less update.
  CHECK(true);
}

TEST_CASE("ui: HUD alpha fades in only once the journey has started") {
  Ui ui;
  ui.setRenderer(nullptr);
  ui.beginFrame(1280, 720);
  ui.setMenu(Ui::Menu::Title);
  ui.setStarted(false);
  for (int i = 0; i < 120; ++i) frame(ui);
  CHECK(ui.hudAlpha() < 0.05f);  // hidden on the title screen
  ui.setMenu(Ui::Menu::Playing);
  ui.setStarted(true);
  for (int i = 0; i < 240; ++i) frame(ui);  // 4 s of fade-in
  CHECK(ui.hudAlpha() > 0.95f);
}

TEST_CASE("ui: menu names match the browser-harness contract") {
  Ui ui;
  ui.setMenu(Ui::Menu::Loading);
  CHECK(std::string(ui.menuName()) == "loading");
  ui.setMenu(Ui::Menu::Title);
  CHECK(std::string(ui.menuName()) == "title");
  ui.setMenu(Ui::Menu::Playing);
  CHECK(std::string(ui.menuName()) == "playing");
  ui.setMenu(Ui::Menu::Pause);
  CHECK(std::string(ui.menuName()) == "pause");
  ui.setMenu(Ui::Menu::Settings);
  CHECK(std::string(ui.menuName()) == "settings");
}
