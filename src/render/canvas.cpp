#include "render/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "render/font.hpp"

namespace render {

namespace {

const char* kVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aSharp;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
out vec2 vSharp;
void main() {
  vUv = aUv;
  vColor = aColor;
  vSharp = aSharp;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

// Textures hold premultiplied colour; the vertex colour tints them. A picture of the original
// enlarged (vSharp: window pixels per picture pixel, > 1) keeps its pixels sharp squares: the
// bilinear blend is squeezed into a window pixel at their edges ("sharp bilinear"), so they all
// come out the same size however the window is.
const char* kFS = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
in vec2 vSharp;
uniform sampler2D uImage;
out vec4 fragColor;
void main() {
  vec2 uv = vUv;
  if (vSharp.x > 1.0) {
    vec2 size = vec2(textureSize(uImage, 0));
    vec2 texel = uv * size;
    vec2 d = fract(texel) - 0.5;
    vec2 range = 0.5 - 0.5 / vSharp;
    uv = (floor(texel) + (d - clamp(d, -range, range)) * vSharp + 0.5) / size;
  }
  vec4 t = texture(uImage, uv);
  fragColor = t * vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

// The original's font: 32 x 8 cells of 17 x 17 pixels (Latin-1 codes), white glyph pixels
// standing on row 9 of their cell, proportional.
constexpr int kCell = 17, kGlyphRows = 13;
constexpr float kCapHeight = 8.0f;  // the capitals, from the top row of a glyph

}  // namespace

Texture& Texture::operator=(Texture&& o) noexcept {
  if (this != &o) {
    if (id) glDeleteTextures(1, &id);
    id = o.id, width = o.width, height = o.height;
    o.id = 0;
  }
  return *this;
}

Texture::~Texture() {
  if (id) glDeleteTextures(1, &id);
}

static void setup(GLuint& id) {
  if (!id) glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void Texture::upload(const gamedata::Picture& p) {
  std::vector<uint8_t> px = p.rgba;
  for (size_t i = 0; i + 3 < px.size(); i += 4)
    for (int k = 0; k < 3; ++k) px[i + k] = uint8_t(px[i + k] * px[i + 3] / 255);
  setup(id);
  width = p.width, height = p.height;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

void Texture::uploadRgb(int w, int h, const std::vector<unsigned char>& rgb) {
  setup(id);
  width = w, height = h;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
}

Canvas::~Canvas() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (white_) glDeleteTextures(1, &white_);
  if (fboTex_) glDeleteTextures(1, &fboTex_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
}

bool Canvas::init(const std::string& fontPath) {
  if (!prog_.build("canvas", kVS, kFS)) return false;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, p));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, uv));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, sharp));
  glBindVertexArray(0);
  const unsigned char white[4] = {255, 255, 255, 255};
  setup(white_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

  // The font as white glyphs on transparent ground.
  gamedata::Picture pic;
  std::string error;
  if (!fontPath.empty() && gamedata::loadPbm(fontPath, pic, -1, error) && pic.width >= 32 * kCell) {
    for (size_t i = 0; i + 3 < pic.rgba.size(); i += 4) {
      bool lit = pic.rgba[i] > 200 && pic.rgba[i + 1] > 200 && pic.rgba[i + 2] > 200;
      pic.rgba[i] = pic.rgba[i + 1] = pic.rgba[i + 2] = pic.rgba[i + 3] = lit ? 255 : 0;
    }
    for (int code = 0; code < 256; ++code) {
      const int x0 = (code % 32) * kCell + 1, y0 = (code / 32) * kCell + 1;
      int right = -1;
      for (int y = 0; y < kCell - 1; ++y)
        for (int x = 0; x < kCell - 1; ++x)
          if (pic.rgba[(size_t(y0 + y) * pic.width + x0 + x) * 4 + 3]) right = std::max(right, x);
      advance_[code] = right < 0 ? 4.0f : float(right + 2);
    }
    font_.upload(pic);
    originalFont_ = true;
  } else {
    // The HUD's 5 x 7 font in the same layout.
    pic = {};
    pic.width = 32 * kCell, pic.height = 8 * kCell;
    pic.rgba.assign(size_t(pic.width) * pic.height * 4, 0);
    for (int code = 32; code < 128; ++code) {
      const GlyphRows& g = glyphRows(char(code));
      const int x0 = (code % 32) * kCell + 1, y0 = (code / 32) * kCell + 1 + 2;
      for (int y = 0; y < kGlyphH; ++y)
        for (int x = 0; x < kGlyphW; ++x)
          if (g[size_t(y)] & (0x10 >> x))
            for (int k = 0; k < 4; ++k) pic.rgba[(size_t(y0 + y + 1) * pic.width + x0 + x) * 4 + size_t(k)] = 255;
      advance_[code] = code == ' ' ? 4.0f : float(kGlyphW + 1);
    }
    font_.upload(pic);
  }
  return true;
}

