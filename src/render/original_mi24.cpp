#include "render/original_mi24.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "render/figures.hpp"
#include "render/models.hpp"
#include "sim/heli.hpp"

namespace render {

namespace {

void add(MeshBuilder& mb, const gamedata::Model& m, const glm::mat4& t) {
  for (const gamedata::Polygon& p : m.polys)
    mb.face(p.pts, p.color, p.oneSided, t, p.uv, p.texture);
}

void add(MeshBuilder& mb, const gamedata::Model& m, glm::vec3 offset) {
  for (const gamedata::Polygon& p : m.polys)
    mb.face(p.pts, p.color, p.oneSided, glm::translate(glm::mat4(1.0f), offset), p.uv, p.texture);
}

// Where the lower nose carries the turret (from its sub-object table).
constexpr glm::vec3 kTurretAt{0.0f, -1.44f, -5.47f};

// The doorways of the model (its doors' places, lifted): x 0.76, z -1.9 .. -0.68, from the sill
// at y -0.95 to 0.12.
constexpr float kDoorX = 0.76f, kDoorZ0 = -1.9f, kDoorZ1 = -0.68f, kSill = -0.95f, kLintel = 0.12f;

using Poly = std::vector<Vertex>;

// The part of a polygon on one side of the plane where coordinate `axis` is `at`.
Poly clip(const Poly& in, int axis, float at, bool keepAbove) {
  Poly out;
  const float sign = keepAbove ? 1.0f : -1.0f;
  for (size_t i = 0; i < in.size(); ++i) {
    const Vertex &a = in[i], &b = in[(i + 1) % in.size()];
    const float da = (a.pos[axis] - at) * sign, db = (b.pos[axis] - at) * sign;
    if (da >= 0.0f) out.push_back(a);
    if ((da >= 0.0f) != (db >= 0.0f)) {
      const float t = da / (da - db);
      Vertex v = a;
      v.pos = glm::mix(a.pos, b.pos, t), v.normal = glm::mix(a.normal, b.normal, t);
      v.color = glm::mix(a.color, b.color, t), v.tex = glm::mix(a.tex, b.tex, t);
      out.push_back(v);
    }
  }
  return out;
}

void emit(MeshBuilder& mb, const Poly& p) {
  for (size_t i = 1; i + 1 < p.size(); ++i) mb.verts.insert(mb.verts.end(), {p[0], p[i], p[i + 1]});
}

// The body with the doorways cut out of its sides: the faces there clipped to what lies before,
// behind, under and over each doorway.
void cutDoorways(const MeshBuilder& body, MeshBuilder& out) {
  for (size_t t = 0; t + 2 < body.verts.size(); t += 3) {
    Poly rest(body.verts.begin() + long(t), body.verts.begin() + long(t) + 3);
    bool side = true;
    for (const Vertex& v : rest) side = side && std::fabs(v.pos.x) > 0.45f && v.pos.x * rest[0].pos.x > 0.0f;
    if (!side) {
      emit(out, rest);
      continue;
    }
    for (const auto& [axis, at, above] : {std::tuple{2, kDoorZ0, false}, {2, kDoorZ1, true}, {1, kSill, false}, {1, kLintel, true}}) {
      emit(out, clip(rest, axis, at, above));
      rest = clip(rest, axis, at, !above);
      if (rest.size() < 3) break;
    }
  }
}

// The cabin inside, as a Mi-24's is painted (grey-green, a little dim, the floor darker): its
// floor, roof, front and back and sides, each seen only from within (so that the near side does
// not hide the rest), and the troops' brown seats along the sides behind the doors.
void buildCabin(MeshBuilder& mb) {
  const float x = kDoorX - 0.02f, roof = kLintel + 0.35f, z0 = kDoorZ0 - 0.4f, z1 = kDoorZ1 + 0.4f;
  const glm::vec3 c{0.0f, (kSill + roof) * 0.5f, (kDoorZ0 + kDoorZ1) * 0.5f};
  auto wall = [&](std::vector<glm::vec3> q, glm::vec3 colour) {
    // (A one-sided face is seen from the side its corners go round clockwise.)
    const glm::vec3 n = glm::cross(q[1] - q[0], q[2] - q[0]);
    if (glm::dot(n, c - q[0]) > 0.0f) std::reverse(q.begin(), q.end());
    mb.face(q, colour, true);
  };
  const glm::vec3 floor{0.24f, 0.24f, 0.21f}, walls{0.34f, 0.35f, 0.30f}, brown{0.30f, 0.21f, 0.16f};
  wall({{-x, kSill, z0}, {x, kSill, z0}, {x, kSill, z1}, {-x, kSill, z1}}, floor);
  wall({{-x, roof, z0}, {x, roof, z0}, {x, roof, z1}, {-x, roof, z1}}, walls);
  wall({{-x, kSill, z0}, {x, kSill, z0}, {x, roof, z0}, {-x, roof, z0}}, walls);
  wall({{-x, kSill, z1}, {x, kSill, z1}, {x, roof, z1}, {-x, roof, z1}}, walls);
  for (float sx : {-x, x}) wall({{sx, kSill, z0}, {sx, kSill, z1}, {sx, roof, z1}, {sx, roof, z0}}, walls);
  for (float sx : {-1.0f, 1.0f}) {
    mb.box(glm::mat4(1.0f), {sx > 0 ? x - 0.42f : -x, kSill + 0.42f, kDoorZ1 + 0.05f}, {sx > 0 ? x : -x + 0.42f, kSill + 0.5f, z1}, brown);
    mb.box(glm::mat4(1.0f), {sx > 0 ? x - 0.1f : -x, kSill + 0.5f, kDoorZ1 + 0.05f}, {sx > 0 ? x : -x + 0.1f, kSill + 1.0f, z1}, brown);
  }
}

}  // namespace

glm::vec3 OriginalMi24::doorGunPivot(int side) {
  return {(side ? 1.0f : -1.0f) * (kDoorX - 0.15f), kSill + 0.5f, (kDoorZ0 + kDoorZ1) * 0.5f};
}

bool OriginalMi24::build(const gamedata::OriginalData& data) {
  const int hind = data.loaded() ? data.find("Mi_24V_Hind_E") : -1;
  const int noseUp = data.find("Mi_24V_Lower_Nose_Gr_Up"), noseDown = data.find("Mi_24V_Lower_Nose_Gr_Dn");
  const int rearUp = data.find("Mi_24V_Lower_Rear"), rearDown = data.find("Mi_24V_Lower_Rear_Gr_Dn");
  const int gun = data.find("Mi_24V_Gun_Turret");
  if (hind < 1 || noseUp < 1 || noseDown < 1 || rearUp < 1 || rearDown < 1 || gun < 1) return false;
  const glm::vec3 lift{0.0f, kOriginalMi24Lift, 0.0f};
  // The doors apart, hinged at their own origins (the upper halves at the top, the lower at the bottom).
  std::vector<std::string> skip{"Mi_24V_Main_Rotor"};
  for (const gamedata::OriginalData::Part& p : data.parts(hind)) {
    const std::string n = data.name(p.object);
    if (n.find("Door") == std::string::npos) continue;
    skip.push_back(n);
    const gamedata::Model door = data.model(p.object);
    const glm::mat4 at = glm::translate(glm::mat4(1.0f), p.at + lift);
    const bool upper = n.find("Upper") != std::string::npos, left = p.at.x < 0.0f;
    // The lower halves hang down along the side, out of the door guns' way (opened as a step,
    // level, the rounds fired down at a drone below would go through them).
    const float swing = glm::radians(upper ? 100.0f : 160.0f) * ((upper == left) ? -1.0f : 1.0f);
    // Shut, a few centimetres out over the dark doorway the body has under them (the
    // original draws them after it; data.model lifts such overlays up to 3.6 cm).
    add(doorsShut, door, glm::translate(glm::mat4(1.0f), glm::vec3(left ? -0.045f : 0.045f, 0.0f, 0.0f)) * at);
    add(doorsOpen, door, at * glm::rotate(glm::mat4(1.0f), swing, glm::vec3(0, 0, 1)));
  }
  // The doors' insides are plain blue in the data: painted as a Mi-24's cabin is, grey-green.
  for (MeshBuilder* mb : {&doorsShut, &doorsOpen})
    for (Vertex& v : mb->verts)
      if (v.tex.z < 0.0f && v.color.b > 0.9f && v.color.r < 0.1f) v.color = glm::vec4(0.45f, 0.44f, 0.36f, v.color.a);
  add(body, data.model(hind, skip), lift);
  // The pieces: the middle (without its parts), then each part.
  std::vector<std::string> apart = skip;
  std::vector<std::pair<int, glm::vec3>> parts;
  for (const gamedata::OriginalData::Part& p : data.parts(hind)) {
    const std::string n = data.name(p.object);
    if (n.find("Door") != std::string::npos || n.find("Main_Rotor") != std::string::npos || n == "Null_Object") continue;
    apart.push_back(n);
    parts.emplace_back(p.object, p.at);
  }
  pieces.emplace_back();
  add(pieces.back(), data.model(hind, apart), lift);
  add(pieces.back(), data.model(noseUp, {"Mi_24V_Gun_Turret"}), lift);
  add(pieces.back(), data.model(rearUp), lift);
  add(pieces.back(), data.model(gun), kTurretAt + lift);
  for (const auto& [object, at] : parts) {
    pieces.emplace_back();
    add(pieces.back(), data.model(object), at + lift);
  }
  for (MeshBuilder& mb : pieces) {
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const Vertex& v : mb.verts) lo = glm::min(lo, v.pos), hi = glm::max(hi, v.pos);
    const glm::vec3 c = mb.verts.empty() ? glm::vec3(0.0f) : (lo + hi) * 0.5f;
    for (Vertex& v : mb.verts) v.pos -= c;
    pieceCentres.push_back(c);
    pieceRadii.push_back(mb.verts.empty() ? 0.5f : std::max(0.3f, std::min({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.5f));
  }
  add(gearUp, data.model(noseUp, {"Mi_24V_Gun_Turret"}), lift);
  add(gearUp, data.model(rearUp), lift);
  add(gearDown, data.model(noseDown, {"Mi_24V_Gun_Turret"}), lift);
  add(gearDown, data.model(rearDown), lift);
  add(turret, data.model(gun), kTurretAt + lift);
  cutDoorways(body, bodyOpen);
  buildCabin(cabin);
  for (int side = 0; side < 2; ++side) {
    // Sitting on the floor behind the gun, his knees up and apart, his hands on the grips.
    const float sx = side ? 1.0f : -1.0f;
    const glm::mat4 at = glm::translate(glm::mat4(1.0f), {sx * (kDoorX - 0.8f), kSill + 0.1f - 0.95f, (kDoorZ0 + kDoorZ1) * 0.5f}) *
                         glm::rotate(glm::mat4(1.0f), -sx * 1.5708f, glm::vec3(0, 1, 0));
    buildPosedMan(gunner[side], data, "Russian_Soldier_AK_47", at, 1.9f, 0.4f, -1.9f, 1.3f, 0.2f);
  }
  return !body.verts.empty();
}

bool buildOriginalWingman(const gamedata::OriginalData& data, MeshBuilder& parked, MeshBuilder& flying) {
  const int hind = data.loaded() ? data.find("Mi_24_Hind_Wingman") : -1;
  const int down = data.find("Mi_24_Wingman_Gear_Down"), up = data.find("Mi_24_Wingman_Gear_Up");
  if (hind < 1 || down < 1 || up < 1) return false;
  const gamedata::Model body = data.model(hind);
  add(parked, body, glm::vec3(0.0f));
  add(parked, data.model(down), glm::vec3(0.0f));
  const glm::vec3 centre{0.0f, -sim::Helicopter::kRestHeight, 0.0f};  // its wheels are on the ground at 0
  add(flying, body, centre);
  add(flying, data.model(up), centre);
  return !parked.verts.empty();
}

}  // namespace render
