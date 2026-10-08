#include "app/ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/version.h"  // appVersion()
#include "game/interactables.h"

namespace aaa {
namespace {
// Palette mirrors the original DOM UI (see web/shell.html): restrained, serif, quiet.
constexpr uint32_t kInk = 0xffe8e4da;      // #e8e4da
constexpr uint32_t kDim = 0xff9a978e;      // #9a978e
constexpr uint32_t kAccent = 0xffc9b98f;   // #c9b98f
constexpr uint32_t kBg = 0xff0b0e10;       // #0b0e10
constexpr uint32_t kLow = 0xffe9a07a;      // #e9a07a
constexpr uint32_t kToast = 0xffefe9dc;    // #efe9dc
constexpr uint32_t kClockSub = 0xffcfcabd; // #cfcabd
constexpr uint32_t kTrack = 0x22ffffff;    // bar track
constexpr uint32_t kVitalColor[4] = {0xffd8cfbb, 0xffe3a76b, 0xffb9c27e, 0xff8fb4c9};

uint32_t withAlpha(uint32_t rgba, float a) {
  const uint32_t alpha = static_cast<uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
  return (rgba & 0x00ffffffu) | (alpha << 24);
}

const char* kVitalLabel[4] = {"Health", "Warmth", "Food", "Water"};

const char* phaseName(float h) {
  return h < 5.0f ? "Night" : h < 7.0f ? "Dawn" : h < 11.0f ? "Morning" : h < 14.0f ? "Midday"
         : h < 17.5f             ? "Afternoon"
         : h < 19.5f             ? "Dusk"
                                 : "Night";
}

float clampSize(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
}  // namespace

const char* Ui::menuName() const {
  switch (menu_) {
    case Menu::Loading: return "loading";
    case Menu::Title: return "title";
    case Menu::Playing: return "playing";
    case Menu::Pause: return "pause";
    case Menu::Settings: return "settings";
  }
  return "?";
}

void Ui::setSettings(int quality, float volume, float sens, bool invertY, bool fullscreen) {
  quality_ = std::clamp(quality, 0, 2);
  volume_ = std::clamp(volume, 0.0f, 1.0f);
  sens_ = std::clamp(sens, 0.5f, 2.0f);
  invertY_ = invertY;
  fullscreen_ = fullscreen;
}

void Ui::notify(const std::string& text) {
  if (text.empty()) return;
  toasts_.push_back({text, 0.0f});
  if (toasts_.size() > 6) toasts_.erase(toasts_.begin());
}

void Ui::beginFrame(uint32_t fbW, uint32_t fbH) {
  fbW_ = std::max(1u, fbW);
  fbH_ = std::max(1u, fbH);
}

bool Ui::buttonRect(const char* id, float& x, float& y, float& w, float& h) const {
  for (const Btn& b : buttons_)
    if (b.id == id) {
      x = b.x; y = b.y; w = b.w; h = b.h;
      return true;
    }
  return false;
}

void Ui::wrap(const char* text, float size, float maxWidth, std::vector<std::string>& out) const {
  out.clear();
  if (!text) return;
  std::string line, word;
  auto flushWord = [&]() {
    if (word.empty()) return;
    const std::string candidate = line.empty() ? word : line + " " + word;
    const float w = renderer_ ? renderer_->textWidth(size, candidate.c_str()) : candidate.size() * size * 0.5f;
    if (!line.empty() && w > maxWidth) {
      out.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    word.clear();
  };
  for (const char* p = text; *p; ++p) {
    if (*p == ' ') {
      flushWord();
    } else {
      word += *p;
    }
  }
  flushWord();
  if (!line.empty()) out.push_back(line);
}

void Ui::layout(const Game& game) {
  buttons_.clear();
  noLock_.clear();
  hover_ = -1;
  const float W = static_cast<float>(fbW_), H = static_cast<float>(fbH_);
  const float left = W * 0.09f;
  auto addBtn = [&](float x, float y, const char* id, const char* label, int style) {
    const float size = clampSize(H * 0.018f, 12.0f, 14.0f);
    const float tracking = size * 0.22f;
    const float tw = renderer_ ? renderer_->textWidth(size, label, tracking) : std::strlen(label) * size * 0.62f + tracking * std::strlen(label);
    Btn b;
    b.x = x;
    b.y = y;
    b.w = std::max(tw + 52.0f, 120.0f);
    b.h = size + 20.0f;
    b.id = id;
    b.label = label;
    b.style = style;
    buttons_.push_back(b);
    return static_cast<int>(buttons_.size() - 1);
  };

  if (menu_ == Menu::Title) {
    float y = H * 0.42f;
    addBtn(left, y, "begin", hasSave_ ? "Continue" : "Begin", 0);
    if (hasSave_) {
      const int r = addBtn(left + 220.0f, y, "restart",
                           restartArmed_ ? "Click again to abandon this journey" : "Start a new journey", 1);
      noLock_.push_back({buttons_[r].x, buttons_[r].y, buttons_[r].w, buttons_[r].h});
    }
  } else if (menu_ == Menu::Pause) {
    float y = H * 0.52f;
    addBtn(left, y, "continue", "Continue", 0);
    const int s = addBtn(left + 220.0f, y, "settings", "Settings", 1);
    noLock_.push_back({buttons_[s].x, buttons_[s].y, buttons_[s].w, buttons_[s].h});
  } else if (menu_ == Menu::Settings) {
    float y = H * 0.30f;
    const float rowH = H * 0.085f;
    const float size = clampSize(H * 0.018f, 12.0f, 14.0f);
    // Quality: three selectable buttons.
    const char* qnames[3] = {"Low", "Medium", "High"};
    float x = left + W * 0.24f;
    for (int i = 0; i < 3; ++i) {
      char id[16];
      std::snprintf(id, sizeof(id), "quality:%d", i);
      const int b = addBtn(x, y, id, qnames[i], quality_ == i ? 4 : 2);
      x += buttons_[b].w + 14.0f;
      noLock_.push_back({buttons_[b].x, buttons_[b].y, buttons_[b].w, buttons_[b].h});
    }
    y += rowH;
    // Steppers and toggles.
    char volId[8], sensId[8];
    std::snprintf(volId, sizeof(volId), "vol-");
    std::snprintf(sensId, sizeof(sensId), "sens-");
    const int vm = addBtn(left + W * 0.24f, y, "vol-", "<", 2);
    const int vp = addBtn(left + W * 0.24f + buttons_[vm].w + 14.0f, y, "vol+", ">", 2);
    noLock_.push_back({buttons_[vm].x, buttons_[vm].y, buttons_[vm].w, buttons_[vm].h});
    noLock_.push_back({buttons_[vp].x, buttons_[vp].y, buttons_[vp].w, buttons_[vp].h});
    y += rowH;
    const int sm = addBtn(left + W * 0.24f, y, "sens-", "<", 2);
    const int sp = addBtn(left + W * 0.24f + buttons_[sm].w + 14.0f, y, "sens+", ">", 2);
    noLock_.push_back({buttons_[sm].x, buttons_[sm].y, buttons_[sm].w, buttons_[sm].h});
    noLock_.push_back({buttons_[sp].x, buttons_[sp].y, buttons_[sp].w, buttons_[sp].h});
    y += rowH;
    const int iy = addBtn(left + W * 0.24f, y, "invertY", invertY_ ? "ON" : "OFF", invertY_ ? 3 : 2);
    noLock_.push_back({buttons_[iy].x, buttons_[iy].y, buttons_[iy].w, buttons_[iy].h});
    if (native_) {
      y += rowH;
      const int fs = addBtn(left + W * 0.24f, y, "fullscreen", fullscreen_ ? "ON" : "OFF", fullscreen_ ? 3 : 2);
      noLock_.push_back({buttons_[fs].x, buttons_[fs].y, buttons_[fs].w, buttons_[fs].h});
    }
    y += rowH * 1.2f;
    const int back = addBtn(left, y, "back", "Back", 1);
    noLock_.push_back({buttons_[back].x, buttons_[back].y, buttons_[back].w, buttons_[back].h});
    (void)size;
  }
}

void Ui::update(float dt, const Game& game, const UiMouse& mouse, UiActions& out) {
  out = UiActions{};
  // Toasts age out (DOM timing: 4.2 s + 30 ms per character, 0.9 s fade-out).
  for (Toast& t : toasts_) t.age += dt;
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) {
                  return t.age > 4.2f + static_cast<float>(t.text.size()) * 0.03f + 0.9f;
                }),
                toasts_.end());
  // HUD fades in once the journey starts (1.5 s, like the DOM transition).
  const float hudTarget = (menu_ == Menu::Playing && started_) ? 1.0f : 0.0f;
  hudAlpha_ += (hudTarget - hudAlpha_) * std::min(1.0f, dt / 0.12f);
  if (std::fabs(hudTarget - hudAlpha_) < 0.01f) hudAlpha_ = hudTarget;
  // Two-click confirmation expires after 4 s.
  if (restartArmed_) restartArmedAt_ += dt;
  if (restartArmed_ && restartArmedAt_ > 4.0f) restartArmed_ = false;

