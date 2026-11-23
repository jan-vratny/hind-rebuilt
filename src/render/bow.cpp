#include "render/bow.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "world/terrain.hpp"

namespace render {

namespace {

const glm::vec3 kShaft{0.05f, 0.05f, 0.055f};
const glm::vec3 kBrass{0.72f, 0.55f, 0.2f}, kSeam{0.5f, 0.37f, 0.12f};
const glm::vec3 kSteel{0.62f, 0.64f, 0.66f}, kPoint{0.25f, 0.26f, 0.28f};
const glm::vec3 kRiser{0.09f, 0.09f, 0.08f}, kLimb{0.16f, 0.15f, 0.13f};
const glm::vec3 kString{0.78f, 0.76f, 0.7f}, kCable{0.3f, 0.3f, 0.32f};
const glm::vec3 kSkin{0.72f, 0.5f, 0.38f};

// A thin rod from a to b.
void rod(MeshBuilder& mb, const glm::mat4& m, glm::vec3 a, glm::vec3 b, float r, glm::vec3 c) { mb.cylinder(m, a, b, r, r, 5, c); }

// A hand (and its forearm) with the wrist at the origin: the fingers along -z, the palm down
// (-y), the thumb on the -x side for the right hand (mirrored for the left). `curl` 0 straight ..
// 1 a fist, for the four fingers; the thumb's own.
struct Grip {
  float curl = 0.0f, thumb = 0.0f;
  float spread = 0.0f;  // the fingers fanned (rad between neighbours)
};

// `elbow`: where the forearm goes from the wrist (in the axes `m` puts the hand into).
void hand(MeshBuilder& mb, glm::mat4 m, bool left, const Grip& g, glm::vec3 elbow) {
  if (left) m = m * glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
  const glm::vec3 skin = kSkin, dark = kSkin * 0.85f;
  // The forearm to the elbow, the palm (a little wider at the knuckles), its heel by the thumb.
  mb.cylinder(m, {0.0f, 0.004f, 0.01f}, glm::vec3(glm::inverse(m) * glm::vec4(elbow, 1.0f)), 0.025f, 0.036f, 8, skin);
  const glm::vec3 c[8] = {{-0.03f, -0.012f, -0.005f}, {0.03f, -0.012f, -0.005f}, {0.03f, 0.014f, -0.005f}, {-0.03f, 0.014f, -0.005f},
                          {-0.042f, -0.011f, -0.088f}, {-0.042f, 0.013f, -0.088f}, {0.042f, 0.013f, -0.088f}, {0.042f, -0.011f, -0.088f}};
  glm::vec3 w[8];
  for (int i = 0; i < 8; ++i) w[i] = glm::vec3(m * glm::vec4(c[i], 1.0f));
  mb.hexa(w, skin);
  mb.cylinder(m, {-0.022f, -0.01f, -0.015f}, {-0.03f, -0.012f, -0.05f}, 0.014f, 0.012f, 6, dark);
  // The fingers from the knuckles, index (by the thumb) to little: three joints each, bending
  // towards the palm, the last two a little less than the first.
  static const float kX[4] = {-0.03f, -0.01f, 0.01f, 0.029f};
  static const float kLen[4][3] = {{0.042f, 0.025f, 0.019f}, {0.046f, 0.029f, 0.021f}, {0.043f, 0.027f, 0.02f}, {0.034f, 0.021f, 0.017f}};
  static const float kR[4] = {0.0088f, 0.0092f, 0.0087f, 0.0076f};
  for (int f = 0; f < 4; ++f) {
    glm::mat4 j = m * glm::translate(glm::mat4(1.0f), glm::vec3(kX[f], 0.0f, -0.088f)) *
                  glm::rotate(glm::mat4(1.0f), g.spread * (1.5f - float(f)), glm::vec3(0, 1, 0));
    const float bend[3] = {1.45f * g.curl, 1.65f * g.curl, 1.1f * g.curl};
    for (int k = 0; k < 3; ++k) {
      j = j * glm::rotate(glm::mat4(1.0f), -bend[k], glm::vec3(1, 0, 0));
      const float r = kR[f] * (1.0f - 0.1f * float(k));
      mb.cylinder(j, {0, 0, 0.004f}, {0, 0, -kLen[f][k]}, r, r * 0.92f, 6, k == 0 ? skin : (k == 1 ? skin * 0.97f : skin * 0.93f));
      j = j * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -kLen[f][k]));
    }
  }
  // The thumb: out from the heel of the hand, forwards and to the side, turned so that it bends
  // across the palm.
  glm::mat4 t = m * glm::translate(glm::mat4(1.0f), glm::vec3(-0.032f, -0.008f, -0.03f)) *
                glm::rotate(glm::mat4(1.0f), 0.75f - 0.55f * g.thumb, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1.0f), -0.9f, glm::vec3(0, 0, 1));
  const float tl[2] = {0.036f, 0.03f};
  for (int k = 0; k < 2; ++k) {
    t = t * glm::rotate(glm::mat4(1.0f), -(0.5f + 0.7f * float(k)) * g.thumb, glm::vec3(1, 0, 0));
    mb.cylinder(t, {0, 0, 0.004f}, {0, 0, -tl[k]}, 0.0105f, 0.0095f, 6, k ? skin * 0.95f : skin);
    t = t * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -tl[k]));
  }
}

// Where a hand goes (for hand()): its palm facing `palm`, its fingers along `fingers` (both
// unit, square to each other), the wrist at `at`.
glm::mat4 handAt(glm::vec3 at, glm::vec3 palm, glm::vec3 fingers) {
  const glm::vec3 y = -palm, z = -fingers, x = glm::cross(y, z);
  return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(at, 1.0f));
}