void Canvas::setBackdrop(const std::string& path) {
  gamedata::Picture pic;
  std::string error;
  if (!path.empty() && gamedata::loadPbm(path, pic, -1, error)) backdrop_.upload(pic);
  else std::fprintf(stderr, "%s: %s\n", path.c_str(), path.empty() ? "not found" : error.c_str());
}

void Canvas::target(int w, int h, float scale, glm::vec2 offset) {
  w_ = w, h_ = h, scale_ = scale, offset_ = offset;
  glViewport(0, 0, w, h);
  prog_.use();
  glUniform2f(prog_.loc("uScreen"), float(w), float(h));
}

void Canvas::beginFixed(int w, int h, int scale) {
  if (scale < 1 || float(w) < kMinWidth * float(scale) || float(h) < kMinHeight * float(scale)) {
    begin(w, h);
    return;
  }
  winW_ = w, winH_ = h;
  winScale_ = float(scale);
  winOffset_ = glm::vec2(0.0f);
  sw_ = float(w) / winScale_, sh_ = float(h) / winScale_;
  offscreen_ = false;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &windowFbo_);
  target(w, h, winScale_, winOffset_);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUniform1i(prog_.loc("uImage"), 0);
  glActiveTexture(GL_TEXTURE0);
  bound_ = 0;
}

void Canvas::begin(int w, int h) {
  winW_ = w, winH_ = h;
  sw_ = kWidth, sh_ = kHeight;
  winScale_ = std::min(float(w) / kWidth, float(h) / kHeight);
  winOffset_ = (glm::vec2(w, h) - glm::vec2(kWidth, kHeight) * winScale_) * 0.5f;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &windowFbo_);
  target(w, h, winScale_, winOffset_);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUniform1i(prog_.loc("uImage"), 0);
  glActiveTexture(GL_TEXTURE0);
  bound_ = 0;
  // Beside the 640 x 480 screen (a wider or taller window): the backdrop, the whole window
  // covered, and the screen's shadow on it.
  if (backdrop_.loaded() && (offset_.x >= 1.0f || offset_.y >= 1.0f)) {
    const float cover = std::max(float(w) / kWidth, float(h) / kHeight) / scale_;
    const glm::vec2 size = glm::vec2(kWidth, kHeight) * cover;
    image(backdrop_, glm::vec2(kWidth, kHeight) * 0.5f - size * 0.5f, size);
    const glm::vec4 shadow{0.03f, 0.03f, 0.03f, 0.75f};
    constexpr float d = 6.0f;
    if (offset_.x >= 1.0f) fill({-d, 0}, {0, kHeight}, shadow), fill({kWidth, 0}, {kWidth + d, kHeight}, shadow);
    if (offset_.y >= 1.0f) fill({0, -d}, {kWidth, 0}, shadow), fill({0, kHeight}, {kWidth, kHeight + d}, shadow);
  }
  // Enlarged by a fraction: the screen drawn a whole number of times as large first.
  const float whole = std::ceil(winScale_ - 0.02f);
  offscreen_ = winScale_ > 1.0f && std::fabs(winScale_ - std::round(winScale_)) > 0.02f;
  if (!offscreen_) return;
  flush();
  const int fw = int(kWidth * whole), fh = int(kHeight * whole);
  if (!fbo_) glGenFramebuffers(1, &fbo_);
  if (fw != fboW_ || fh != fboH_) {
    if (!fboTex_) glGenTextures(1, &fboTex_);
    glBindTexture(GL_TEXTURE_2D, fboTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo_));
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex_, 0);
    fboW_ = fw, fboH_ = fh;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  target(fw, fh, whole, glm::vec2(0.0f));
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  bound_ = 0;
}

void Canvas::end() {
  flush();
  noClip();
  if (offscreen_) {
    // The enlarged screen into the window, smoothly made smaller (upside down: GL's rows).
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(windowFbo_));
    target(winW_, winH_, winScale_, winOffset_);
    use(fboTex_);
    quad({0, 0}, {kWidth, 0}, {kWidth, kHeight}, {0, kHeight}, glm::vec4(1.0f), {0, 1, 1, 0});
    flush();
    offscreen_ = false;
  }
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void Canvas::use(GLuint texture) {
  if (texture == bound_) return;
  flush();
  bound_ = texture;
}

void Canvas::flush() {
  if (verts_.empty()) return;
  prog_.use();
  glBindTexture(GL_TEXTURE_2D, bound_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(V)), verts_.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
  glBindVertexArray(0);
  verts_.clear();
}

void Canvas::quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::vec4 col, glm::vec4 uv, glm::vec2 sharp) {
  auto px = [&](glm::vec2 p) { return offset_ + p * scale_; };
  const V va{px(a), {uv.x, uv.y}, col, sharp}, vb{px(b), {uv.z, uv.y}, col, sharp}, vc{px(c), {uv.z, uv.w}, col, sharp},
      vd{px(d), {uv.x, uv.w}, col, sharp};
  for (const V& v : {va, vb, vc, va, vc, vd}) verts_.push_back(v);
}