  layout(game);

  // Hover + click hit-testing.
  for (size_t i = 0; i < buttons_.size(); ++i)
    if (hit(buttons_[i], mouse.pos)) hover_ = static_cast<int>(i);
  if (mouse.click) {
    for (const Btn& b : buttons_) {
      if (!hit(b, mouse.pos)) continue;
      if (b.id == "begin" || b.id == "continue") {
        out.startOrResume = true;
      } else if (b.id == "restart") {
        if (!restartArmed_) {
          restartArmed_ = true;
          restartArmedAt_ = 0.0f;
          layout(game);  // refresh the label ("Click again to abandon this journey")
        } else {
          out.startOver = true;
          restartArmed_ = false;
        }
      } else if (b.id == "settings") {
        menu_ = Menu::Settings;
        out.openSettings = true;
        layout(game);
      } else if (b.id == "back") {
        menu_ = Menu::Pause;
        out.closeSettings = true;
        layout(game);
      } else if (b.id.rfind("quality:", 0) == 0) {
        quality_ = std::clamp(std::atoi(b.id.c_str() + 8), 0, 2);
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "vol-") {
        volume_ = std::clamp(volume_ - 0.05f, 0.0f, 1.0f);
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "vol+") {
        volume_ = std::clamp(volume_ + 0.05f, 0.0f, 1.0f);
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "sens-") {
        sens_ = std::clamp(sens_ - 0.1f, 0.5f, 2.0f);
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "sens+") {
        sens_ = std::clamp(sens_ + 0.1f, 0.5f, 2.0f);
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "invertY") {
        invertY_ = !invertY_;
        out.settingsChanged = true;
        layout(game);
      } else if (b.id == "fullscreen") {
        fullscreen_ = !fullscreen_;
        out.settingsChanged = true;
        layout(game);
      }
      break;
    }
  }
}