// The warhead brought up quickly (the first tenth of screwing it on), then six slow turns: how
// many cycles of them are through.
constexpr int kTurns = 6;
float turnCycles(float fitting) { return std::max(0.0f, fitting - 0.1f) / 0.9f * float(kTurns); }

}  // namespace

void buildWarhead(MeshBuilder& mb, const glm::mat4& m, float spin) {
  const glm::mat4 w = m * glm::rotate(glm::mat4(1.0f), spin, glm::vec3(0, 0, 1));
  // The steel collar, then six brass rings, each narrowing a little to the step of the next.
  mb.cylinder(w, {0, 0, 0.0f}, {0, 0, -0.016f}, 0.0075f, 0.0075f, 10, kSteel);
  constexpr int kSides = 12, kSteps = 6;
  std::vector<std::vector<glm::vec3>> rings;
  auto ring = [&](float z, float r) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < kSides; ++i) {
      const float a = float(i) * 6.2831853f / kSides;
      p.push_back({r * std::cos(a), r * std::sin(a), z});
    }
    rings.push_back(p);
  };
  const float back = -0.016f, len = 0.1f;
  ring(back, 0.008f);
  for (int k = 0; k < kSteps; ++k) {
    const float z0 = back - len * float(k) / kSteps, z1 = back - len * float(k + 1) / kSteps;
    const float r = 0.025f - 0.0027f * float(k);
    ring(z0 - 0.002f, r);
    ring(z1, r * 0.9f);
  }
  mb.loft(w, rings, [](int, int seg, glm::vec3) { return seg == 0 ? kSeam : kBrass; });
  // The point.
  const float tip = back - len;
  mb.cylinder(w, {0, 0, tip}, {0, 0, tip - 0.035f}, 0.006f, 0.0f, 6, kPoint);
}

void buildArrow(MeshBuilder& mb, const glm::mat4& m, bool warhead) {
  mb.cylinder(m, {0, 0, 0}, {0, 0, -kArrowShaft}, 0.0045f, 0.0045f, 6, kShaft);
  // The vanes: three, a red cock vane and two olive ones.
  for (int k = 0; k < 3; ++k) {
    const float a = float(k) * 2.0944f + 1.5708f;
    const glm::vec3 out{std::cos(a), std::sin(a), 0.0f};
    const glm::vec3 c = k == 0 ? glm::vec3(0.6f, 0.1f, 0.08f) : glm::vec3(0.3f, 0.33f, 0.18f);
    const glm::vec3 p0 = out * 0.004f + glm::vec3(0, 0, -0.025f), p1 = out * 0.004f + glm::vec3(0, 0, -0.11f);
    const glm::vec3 p2 = out * 0.016f + glm::vec3(0, 0, -0.045f), p3 = out * 0.016f + glm::vec3(0, 0, -0.025f);
    const auto xf = [&](glm::vec3 p) { return glm::vec3(m * glm::vec4(p, 1.0f)); };
    mb.quad(xf(p0), xf(p1), xf(p2), xf(p3), c);
  }
  if (warhead) buildWarhead(mb, m * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -kArrowShaft)), 0.0f);
}

glm::vec3 buildBow(MeshBuilder& mb, const glm::mat4& bow, float pull) {
  // The riser: the grip, and its arms curving forward to the limbs.
  mb.box(bow, {-0.014f, -0.06f, -0.022f}, {0.014f, 0.06f, 0.022f}, kRiser);
  for (float s : {1.0f, -1.0f}) {
    rod(mb, bow, {0, s * 0.06f, -0.01f}, {0, s * 0.33f, -0.08f}, 0.011f, kRiser);
    // The limb back towards him, a cam (wheel) on its tip.
    const glm::vec3 tip{0.0f, s * 0.47f, 0.0f};
    rod(mb, bow, {0, s * 0.33f, -0.08f}, tip, 0.009f, kLimb);
    mb.cylinder(bow, tip + glm::vec3(-0.008f, 0, 0), tip + glm::vec3(0.008f, 0, 0), 0.038f, 0.038f, 10, kCable);
  }
  // The string from the cams' backs to the nock point; the cables straight between the cams, a
  // little aside.
  const glm::vec3 top{0.0f, 0.47f, 0.038f}, bottom{0.0f, -0.47f, 0.038f}, nock{0.0f, 0.0f, 0.06f + pull};
  rod(mb, bow, top, nock, 0.0018f, kString);
  rod(mb, bow, nock, bottom, 0.0018f, kString);
  rod(mb, bow, {0.012f, 0.45f, -0.025f}, {0.012f, -0.45f, -0.025f}, 0.0018f, kCable);
  rod(mb, bow, {0.018f, 0.45f, 0.0f}, {0.018f, -0.45f, 0.0f}, 0.0018f, kCable);
  return nock;
}

namespace {

// A model of the remake's data (tools/glb2mesh.py): its triangles and their colours.
struct ModelTri {
  glm::vec3 p[3];
  glm::vec3 color;
};
std::vector<ModelTri> loadModel(const char* name) {
  std::vector<std::string> paths;
  if (char* base = SDL_GetBasePath()) {
    paths.push_back(std::string(base) + "../data/models/" + name);
    paths.push_back(std::string(base) + "data/models/" + name);
    SDL_free(base);
  }
  paths.push_back(std::string(HIND_DATA_DIR "/models/") + name);
  for (const std::string& path : paths) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) continue;
    std::vector<ModelTri> tris;
    char magic[4];
    uint32_t head[2];
    if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "HMSH", 4) == 0 && std::fread(head, 4, 2, f) == 2 && head[0] == 1) {
      for (uint32_t k = 0; k < head[1]; ++k) {
        float c[9];
        uint8_t rgb[4];
        if (std::fread(c, 4, 9, f) != 9 || std::fread(rgb, 1, 4, f) != 4) {
          tris.clear();
          break;
        }
        tris.push_back({{{c[0], c[1], c[2]}, {c[3], c[4], c[5]}, {c[6], c[7], c[8]}}, glm::vec3(rgb[0], rgb[1], rgb[2]) / 255.0f});
      }
    }
    std::fclose(f);
    if (tris.empty()) std::fprintf(stderr, "Model %s: not a model we read\n", path.c_str());
    return tris;
  }
  return {};
}

}  // namespace

