#include "render/ui_renderer.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "core/log.h"
#include "render/shader_library.h"

// stb_truetype (MIT / public domain) ships with the pinned bgfx; the include directory
// is added as SYSTEM in CMake so its warnings never fail the project -Werror build.
// The implementation lives in this one translation unit (bgfx's own imgui copy of
// stb_truetype is never compiled into our targets).
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace aaa {
namespace {
// Glyph set: printable ASCII plus the punctuation the UI copy uses.
constexpr uint32_t kFirstCp = 32;
constexpr uint32_t kLastCp = 126;
const uint32_t kExtraCp[] = {0x00B0, 0x00B7, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2192};
constexpr int kCpCount = static_cast<int>(kLastCp - kFirstCp + 1) + static_cast<int>(sizeof(kExtraCp) / sizeof(kExtraCp[0]));
// Glyphs are rasterised into the atlas at this em height; the SDF shader scales.
constexpr float kAtlasPx = 48.0f;
constexpr int kAtlasW = 1024, kAtlasH = 512;
constexpr int kPadding = 4;        // SDF padding around each glyph
constexpr float kDistScale = 32.0f;  // SDF intensity per pixel of distance

uint32_t cpIndex(uint32_t cp) {
  if (cp >= kFirstCp && cp <= kLastCp) return cp - kFirstCp;
  for (size_t i = 0; i < sizeof(kExtraCp) / sizeof(kExtraCp[0]); ++i)
    if (kExtraCp[i] == cp) return static_cast<uint32_t>(kLastCp - kFirstCp + 1 + i);
  return static_cast<uint32_t>(('?') - kFirstCp);  // unknown -> '?'
}
}  // namespace

UiRenderer::~UiRenderer() { shutdown(); }

void UiRenderer::shutdown() {
  if (bgfx::isValid(atlasTex_)) bgfx::destroy(atlasTex_);
  if (bgfx::isValid(sAtlas_)) bgfx::destroy(sAtlas_);
  atlasTex_ = BGFX_INVALID_HANDLE;
  sAtlas_ = BGFX_INVALID_HANDLE;
  prog_ = BGFX_INVALID_HANDLE;
  ready_ = false;
}

void UiRenderer::loadFace(const std::string& path, bool italic) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    AAA_LOG_ERROR("ui font missing: %s", path.c_str());
    return;
  }
  const std::streamsize size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> ttf(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(ttf.data()), size);

  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
    AAA_LOG_ERROR("ui font invalid: %s", path.c_str());
    return;
  }
  // Keep the TTF alive for the lifetime of the atlas build (stb needs it only during
  // rasterisation, but the buffer must outlive the calls below).
  const float scale = stbtt_ScaleForPixelHeight(&info, kAtlasPx);
  int ascent = 0, descent = 0, lineGap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
  if (!italic) {
    ascent48_ = static_cast<float>(ascent) * scale;
    descent48_ = static_cast<float>(descent) * scale;
    lineGap48_ = static_cast<float>(lineGap) * scale;
  }

  auto& glyphs = glyphs_[italic ? 1 : 0];
  glyphs.resize(kCpCount);
  for (uint32_t cp = kFirstCp; cp <= kLastCp; ++cp) {
    Glyph& g = glyphs[cpIndex(cp)];
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, static_cast<int>(cp), kPadding, 128, kDistScale, &w, &h,
                                              &xoff, &yoff);
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &advance, &lsb);
    g.advance = static_cast<float>(advance) * scale;
    if (!sdf || w <= 0 || h <= 0) {
      stbtt_FreeSDF(sdf, info.userdata);
      g.w = g.h = 0;  // whitespace / missing: nothing to draw
      continue;
    }
    // Shelf-pack into the atlas (texel 0,0 stays pure white for solid quads).
    if (packX_ + w + kPadding > kAtlasW) {
      packX_ = 1;
      packY_ += shelfH_ + kPadding;
      shelfH_ = 0;
    }
    if (packY_ + h + kPadding > kAtlasH) {
      AAA_LOG_ERROR("ui font atlas overflow (%s)", path.c_str());
      stbtt_FreeSDF(sdf, info.userdata);
      g.w = g.h = 0;
      continue;
    }
    for (int row = 0; row < h; ++row)
      std::memcpy(&atlas_[(packY_ + row) * kAtlasW + packX_], sdf + row * w, static_cast<size_t>(w));
    g.u0 = static_cast<float>(packX_) / kAtlasW;
    g.v0 = static_cast<float>(packY_) / kAtlasH;
    g.u1 = static_cast<float>(packX_ + w) / kAtlasW;
    g.v1 = static_cast<float>(packY_ + h) / kAtlasH;
    g.w = static_cast<float>(w);
    g.h = static_cast<float>(h);
    g.xoff = static_cast<float>(xoff);
    g.yoff = static_cast<float>(yoff);
    packX_ += w + kPadding;
    shelfH_ = std::max(shelfH_, h);
    stbtt_FreeSDF(sdf, info.userdata);
  }
  // Extra codepoints beyond ASCII.
  for (uint32_t cp : kExtraCp) {
    Glyph& g = glyphs[cpIndex(cp)];
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, static_cast<int>(cp), kPadding, 128, kDistScale, &w, &h,
                                              &xoff, &yoff);
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &advance, &lsb);
    g.advance = static_cast<float>(advance) * scale;
    if (!sdf || w <= 0 || h <= 0) {
      stbtt_FreeSDF(sdf, info.userdata);
      g.w = g.h = 0;
      continue;
    }
    if (packX_ + w + kPadding > kAtlasW) {
      packX_ = 1;
      packY_ += shelfH_ + kPadding;
      shelfH_ = 0;
    }
    for (int row = 0; row < h; ++row)
      std::memcpy(&atlas_[(packY_ + row) * kAtlasW + packX_], sdf + row * w, static_cast<size_t>(w));
    g.u0 = static_cast<float>(packX_) / kAtlasW;
    g.v0 = static_cast<float>(packY_) / kAtlasH;
    g.u1 = static_cast<float>(packX_ + w) / kAtlasW;
    g.v1 = static_cast<float>(packY_ + h) / kAtlasH;
    g.w = static_cast<float>(w);
    g.h = static_cast<float>(h);
    g.xoff = static_cast<float>(xoff);
    g.yoff = static_cast<float>(yoff);
    packX_ += w + kPadding;
    shelfH_ = std::max(shelfH_, h);
    stbtt_FreeSDF(sdf, info.userdata);
  }
  AAA_LOG_INFO("ui font %s: %d glyphs packed (%s)", path.c_str(), kCpCount, italic ? "italic" : "regular");
}

