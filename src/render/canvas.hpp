#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gamedata/picture.hpp"
#include "render/gl.hpp"

// Two-dimensional drawing for the menu screens, in the original game's 640 x 480 screen
// coordinates (y down), scaled to fit the window and centred in it.
namespace render {

struct Texture {
  GLuint id = 0;
  int width = 0, height = 0;
  Texture() = default;
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& o) noexcept : id(o.id), width(o.width), height(o.height) { o.id = 0; }
  Texture& operator=(Texture&& o) noexcept;
  ~Texture();
  bool loaded() const { return id != 0; }
  // From a picture (premultiplied alpha), or 8-bit RGB pixels rows from the top.
  void upload(const gamedata::Picture& p);
  void uploadRgb(int w, int h, const std::vector<unsigned char>& rgb);
};

class Canvas {
public:
  static constexpr float kWidth = 640.0f, kHeight = 480.0f;
  static constexpr float kMinWidth = 560.0f, kMinHeight = 400.0f;  // a screen at a fixed scale

  ~Canvas();
  // `fontPath`: the original's FONTS/11X15.LBM; without it the HUD's small font is used.
  bool init(const std::string& fontPath);
  // A picture of the original (MENUSCRN/TWO_PLYR.LBM, plain brushed steel) laid unchanged under
  // the 640 x 480 screen where the window is wider or taller, so that the screen sits on it like
  // a plate.
  void setBackdrop(const std::string& path);

  // Starts a frame for a window of w x h pixels, the 640 x 480 screen as large as it fits.
  void begin(int w, int h);
  // Or at `scale` window pixels to a pixel whatever the window's size, the screen as large as the
  // window then is (a window too small even for kMinWidth x kMinHeight is fitted as by begin).
  void beginFixed(int w, int h, int scale);
  // The screen's size: 640 x 480, or larger at a fixed scale.
  glm::vec2 size() const { return {sw_, sh_}; }
  void end();
  // Window pixels (y down, as SDL gives the mouse) to screen coordinates.
  glm::vec2 toScreen(glm::vec2 window) const { return (window - winOffset_) / winScale_; }
  glm::vec2 toWindow(glm::vec2 screen) const { return winOffset_ + screen * winScale_; }
  float scale() const { return winScale_; }

  void fill(glm::vec2 lo, glm::vec2 hi, glm::vec4 c);
  void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 col);
  void frame(glm::vec2 lo, glm::vec2 hi, glm::vec4 c, float width = 1.0f);
  void line(glm::vec2 a, glm::vec2 b, glm::vec4 c, float width = 1.0f);
  void circle(glm::vec2 centre, float r, glm::vec4 c, float width = 1.0f);
  // A picture at `pos` (its own size unless given), part `uv` of it (x0, y0, x1, y1 in 0..1).
  void image(const Texture& t, glm::vec2 pos, glm::vec2 size = glm::vec2(-1.0f),
             glm::vec4 uv = glm::vec4(0, 0, 1, 1), glm::vec4 tint = glm::vec4(1.0f));
  // Text in the original's font; align -1 left, 0 centre, 1 right. Returns its width.
  float text(const std::string& s, glm::vec2 pos, glm::vec4 c, int align = -1, float size = 1.0f);
  float textWidth(const std::string& s, float size = 1.0f) const;
  float lineHeight(float size = 1.0f) const { return 12.0f * pixelSize(size); }
  // The size text is drawn at for `size`: its pixels whole window pixels (see text).
  float pixelSize(float size) const;
  // Lines of at most `width` broken between words.
  std::vector<std::string> wrap(const std::string& s, float width, float size = 1.0f) const;
  // Drawing is cut to this rectangle until reset with an empty one.
  void clip(glm::vec2 lo, glm::vec2 hi);
  void noClip();

private:
  struct V {
    glm::vec2 p, uv;
    glm::vec4 c;
    glm::vec2 sharp;  // window pixels per texel of an enlarged picture (sharp bilinear), else 0
  };
  void use(GLuint texture);
  void flush();
  void quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::vec4 col, glm::vec4 uv = glm::vec4(0),
            glm::vec2 sharp = glm::vec2(0.0f));

  Program prog_;
  GLuint vao_ = 0, vbo_ = 0, white_ = 0;
  Texture font_, backdrop_;
  float advance_[256] = {};
  bool originalFont_ = false;
  std::vector<V> verts_;
  GLuint bound_ = 0;
  // Where drawing goes: the window, or (enlarged by a fraction, such as 2.25 times) a picture
  // of the screen a whole number of times as large, then made smaller into the window in one
  // piece, so that the font's and the pictures' pixels come out even and everything keeps its
  // proportions.
  void target(int w, int h, float scale, glm::vec2 offset);
  int w_ = 0, h_ = 0;
  float scale_ = 1.0f;
  glm::vec2 offset_{0.0f};
  float sw_ = kWidth, sh_ = kHeight;
  int winW_ = 0, winH_ = 0;
  float winScale_ = 1.0f;
  glm::vec2 winOffset_{0.0f};
  GLuint fbo_ = 0, fboTex_ = 0;
  int fboW_ = 0, fboH_ = 0;
  bool offscreen_ = false;
  GLint windowFbo_ = 0;
};

}  // namespace render