void Canvas::fill(glm::vec2 lo, glm::vec2 hi, glm::vec4 c) {
  use(white_);
  quad(lo, {hi.x, lo.y}, hi, {lo.x, hi.y}, c);
}

void Canvas::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 col) {
  use(white_);
  quad(a, b, c, c, col);
}

void Canvas::frame(glm::vec2 lo, glm::vec2 hi, glm::vec4 c, float width) {
  fill(lo, {hi.x, lo.y + width}, c);
  fill({lo.x, hi.y - width}, hi, c);
  fill({lo.x, lo.y + width}, {lo.x + width, hi.y - width}, c);
  fill({hi.x - width, lo.y + width}, {hi.x, hi.y - width}, c);
}

void Canvas::line(glm::vec2 a, glm::vec2 b, glm::vec4 c, float width) {
  glm::vec2 d = b - a;
  float len = glm::length(d);
  if (len < 1e-4f) return;
  glm::vec2 n = glm::vec2(-d.y, d.x) / len * (width * 0.5f);
  use(white_);
  quad(a - n, b - n, b + n, a + n, c);
}

void Canvas::circle(glm::vec2 centre, float r, glm::vec4 c, float width) {
  const int n = std::clamp(int(r * 1.5f), 12, 48);
  for (int i = 0; i < n; ++i) {
    float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
    line(centre + r * glm::vec2(std::cos(a0), std::sin(a0)), centre + r * glm::vec2(std::cos(a1), std::sin(a1)), c, width);
  }
}

void Canvas::image(const Texture& t, glm::vec2 pos, glm::vec2 size, glm::vec4 uv, glm::vec4 tint) {
  if (!t.loaded()) return;
  if (size.x < 0.0f) size = glm::vec2(t.width * (uv.z - uv.x), t.height * (uv.w - uv.y));
  use(t.id);
  const glm::vec2 texels(t.width * (uv.z - uv.x), t.height * (uv.w - uv.y));
  quad(pos, {pos.x + size.x, pos.y}, pos + size, {pos.x, pos.y + size.y}, tint, uv, glm::abs(size * scale_ / texels));
}

float Canvas::pixelSize(float size) const {
  // The font's pixels a whole number of the window's (an uneven 2.25 makes strokes 2 and 3
  // pixels wide): as large as fits in the size asked for, never below one window pixel each.
  const float px = size * scale_;
  return px >= 1.0f ? std::floor(px + 1e-3f) / scale_ : size;
}

float Canvas::textWidth(const std::string& s, float size) const {
  float w = 0.0f;
  for (unsigned char ch : s) w += advance_[ch];
  return w * pixelSize(size);
}

float Canvas::text(const std::string& s, glm::vec2 pos, glm::vec4 c, int align, float size) {
  const float w = textWidth(s, size);
  const float k = pixelSize(size);
  // The capitals stay centred where the layout has them; on whole window pixels.
  auto snap = [&](float v, float off) { return (std::round(off + v * scale_) - off) / scale_; };
  float x = snap(pos.x - (align == 0 ? w * 0.5f : align > 0 ? w : 0.0f), offset_.x);
  const float y = snap(pos.y + (size - k) * kCapHeight * 0.5f, offset_.y);
  use(font_.id);
  const float fw = float(font_.width), fh = float(font_.height);
  for (unsigned char ch : s) {
    const float u0 = float((ch % 32) * kCell + 1), v0 = float((ch / 32) * kCell + 1);
    const float cw = advance_[ch];
    if (ch != ' ')
      quad({x, y}, {x + cw * k, y}, {x + cw * k, y + kGlyphRows * k}, {x, y + kGlyphRows * k}, c,
           {u0 / fw, v0 / fh, (u0 + cw) / fw, (v0 + kGlyphRows) / fh}, glm::vec2(k * scale_));
    x += cw * k;
  }
  return w;
}

std::vector<std::string> Canvas::wrap(const std::string& s, float width, float size) const {
  std::vector<std::string> lines;
  std::string line, word;
  auto put = [&]() {
    if (word.empty()) return;
    std::string tried = line.empty() ? word : line + " " + word;
    if (!line.empty() && textWidth(tried, size) > width) {
      lines.push_back(line);
      line = word;
    } else {
      line = tried;
    }
    word.clear();
  };
  for (char ch : s) {
    if (ch == ' ' || ch == '\n') {
      put();
      if (ch == '\n') lines.push_back(line), line.clear();
    } else {
      word += ch;
    }
  }
  put();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

void Canvas::clip(glm::vec2 lo, glm::vec2 hi) {
  flush();
  glm::vec2 a = offset_ + lo * scale_, b = offset_ + hi * scale_;
  glEnable(GL_SCISSOR_TEST);
  glScissor(int(std::floor(a.x)), int(std::floor(float(h_) - b.y)), int(std::ceil(b.x - a.x)), int(std::ceil(b.y - a.y)));
}

void Canvas::noClip() {
  flush();
  glDisable(GL_SCISSOR_TEST);
}

}  // namespace render
