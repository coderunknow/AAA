#include "render/shader_library.h"

#include <fstream>
#include <vector>

#include "core/log.h"

namespace aaa {

ShaderLibrary::~ShaderLibrary() {
  for (auto& [k, p] : programs_) bgfx::destroy(p);
  for (auto& [k, s] : shaders_) bgfx::destroy(s);
}

const char* ShaderLibrary::profileDir() const {
  switch (bgfx::getRendererType()) {
    case bgfx::RendererType::OpenGL: return "glsl";
    case bgfx::RendererType::Vulkan: return "spirv";
    case bgfx::RendererType::OpenGLES:
    case bgfx::RendererType::Noop:
    default: return "essl";
  }
}

bgfx::ShaderHandle ShaderLibrary::shader(const char* name) {
  auto it = shaders_.find(name);
  if (it != shaders_.end()) return it->second;
  const std::string path = root_ + "/shaders/" + profileDir() + "/" + name + ".bin";
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    AAA_LOG_ERROR("shader binary missing: %s", path.c_str());
    errors_ = true;
    return BGFX_INVALID_HANDLE;
  }
  const std::streamsize size = f.tellg();
  f.seekg(0);
  const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(size) + 1);
  f.read(reinterpret_cast<char*>(mem->data), size);
  mem->data[size] = 0;
  bgfx::ShaderHandle h = bgfx::createShader(mem);
  if (!bgfx::isValid(h)) {
    AAA_LOG_ERROR("shader create failed: %s", path.c_str());
    errors_ = true;
  } else {
    bgfx::setName(h, name);
  }
  shaders_[name] = h;
  return h;
}

bgfx::ProgramHandle ShaderLibrary::program(const char* vs, const char* fs) {
  const std::string key = std::string(vs) + "|" + fs;
  auto it = programs_.find(key);
  if (it != programs_.end()) return it->second;
  bgfx::ShaderHandle v = shader(vs), f = shader(fs);
  bgfx::ProgramHandle p = BGFX_INVALID_HANDLE;
  if (bgfx::isValid(v) && bgfx::isValid(f)) p = bgfx::createProgram(v, f, false);
  if (!bgfx::isValid(p)) {
    AAA_LOG_ERROR("program link failed: %s + %s", vs, fs);
    errors_ = true;
  }
  programs_[key] = p;
  return p;
}

}  // namespace aaa
