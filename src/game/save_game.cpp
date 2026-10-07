#include "game/save_game.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace aaa {
namespace {

// --- minimal JSON (objects, numbers, strings, bools, null) -----------------------------------
struct JValue {
  enum Type { Null, Bool, Number, String, Object } type = Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::map<std::string, std::shared_ptr<JValue>> obj;
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
  s += "}}";
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
  } else if (version == 2) {
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
  } else {
    return SaveLoadResult::UnsupportedVersion;
  }

  if (d.worldSeed != expectedSeed) return SaveLoadResult::WrongWorld;
  // Validation: anything outside the playable world is rejected rather than clamped.
  const bool ok = std::fabs(d.playerPos.x) <= 512.0f && std::fabs(d.playerPos.z) <= 512.0f &&
                  d.playerPos.y > -50.0f && d.playerPos.y < 1000.0f && std::isfinite(d.playerYaw) &&
                  d.hours >= 0.0f && d.hours < 24.0f && d.playSeconds >= 0.0 && d.quality >= 0 && d.quality <= 2 &&
                  d.mouseSensitivity > 0.05f && d.mouseSensitivity < 10.0f;
  if (!ok) return SaveLoadResult::Invalid;
  out = d;
  return migrated ? SaveLoadResult::Migrated : SaveLoadResult::Ok;
}

}  // namespace aaa