bool UiRenderer::init(ShaderLibrary& shaders, const std::string& assetRoot) {
  atlasW_ = kAtlasW;
  atlasH_ = kAtlasH;
  atlas_.assign(static_cast<size_t>(kAtlasW) * kAtlasH, 0);
  atlas_[0] = 255;  // texel (0,0): pure white, used by solid quads
  packX_ = 1;
  packY_ = 1;
  loadFace(assetRoot + "/fonts/SourceSerif4-Regular.ttf", false);
  loadFace(assetRoot + "/fonts/SourceSerif4-It.ttf", true);
  if (glyphs_[0].empty() || glyphs_[1].empty()) {
    AAA_LOG_ERROR("ui font atlas build failed under %s", assetRoot.c_str());
    return false;
  }

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  atlasTex_ = bgfx::createTexture2D(static_cast<uint16_t>(kAtlasW), static_cast<uint16_t>(kAtlasH), false, 1,
                                    bgfx::TextureFormat::R8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP);
  if (!bgfx::isValid(atlasTex_)) {
    AAA_LOG_ERROR("ui font atlas texture creation failed");
    return false;
  }
  bgfx::updateTexture2D(atlasTex_, 0, 0, 0, 0, static_cast<uint16_t>(kAtlasW), static_cast<uint16_t>(kAtlasH),
                        bgfx::copy(atlas_.data(), static_cast<uint32_t>(atlas_.size())));

  sAtlas_ = bgfx::createUniform("s_atlas", bgfx::UniformType::Sampler);
  prog_ = shaders.program("vs_ui", "fs_ui");
  if (!bgfx::isValid(prog_)) {
    AAA_LOG_ERROR("ui shader program failed to link");
    return false;
  }
  ready_ = true;
  return true;
}

