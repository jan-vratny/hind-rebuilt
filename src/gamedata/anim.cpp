#include "gamedata/anim.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace gamedata {

namespace {

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

}  // namespace

bool Anim::open(std::vector<uint8_t> data, std::string& error) {
  data_ = std::move(data);
  if (data_.size() < 12 || std::memcmp(data_.data(), "ANIM", 4) != 0) {
    error = "not an animation";
    return false;
  }
  frames_ = u16(&data_[4]), width_ = u16(&data_[6]), height_ = u16(&data_[8]);
  if (width_ <= 0 || height_ <= 0 || width_ % 8 || height_ % 8 || width_ > 1024 || height_ > 1024) {
    error = "unknown size of animation";
    return false;
  }
  pixels_.assign(size_t(width_) * size_t(height_), 0);
  pos_ = 12, frame_ = 0;
  return true;
}

bool Anim::next() {
  if (frame_ >= frames_ || pos_ + 5 > data_.size()) return false;
  const uint8_t flags = data_[pos_];
  const size_t size = u32(&data_[pos_ + 1]);
  pos_ += 5;
  size_t change = 0;  // a palette change's bytes (none in the original's animations)
  if (flags & 0x20) {
    if (pos_ + 4 > data_.size()) return false;
    change = u32(&data_[pos_]);
    pos_ += 4;
  }
  if (pos_ + size > data_.size()) return false;
  const uint8_t* p = &data_[pos_];
  const uint8_t* end = p + size;
  pos_ += size;
  ++frame_;
  if (!(flags & 0x20)) {
    if (size < 768) return false;
    for (int i = 0; i < 768; ++i) palette_[i] = uint8_t(std::min(255, p[i] * 4 + p[i] / 16));
    p += 768;
  } else {
    p += std::min(change, size);
  }
  if (flags & 0x01) {
    if (size_t(end - p) < pixels_.size()) return false;
    std::copy(p, p + pixels_.size(), pixels_.begin());
    return true;
  }
  if (flags & 0x10) return blocks(p, end);
  return true;
}

bool Anim::blocks(const uint8_t* p, const uint8_t* end) {
  const int w = width_;
  for (int by = 0; by < height_; by += 8)
    for (int bx = 0; bx < w; bx += 8) {
      if (p >= end) return false;
      uint8_t* o = &pixels_[size_t(by) * size_t(w) + size_t(bx)];
      const uint8_t op = *p++;
      auto need = [&](long n) { return end - p >= n; };
      if (op == 0) {
        if (!need(1)) return false;
        for (int r = 0; r < 8; ++r) std::memset(o + r * w, *p, 8);
        ++p;
      } else if (op <= 4) {
        const int colours = 1 << op;
        if (!need(colours + 8 * op)) return false;
        const uint8_t* c = p;
        p += colours;
        for (int r = 0; r < 8; ++r) {
          uint32_t bits = 0;
          for (int k = 0; k < op; ++k) bits |= uint32_t(p[k]) << (8 * k);
          p += op;
          for (int i = 0; i < 8; ++i, bits >>= op) o[r * w + i] = c[bits & uint32_t(colours - 1)];
        }
      } else if (op == 5) {
        // as it was
      } else if (op == 6 || op == 7) {
        uint8_t rowsChanged = 0xff;
        if (op == 6) {
          if (!need(1)) return false;
          rowsChanged = *p++;
        }
        const int n = std::popcount(rowsChanged);
        if (!need(n)) return false;
        const uint8_t* masks = p;
        p += n;
        for (int r = 0; r < 8; ++r) {
          if (!(rowsChanged & (1 << r))) continue;
          const uint8_t m = *masks++;
          if (!need(std::popcount(m))) return false;
          for (int i = 0; i < 8; ++i)
            if (m & (1 << i)) o[r * w + i] = *p++;
        }
      } else {
        if (!need(1)) return false;
        ++p;  // (as the original: one byte skipped)
      }
    }
  return true;
}

void Anim::rgb(std::vector<uint8_t>& out) const {
  out.resize(pixels_.size() * 3);
  for (size_t i = 0; i < pixels_.size(); ++i) std::memcpy(&out[i * 3], &palette_[pixels_[i] * 3], 3);
}

std::string randomAnimation(unsigned random) {
  static const char* const kNames[22] = {"KAZ01", "BASE",  "NIGHT",   "BUNKER",  "DROP",  "RECON", "DUMP",  "CVOY",
                                         "SCRAM", "AMMO",  "FLY",     "FLYBY",   "DEAD",  "AFCITY", "AIRPORT", "APCHE",
                                         "BATTLE", "FIELD", "RADAR", "RIVER", "TORA", "WATER"};
  // The groups start at the 1st, 8th and 15th (sharing CVOY and AIRPORT).
  static const int kGroup[3] = {0, 7, 14};
  return std::string("ANIMS/") + kNames[kGroup[random % 3] + int(random / 3 % 8)] + ".Q3";
}

}  // namespace gamedata
