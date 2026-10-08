#pragma once
// Screen-space immediate-mode UI: SDF text (Source Serif 4, SIL OFL-1.1, generated
// into a distance-field atlas at load time) plus solid quads. Drawn in a dedicated
// view after the post pass, so menus/HUD are identical on web and native.
#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace aaa {

class ShaderLibrary;

// Packs a color as 0xAABBGGRR (little-endian RGBA bytes for bgfx Color0).
inline uint32_t uiRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(a) << 24);
}

class UiRenderer {
 public:
  ~UiRenderer();

  // Loads <assetRoot>/fonts/SourceSerif4-{Regular,It}.ttf, builds the SDF atlas and
  // the UI program. Returns false (and logs) when the fonts are missing.
  bool init(ShaderLibrary& shaders, const std::string& assetRoot);
  void shutdown();
  void resize(uint32_t fbW, uint32_t fbH) { fbW_ = fbW; fbH_ = fbH; }
  bool ready() const { return ready_; }

  // Frame lifecycle: begin() clears the command list; draw calls append; submit()
  // renders everything accumulated since begin() into `view`.
  void begin();
  // Solid rectangle, framebuffer pixels, origin top-left, y down.
  void rect(float x, float y, float w, float h, uint32_t rgba);
  // Text. `y` is the top of the line box; `size` is the em height in pixels.
  // align: 0 = left (at x), 1 = centered on x, 2 = right (ends at x).
  // tracking: extra pixels between glyphs. italic selects the italic face.
  void text(float x, float y, float size, uint32_t rgba, const char* str, int align = 0, float tracking = 0.0f,
            bool italic = false);
  float textWidth(float size, const char* str, float tracking = 0.0f, bool italic = false) const;
  float lineHeight(float size) const;  // ascent + descent, pixels, for `size`
  float ascent(float size) const;      // baseline offset from the top of the line box
  void submit(bgfx::ViewId view);

 private:
  struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas UVs
    float w = 0, h = 0;                    // bitmap size in atlas pixels
    float xoff = 0, yoff = 0;              // bitmap offset from the pen/baseline
    float advance = 0;                     // pen advance, atlas pixels
  };
  struct Vertex {
    float x, y, z;
    float u, v;
    uint32_t color;
  };

  const Glyph* glyph(bool italic, uint32_t codepoint) const;
  void loadFace(const std::string& path, bool italic);
  void emitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba);

  bool ready_ = false;
  uint32_t fbW_ = 1280, fbH_ = 720;
  std::vector<uint8_t> atlas_;       // R8, kAtlasW x kAtlasH
  int atlasW_ = 0, atlasH_ = 0;
  int packX_ = 1, packY_ = 1, shelfH_ = 0;  // shelf packer state
  std::vector<Glyph> glyphs_[2];     // [0] regular, [1] italic; index = codepoint - kFirstCp
  float ascent48_ = 0, descent48_ = 0, lineGap48_ = 0;  // 48px face metrics
  std::vector<Vertex> verts_;
  bgfx::VertexLayout layout_{};
  bgfx::TextureHandle atlasTex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sAtlas_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_ = BGFX_INVALID_HANDLE;
};

}  // namespace aaa
