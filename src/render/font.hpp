#pragma once
#include <array>
#include <cstdint>

namespace render {

// 5 x 7 pixel font of the HUD and the cockpit displays, like the bitmap lettering of the original.
// A glyph is 7 rows, top row first; bit 4 of a row is its leftmost pixel. Lower case maps to upper
// case, unknown characters are blank.
constexpr int kGlyphW = 5, kGlyphH = 7;
using GlyphRows = std::array<std::uint8_t, kGlyphH>;
const GlyphRows& glyphRows(char ch);

}  // namespace render
