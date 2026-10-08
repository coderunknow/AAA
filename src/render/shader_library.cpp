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
  // Explicit per-backend mapping (PROMPT §8.1). There is deliberately no silent
  // fallback: an unsupported backend returns nullptr and the caller fails clearly.
  //   web (WebGL2 / GLES3): essl    Linux OpenGL: glsl    Linux Vulkan: spirv
  //   Windows D3D11/12: dx11        macOS Metal: metal    headless Noop: essl
  switch (bgfx::getRendererType()) {
    case bgfx::RendererType::OpenGL: return "glsl";
    case bgfx::RendererType::OpenGLES: return "essl";
    case bgfx::RendererType::Vulkan: return "spirv";
    case bgfx::RendererType::Direct3D11: return "dx11";
    case bgfx::RendererType::Direct3D12: return "dx11";  // SM 5.0 bytecode, as D3D11
    case bgfx::RendererType::Metal: return "metal";
    case bgfx::RendererType::Noop: return "essl";  // headless builds compile essl; Noop never executes it
    case bgfx::RendererType::Agc:
    case bgfx::RendererType::Gnm:
    case bgfx::RendererType::Nvn:
    case bgfx::RendererType::WebGPU:  // never used or claimed on the web (WebGL2 backend)
    case bgfx::RendererType::Count:
    default: return nullptr;
  }
}

bgfx::ShaderHandle ShaderLibrary::shader(const char* name) {
  auto it = shaders_.find(name);
  if (it != shaders_.end()) return it->second;
  const char* profile = profileDir();
  if (!profile) {
    AAA_LOG_ERROR("no compiled shader profile for backend %s (shader '%s')",
                  bgfx::getRendererName(bgfx::getRendererType()), name);
    errors_ = true;
    return BGFX_INVALID_HANDLE;
  }
  const std::string path = root_ + "/shaders/" + profile + "/" + name + ".bin";
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
