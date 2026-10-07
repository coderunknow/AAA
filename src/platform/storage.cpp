#include "platform/storage.h"

#include <SDL3/SDL.h>

#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <cstdlib>
#endif

#include "core/log.h"

namespace aaa {

std::optional<std::string> MemoryStorage::load(const std::string& key) {
  for (int i = 0; i < count_; ++i)
    if (keys_[i] == key) return values_[i];
  return std::nullopt;
}
bool MemoryStorage::save(const std::string& key, const std::string& value) {
  for (int i = 0; i < count_; ++i)
    if (keys_[i] == key) { values_[i] = value; return true; }
  if (count_ >= 16) return false;
  keys_[count_] = key;
  values_[count_++] = value;
  return true;
}
bool MemoryStorage::remove(const std::string& key) {
  for (int i = 0; i < count_; ++i)
    if (keys_[i] == key) {
      keys_[i] = keys_[count_ - 1];
      values_[i] = values_[--count_];
      return true;
    }
  return false;
}

#if defined(__EMSCRIPTEN__)
// localStorage can throw (privacy mode, quota) — every access is guarded.
EM_JS(char*, aaa_ls_get, (const char* key), {
  try {
    const v = window.localStorage.getItem(UTF8ToString(key));
    if (v === null) return 0;
    const n = lengthBytesUTF8(v) + 1;
    const p = _malloc(n);
    stringToUTF8(v, p, n);
    return p;
  } catch (e) { return 0; }
});
EM_JS(int, aaa_ls_set, (const char* key, const char* value), {
  try { window.localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); return 1; } catch (e) { return 0; }
});
EM_JS(int, aaa_ls_remove, (const char* key), {
  try { window.localStorage.removeItem(UTF8ToString(key)); return 1; } catch (e) { return 0; }
});

namespace {
class WebStorage final : public KeyValueStorage {
 public:
  std::optional<std::string> load(const std::string& key) override {
    char* p = aaa_ls_get(key.c_str());
    if (!p) return std::nullopt;
    std::string s(p);
    std::free(p);
    return s;
  }
  bool save(const std::string& key, const std::string& value) override {
    return aaa_ls_set(key.c_str(), value.c_str()) != 0;
  }
  bool remove(const std::string& key) override { return aaa_ls_remove(key.c_str()) != 0; }
};
}  // namespace

KeyValueStorage& platformStorage() {
  static WebStorage s;
  return s;
}
#else
namespace {
class FileStorage final : public KeyValueStorage {
 public:
  FileStorage() {
    char* p = SDL_GetPrefPath("Mistpine", "Mistpine");
    if (p) {
      dir_ = p;
      SDL_free(p);
    }
  }
  std::optional<std::string> load(const std::string& key) override {
    if (dir_.empty()) return std::nullopt;
    size_t size = 0;
    void* data = SDL_LoadFile(path(key).c_str(), &size);
    if (!data) return std::nullopt;
    std::string s(static_cast<const char*>(data), size);
    SDL_free(data);
    return s;
  }
  bool save(const std::string& key, const std::string& value) override {
    if (dir_.empty()) return false;
    // Write-then-rename so a crash never leaves a truncated save.
    const std::string tmp = path(key) + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), value.data(), value.size())) return false;
    return SDL_RenamePath(tmp.c_str(), path(key).c_str());
  }
  bool remove(const std::string& key) override { return !dir_.empty() && SDL_RemovePath(path(key).c_str()); }

 private:
  std::string path(const std::string& key) const { return dir_ + key + ".json"; }
  std::string dir_;
};
}  // namespace

KeyValueStorage& platformStorage() {
  static FileStorage s;
  return s;
}
#endif

}  // namespace aaa