void Ui::drawVeil(UiRenderer& r) {
  const float W = static_cast<float>(fbW_), H = static_cast<float>(fbH_);
  r.rect(0, 0, W, H, withAlpha(kBg, 0.47f));
  // Left panel: a stepped approximation of the DOM's horizontal gradient.
  const float strips[4] = {0.93f, 0.78f, 0.45f, 0.10f};
  const float stripW = W * 0.60f / 4.0f;
  for (int i = 0; i < 4; ++i) r.rect(i * stripW, 0, stripW + 1, H, withAlpha(kBg, strips[i] * 0.55f));
}

void Ui::drawMenuCommon(UiRenderer& r, const char* title, const char* lead, const Game& game) {
  (void)game;
  const float W = static_cast<float>(fbW_), H = static_cast<float>(fbH_);
  const float left = W * 0.09f;
  drawVeil(r);
  const float titleSize = clampSize(W * 0.04f, 28.0f, 52.0f);
  r.text(left, H * 0.16f, titleSize, kInk, title, 0, titleSize * 0.5f);
  const float subSize = clampSize(H * 0.018f, 11.0f, 14.0f);
  r.text(left, H * 0.16f + titleSize * 1.35f, subSize, kDim, "A VALLEY IN THE CLOUDS", 0, subSize * 0.25f);
  const float leadSize = clampSize(H * 0.021f, 13.0f, 16.0f);
  std::vector<std::string> lines;
  wrap(lead, leadSize, std::min(420.0f, W * 0.42f), lines);
  float y = H * 0.16f + titleSize * 1.35f + subSize * 2.2f + 18.0f;
  for (const std::string& l : lines) {
    r.text(left, y, leadSize, kDim, l.c_str());
    y += leadSize * 1.65f;
  }
  // Keys grid.
  const float keySize = clampSize(H * 0.016f, 11.0f, 13.0f);
  const float descSize = clampSize(H * 0.019f, 12.0f, 15.0f);
  struct KeyRow { const char* key; const char* desc; };
  const KeyRow* rows = nullptr;
  int rowCount = 0;
  static const KeyRow kTitleRows[] = {
      {"W A S D", "move   ·   SHIFT run   ·   SPACE jump"},
      {"E", "gather, drink, tend the fire, rest at the shrine"},
      {"F", "build a fire (3 branches + flint)"},
      {"R", "eat"},
      {"ESC", "pause"},
  };
  static const KeyRow kPauseRows[] = {
      {"W A S D", "move"},
      {"MOUSE", "look   ·   WHEEL camera distance"},
      {"SHIFT / CTRL", "run / walk slowly"},
      {"SPACE / C", "jump / crouch"},
      {"E", "interact"},
      {"F / R", "build fire / eat"},
  };
  if (menu_ == Menu::Title) {
    rows = kTitleRows;
    rowCount = 5;
  } else {
    rows = kPauseRows;
    rowCount = 6;
  }
  y += 14.0f;
  for (int i = 0; i < rowCount; ++i) {
    r.text(left, y, keySize, kAccent, rows[i].key);
    r.text(left + W * 0.11f, y, descSize, kDim, rows[i].desc);
    y += std::max(keySize, descSize) * 1.9f;
  }
  // Buttons.
  y += 26.0f;
  for (const Btn& b : buttons_) {
    const float size = clampSize(H * 0.018f, 12.0f, 14.0f);
    const float tracking = size * 0.22f;
    uint32_t border = kAccent, label = kInk, bg = 0x10ffffff;
    if (b.style == 1) { border = 0; label = kDim; bg = 0; }
    if (b.style == 2) { border = withAlpha(kDim, 0.5f); label = kDim; bg = 0; }
    if (b.style == 3) { border = kAccent; label = kAccent; bg = 0; }
    if (b.style == 4) { border = kAccent; label = kInk; bg = 0x30c9b98f; }
    const bool hov = hover_ >= 0 && &b == &buttons_[hover_];
    if (hov && bg == 0) bg = 0x30c9b98f;
    if (hov && b.style == 1) label = kInk;
    if (border) {
      r.rect(b.x, b.y, b.w, 1, border);
      r.rect(b.x, b.y + b.h - 1, b.w, 1, border);
      r.rect(b.x, b.y, 1, b.h, border);
      r.rect(b.x + b.w - 1, b.y, 1, b.h, border);
    }
    if (bg) r.rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, bg);
    r.text(b.x + b.w * 0.5f, b.y + (b.h - size) * 0.5f, size, label, b.label.c_str(), 1, tracking);
  }
  // Hint + version.
  r.text(left, y + 52.0f, clampSize(H * 0.016f, 11.0f, 13.0f), kDim, "Headphones recommended");
  char ver[32];
  std::snprintf(ver, sizeof(ver), "Mistpine %s", appVersion());
  r.text(W - 34.0f, H - 30.0f, clampSize(H * 0.015f, 10.0f, 12.0f), withAlpha(kDim, 0.7f), ver, 2);
}

