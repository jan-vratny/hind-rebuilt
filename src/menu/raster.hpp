#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// A picture drawn pixel by pixel, as the original draws its planning map: lines without
// smoothing, filled polygons, circles; RGB rows from the top.
namespace menu {

class Raster {
public:
  void resize(int w, int h) {
    w_ = w, h_ = h;
    rgb_.assign(size_t(w) * size_t(h) * 3, 0);
  }
  int width() const { return w_; }
  int height() const { return h_; }
  const std::vector<unsigned char>& pixels() const { return rgb_; }

  void set(int x, int y, glm::u8vec3 c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    unsigned char* p = &rgb_[(size_t(y) * size_t(w_) + size_t(x)) * 3];
    p[0] = c.r, p[1] = c.g, p[2] = c.b;
  }
  void line(glm::vec2 a, glm::vec2 b, glm::u8vec3 c);
  void circle(glm::vec2 centre, float r, glm::u8vec3 c);
  void disc(glm::vec2 centre, float r, glm::u8vec3 c);
  // Even-odd fill of the pixels whose centres are inside.
  void polygon(const std::vector<glm::vec2>& pts, glm::u8vec3 c);
  void rect(glm::ivec2 lo, glm::ivec2 hi, glm::u8vec3 c);  // outline, lo .. hi inclusive

private:
  int w_ = 0, h_ = 0;
  std::vector<unsigned char> rgb_;
};

}  // namespace menu
