#pragma once
#include <cstdint>
#include <vector>

// The Ukrainian missions' Hind: the Czech Mi-24Vs given to Ukraine fly in the Czech three-tone
// camouflage (an olive green, a light grey and a dark grey green) with Ukraine's blue and yellow
// along the nose and the cabin, on the wing tips' plates, and white bands around the tail boom.
// The camouflage is a tile of the texture atlas in place of the original's green and sand (its
// texture 2); the markings the scene shader paints by the place on the airframe (uLivery).
namespace render {

// The original's texture of the Mi-24's camouflage, and the atlas tile the Czech one goes in.
constexpr int kHindTexture = 2, kLiveryTile = 63;

// The Czech camouflage: kAtlasTile square, RGBA, wrapping round seamlessly.
std::vector<uint8_t> czechCamouflage();

// The original's textures with the Czech camouflage added at kLiveryTile (for uploadAtlas).
std::vector<uint8_t> withLivery(const std::vector<uint8_t>& textures, int& count);

}  // namespace render