void Ui::drawHud(UiRenderer& r, const Game& game) {
  const float W = static_cast<float>(fbW_), H = static_cast<float>(fbH_);
  const float a = hudAlpha_;
  if (a <= 0.01f) return;
  const Vitals& v = game.survival().vitals();
  const float vals[4] = {v.health, v.warmth, v.satiety, v.hydration};
  // Quiet vitals: fade to half opacity while everything is healthy.
  bool calm = true;
  for (float x : vals)
    if (x < 70.0f) calm = false;
  const float va = a * (calm ? 0.5f : 1.0f);
  const float labelSize = clampSize(H * 0.016f, 11.0f, 13.0f);
  const float rowH = std::max(24.0f, labelSize * 2.1f);
  const float x0 = 34.0f;
  float y = H - 30.0f - rowH * 4.0f;
  for (int i = 0; i < 4; ++i) {
    const uint32_t labelCol = withAlpha(vals[i] < 30.0f ? kLow : kDim, va);
    r.text(x0, y, labelSize, labelCol, kVitalLabel[i]);
    const float bx = x0 + 82.0f, bw = 120.0f, bh = 3.0f;
    r.rect(bx, y + labelSize * 0.55f, bw, bh, withAlpha(kTrack, va));
    r.rect(bx, y + labelSize * 0.55f, bw * std::clamp(vals[i] / 100.0f, 0.0f, 1.0f), bh, withAlpha(kVitalColor[i], va));
    y += rowH;
  }
  // Clock + temperature, top-right.
  const float h = game.timeOfDay().hours();
  const float clockSize = clampSize(H * 0.021f, 13.0f, 16.0f);
  const float subSize = clampSize(H * 0.018f, 12.0f, 14.0f);
  char line1[64], line2[96];
  std::snprintf(line1, sizeof(line1), "Day %d  ·  %s", game.day(), phaseName(h));
  std::snprintf(line2, sizeof(line2), "%02d:%02d  ·  %.0f°C%s%s", static_cast<int>(h),
                static_cast<int>(std::fmod(h, 1.0f) * 60.0f), game.survival().feltTemperature(),
                v.wetness > 0.25f ? "  ·  soaked" : "", game.fireHeat() > 0.15f ? "  ·  by the fire" : "");
  r.text(W - 34.0f, 26.0f, clockSize, withAlpha(kInk, a), line1, 2);
  r.text(W - 34.0f, 26.0f + clockSize * 1.5f, subSize, withAlpha(kClockSub, a), line2, 2);
  // Inventory, bottom-right.
  const Inventory& inv = game.inventory();
  float iy = H - 30.0f;
  const float invSize = clampSize(H * 0.018f, 12.0f, 14.0f);
  for (int i = 0; i < kItemKindCount; ++i) {
    const int n = inv.get(static_cast<ItemKind>(i));
    if (n <= 0) continue;
    char num[16];
    std::snprintf(num, sizeof(num), "%d", n);
    const float numW = r.textWidth(invSize, num);
    r.text(W - 34.0f - numW - 10.0f, iy - invSize, invSize, withAlpha(kInk, a), itemName(static_cast<ItemKind>(i), n));
    r.text(W - 34.0f, iy - invSize, invSize, withAlpha(kAccent, a), num, 2);
    iy -= invSize * 1.7f;
  }
  // Prompt, bottom-centre.
  const std::string& prompt = game.prompt();
  if (!prompt.empty()) {
    const float ps = clampSize(H * 0.021f, 13.0f, 16.0f);
    r.text(W * 0.5f, H - H * 0.15f - ps, ps, withAlpha(kInk, a), prompt.c_str(), 1);
  }
  // Toasts: italic, stacked above the prompt, newest closest to it.
  const float ts = clampSize(H * 0.021f, 13.0f, 16.0f);
  const float lineH = ts * 1.9f;
  const size_t shown = std::min<size_t>(toasts_.size(), 3);
  for (size_t i = 0; i < shown; ++i) {
    const Toast& t = toasts_[toasts_.size() - shown + i];
    float ta = a;
    if (t.age < 0.8f) ta *= t.age / 0.8f;  // fade in
    const float life = 4.2f + static_cast<float>(t.text.size()) * 0.03f;
    if (t.age > life) ta *= std::clamp(1.0f - (t.age - life) / 0.9f, 0.0f, 1.0f);  // fade out
    const float ty = H - H * 0.21f - (shown - i) * lineH;
    r.text(W * 0.5f, ty, ts, withAlpha(kToast, ta), t.text.c_str(), 1, 0.0f, true);
  }
}