void buildM60(MeshBuilder& mb, const glm::mat4& m) {
  // Jawa's M60 (data/models/m60.mesh, CC BY 4.0); without it one made of boxes and rods.
  static const std::vector<ModelTri> model = loadModel("m60.mesh");
  if (!model.empty()) {
    for (const ModelTri& t : model)
      mb.tri(glm::vec3(m * glm::vec4(t.p[0], 1.0f)), glm::vec3(m * glm::vec4(t.p[1], 1.0f)), glm::vec3(m * glm::vec4(t.p[2], 1.0f)), t.color);
    return;
  }
  const glm::vec3 kMetal{0.24f, 0.25f, 0.23f}, kDark{0.15f, 0.15f, 0.14f}, kStock{0.22f, 0.23f, 0.16f};
  const glm::vec3 kCase{0.7f, 0.55f, 0.2f}, kLink{0.2f, 0.2f, 0.2f};
  // The receiver with its feed cover on top, the trigger group and pistol grip under it, the
  // butt stock behind.
  mb.box(m, {-0.03f, -0.035f, -0.27f}, {0.03f, 0.045f, 0.1f}, kMetal);
  mb.box(m, {-0.034f, 0.045f, -0.2f}, {0.034f, 0.065f, 0.02f}, kDark);
  mb.box(m, {-0.012f, -0.065f, -0.03f}, {0.012f, -0.035f, 0.06f}, kDark);
  {
    const glm::vec3 c[8] = {{-0.017f, -0.035f, 0.0f}, {0.017f, -0.035f, 0.0f}, {0.017f, -0.035f, 0.05f}, {-0.017f, -0.035f, 0.05f},
                            {-0.017f, -0.17f, 0.04f}, {-0.017f, -0.17f, 0.09f}, {0.017f, -0.17f, 0.09f}, {0.017f, -0.17f, 0.04f}};
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i) w[i] = glm::vec3(m * glm::vec4(c[i], 1.0f));
    mb.hexa(w, kStock);
  }
  {
    const glm::vec3 c[8] = {{-0.025f, -0.03f, 0.1f}, {0.025f, -0.03f, 0.1f}, {0.025f, 0.035f, 0.1f}, {-0.025f, 0.035f, 0.1f},
                            {-0.022f, -0.12f, 0.48f}, {-0.022f, 0.04f, 0.48f}, {0.022f, 0.04f, 0.48f}, {0.022f, -0.12f, 0.48f}};
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i) w[i] = glm::vec3(m * glm::vec4(c[i], 1.0f));
    mb.hexa(w, kStock);
  }
  // The barrel, the gas cylinder under it, the front sight, the carrying handle, the flash hider;
  // the bipod folded back under the barrel.
  mb.cylinder(m, {0.0f, 0.015f, -0.27f}, {0.0f, 0.015f, -0.83f}, 0.014f, 0.012f, 8, kDark);
  mb.cylinder(m, {0.0f, -0.015f, -0.27f}, {0.0f, -0.015f, -0.6f}, 0.012f, 0.012f, 8, kMetal);
  mb.cylinder(m, {0.0f, 0.015f, -0.83f}, {0.0f, 0.015f, -0.9f}, 0.017f, 0.017f, 8, kMetal);
  mb.box(m, {-0.004f, 0.025f, -0.81f}, {0.004f, 0.06f, -0.795f}, kMetal);
  mb.box(m, {-0.005f, 0.03f, -0.34f}, {0.005f, 0.1f, -0.32f}, kMetal);
  mb.box(m, {-0.008f, 0.09f, -0.38f}, {0.008f, 0.105f, -0.22f}, kMetal);
  for (const float x : {-0.02f, 0.02f}) mb.cylinder(m, {x, -0.025f, -0.62f}, {x * 1.2f, -0.03f, -0.35f}, 0.005f, 0.005f, 5, kMetal);
  // The belt out of its left side and hanging down: cartridges on their links.
  for (int k = 0; k < 9; ++k) {
    const float a = float(k) * 0.16f;
    const glm::vec3 at{-0.04f - 0.012f * float(k) * std::cos(a), 0.035f - 0.018f * float(k) * std::sin(a + 0.3f), -0.1f};
    mb.box(m * glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)), {-0.006f, -0.004f, -0.004f},
           {0.006f, 0.004f, 0.004f}, kLink);
    mb.cylinder(m * glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)), {0.0f, 0.0f, 0.03f},
                {0.0f, 0.0f, -0.04f}, 0.0055f, 0.0045f, 6, kCase);
  }
}

