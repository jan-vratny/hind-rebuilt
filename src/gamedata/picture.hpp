#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Pictures of the original game: IFF "PBM " files of Deluxe Paint (8-bit chunky pixels with a
// 256 colour palette, ByteRun1 compressed), such as COCKPITS/PILOT.LBM.
namespace gamedata {

struct Picture {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;  // rows from the top
};

// Colours taking the place of a picture's own from index `first` on. The game sets indices
// 224..255 of the cockpit pictures from COCKPITS/COCKPITS.RGB, which among others turns the
// shaded dial faces flat black as they are seen in flight.
struct PaletteRange {
  int first = 0;
  std::vector<uint8_t> rgb;
};
// A raw palette file of 8-bit RGB triplets (such as COCKPITS.RGB) placed from index `first`.
bool loadPalette(const std::string& path, int first, PaletteRange& out, std::string& error);

// Colour index `transparent` gets alpha 0 (-1: everything opaque).
bool loadPbm(const std::string& path, Picture& out, int transparent, std::string& error,
             const PaletteRange* replace = nullptr);

// The cockpit's small pictures (COCKPITS/*.640: lit lamps, dial faces, digits): raw 8-bit pixels
// without a header, `width` per row, coloured by `palette`; index 0 is transparent.
bool loadRaw(const std::string& path, int width, const PaletteRange& palette, Picture& out, std::string& error);

// A Netpbm PAM picture (RGB_ALPHA, 8 bits): ours, such as the rows painted over the cockpit
// pictures (data/cockpit).
bool loadPam(const std::string& path, Picture& out, std::string& error);

// `top` put over `picture` (as wide): the picture `top.height` rows taller.
bool stackOver(const Picture& top, Picture& picture);

}  // namespace gamedata