void Ui::draw(const Game& game) {
  UiRenderer& r = *renderer_;
  if (!r.ready()) return;
  const float W = static_cast<float>(fbW_), H = static_cast<float>(fbH_);
  if (menu_ == Menu::Loading) {
    const float titleSize = clampSize(W * 0.04f, 28.0f, 52.0f);
    r.text(W * 0.5f, H * 0.38f, titleSize, kInk, "MISTPINE", 1, titleSize * 0.5f);
    const float subSize = clampSize(H * 0.018f, 11.0f, 14.0f);
    r.text(W * 0.5f, H * 0.38f + titleSize * 1.35f, subSize, kDim, "A VALLEY IN THE CLOUDS", 1, subSize * 0.25f);
    const float barW = std::min(360.0f, W * 0.6f), barH = 2.0f;
    const float bx = (W - barW) * 0.5f, by = H * 0.38f + titleSize * 1.35f + subSize * 3.0f + 42.0f;
    r.rect(bx, by, barW, barH, withAlpha(kTrack, 0.6f));
    r.rect(bx, by, barW * std::clamp(loadFrac_, 0.0f, 1.0f), barH, kAccent);
    r.text(W * 0.5f, by + 14.0f, subSize, kDim, loadStage_.c_str(), 1);
    return;
  }
  if (menu_ == Menu::Title) {
    drawMenuCommon(r, "MISTPINE",
                   hasSave_ ? "The valley has kept your place. The fires you left may have burned low."
                             : "The pass is closed behind you. Somewhere up the valley stands an old shrine. "
                               "Gather wood, keep warm, and do not let the night find you without a fire.",
                   game);
    return;
  }
  if (menu_ == Menu::Pause) {
    drawMenuCommon(r, "PAUSED", "Your journey is saved.", game);
    return;
  }
  if (menu_ == Menu::Settings) {
    drawVeil(r);
    const float left = W * 0.09f;
    const float titleSize = clampSize(W * 0.04f, 28.0f, 52.0f);
    r.text(left, H * 0.16f, titleSize, kInk, "SETTINGS", 0, titleSize * 0.5f);
    const float labelSize = clampSize(H * 0.019f, 12.0f, 15.0f);
    const float rowH = H * 0.085f;
    float y = H * 0.30f;
    auto row = [&](const char* label) { r.text(left, y, labelSize, kDim, label); };
    row("Quality");
    y += rowH;
    row("Master volume");
    {
      char v[16];
      std::snprintf(v, sizeof(v), "%d%%", static_cast<int>(volume_ * 100.0f + 0.5f));
      r.text(left + W * 0.24f + 120.0f, y, labelSize, kInk, v);
    }
    y += rowH;
    row("Mouse sensitivity");
    {
      char v[16];
      std::snprintf(v, sizeof(v), "%.1fx", sens_);
      r.text(left + W * 0.24f + 120.0f, y, labelSize, kInk, v);
    }
    y += rowH;
    row("Invert Y");
    if (native_) {
      y += rowH;
      row("Fullscreen");
    }
    // Buttons (drawn from the layout, like the menus).
    for (const Btn& b : buttons_) {
      const float size = clampSize(H * 0.018f, 12.0f, 14.0f);
      const float tracking = size * 0.22f;
      uint32_t border = withAlpha(kDim, 0.5f), label = kDim, bg = 0;
      if (b.style == 3) { border = kAccent; label = kAccent; }
      if (b.style == 4) { border = kAccent; label = kInk; bg = 0x30c9b98f; }
      if (b.style == 1) { border = 0; label = kDim; }
      const bool hov = hover_ >= 0 && &b == &buttons_[hover_];
      if (hov && bg == 0) bg = 0x30c9b98f;
      if (hov && b.style == 1) label = kInk;
      if (border) {
        r.rect(b.x, b.y, b.w, 1, border);
        r.rect(b.x, b.y + b.h - 1, b.w, 1, border);
        r.rect(b.x, b.y, 1, b.h, border);
        r.rect(b.x + b.w - 1, b.y, 1, b.h, border);
      }
      if (bg) r.rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, bg);
      r.text(b.x + b.w * 0.5f, b.y + (b.h - size) * 0.5f, size, label, b.label.c_str(), 1, tracking);
    }
    return;
  }
  // Playing: HUD only (menus are hidden; the world stays visible).
  drawHud(r, game);
}

}  // namespace aaa
