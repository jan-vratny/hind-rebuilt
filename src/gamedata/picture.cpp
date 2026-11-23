#include "gamedata/picture.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gamedata {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }

}  // namespace

bool loadPalette(const std::string& path, int first, PaletteRange& out, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  out.first = first;
  out.rgb.assign(std::istreambuf_iterator<char>(f), {});
  if (out.rgb.empty() || out.rgb.size() % 3 || first < 0 || size_t(first) + out.rgb.size() / 3 > 256) {
    error = path + ": not a palette";
    out.rgb.clear();
    return false;
  }
  return true;
}

bool loadPbm(const std::string& path, Picture& out, int transparent, std::string& error, const PaletteRange* replace) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> d(std::istreambuf_iterator<char>(f), {});
  if (d.size() < 12 || std::string(d.begin(), d.begin() + 4) != "FORM" || std::string(d.begin() + 8, d.begin() + 12) != "PBM ") {
    error = path + ": not an IFF PBM picture";
    return false;
  }
  const uint8_t *bmhd = nullptr, *cmap = nullptr, *body = nullptr;
  size_t cmapLen = 0, bodyLen = 0;
  for (size_t pos = 12; pos + 8 <= d.size();) {
    std::string id(d.begin() + pos, d.begin() + pos + 4);
    size_t n = be32(&d[pos + 4]);
    if (pos + 8 + n > d.size()) break;
    const uint8_t* p = &d[pos + 8];
    if (id == "BMHD" && n >= 20) bmhd = p;
    if (id == "CMAP") cmap = p, cmapLen = n;
    if (id == "BODY") body = p, bodyLen = n;
    pos += 8 + n + (n & 1);
  }
  if (!bmhd || !cmap || !body) {
    error = path + ": incomplete picture";
    return false;
  }
  const int w = int(be16(bmhd)), h = int(be16(bmhd + 2));
  const int planes = bmhd[8], compression = bmhd[10];
  if (planes != 8 || w <= 0 || h <= 0 || compression > 1) {
    error = path + ": unsupported picture format";
    return false;
  }
  // Rows are padded to an even number of bytes.
  const size_t row = size_t(w + (w & 1));
  std::vector<uint8_t> idx;
  idx.reserve(row * h);
  if (compression == 0) {
    idx.assign(body, body + std::min(bodyLen, row * h));
  } else {
    for (size_t i = 0; i < bodyLen && idx.size() < row * h;) {
      int c = body[i++];
      if (c < 128) {
        for (int k = 0; k <= c && i < bodyLen; ++k) idx.push_back(body[i++]);
      } else if (c > 128 && i < bodyLen) {
        idx.insert(idx.end(), size_t(257 - c), body[i++]);
      }
    }
  }
  if (idx.size() < row * h) {
    error = path + ": picture data cut short";
    return false;
  }
  std::vector<uint8_t> pal(768, 0);
  std::copy(cmap, cmap + std::min<size_t>(cmapLen, pal.size()), pal.begin());
  if (replace) std::copy(replace->rgb.begin(), replace->rgb.end(), pal.begin() + replace->first * 3);
  out.width = w;
  out.height = h;
  out.rgba.resize(size_t(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t c = idx[size_t(y) * row + x];
      uint8_t* px = &out.rgba[(size_t(y) * w + x) * 4];
      for (int k = 0; k < 3; ++k) px[k] = pal[c * 3 + k];
      px[3] = c == transparent ? 0 : 255;
    }
  return true;
}

bool loadRaw(const std::string& path, int width, const PaletteRange& palette, Picture& out, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> d(std::istreambuf_iterator<char>(f), {});
  if (width <= 0 || d.empty() || d.size() % size_t(width)) {
    error = path + ": not a picture " + std::to_string(width) + " pixels wide";
    return false;
  }
  out.width = width;
  out.height = int(d.size() / size_t(width));
  out.rgba.assign(d.size() * 4, 0);
  for (size_t i = 0; i < d.size(); ++i) {
    size_t at = (size_t(d[i]) - size_t(palette.first)) * 3;
    if (d[i] == 0 || d[i] < palette.first || at + 3 > palette.rgb.size()) continue;
    std::copy(&palette.rgb[at], &palette.rgb[at] + 3, &out.rgba[i * 4]);
    out.rgba[i * 4 + 3] = 255;
  }
  return true;
}

bool loadPam(const std::string& path, Picture& out, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = path + ": not found";
    return false;
  }
  std::string line;
  int width = 0, height = 0, depth = 0, maxval = 0;
  std::getline(f, line);
  bool ok = line == "P7";
  while (ok && std::getline(f, line) && line != "ENDHDR") {
    std::istringstream l(line);
    std::string key;
    l >> key;
    if (key == "WIDTH") l >> width;
    else if (key == "HEIGHT") l >> height;
    else if (key == "DEPTH") l >> depth;
    else if (key == "MAXVAL") l >> maxval;
  }
  ok = ok && line == "ENDHDR" && width > 0 && height > 0 && depth == 4 && maxval == 255;
  if (ok) {
    out.width = width, out.height = height;
    out.rgba.assign(size_t(width) * size_t(height) * 4, 0);
    ok = bool(f.read(reinterpret_cast<char*>(out.rgba.data()), std::streamsize(out.rgba.size())));
  }
  if (!ok) error = path + ": not an RGB_ALPHA PAM picture";
  return ok;
}

bool stackOver(const Picture& top, Picture& picture) {
  if (top.width != picture.width || top.height <= 0) return false;
  picture.rgba.insert(picture.rgba.begin(), top.rgba.begin(), top.rgba.end());
  picture.height += top.height;
  return true;
}

}  // namespace gamedata