glm::mat4 m60View(float kick, float reload) {
  // At his right hip, its muzzle on the middle of the view some 40 m ahead; kicking back and up
  // at each shot; lowered and canted while a belt goes in.
  const bool loading = reload >= 0.0f;
  const float low = loading ? std::sin(std::min(1.0f, reload) * 3.14159f) : 0.0f;
  const glm::vec3 at = glm::vec3(0.15f, -0.21f - 0.12f * low, -0.36f + 0.025f * kick);
  const glm::vec3 to = glm::normalize(glm::vec3(0.0f, 0.0f, -40.0f) - at);
  const float yaw = std::atan2(-to.x, -to.z), pitch = std::asin(to.y);
  const glm::mat4 gun = glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                        glm::rotate(glm::mat4(1.0f), pitch + 0.035f * kick + 0.25f * low, glm::vec3(1, 0, 0)) *
                        glm::rotate(glm::mat4(1.0f), 0.5f * low, glm::vec3(0, 0, 1));
  return gun;
}

void buildM60View(MeshBuilder& mb, float kick, float reload) {
  const bool loading = reload >= 0.0f;
  const glm::mat4 gun = m60View(kick, reload);
  buildM60(mb, gun);
  // The right hand round the pistol grip, the palm to its right side; the left under the barrel,
  // palm up (on the feed cover, lifting it, while he loads).
  hand(mb, gun * handAt(kM60Grip + glm::vec3(0.02f, 0.0f, 0.03f), {-1, 0, 0}, glm::normalize(glm::vec3(0.0f, -0.3f, -1.0f))), false,
       {0.8f, 0.6f, 0.0f}, {0.3f, -0.55f, 0.0f});
  const glm::vec3 fore = loading ? glm::vec3(0.0f, 0.08f, -0.1f) : kM60Fore + glm::vec3(-0.02f, -0.03f, 0.0f);
  hand(mb, gun * handAt(fore, loading ? glm::vec3(0, -1, 0) : glm::vec3(0, 1, 0), {1, 0, 0}), true, {0.7f, 0.5f, 0.0f}, {-0.25f, -0.5f, -0.15f});
}

void buildLaw(MeshBuilder& mb, const glm::mat4& m) {
  static const std::vector<ModelTri> model = loadModel("m72.mesh");
  if (!model.empty()) {
    for (const ModelTri& t : model)
      mb.tri(glm::vec3(m * glm::vec4(t.p[0], 1.0f)), glm::vec3(m * glm::vec4(t.p[1], 1.0f)), glm::vec3(m * glm::vec4(t.p[2], 1.0f)), t.color);
    return;
  }
  const glm::vec3 kOlive{0.29f, 0.32f, 0.19f}, kInner{0.25f, 0.28f, 0.17f}, kBlack{0.07f, 0.07f, 0.07f}, kYellow{0.75f, 0.62f, 0.12f};
  // The outer tube behind, the inner one drawn out ahead of it; rubber rings at the ends.
  mb.cylinder(m, {0.0f, 0.0f, 0.45f}, {0.0f, 0.0f, -0.06f}, 0.042f, 0.042f, 12, kOlive, kBlack);
  mb.cylinder(m, {0.0f, 0.0f, -0.06f}, {0.0f, 0.0f, -0.43f}, 0.036f, 0.036f, 12, kInner, kBlack);
  mb.cylinder(m, {0.0f, 0.0f, 0.45f}, {0.0f, 0.0f, 0.42f}, 0.046f, 0.046f, 12, kBlack);
  mb.cylinder(m, {0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, -0.43f}, 0.04f, 0.04f, 12, kBlack);
  mb.cylinder(m, {0.0f, 0.0f, -0.14f}, {0.0f, 0.0f, -0.16f}, 0.0365f, 0.0365f, 12, kYellow);
  // The trigger housing on top, its rubber bar; the rear sight's peep, the front sight's frame.
  mb.box(m, {-0.018f, 0.035f, -0.05f}, {0.018f, 0.06f, 0.08f}, kOlive);
  mb.box(m, {-0.008f, 0.06f, -0.04f}, {0.008f, 0.07f, 0.0f}, kBlack);
  mb.box(m, {-0.012f, 0.04f, 0.18f}, {0.012f, 0.066f, 0.186f}, kBlack);
  mb.box(m, {-0.026f, 0.034f, -0.32f}, {-0.02f, 0.1f, -0.31f}, kBlack);
  mb.box(m, {0.02f, 0.034f, -0.32f}, {0.026f, 0.1f, -0.31f}, kBlack);
  mb.box(m, {-0.026f, 0.094f, -0.32f}, {0.026f, 0.1f, -0.31f}, kBlack);
  mb.box(m, {-0.002f, 0.06f, -0.316f}, {0.002f, 0.094f, -0.314f}, kBlack);
}

void buildLawRocket(MeshBuilder& mb, const glm::mat4& m) {
  static const std::vector<ModelTri> model = loadModel("m72rocket.mesh");
  if (!model.empty()) {
    for (const ModelTri& t : model)
      mb.tri(glm::vec3(m * glm::vec4(t.p[0], 1.0f)), glm::vec3(m * glm::vec4(t.p[1], 1.0f)), glm::vec3(m * glm::vec4(t.p[2], 1.0f)), t.color);
    return;
  }
  const glm::vec3 kWarhead{0.12f, 0.13f, 0.11f}, kBrass{0.62f, 0.5f, 0.25f}, kNozzle{0.3f, 0.28f, 0.25f};
  // The warhead: its nose cone, the widest part, tapering to the neck.
  mb.cylinder(m, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.12f}, 0.006f, 0.033f, 10, kWarhead);
  mb.cylinder(m, {0.0f, 0.0f, 0.12f}, {0.0f, 0.0f, 0.2f}, 0.033f, 0.02f, 10, kWarhead);
  // The motor tube, its nozzle; the fins out of the tail, sprung back and out.
  mb.cylinder(m, {0.0f, 0.0f, 0.2f}, {0.0f, 0.0f, 0.47f}, 0.02f, 0.02f, 8, kBrass);
  mb.cylinder(m, {0.0f, 0.0f, 0.47f}, {0.0f, 0.0f, 0.5f}, 0.02f, 0.014f, 8, kNozzle);
  for (int k = 0; k < 6; ++k) {
    const glm::mat4 f = glm::rotate(m, float(k) * glm::two_pi<float>() / 6.0f, glm::vec3(0, 0, 1));
    mb.slab(f, {{0.0f, 0.018f, 0.44f}, {0.0f, 0.07f, 0.53f}, {0.0f, 0.065f, 0.55f}, {0.0f, 0.018f, 0.48f}}, {0.002f, 0.0f, 0.0f}, kNozzle);
  }
}