const UiRenderer::Glyph* UiRenderer::glyph(bool italic, uint32_t codepoint) const {
  if (codepoint < 32) return nullptr;
  const auto& g = glyphs_[italic ? 1 : 0];
  if (g.empty()) return nullptr;
  return &g[cpIndex(codepoint)];
}

void UiRenderer::begin() { verts_.clear(); }

void UiRenderer::emitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                          uint32_t rgba) {
  if (verts_.size() + 6 > 65535) return;  // hard cap: drop overflow rather than corrupt memory
  const float z = 0.0f;
  const Vertex quad[6] = {
      {x, y, z, u0, v0, rgba}, {x + w, y, z, u1, v0, rgba}, {x + w, y + h, z, u1, v1, rgba},
      {x, y, z, u0, v0, rgba}, {x + w, y + h, z, u1, v1, rgba}, {x, y + h, z, u0, v1, rgba},
  };
  verts_.insert(verts_.end(), quad, quad + 6);
}

void UiRenderer::rect(float x, float y, float w, float h, uint32_t rgba) {
  // Solid quad: sample the pure-white texel (0,0) so the SDF threshold passes everywhere.
  const float u = 0.5f / kAtlasW, v = 0.5f / kAtlasH;
  emitQuad(x, y, w, h, u, v, u, v, rgba);
}

float UiRenderer::textWidth(float size, const char* str, float tracking, bool italic) const {
  if (!str) return 0.0f;
  const float s = size / kAtlasPx;
  float width = 0.0f;
  bool first = true;
  for (const char* p = str; *p; ++p) {
    const Glyph* g = glyph(italic, static_cast<uint8_t>(*p));
    if (!g) continue;
    if (!first) width += tracking;
    first = false;
    width += g->advance * s;
  }
  return width;
}

float UiRenderer::lineHeight(float size) const {
  return (ascent48_ - descent48_ + lineGap48_) * (size / kAtlasPx);
}

float UiRenderer::ascent(float size) const { return ascent48_ * (size / kAtlasPx); }

void UiRenderer::text(float x, float y, float size, uint32_t rgba, const char* str, int align, float tracking,
                      bool italic) {
  if (!str || !*str) return;
  const float s = size / kAtlasPx;
  float width = textWidth(size, str, tracking, italic);
  float pen = x - (align == 1 ? width * 0.5f : align == 2 ? width : 0.0f);
  const float baseline = y + ascent48_ * s;
  for (const char* p = str; *p; ++p) {
    const Glyph* g = glyph(italic, static_cast<uint8_t>(*p));
    if (!g) continue;
    if (g->w > 0.0f && g->h > 0.0f) {
      const float gx = pen + g->xoff * s;
      const float gy = baseline + g->yoff * s;
      emitQuad(gx, gy, g->w * s, g->h * s, g->u0, g->v0, g->u1, g->v1, rgba);
    }
    pen += g->advance * s + tracking;
  }
}

void UiRenderer::submit(bgfx::ViewId view) {
  if (!ready_ || verts_.empty()) return;
  const uint32_t count = static_cast<uint32_t>(verts_.size());
  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, count, layout_);
  if (tvb.data == nullptr || tvb.size < count * sizeof(Vertex)) {
    AAA_LOG_WARN("ui transient vertex buffer exhausted (%u verts)", count);
    return;
  }
  std::memcpy(tvb.data, verts_.data(), static_cast<size_t>(count) * sizeof(Vertex));

  const bgfx::Caps* caps = bgfx::getCaps();
  float viewM[16], proj[16];
  bx::mtxIdentity(viewM);
  // Pixel coordinates, origin top-left, y down — the same ortho bgfx uses for dbgText.
  bx::mtxOrtho(proj, 0.0f, static_cast<float>(fbW_), static_cast<float>(fbH_), 0.0f, -1.0f, 1000.0f, 0.0f,
               caps->homogeneousDepth);
  bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
  bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(fbW_), static_cast<uint16_t>(fbH_));
  bgfx::setViewTransform(view, viewM, proj);
  bgfx::setTexture(0, sAtlas_, atlasTex_, BGFX_SAMPLER_UVW_CLAMP);
  bgfx::setVertexBuffer(0, &tvb, 0, count);
  // Premultiplied alpha; the blend also keeps destination alpha at 1 (opaque canvas).
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                 BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
  bgfx::submit(view, prog_);
}

}  // namespace aaa
