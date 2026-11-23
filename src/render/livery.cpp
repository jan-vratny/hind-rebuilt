#include "render/livery.hpp"

#include <algorithm>
#include <cmath>

#include "render/gl.hpp"

namespace render {

namespace {

// Smooth value noise wrapping round every `period` cells.
float noise(float x, float y, int period, unsigned seed) {
  auto hash = [&](int i, int j) {
    unsigned h = unsigned((i % period + period) % period) * 73856093u ^ unsigned((j % period + period) % period) * 19349663u ^ seed * 83492791u;
    h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
    return float(h & 0xffff) / 65535.0f;
  };
  const int i = int(std::floor(x)), j = int(std::floor(y));
  float fx = x - float(i), fy = y - float(j);
  fx = fx * fx * (3.0f - 2.0f * fx), fy = fy * fy * (3.0f - 2.0f * fy);
  const float a = hash(i, j), b = hash(i + 1, j), c = hash(i, j + 1), d = hash(i + 1, j + 1);
  return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

float blotches(float u, float v, unsigned seed) {
  // Two octaves: the big patches and their ragged edges.
  return 0.75f * noise(u * 3.0f, v * 3.0f, 3, seed) + 0.25f * noise(u * 9.0f, v * 9.0f, 9, seed + 7);
}

}  // namespace

std::vector<uint8_t> czechCamouflage() {
  // The colours as the photographs show them, about as bright as the original's camouflage.
  const uint8_t olive[3] = {128, 132, 86}, grey[3] = {188, 190, 172}, dark[3] = {84, 92, 76};
  std::vector<uint8_t> t(size_t(kAtlasTile) * kAtlasTile * 4, 255);
  for (int y = 0; y < kAtlasTile; ++y)
    for (int x = 0; x < kAtlasTile; ++x) {
      const float u = float(x) / kAtlasTile, v = float(y) / kAtlasTile;
      const uint8_t* c = olive;
      if (blotches(u, v, 1) > 0.50f) c = grey;
      else if (blotches(u, v, 2) > 0.58f) c = dark;
      std::copy(c, c + 3, &t[(size_t(y) * kAtlasTile + size_t(x)) * 4]);
    }
  return t;
}

std::vector<uint8_t> withLivery(const std::vector<uint8_t>& textures, int& count) {
  const size_t tile = size_t(kAtlasTile) * kAtlasTile * 4;
  std::vector<uint8_t> all = textures;
  all.resize(tile * (kLiveryTile + 1), 255);
  const std::vector<uint8_t> camo = czechCamouflage();
  std::copy(camo.begin(), camo.end(), all.begin() + std::ptrdiff_t(tile * kLiveryTile));
  count = kLiveryTile + 1;
  return all;
}

}  // namespace render
