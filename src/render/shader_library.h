#pragma once
// Loads compiled shader binaries (assets/shaders/<profile>/<name>.bin) and links programs.
#include <bgfx/bgfx.h>

#include <string>
#include <unordered_map>

namespace aaa {

class ShaderLibrary {
 public:
  explicit ShaderLibrary(std::string assetRoot) : root_(std::move(assetRoot)) {}
  ~ShaderLibrary();

  // Returns BGFX_INVALID_HANDLE (and logs) if a binary is missing or invalid.
  bgfx::ProgramHandle program(const char* vs, const char* fs);
  // Compiled-shader profile directory for the active backend, or nullptr when the
  // backend has no profile (unsupported) — callers must fail clearly, not fall back.
  const char* profileDir() const;
  int loadedShaderCount() const { return static_cast<int>(shaders_.size()); }
  bool hadErrors() const { return errors_; }

 private:
  bgfx::ShaderHandle shader(const char* name);

  std::string root_;
  std::unordered_map<std::string, bgfx::ShaderHandle> shaders_;
  std::unordered_map<std::string, bgfx::ProgramHandle> programs_;
  bool errors_ = false;
};

}  // namespace aaa