void buildLawView(MeshBuilder& mb, float kick, float ready) {
  // On his right shoulder, its front sight just under his right eye's line, aimed at the middle
  // of the view some 40 m ahead; jumping at the shot. A new tube: the old one down out of sight,
  // the next one up.
  // (Lowered all the way while it is not ready, coming up through the last quarter.)
  const float r = std::clamp(ready, 0.0f, 1.0f);
  const float l = r >= 1.0f ? 0.0f : r > 0.75f ? glm::smoothstep(1.0f, 0.75f, r) : 1.0f;
  const glm::vec3 at = glm::vec3(0.15f, -0.17f - 0.5f * l, -0.62f + 0.05f * kick);
  const glm::vec3 to = glm::normalize(glm::vec3(0.0f, 0.0f, -40.0f) - at);
  const float yaw = std::atan2(-to.x, -to.z), pitch = std::asin(to.y);
  const glm::mat4 tube = glm::translate(glm::mat4(1.0f), at) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                         glm::rotate(glm::mat4(1.0f), pitch + 0.06f * kick - 0.6f * l, glm::vec3(1, 0, 0));
  buildLaw(mb, tube);
  hand(mb, tube * handAt(kLawGrip + glm::vec3(0.0f, 0.0f, 0.04f), {0, -1, 0}, {0, 0, -1}), false, {0.55f, 0.4f, 0.0f}, {0.2f, -0.45f, 0.2f});
  hand(mb, tube * handAt(kLawFore + glm::vec3(0.0f, -0.0f, 0.04f), {0, 1, 0}, {1, 0, 0}), true, {0.7f, 0.5f, 0.0f}, {-0.25f, -0.5f, -0.1f});
}

void Casings::eject(glm::vec3 at, glm::vec3 right, glm::vec3 up, glm::vec3 back, glm::vec3 vel) {
  const auto rnd = [&]() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffff) / float(0x1000000) * 2.0f - 1.0f;
  };
  // Out to the right and up, a little ahead (away from his eye: in sight a while); each its own
  // way (as in the films).
  const glm::vec3 v = right * (1.1f + 0.35f * rnd()) + up * (2.2f + 0.5f * rnd()) - back * (0.9f + 0.4f * rnd());
  const glm::vec3 spin = glm::normalize(up + 0.4f * glm::vec3(rnd(), rnd(), rnd()));
  cases_.push_back({at, vel + v, glm::normalize(right - 0.3f * back), spin, 20.0f + 8.0f * rnd(), 0.0f, false});
}

void Casings::step(float dt, const world::Terrain& terrain) {
  for (Case& c : cases_) {
    c.age += dt;
    c.vel.y -= 9.81f * dt;
    c.pos += c.vel * dt;
    // Turning end over end.
    const float a = c.spin * dt, cs = std::cos(a), sn = std::sin(a);
    c.axis = c.axis * cs + glm::cross(c.spinAxis, c.axis) * sn + c.spinAxis * glm::dot(c.spinAxis, c.axis) * (1.0f - cs);
    const float ground = terrain.groundHeight(c.pos.x, c.pos.z) + 0.006f;
    if (c.pos.y < ground) {
      c.pos.y = ground;
      if (c.bounced) c.vel = glm::vec3(0.0f), c.spin = 0.0f;
      else c.vel = glm::vec3(0.35f * c.vel.x, -0.3f * c.vel.y, 0.35f * c.vel.z), c.spin *= 0.5f, c.bounced = true;
    }
  }
  cases_.erase(std::remove_if(cases_.begin(), cases_.end(), [](const Case& c) { return c.age > 1.5f; }), cases_.end());
}

void Casings::build(MeshBuilder& mb, const glm::mat4& view) const {
  // A 7.62 mm case: brass, 51 mm, its neck narrower.
  const glm::vec3 kBrass{0.86f, 0.64f, 0.24f}, kNeck{0.78f, 0.56f, 0.2f}, kBase{0.6f, 0.45f, 0.18f};
  for (const Case& c : cases_) {
    const glm::vec3 p(view * glm::vec4(c.pos, 1.0f)), d = glm::mat3(view) * c.axis;
    if (p.z > -0.05f || p.z < -5.0f) continue;
    const glm::vec3 a = p - d * 0.026f, b = p + d * 0.014f, e = p + d * 0.026f;
    mb.cylinder(glm::mat4(1.0f), a, b, 0.006f, 0.0058f, 6, kBrass, kBase);
    mb.cylinder(glm::mat4(1.0f), b, e, 0.0058f, 0.0045f, 6, kNeck);
  }
}

