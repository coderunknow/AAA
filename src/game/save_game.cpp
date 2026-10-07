#include "game/save_game.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace aaa {
namespace {

// --- minimal JSON (objects, numbers, strings, bools, null) -----------------------------------
struct JValue {
  enum Type { Null, Bool, Number, String, Object, Array } type = Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::map<std::string, std::shared_ptr<JValue>> obj;
  std::vector<std::shared_ptr<JValue>> arr;
  const JValue* get(const char* k) const {
    auto it = obj.find(k);
    return it == obj.end() ? nullptr : it->second.get();
  }
};

struct Parser {
  const char* p;
  const char* end;
  int depth = 0;
  void ws() { while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p; }
  bool lit(const char* t) {
    size_t n = std::char_traits<char>::length(t);
    if (static_cast<size_t>(end - p) < n || std::char_traits<char>::compare(p, t, n) != 0) return false;
    p += n;
    return true;
  }
  bool str(std::string& out) {
    if (p >= end || *p != '"') return false;
    ++p;
    while (p < end && *p != '"') {
      if (*p == '\\') {
        if (++p >= end) return false;
        const char c = *p;
        out += c == 'n' ? '\n' : c == 't' ? '\t' : c;  // \uXXXX not needed for our keys
      } else {
        out += *p;
      }
      ++p;
    }
    if (p >= end) return false;
    ++p;
    return true;
  }
  bool value(JValue& v) {
    if (++depth > 16) return false;
    ws();
    if (p >= end) return false;
    bool ok = true;
    if (*p == '{') {
      v.type = JValue::Object;
      ++p;
      ws();
      if (p < end && *p == '}') { ++p; --depth; return true; }
      for (;;) {
        ws();
        std::string key;
        if (!str(key)) return false;
        ws();
        if (p >= end || *p != ':') return false;
        ++p;
        auto child = std::make_shared<JValue>();
        if (!value(*child)) return false;
        v.obj[key] = child;
        ws();
        if (p < end && *p == ',') { ++p; continue; }
        if (p < end && *p == '}') { ++p; break; }
        return false;
      }
    } else if (*p == '[') {
      v.type = JValue::Array;
      ++p;
      ws();
      if (p < end && *p == ']') { ++p; --depth; return true; }
      for (;;) {
        if (v.arr.size() >= 4096) return false;
        auto child = std::make_shared<JValue>();
        if (!value(*child)) return false;
        v.arr.push_back(child);
        ws();
        if (p < end && *p == ',') { ++p; continue; }
        if (p < end && *p == ']') { ++p; break; }
        return false;
      }
    } else if (*p == '"') {
      v.type = JValue::String;
      ok = str(v.s);
    } else if (lit("true")) {
      v.type = JValue::Bool; v.b = true;
    } else if (lit("false")) {
      v.type = JValue::Bool; v.b = false;
    } else if (lit("null")) {
      v.type = JValue::Null;
    } else {
      char* e = nullptr;
      v.n = std::strtod(p, &e);
      if (e == p || e > end) return false;
      v.type = JValue::Number;
      p = e;
    }
    --depth;
    return ok;
  }
};

bool num(const JValue* o, const char* k, double& out) {
  const JValue* v = o ? o->get(k) : nullptr;
  if (!v || v->type != JValue::Number || !std::isfinite(v->n)) return false;
  out = v->n;
  return true;
}

void appendNum(std::string& s, const char* key, double v, bool comma = true) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "\"%s\":%.9g%s", key, v, comma ? "," : "");
  s += buf;
}
}  // namespace

const char* saveLoadResultName(SaveLoadResult r) {
  switch (r) {
    case SaveLoadResult::Ok: return "ok";
    case SaveLoadResult::Migrated: return "migrated";
    case SaveLoadResult::Empty: return "empty";
    case SaveLoadResult::Corrupt: return "corrupt";
    case SaveLoadResult::UnsupportedVersion: return "unsupported version";
    case SaveLoadResult::WrongWorld: return "different world";
    case SaveLoadResult::Invalid: return "invalid values";
  }
  return "?";
}

