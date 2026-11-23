#pragma once
#include <glm/glm.hpp>

// The light of the time of day and the weather (no GL: the tests use it too).
namespace render {

// The sky and the light on the world. The scene shader lights a face by
// light * (ambient + diffuse * max(n . sunDir, 0)), fogged into the horizon's colour.
struct Palette {
  glm::vec3 horizon{0.74f, 0.80f, 0.86f};
  glm::vec3 zenith{0.33f, 0.50f, 0.76f};
  glm::vec3 groundHaze{0.62f, 0.66f, 0.66f};
  glm::vec3 sunDir = glm::normalize(glm::vec3(-0.45f, 0.75f, 0.45f));  // the moon's at night
  glm::vec3 light{1.0f};
  float ambient = 0.45f, diffuse = 0.65f;
  float stars = 0.0f;       // 0..1
  float fogDist = 1.0e9f;   // m: how far one sees through fog or rain
  glm::vec3 cockpit{1.0f};  // the light inside the cockpit
  float moon = 0.0f;        // 0..1: the moon's crescent in the sky, at sunDir
  float lamps = 0.0f;       // 0..1: the airfields' runway lights on (from dusk)
  float mapLight = 1.0f;    // how bright the moving map's picture is (dark at night, as the original's)
  float shadow = 1.0f;      // 0..1: how strong the aircraft's shadows are (none at night and under cloud)
};

// The colours of the original's skies (3DDATA/*.RGB, ramps from the zenith to the horizon);
// the defaults stand in when the original is not at hand.
struct SkyColours {
  glm::vec3 overcastZenith{0.42f, 0.40f, 0.50f}, overcastHorizon{0.53f, 0.51f, 0.59f};
};

// The sky at local time `hour` (0..24) in the mission's weather (0 clear, 1 overcast, 2 light
// fog, 3 thick fog, 4 rain): the sun rises in the east at six and sets in the west at eighteen,
// some 58 degrees high at noon; dusk reddens the light, at night the world is lit faintly blue
// by the moon under the stars. Under cloud the original darkens its whole world evenly, sky and
// ground alike, to about half at sunrise (SARATT04 at six), with no red in it.
Palette skyAt(float hour, int weather, const SkyColours& sky = {});
// The light inside the cockpit: the day's through the glazing, at night the instruments' lamps.
glm::vec3 cockpitLight(const Palette& p);

}  // namespace render