void buildBowmanFigure(MeshBuilder& mb, const glm::mat4& m, float stride, float walk, float draw, float pitch, int weapon) {
  const bool m60 = weapon == 1, law = weapon == 2;
  const glm::vec3 kTrousers{0.32f, 0.34f, 0.2f}, kBoot{0.14f, 0.11f, 0.09f}, kHair{0.06f, 0.05f, 0.05f}, kBand{0.62f, 0.08f, 0.06f};
  const glm::vec3 kLeather{0.3f, 0.2f, 0.12f};
  const auto limb = [&](glm::vec3 a, glm::vec3 b, float ra, float rb, glm::vec3 c) { mb.cylinder(m, a, b, ra, rb, 7, c); };
  const auto at = [&](glm::vec3 p) { return glm::vec3(m * glm::vec4(p, 1.0f)); };
  // A step lifts and drops him a little.
  const float lift = 0.025f * walk * std::fabs(std::cos(stride));
  const glm::vec3 up{0.0f, lift, 0.0f};
  // The legs: the thigh swings, the knee bends as the leg comes through; the boots.
  for (int side = 0; side < 2; ++side) {
    const float sx = side ? 0.1f : -0.1f, phase = stride + (side ? 3.14159f : 0.0f);
    const float a = 0.5f * walk * std::sin(phase), k = 0.9f * walk * std::max(0.0f, -std::cos(phase));
    const glm::vec3 hip = glm::vec3(sx, 0.93f, 0.0f) + up;
    const glm::vec3 knee = hip + 0.45f * glm::vec3(0.0f, -std::cos(a), -std::sin(a));
    const glm::vec3 ankle = knee + 0.45f * glm::vec3(0.0f, -std::cos(a - k), -std::sin(a - k));
    limb(hip, knee, 0.075f, 0.06f, kTrousers);
    limb(knee, ankle, 0.058f, 0.05f, kTrousers);
    mb.box(m * glm::translate(glm::mat4(1.0f), ankle) * glm::rotate(glm::mat4(1.0f), -0.5f * (a - k) * 0.4f, glm::vec3(1, 0, 0)),
           {-0.05f, -0.035f, -0.2f}, {0.05f, 0.06f, 0.06f}, kBoot);
  }
  // The body: trousers and belt to the waist, bare from there to the shoulders; the quiver's strap
  // across his chest.
  mb.box(m * glm::translate(glm::mat4(1.0f), up), {-0.17f, 0.86f, -0.1f}, {0.17f, 1.0f, 0.1f}, kTrousers);
  mb.box(m * glm::translate(glm::mat4(1.0f), up), {-0.172f, 0.97f, -0.102f}, {0.172f, 1.02f, 0.102f}, kLeather);
  {
    const glm::vec3 c[8] = {{-0.15f, 1.0f, -0.09f}, {-0.15f, 1.0f, 0.09f}, {0.15f, 1.0f, 0.09f}, {0.15f, 1.0f, -0.09f},
                            {-0.21f, 1.46f, -0.1f}, {0.21f, 1.46f, -0.1f}, {0.21f, 1.46f, 0.11f}, {-0.21f, 1.46f, 0.11f}};
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i) w[i] = at(c[i] + up);
    mb.hexa(w, kSkin);
  }
  limb(glm::vec3(-0.17f, 1.44f, -0.11f) + up, glm::vec3(0.16f, 1.0f, -0.1f) + up, 0.016f, 0.016f, kLeather);
  // The quiver on his back, its arrows' vanes over his right shoulder.
  const glm::vec3 qa = glm::vec3(-0.1f, 1.0f, 0.15f) + up, qb = glm::vec3(0.12f, 1.48f, 0.17f) + up;
  limb(qa, qb, 0.045f, 0.05f, kLeather * 0.8f);
  for (int k = 0; k < 4; ++k) {
    const glm::vec3 o = glm::vec3(0.02f * float(k % 2) - 0.01f, 0.0f, 0.02f * float(k / 2) - 0.01f);
    const glm::vec3 end = qb + o + glm::normalize(qb - qa) * 0.2f;
    limb(qb + o, end, 0.005f, 0.005f, kShaft);
    limb(end - glm::normalize(qb - qa) * 0.09f, end, 0.014f, 0.004f, glm::vec3(0.6f, 0.1f, 0.08f));
  }
  // The neck and head: long black hair down to his shoulders, a red headband with its ends hanging
  // behind.
  limb(glm::vec3(0.0f, 1.45f, 0.0f) + up, glm::vec3(0.0f, 1.54f, 0.0f) + up, 0.055f, 0.05f, kSkin);
  limb(glm::vec3(0.0f, 1.53f, -0.005f) + up, glm::vec3(0.0f, 1.76f, 0.0f) + up, 0.085f, 0.095f, kSkin);
  limb(glm::vec3(0.0f, 1.72f, 0.01f) + up, glm::vec3(0.0f, 1.81f, 0.01f) + up, 0.1f, 0.075f, kHair);
  mb.box(m * glm::translate(glm::mat4(1.0f), up), {-0.105f, 1.38f, 0.02f}, {0.105f, 1.76f, 0.11f}, kHair);
  limb(glm::vec3(0.0f, 1.685f, 0.0f) + up, glm::vec3(0.0f, 1.715f, 0.0f) + up, 0.101f, 0.101f, kBand);
  limb(glm::vec3(0.0f, 1.7f, 0.1f) + up, glm::vec3(-0.03f, 1.5f, 0.17f) + up, 0.012f, 0.008f, kBand);
  limb(glm::vec3(0.0f, 1.7f, 0.1f) + up, glm::vec3(0.035f, 1.52f, 0.15f) + up, 0.012f, 0.008f, kBand);
  // The arms: walking, the right one swinging, the left carrying the bow at his side; drawing,
  // the left raised straight along his aim with the bow, the right pulling the string to his chin.
  const float r = std::min(1.0f, draw * 3.0f), raise = r * r * (3.0f - 2.0f * r);
  const glm::vec3 aim{0.0f, std::sin(pitch), -std::cos(pitch)};
  const glm::vec3 sl = glm::vec3(-0.21f, 1.42f, 0.0f) + up, sr = glm::vec3(0.21f, 1.42f, 0.0f) + up;
  const float swing = -0.4f * walk * std::sin(stride);
  const glm::vec3 walkElbowR = sr + 0.29f * glm::vec3(0.02f, -std::cos(swing), -std::sin(swing));
  const glm::vec3 walkHandR = walkElbowR + 0.26f * glm::vec3(0.0f, -std::cos(swing + 0.35f), -std::sin(swing + 0.35f));
  const glm::vec3 walkElbowL = sl + glm::vec3(-0.03f, -0.28f, -0.06f), walkHandL = walkElbowL + glm::vec3(0.0f, -0.22f, -0.14f);
  const glm::vec3 aimHandL = sl + glm::vec3(0.1f, 0.0f, 0.0f) + aim * 0.6f, aimElbowL = (sl + aimHandL) * 0.5f + glm::vec3(-0.04f, 0.0f, 0.0f);
  const glm::vec3 chin = glm::vec3(0.04f, 1.52f, -0.1f) + up;
  const glm::vec3 aimHandR = glm::mix(aimHandL - aim * 0.1f + glm::vec3(0.04f, 0.0f, 0.0f), chin, draw);
  const glm::vec3 aimElbowR = glm::mix(glm::vec3(0.3f, 1.3f, -0.25f), glm::vec3(0.34f, 1.47f, 0.18f), draw) + up;
  glm::vec3 handL = glm::mix(walkHandL, aimHandL, raise), elbowL = glm::mix(walkElbowL, aimElbowL, raise);
  glm::vec3 handR = glm::mix(walkHandR, aimHandR, raise), elbowR = glm::mix(walkElbowR, aimElbowR, raise);
  // The M60 at his right hip along his aim, a hand on its grip and one under its barrel.
  const glm::mat4 gunAt = glm::translate(glm::mat4(1.0f), glm::vec3(0.16f, 1.1f, -0.18f) + up) * glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0));
  if (m60) {
    handR = glm::vec3(gunAt * glm::vec4(kM60Grip + glm::vec3(0.0f, -0.03f, 0.0f), 1.0f));
    handL = glm::vec3(gunAt * glm::vec4(kM60Fore + glm::vec3(0.0f, -0.04f, 0.0f), 1.0f));
    elbowR = (sr + handR) * 0.5f + glm::vec3(0.08f, -0.05f, 0.1f);
    elbowL = (sl + handL) * 0.5f + glm::vec3(-0.06f, -0.12f, 0.0f);
  }
  // The LAW on his right shoulder along his aim, his right hand over its trigger, his left under it.
  const glm::mat4 lawAt = glm::translate(glm::mat4(1.0f), glm::vec3(0.17f, 1.5f, -0.12f) + up) * glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0));
  if (law) {
    handR = glm::vec3(lawAt * glm::vec4(kLawGrip + glm::vec3(0.0f, 0.02f, 0.06f), 1.0f));
    handL = glm::vec3(lawAt * glm::vec4(kLawFore + glm::vec3(0.0f, -0.03f, 0.0f), 1.0f));
    elbowR = sr + glm::vec3(0.12f, -0.22f, -0.05f);
    elbowL = (sl + handL) * 0.5f + glm::vec3(0.02f, -0.15f, 0.0f);
  }
  for (const auto& [sh, el, hd] : {std::tuple{sl, elbowL, handL}, std::tuple{sr, elbowR, handR}}) {
    limb(sh, el, 0.05f, 0.042f, kSkin);
    limb(el, hd, 0.04f, 0.032f, kSkin);
    mb.box(m * glm::translate(glm::mat4(1.0f), hd), {-0.035f, -0.045f, -0.035f}, {0.035f, 0.03f, 0.035f}, kSkin * 0.95f);
  }
  if (m60) {
    buildM60(mb, m * gunAt);
    return;
  }
  if (law) {
    buildLaw(mb, m * lawAt);
    return;
  }
  // The bow in his left fist: upright and pointing ahead as he walks, along his aim drawn; an arrow
  // on its string as he draws.
  const float tilt = glm::mix(0.0f, pitch, raise);
  const glm::mat4 bow = m * glm::translate(glm::mat4(1.0f), handL) * glm::rotate(glm::mat4(1.0f), tilt, glm::vec3(1, 0, 0)) *
                        glm::rotate(glm::mat4(1.0f), glm::mix(0.0f, -0.12f, raise), glm::vec3(0, 0, 1));
  const glm::vec3 nock = buildBow(mb, bow, draw * 0.55f);
  if (draw > 0.02f) buildArrow(mb, bow * glm::translate(glm::mat4(1.0f), nock + glm::vec3(0.012f, 0.0f, 0.0f)));
}