std::string serializeSave(const SaveData& d) {
  std::string s = "{\"format\":\"mistpine-save\",";
  appendNum(s, "version", SaveData::kCurrentVersion);
  s += "\"world\":{";
  appendNum(s, "seed", d.worldSeed);
  appendNum(s, "hours", d.hours);
  appendNum(s, "playSeconds", d.playSeconds, false);
  s += "},\"player\":{";
  appendNum(s, "x", d.playerPos.x);
  appendNum(s, "y", d.playerPos.y);
  appendNum(s, "z", d.playerPos.z);
  appendNum(s, "yaw", d.playerYaw, false);
  s += "},\"settings\":{";
  appendNum(s, "quality", d.quality);
  appendNum(s, "mouseSensitivity", d.mouseSensitivity);
  s += std::string("\"invertY\":") + (d.invertY ? "true" : "false");
  s += "},\"survival\":{";
  appendNum(s, "day", d.day);
  appendNum(s, "health", d.vitals.health);
  appendNum(s, "warmth", d.vitals.warmth);
  appendNum(s, "satiety", d.vitals.satiety);
  appendNum(s, "hydration", d.vitals.hydration);
  appendNum(s, "wetness", d.vitals.wetness);
  s += "\"inventory\":[";
  for (int i = 0; i < 4; ++i) s += std::to_string(d.inventory[i]) + (i < 3 ? "," : "");
  s += "],";
  if (d.hasRestPoint) {
    s += "\"rest\":{";
    appendNum(s, "x", d.restPoint.x);
    appendNum(s, "y", d.restPoint.y);
    appendNum(s, "z", d.restPoint.z, false);
    s += "},";
  }
  s += "\"picked\":[";
  for (size_t i = 0; i < d.picked.size(); ++i) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s[%u,%.6g]", i ? "," : "", static_cast<unsigned>(d.picked[i].id), d.picked[i].regrowAt);
    s += buf;
  }
  s += "],\"fires\":[";
  for (size_t i = 0; i < d.fires.size(); ++i) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s[%.6g,%.6g,%.6g,%.6g]", i ? "," : "", d.fires[i].pos.x, d.fires[i].pos.y,
                  d.fires[i].pos.z, d.fires[i].fuel);
    s += buf;
  }
  s += "]}}";
  return s;
}

