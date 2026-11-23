#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The original's animations (ANIMS/*.Q3 on its CD, not installed): "ANIM", the number of frames,
// width, height and colours (16-bit each); then each frame: a flags byte, its size (32-bit), and
// with flag 0x20 the size of a palette change (32-bit) before its data.
//   0x01: the first frame: the palette (768 bytes, 6-bit) and the picture as it is.
//   0x10: a change of the frame before, in blocks of 8x8 pixels left to right, top to bottom,
//         each an opcode: 0 one colour; 1..4 2^n colours and n bits a pixel (n bytes a row, the
//         lowest bits the leftmost pixel); 5 as it was; 6 a mask of the rows changed, each one's
//         mask of its pixels, the new pixels; 7 the same for all eight rows.
// They play at 12.5 frames a second (HIND95.EXE: a frame every 16 ticks of its 200 Hz clock),
// without sound.
namespace gamedata {

class Anim {
public:
  static constexpr float kFps = 12.5f;
  bool open(std::vector<uint8_t> data, std::string& error);
  int width() const { return width_; }
  int height() const { return height_; }
  int frames() const { return frames_; }
  int frame() const { return frame_; }  // the frames decoded
  // The next frame; false at the end (or broken data).
  bool next();
  // The frame as RGB, rows from the top.
  void rgb(std::vector<uint8_t>& out) const;

private:
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
  int width_ = 0, height_ = 0, frames_ = 0, frame_ = 0;
  std::vector<uint8_t> pixels_;
  uint8_t palette_[768] = {};
  bool blocks(const uint8_t* p, const uint8_t* end);
};

// The animations the original's Animations (GoTo menu) picks from: three groups of eight, one
// group at random, one of it at random (HIND95.EXE's table); "ANIMS/FLY.Q3".
std::string randomAnimation(unsigned random);

}  // namespace gamedata
