#pragma once
// Narrow persistence abstraction: string key -> string value.
// Web: window.localStorage. Native: one file per key in the SDL pref path.
#include <optional>
#include <string>

namespace aaa {

class KeyValueStorage {
 public:
  virtual ~KeyValueStorage() = default;
  virtual std::optional<std::string> load(const std::string& key) = 0;
  virtual bool save(const std::string& key, const std::string& value) = 0;
  virtual bool remove(const std::string& key) = 0;
};

// The platform's default storage backend.
KeyValueStorage& platformStorage();

// In-memory backend (tests, headless runs).
class MemoryStorage final : public KeyValueStorage {
 public:
  std::optional<std::string> load(const std::string& key) override;
  bool save(const std::string& key, const std::string& value) override;
  bool remove(const std::string& key) override;

 private:
  std::string keys_[16];
  std::string values_[16];
  int count_ = 0;
};

}  // namespace aaa