SaveLoadResult deserializeSave(const std::string& text, uint32_t expectedSeed, SaveData& out) {
  if (text.empty()) return SaveLoadResult::Empty;
  if (text.size() > 64 * 1024) return SaveLoadResult::Corrupt;
  JValue root;
  Parser ps{text.data(), text.data() + text.size()};
  if (!ps.value(root) || root.type != JValue::Object) return SaveLoadResult::Corrupt;
  ps.ws();
  if (ps.p != ps.end) return SaveLoadResult::Corrupt;

  double version = 0;
  if (!num(&root, "version", version)) return SaveLoadResult::Corrupt;
  SaveData d;
  bool migrated = false;
  if (version == 1) {
    // v1 (first prototype milestone): flat layout, time stored as a day fraction, no settings.
    double seed, px, py, pz, yaw, dayFraction;
    if (!num(&root, "seed", seed) || !num(&root, "px", px) || !num(&root, "py", py) || !num(&root, "pz", pz) ||
        !num(&root, "yaw", yaw) || !num(&root, "dayFraction", dayFraction))
      return SaveLoadResult::Corrupt;
    d.worldSeed = static_cast<uint32_t>(seed);
    d.playerPos = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
    d.playerYaw = static_cast<float>(yaw);
    d.hours = static_cast<float>(dayFraction * 24.0);
    migrated = true;
  } else if (version == 2 || version == 3) {
    const JValue* fmt = root.get("format");
    if (!fmt || fmt->type != JValue::String || fmt->s != "mistpine-save") return SaveLoadResult::Corrupt;
    const JValue* w = root.get("world");
    const JValue* p = root.get("player");
    const JValue* st = root.get("settings");
    double seed, hours, play, x, y, z, yaw;
    if (!num(w, "seed", seed) || !num(w, "hours", hours) || !num(w, "playSeconds", play) || !num(p, "x", x) ||
        !num(p, "y", y) || !num(p, "z", z) || !num(p, "yaw", yaw))
      return SaveLoadResult::Corrupt;
    d.worldSeed = static_cast<uint32_t>(seed);
    d.hours = static_cast<float>(hours);
    d.playSeconds = play;
    d.playerPos = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    d.playerYaw = static_cast<float>(yaw);
    double q, ms;
    if (num(st, "quality", q)) d.quality = static_cast<int>(q);  // settings are optional
    if (num(st, "mouseSensitivity", ms)) d.mouseSensitivity = static_cast<float>(ms);
    if (st)
      if (const JValue* inv = st->get("invertY"); inv && inv->type == JValue::Bool) d.invertY = inv->b;
    if (version == 2) {
      migrated = true;  // v2 -> v3: survival state starts fresh (defaults)
    } else {
      const JValue* sv = root.get("survival");
      double day, hp, wa, sa, hy, we;
      if (!num(sv, "day", day) || !num(sv, "health", hp) || !num(sv, "warmth", wa) || !num(sv, "satiety", sa) ||
          !num(sv, "hydration", hy) || !num(sv, "wetness", we))
        return SaveLoadResult::Corrupt;
      d.day = static_cast<int>(day);
      d.vitals = {static_cast<float>(hp), static_cast<float>(wa), static_cast<float>(sa), static_cast<float>(hy),
                  static_cast<float>(we)};
      const JValue* inv = sv->get("inventory");
      if (!inv || inv->type != JValue::Array || inv->arr.size() != 4) return SaveLoadResult::Corrupt;
      for (int i = 0; i < 4; ++i) {
        if (inv->arr[i]->type != JValue::Number) return SaveLoadResult::Corrupt;
        d.inventory[i] = static_cast<int>(inv->arr[i]->n);
      }
      if (const JValue* r = sv->get("rest")) {
        double rx, ry, rz;
        if (!num(r, "x", rx) || !num(r, "y", ry) || !num(r, "z", rz)) return SaveLoadResult::Corrupt;
        d.hasRestPoint = true;
        d.restPoint = {static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz)};
      }
      const JValue* pk = sv->get("picked");
      const JValue* fr = sv->get("fires");
      if (!pk || pk->type != JValue::Array || !fr || fr->type != JValue::Array) return SaveLoadResult::Corrupt;
      if (pk->arr.size() > SaveData::kMaxPicked || fr->arr.size() > SaveData::kMaxFires) return SaveLoadResult::Invalid;
      for (const auto& e : pk->arr) {
        if (e->type != JValue::Array || e->arr.size() != 2 || e->arr[0]->type != JValue::Number ||
            e->arr[1]->type != JValue::Number)
          return SaveLoadResult::Corrupt;
        const double id = e->arr[0]->n, at = e->arr[1]->n;
        if (!(id >= 1 && id <= 65535) || !(at >= 0.0) || !std::isfinite(at)) return SaveLoadResult::Invalid;
        d.picked.push_back({static_cast<uint16_t>(id), at});
      }
      for (const auto& e : fr->arr) {
        if (e->type != JValue::Array || e->arr.size() != 4) return SaveLoadResult::Corrupt;
        float v[4];
        for (int i = 0; i < 4; ++i) {
          if (e->arr[i]->type != JValue::Number || !std::isfinite(e->arr[i]->n)) return SaveLoadResult::Corrupt;
          v[i] = static_cast<float>(e->arr[i]->n);
        }
        d.fires.push_back({{v[0], v[1], v[2]}, v[3]});
      }
    }
  } else {
    return SaveLoadResult::UnsupportedVersion;
  }

  if (d.worldSeed != expectedSeed) return SaveLoadResult::WrongWorld;
  // Validation: anything outside the playable world is rejected rather than clamped.
  const bool ok = std::fabs(d.playerPos.x) <= 512.0f && std::fabs(d.playerPos.z) <= 512.0f &&
                  d.playerPos.y > -50.0f && d.playerPos.y < 1000.0f && std::isfinite(d.playerYaw) &&
                  d.hours >= 0.0f && d.hours < 24.0f && d.playSeconds >= 0.0 && d.quality >= 0 && d.quality <= 2 &&
                  d.mouseSensitivity > 0.05f && d.mouseSensitivity < 10.0f;
  bool survivalOk = d.day >= 1 && d.day <= 100000;
  auto pct = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 100.0f; };
  survivalOk = survivalOk && pct(d.vitals.health) && pct(d.vitals.warmth) && pct(d.vitals.satiety) &&
               pct(d.vitals.hydration) && std::isfinite(d.vitals.wetness) && d.vitals.wetness >= 0.0f &&
               d.vitals.wetness <= 1.0f;
  for (int i = 0; i < 4; ++i) survivalOk = survivalOk && d.inventory[i] >= 0 && d.inventory[i] <= 64;
  auto inWorld = [](Vec3 p) { return std::fabs(p.x) <= 512.0f && std::fabs(p.z) <= 512.0f && p.y > -50.0f && p.y < 1000.0f; };
  if (d.hasRestPoint) survivalOk = survivalOk && inWorld(d.restPoint);
  for (const SavedFire& f : d.fires) survivalOk = survivalOk && inWorld(f.pos) && f.fuel >= 0.0f && f.fuel <= 3600.0f;
  if (!survivalOk) return SaveLoadResult::Invalid;
  if (!ok) return SaveLoadResult::Invalid;
  out = d;
  return migrated ? SaveLoadResult::Migrated : SaveLoadResult::Ok;
}

}  // namespace aaa