int warheadTurns(float fitting) {
  // Each turn begins with the grip's end (0.2 into its cycle).
  const float cycles = turnCycles(fitting);
  return cycles < 0.2f ? 0 : std::min(kTurns, int(std::floor(cycles - 0.2f)) + 1);
}

void buildBowView(MeshBuilder& mb, float draw, bool nocked, float fitting) {
  const bool screwing = fitting >= 0.0f;
  // The bow in the left hand: held low and canted, the arrow on it pointing ahead; raised as he
  // draws to arm's length before his eye, a little left of the middle (the arrow's line just
  // under it); lowered out of the way while he screws on a warhead.
  const float raise = std::min(1.0f, draw * 3.0f), r = raise * raise * (3.0f - 2.0f * raise);
  const glm::vec3 held = glm::mix(glm::vec3(-0.2f, -0.36f, -0.55f), glm::vec3(-0.05f, -0.055f, -0.62f), r);
  glm::mat4 bow = glm::translate(glm::mat4(1.0f), held) * glm::rotate(glm::mat4(1.0f), glm::mix(0.55f, 0.12f, r), glm::vec3(0, 0, 1)) *
                  glm::rotate(glm::mat4(1.0f), glm::mix(-0.12f, 0.0f, r), glm::vec3(1, 0, 0));
  if (screwing) bow = glm::translate(glm::mat4(1.0f), glm::vec3(-0.3f, -0.62f, -0.55f)) * glm::rotate(glm::mat4(1.0f), 0.9f, glm::vec3(0, 0, 1));
  // The string drawn back as far as the anchor under his chin.
  const float pull = screwing ? 0.0f : draw * 0.4f;
  const glm::vec3 nock = buildBow(mb, bow, pull);
  // The left hand round the grip, its palm against the grip's left side, the fingers round its
  // front, the thumb up over them.
  hand(mb, bow * handAt({-0.03f, -0.005f, 0.062f}, {1, 0, 0}, {0, 0, -1}), true, {0.62f, 0.55f, 0.0f}, {-0.3f, -0.32f, -0.3f});
  if (!screwing) {
    if (nocked) {
      // On the rest beside the grip, its nock on the string; the right hand's fingers on it.
      const glm::vec3 n = glm::vec3(bow * glm::vec4(nock, 1.0f));
      // (Along the bow's own forward axis, beside its grip.)
      const glm::mat4 arrow = glm::translate(glm::mat4(1.0f), n) * glm::mat4(glm::mat3(bow)) * glm::translate(glm::mat4(1.0f), glm::vec3(0.012f, 0.0f, 0.0f));
      buildArrow(mb, arrow);
      // Three fingers hooked on the string round the nock, the palm to the left, the back of the
      // hand out (drawn to his chin, out of sight).
      if (pull < 0.3f) hand(mb, arrow * handAt({0.03f, -0.012f, 0.085f}, {-1, 0, 0}, {0, 0, -1}), false, {0.75f, 0.45f, 0.0f}, {0.3f, -0.15f, 0.1f});
    }
    return;
  }
  // Screwing on the warhead: the arrow held up before him (its tip up and a little to the right),
  // the left hand round the shaft; the warhead comes up to its end in the right hand, which then
  // turns it as a cap is screwed on: grip, a quarter turn, let go, back, grip again, the warhead
  // going a little further on each turn. (The arrow's axes: +x towards him, +y to his left, -z
  // up along it to its tip.)
  const glm::vec3 tipward = glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f)), leftward = glm::normalize(glm::vec3(-1.0f, 0.3f, 0.0f));
  const glm::mat4 across(glm::vec4(glm::cross(leftward, -tipward), 0.0f), glm::vec4(leftward, 0.0f), glm::vec4(-tipward, 0.0f),
                         glm::vec4(-0.17f, -0.76f, -0.5f, 1.0f));
  buildArrow(mb, across, false);
  // (Its palm towards him, the back of the hand out, the thumb up along the shaft.)
  hand(mb, across * handAt({-0.018f, 0.095f, -0.5f}, {1, 0, 0}, {0, -1, 0}), true, {0.72f, 0.5f, 0.0f}, {-0.3f, -0.6f, -0.25f});
  // The warhead brought up quickly (the first tenth) till its collar touches the shaft's end, then
  // six slow turns that screw the collar onto the shaft, each a grip (0 ..
  // 0.2), a quarter turn (.. 0.7), letting go (.. 0.82) and the hand back (.. 1).
  const float come = std::min(1.0f, fitting / 0.1f);
  const float cycles = turnCycles(fitting), phase = cycles - std::floor(cycles);
  const float done = std::floor(cycles) + std::clamp((phase - 0.2f) / 0.5f, 0.0f, 1.0f);
  const float sweep = phase < 0.2f ? 0.0f : phase < 0.7f ? (phase - 0.2f) / 0.5f : phase < 0.82f ? 1.0f : 1.0f - (phase - 0.82f) / 0.18f;
  const float hold = come < 1.0f ? 1.0f : phase < 0.2f ? phase / 0.2f : phase < 0.7f ? 1.0f : phase < 0.82f ? 1.0f - (phase - 0.7f) / 0.12f : 0.0f;
  const float quarter = 1.5708f;
  const glm::vec3 from{0.09f, -0.16f, -kArrowShaft - 0.18f}, at{0.0f, 0.0f, -kArrowShaft + 0.016f * std::min(1.0f, done / float(kTurns))};
  const float e = come * come * (3.0f - 2.0f * come);
  const glm::mat4 head = across * glm::translate(glm::mat4(1.0f), glm::mix(from, at, e));
  buildWarhead(mb, head, done * quarter);
  // The hand under the warhead, palm up, the fingers round its far side and the thumb over its
  // top: the wrist turns it a quarter turn and back open (the forearm rolling under).
  // (The same way round as the warhead goes on.)
  const glm::mat4 turned = head * glm::rotate(glm::mat4(1.0f), -0.7f - quarter + quarter * sweep, glm::vec3(0, 0, 1));
  hand(mb, turned * handAt({0.05f, -0.04f, -0.065f}, {0, 1, 0}, {-1, 0, 0}), false, {0.4f + 0.3f * hold, 0.55f + 0.35f * hold, 0.0f},
       {0.38f, -0.75f, -0.3f});
}

}  // namespace render
