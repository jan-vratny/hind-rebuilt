#include "render/vehicles.hpp"

#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "render/drones.hpp"
#include "render/mesh.hpp"
#include "render/models.hpp"
#include "render/original_mi24.hpp"
#include "render/recognition.hpp"
#include "render/shadow.hpp"

namespace render {

namespace {

const glm::vec3 kOlive{0.42f, 0.47f, 0.29f};

// The undercarriage of the aircraft of the missions: objects of the data, down and up.
struct GearPart {
  const char* object;
  glm::vec3 at{0.0f};
};
struct AircraftGear {
  std::vector<GearPart> down, up;
};
AircraftGear aircraftGear(const std::string& name) {
  if (name == "MiG_29_Fulcrum") return {{{"MiG_29_Gear_Down"}}, {{"MiG_29_Gear_Up"}}};
  if (name == "Su_25_Frogfoot") return {{{"Su_25_Gear_Down"}}, {}};
  // (Where the game puts them on: the empty objects of its model.)
  if (name == "MiG_27_Flogger") return {{{"mig_27_left_gear", {-1.2f, 0.0f, 0.6f}}, {"mig_27_right_gear", {1.3f, 0.0f, 0.5f}}, {"Mig_27_nose_gear", {0.0f, 0.0f, -6.1f}}}, {}};
  if (name.rfind("F16", 0) == 0) return {{{"F16_Wingman_Gear_Down"}}, {{"F16_Wingman_Gear_Up"}}};
  if (name.rfind("Antonov", 0) == 0) return {{{"An_12_Main_Gear_Down"}, {"An_12_Nose_Gear_Down"}}, {{"An_12_Main_Gear_Up"}, {"An_12_Nose_Gear_up"}}};
  return {};
}
constexpr float kRotorSag = 0.7f;  // m a stopped blade's tip hangs (scaled with the blade)

// Burnt paint: dark and greyish, with a trace of the original colour.
glm::vec3 charred(glm::vec3 c) { return glm::mix(glm::vec3(0.09f, 0.085f, 0.08f), c, 0.18f); }

void charredCopy(const MeshBuilder& from, MeshBuilder& to) {
  to.verts = from.verts;
  for (Vertex& v : to.verts) v.color = glm::vec4(charred(glm::vec3(v.color)), v.color.a);
}

}  // namespace

bool VehicleRenderer::build(const gamedata::OriginalData& original) {
  original_ = &original;
  kinds_.clear();
  bool all = original.loaded();
  for (int k = 0; k < int(sim::VehicleKind::Count); ++k)
    all = all && (sim::vehicleType(sim::VehicleKind(k)).cls == sim::UnitClass::Drone ||
                  original.find(sim::vehicleType(sim::VehicleKind(k)).original) > 0);
  return all;
}

VehicleRenderer::Meshes& VehicleRenderer::meshes(sim::VehicleKind kind) {
  if (size_t(kind) >= kinds_.size()) kinds_.resize(size_t(kind) + 1);
  Meshes& m = kinds_[size_t(kind)];
  if (m.built) return m;
  m.built = true;
  const sim::VehicleType& t = sim::vehicleType(kind);
  MeshBuilder ok, burnt;
  if (t.original.find("Mi_24") != std::string::npos && original_ &&
      buildOriginalWingman(*original_, ok, burnt)) {
    // The original's other Hinds: parked with the gear down, flying with it up under our rotor.
    m.flying.upload(burnt.verts);
    burnt.verts.clear();
    charredCopy(ok, burnt);
    m.intact.upload(ok.verts);
    m.wreck.upload(burnt.verts);
    MeshBuilder rotor;
    buildMainRotor(rotor, glm::mat4(1.0f), 0.0f);
    m.rotor.upload(rotor.verts);
    rotor.verts.clear();
    buildMainRotor(rotor, glm::mat4(1.0f), kRotorSag);
    m.rotorStopped.upload(rotor.verts);
    m.rotorHub = kMainRotorHub;
    // (Its parked model stands with its wheels at 0.)
    m.parkedHub = kMainRotorHub + glm::vec3(0.0f, sim::Helicopter::kRestHeight, 0.0f);
    // The data's has no tail rotor: ours, where the player's original Hind has it.
    rotor.verts.clear();
    buildTailRotor(rotor, glm::mat4(1.0f));
    m.tailRotor.upload(rotor.verts);
    m.tailHub = kTailRotorHub, m.parkedTailHub = kTailRotorHub + glm::vec3(0.0f, sim::Helicopter::kRestHeight, 0.0f);
    m.cull = true;
    return m;
  }
  if (t.cls == sim::UnitClass::Drone) {
    // Not in the original: our Shahed-136, Geran-3 or Banderol.
    if (sim::isCruiseMissile(kind)) buildBanderol(ok, glm::mat4(1.0f));
    else if (sim::isJetDrone(kind)) buildGeran3(ok, glm::mat4(1.0f));
    else buildShahed(ok, glm::mat4(1.0f));
    charredCopy(ok, burnt);
    m.intact.upload(ok.verts);
    m.wreck.upload(burnt.verts);
    return m;
  }
  if (t.original.find("Mi_24") != std::string::npos) {
    // Our own Mi-24.
    buildMi24(ok, glm::mat4(1.0f), Mi24Parts::Parked);
    charredCopy(ok, burnt);
    m.intact.upload(ok.verts);
    m.wreck.upload(burnt.verts);
    MeshBuilder fly, rotor;
    buildMi24(fly, glm::mat4(1.0f), Mi24Parts::Exterior);  // flying with the gear up
    m.flying.upload(fly.verts);
    buildMainRotor(rotor, glm::mat4(1.0f), 0.0f);
    m.rotor.upload(rotor.verts);
    rotor.verts.clear();
    buildTailRotor(rotor, glm::mat4(1.0f));
    m.tailRotor.upload(rotor.verts);
    m.tailHub = kTailRotorHub, m.parkedTailHub = kTailRotorHub + glm::vec3(0.0f, sim::Helicopter::kRestHeight, 0.0f);
    return m;
  }
  int index = -1;
  if (original_ && original_->loaded()) {
    index = t.object > 0 ? t.object : original_->find(t.original);
    if (index > 0) index = original_->cleanest(index);
  }
  gamedata::Model model;
  if (index > 0) model = original_->model(index);
  if (!model.polys.empty()) {
    // Paint on the ground (a runway's markings) lies on its asphalt.
    const glm::vec3 lift{0.0f, model.hi.y - model.lo.y < 0.05f ? 0.2f : 0.0f, 0.0f};
    for (gamedata::Polygon p : model.polys) {
      for (glm::vec3& v : p.pts) v += lift;
      ok.face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
    }
    m.cull = true;
    // The aircraft stand on their undercarriage, which is apart in the data (the game puts it on):
    // down on the ground, in the air up or none.
    if (t.cls == sim::UnitClass::Aircraft) {
      const AircraftGear gear = aircraftGear(t.original);
      MeshBuilder fly = ok;
      const auto put = [&](MeshBuilder& mb, const std::vector<GearPart>& list) {
        for (const GearPart& g : list) {
          const int at = original_->find(g.object);
          if (at <= 0) continue;
          for (const gamedata::Polygon& p : original_->model(at).polys) {
            std::vector<glm::vec3> pts = p.pts;
            for (glm::vec3& v : pts) v += g.at;
            mb.face(pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
          }
        }
      };
      put(ok, gear.down);
      put(fly, gear.up);
      m.flying.upload(fly.verts);
    }
    // The other helicopters: our blades on their hubs, turning or standing still (the same model
    // for both, in the air).
    glm::vec3 hub;
    float scale;
    if (t.cls == sim::UnitClass::Helicopter && helicopterRotor(*original_, index, hub, scale)) {
      m.rotorHub = m.parkedHub = hub, m.rotorScale = scale;
      m.flying.upload(ok.verts);
      MeshBuilder rotor;
      buildMainRotor(rotor, glm::scale(glm::mat4(1.0f), glm::vec3(scale)), 0.0f);
      m.rotor.upload(rotor.verts);
      rotor.verts.clear();
      buildMainRotor(rotor, glm::scale(glm::mat4(1.0f), glm::vec3(scale)), kRotorSag);
      m.rotorStopped.upload(rotor.verts);
      // And a tail rotor where the data has none (the Hip's is as big as the Hind's).
      if (helicopterTailRotor(*original_, index, m.tailHub)) {
        rotor.verts.clear();
        buildTailRotor(rotor, glm::mat4(1.0f));
        m.tailRotor.upload(rotor.verts);
        m.parkedTailHub = m.tailHub;
      }
    }
  } else {
    if (original_ && original_->loaded()) std::fprintf(stderr, "No model %s in the original data\n", t.original.c_str());
    ok.box(glm::mat4(1.0f), t.lo, t.hi, kOlive);
  }
  charredCopy(ok, burnt);
  m.intact.upload(ok.verts);
  m.wreck.upload(burnt.verts);
  return m;
}

const VehicleRenderer::Pieces& VehicleRenderer::pieces(sim::VehicleKind kind) {
  Meshes& m = meshes(kind);
  if (m.piecesBuilt) return m.pieces;
  m.piecesBuilt = true;
  const sim::VehicleType& t = sim::vehicleType(kind);
  if (!original_ || !original_->loaded()) return m.pieces;
  int index = t.object > 0 ? t.object : original_->find(t.original);
  if (index <= 0) return m.pieces;
  index = original_->cleanest(index);
  const auto addModel = [](MeshBuilder& mb, const gamedata::Model& model, glm::vec3 at) {
    for (gamedata::Polygon p : model.polys) {
      for (glm::vec3& v : p.pts) v += at;
      mb.face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
    }
  };
  std::vector<MeshBuilder> parts;
  if (t.arms == sim::Arms::TankGun) {
    if (t.original != "T62_MBT" && t.original != "T72_MBT" && t.original != "T80_MBT") return m.pieces;
    MeshBuilder hull, turret;
    // Its turret: the model's part of that name, or (the T-80's) what lies high up in the middle.
    std::string named;
    for (const gamedata::OriginalData::Part& p : original_->parts(index))
      if (original_->name(p.object).find("Turret") != std::string::npos) named = original_->name(p.object), addModel(turret, original_->model(p.object), p.at);
    if (!named.empty()) {
      addModel(hull, original_->model(index, {named}), glm::vec3(0.0f));
    } else {
      const gamedata::Model whole = original_->model(index);
      const float at = whole.lo.y + (whole.hi.y - whole.lo.y) * 0.55f;
      for (const gamedata::Polygon& p : whole.polys) {
        glm::vec3 c{0.0f};
        for (const glm::vec3& v : p.pts) c += v;
        c /= float(p.pts.size());
        (c.y > at ? turret : hull).face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
      }
    }
    if (turret.verts.empty() || hull.verts.empty()) return m.pieces;
    MeshBuilder burnt;
    charredCopy(hull, burnt);
    m.hull.upload(burnt.verts);
    burnt.verts.clear();
    charredCopy(turret, burnt);
    parts.push_back(std::move(burnt));
  } else {
  // Its own parts, the rest of it the piece they leave.
  std::vector<std::string> apart;
  for (const gamedata::OriginalData::Part& p : original_->parts(index)) {
    const std::string n = original_->name(p.object);
    if (n == "Null_Object" || original_->model(p.object).polys.empty()) continue;
    apart.push_back(n);
    parts.emplace_back();
    addModel(parts.back(), original_->model(p.object), p.at);
  }
  const gamedata::Model rest = original_->model(index, apart);
  if (parts.size() >= 3) {
    if (!rest.polys.empty()) {
      parts.emplace_back();
      addModel(parts.back(), rest, glm::vec3(0.0f));
    }
  } else {
    // Cut up: the middle along its length, what lies out to the sides (wings) apart.
    parts.clear();
    const gamedata::Model whole = original_->model(index);
    if (whole.polys.empty()) return m.pieces;
    const glm::vec3 size = whole.hi - whole.lo;
    std::vector<MeshBuilder> cells(9);
    for (const gamedata::Polygon& p : whole.polys) {
      glm::vec3 c{0.0f};
      for (const glm::vec3& v : p.pts) c += v;
      c /= float(p.pts.size());
      const int x = c.x < whole.lo.x + size.x / 3.0f ? 0 : c.x > whole.hi.x - size.x / 3.0f ? 2 : 1;
      const int z = std::clamp(int((c.z - whole.lo.z) / std::max(size.z, 0.1f) * 3.0f), 0, 2);
      cells[size_t(x + 3 * z)].face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
    }
    for (MeshBuilder& c : cells)
      if (!c.verts.empty()) parts.push_back(std::move(c));
  }
  // Its blades go too.
  glm::vec3 hub;
  float scale;
  if (t.cls == sim::UnitClass::Helicopter && helicopterRotor(*original_, index, hub, scale)) {
    parts.emplace_back();
    buildMainRotor(parts.back(), glm::scale(glm::translate(glm::mat4(1.0f), hub), glm::vec3(scale)), 0.0f);
  }
  }
  for (MeshBuilder& mb : parts) {
    if (mb.verts.empty()) continue;
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const Vertex& v : mb.verts) lo = glm::min(lo, v.pos), hi = glm::max(hi, v.pos);
    const glm::vec3 c = (lo + hi) * 0.5f;
    for (Vertex& v : mb.verts) v.pos -= c;
    m.pieces.meshes.emplace_back().upload(mb.verts);
    m.pieces.centres.push_back(c);
    m.pieces.radii.push_back(std::max(0.3f, std::min({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.5f));
  }
  return m.pieces;
}

void VehicleRenderer::drawPieces(const Program& scene, const sim::Debris& debris, sim::VehicleKind kind, glm::vec3 eye, float range) {
  const Pieces& p = pieces(kind);
  for (size_t i = 0; i < debris.pieces().size() && i < p.meshes.size(); ++i) {
    if (glm::distance(debris.pieces()[i].pos, eye) > range) continue;
    scene.set("uModel", glm::translate(glm::mat4(1.0f), debris.pieces()[i].pos) * glm::mat4_cast(debris.pieces()[i].orient));
    p.meshes[i].draw();
  }
}

void VehicleRenderer::draw(const Program& scene, const sim::Targets& targets, glm::vec3 eye, float range, float time, bool livery,
                           const world::Terrain* terrain) {
  if (blur_.empty()) {
    MeshBuilder mb;
    buildMainRotorDisc(mb);
    blur_.upload(mb.verts);
  }
  for (const sim::Vehicle& v : targets.list) {
    if (v.aboard || v.brokenUp) continue;
    const sim::VehicleType& t = sim::vehicleType(v.kind);
    float d = glm::distance(eye, v.pos);
    if (d > (t.cls == sim::UnitClass::Soldier ? range * 0.25f : range)) continue;
    Meshes& m = meshes(v.kind);
    const bool airborne = t.cls == sim::UnitClass::Aircraft ? terrain && v.pos.y > terrain->groundHeight(v.pos.x, v.pos.z) + 1.5f
                                                            : ((v.moving() && v.delay <= 0.0f && glm::length(v.vel) > 1.0f) || v.hover) && t.cls == sim::UnitClass::Helicopter;
    const bool fly = !v.destroyed && airborne && !m.flying.empty();
    const bool fallen = v.destroyed && t.cls == sim::UnitClass::Soldier;
    const GpuMesh& mesh = v.destroyed && !fallen ? (v.turretOff && !m.hull.empty() ? m.hull : m.wreck) : fly ? m.flying : m.intact;
    glm::mat4 world = v.toWorld();
    // A fallen man lies on the ground.
    if (fallen) world = glm::rotate(glm::translate(world, glm::vec3(0.0f, 0.2f, 0.0f)), -1.5708f, glm::vec3(1, 0, 0));
    scene.set("uModel", world);
    if (m.cull) glEnable(GL_CULL_FACE);
    // Our Hinds in the mission's colours: the parked one's wheels stand at 0, the flying one's
    // centre is where the player's Hind has its.
    const bool painted = livery && v.side == sim::Side::Ours && t.original.find("Mi_24") != std::string::npos && !v.destroyed && m.cull;
    if (painted) {
      scene.set("uLivery", 1.0f);
      scene.set("uLiveryOffset", glm::vec3(0.0f, fly ? 0.0f : -sim::Helicopter::kRestHeight, 0.0f));
    }
    mesh.draw();
    if (painted) scene.set("uLivery", 0.0f), scene.set("uLiveryOffset", glm::vec3(0.0f));
    glDisable(GL_CULL_FACE);
    if (fly && !m.rotor.empty()) {
      scene.set("uModel", glm::translate(world, m.rotorHub) * glm::rotate(glm::mat4(1.0f), time * 25.0f, glm::vec3(0, 1, 0)));
      m.rotor.draw();
      // And the blur of them, as on our own.
      scene.set("uModel", glm::scale(glm::translate(world, m.rotorHub), glm::vec3(m.rotorScale)));
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
      blur_.draw();
      glDepthMask(GL_TRUE);
      glDisable(GL_BLEND);
    } else if (!v.destroyed && !m.rotorStopped.empty()) {
      // Parked: the blades stand still on its hub.
      scene.set("uModel", glm::translate(world, m.parkedHub) * glm::rotate(glm::mat4(1.0f), 0.4f, glm::vec3(0, 1, 0)));
      m.rotorStopped.draw();
    }
    // The tail rotor: turning some five times as fast as the main one in the air, still parked.
    if (!v.destroyed && !m.tailRotor.empty()) {
      scene.set("uModel", glm::translate(world, fly ? m.tailHub : m.parkedTailHub) *
                              glm::rotate(glm::mat4(1.0f), fly ? time * 120.0f : 0.3f, glm::vec3(1, 0, 0)));
      m.tailRotor.draw();
    }
  }
}

void VehicleRenderer::drawShadows(const Program& scene, const sim::Targets& targets, const world::Terrain& terrain, glm::vec3 eye,
                                  float range, glm::vec3 toLight) {
  if (disc_.empty()) {
    MeshBuilder mb;
    std::vector<glm::vec3> rim;
    for (int i = 0; i < 24; ++i) {
      const float a = float(i) * 6.2832f / 24.0f;
      rim.push_back(kMainRotorHub + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * kMainRotorRadius);
    }
    mb.face(rim, glm::vec3(0.0f), false);
    disc_.upload(mb.verts);
  }
  for (const sim::Vehicle& v : targets.list) {
    const sim::VehicleType& t = sim::vehicleType(v.kind);
    if (v.aboard || v.destroyed || (t.cls != sim::UnitClass::Helicopter && t.cls != sim::UnitClass::Aircraft)) continue;
    if (glm::distance(eye, v.pos) > range) continue;
    glm::vec3 at, normal;
    if (!shadowPlane(terrain, v.pos, toLight, at, normal)) continue;
    Meshes& m = meshes(v.kind);
    const bool heli = t.cls == sim::UnitClass::Helicopter;
    const bool fly = (heli ? (v.moving() && v.delay <= 0.0f && glm::length(v.vel) > 1.0f) || v.hover : v.pos.y > terrain.groundHeight(v.pos.x, v.pos.z) + 1.5f) && !m.flying.empty();
    const glm::mat4 flat = shadowMatrix(at, normal, toLight) * v.toWorld();
    scene.set("uModel", flat);
    scene.set("uShadow", 1.0f);
    (fly ? m.flying : m.intact).draw();
    if (fly && heli) {
      scene.set("uShadow", 2.0f);
      if (!m.rotor.empty()) scene.set("uModel", flat * glm::translate(glm::mat4(1.0f), m.rotorHub) * glm::scale(glm::mat4(1.0f), glm::vec3(m.rotorScale)) * glm::translate(glm::mat4(1.0f), -kMainRotorHub));
      disc_.draw();
    } else if (!fly && !m.rotorStopped.empty()) {
      scene.set("uModel", flat * glm::translate(glm::mat4(1.0f), m.parkedHub) * glm::rotate(glm::mat4(1.0f), 0.4f, glm::vec3(0, 1, 0)));
      m.rotorStopped.draw();
    }
  }
  scene.set("uShadow", 0.0f);
}

}  // namespace render
