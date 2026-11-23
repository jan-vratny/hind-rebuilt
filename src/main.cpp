// Hind - a Mi-24 helicopter simulator in the spirit of Digital Integration's 1996 original.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "audio/game_audio.hpp"
#include "gamedata/campaign.hpp"
#include "gamedata/cd.hpp"
#include "gamedata/mission.hpp"
#include "gamedata/original.hpp"
#include "gamedata/picture.hpp"
#include "gamedata/zone.hpp"
#include "gamedata/zonemap.hpp"
#include "menu/frontend.hpp"
#include "menu/planner.hpp"
#include "menu/setup.hpp"
#include "net/link.hpp"
#include "net/mirror.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "render/camera.hpp"
#include "render/cockpit.hpp"
#include "render/effects.hpp"
#include "render/hud.hpp"
#include "render/livery.hpp"
#include "render/screenpass.hpp"
#include "render/gauges.hpp"
#include "render/drones.hpp"
#include "render/models.hpp"
#include "render/original_mi24.hpp"
#include "render/shadow.hpp"
#include "sim/pilot_log.hpp"
#include "sim/topten.hpp"
#include "render/bow.hpp"
#include "render/figures.hpp"
#include "render/overlay.hpp"
#include "render/shaders.hpp"
#include "render/sky.hpp"
#include "render/terrain_renderer.hpp"
#include "render/vehicles.hpp"
#include "sim/aircombat.hpp"
#include "sim/battle.hpp"
#include "sim/enemy.hpp"
#include "sim/heli.hpp"
#include "sim/mission.hpp"
#include "sim/orders.hpp"
#include "sim/targets.hpp"
#include "sim/weapons.hpp"
#include "sim/threats.hpp"
#include "sim/bowman.hpp"
#include "sim/debris.hpp"
#include "sim/troops.hpp"
#include "sim/spotter.hpp"
#include "sim/wso.hpp"
#include "sim/wing_arms.hpp"
#include "world/terrain.hpp"

namespace {

constexpr float kPhysicsDt = 1.0f / 240.0f;
constexpr Uint64 kNetSilenceMs = 10000;   // the network game: the other side silent this long in a flight is gone
constexpr float kShadowRange = 4000.0f;  // m: the aircraft's shadows drawn this far from the eye
constexpr float kTimeHaltRange = 3000.0f;  // m: an enemy this close stops time compression
// The network game: Hinds of their own besides the host's, and players in all (but the host).
constexpr int kMaxWingmen = 3, kMaxPeers = 9;

// A player in our game over the network, on his own connection: in our Hind's crew (at the
// sight or a door gun; his game mirrors ours), or, in the host's game, a wingman flying a Hind of
// his own (in Wing `slot`).
struct Peer {
  net::Link link;
  std::string name, view;  // who, and where he sits (main's --view), from his Hello
  net::Mirror mirror;      // the snapshots to him, each against the last one he got
  bool startSent = false, waitSent = false;
  int64_t inputAt = 0;     // ms of the last word from him (later: the time he has to set up a flight)
  // In the crew: what he does, and where he was last.
  net::GunnerInput input;
  uint8_t launchesSeen = 0, releasesSeen = 0, locksSeen = 0;  // his interceptor launches, lets-go and locks done here
  bool droneOf[4] = {};  // the interceptors he flies, by their owner (1 the sight, 2 and 3 the doors)
  bool aboard = false;
  uint8_t seat = 0;        // 0 on board, 1 the sight, 2 and 3 the left and the right door
  int slot = -1;           // a wingman's Hind
  bool wingman() const { return view == "wingman"; }
  // The lone bowman (sim::Bowman): where his reports put him.
  bool bowman() const { return view == "bowman"; }
  net::BowmanPose pose;
  std::vector<sim::Projectile> tracers;  // his M60's tracers in the air
  bool onFoot = false;  // his reports come
  std::string label() const { return name.empty() ? std::string("A PLAYER") : name; }
};

// A wingman's Hind in the host's game: where his reports put it, his rounds, what the enemy did
// to it (counted here).
struct Wing {
  sim::Helicopter heli;
  sim::Weapons weapons;
  bool flying = false, down = false;
  std::vector<int> standIns;  // the mission's AI wingmen, out while he flies instead
  std::string message;        // told to him, for his next snapshot
  explicit Wing(const world::Terrain& t) : heli(t), weapons(t) {}
};
// L: how bright the instruments' lamps light the dials at night, and the cockpit pictures' rows
// (of 480) where the instrument panels start.
constexpr glm::vec3 kInstrumentLamps{0.9f, 0.9f, 0.88f};
constexpr float kPilotPanelRow = 268.0f, kGunnerPanelRow = 300.0f;

void usage() {
  std::printf(
      "hind [options]\n"
      "  --width N --height N     window size\n"
      "  --fullscreen | --windowed\n"
      "  --display N|left|right   monitor to open on (SDL display number or the outermost one)\n"
      "  --model novice|stable|realistic\n"
      "  --config PATH            configuration file (default ~/.config/hind/hind.ini)\n"
      "  --no-crash               ignore crashes\n"
      "  --original DIR           directory of the original game (models of the vehicles)\n"
      "  --setup                  ask where the original game is (as at the first start)\n"
      "  --mission NAME           fly a mission of the original (e.g. AFGHNS03), campaign or none\n"
      "  --missions               list the missions of the original game\n"
      "  --no-menu                straight into the flying, without the game's menus\n"
      "  --host                   network game: fly as the pilot; a gunner can join over the local network\n"
      "  --join ADDRESS           network game: join the pilot's game at ADDRESS (host or host:port) as the gunner\n"
      "development:\n"
      "  --start-air ALT KMH      start airborne at ALT metres above the airfield\n"
      "  --start-bank DEG         with --start-air: banked DEG degrees (+ right)\n"
      "  --start-pitch DEG        with --start-air: nose DEG degrees up (- down)\n"
      "  --start-pos X Z HDG      start there (metres east and south of the airfield, heading in degrees)\n"
      "  --view pilot|gunner|door|door-left|door-right|external|weapon|target|lock|watch|remote|bowman  start in this view\n"
      "  --bow ready|walk|draw:D|fit:F|m60|hurt:H with --view bowman (without --join: his alone; --orbit D,YAW,PITCH or F6: from outside):\n"
      "                           held drawn D (0..1), or F (0..1) through screwing on a warhead; the M60;\n"
      "                           hurt H (0..1: his health down so much, the picture bloodied)\n"
      "  --fire yakb|upk|s8|s5|fab250|fab500|ofab250|zab500|kmgu|r60|9m114\n"
      "                           hold the trigger with this weapon (its store goes on the inner pylons,\n"
      "                           9M114 on the wingtips)\n"
      "  --arm WEAPON             as --fire, but only armed (the HUD's weapons mode)\n"
      "  --zone NAME              afghan, kazak, korea, saratov (original game) or procedural\n"
      "  --plan briefing|map|payload  show this planning screen also when taking a screenshot\n"
      "  --plan-keys \"K K ...\"    then press these keys (SDL names, e.g. Tab, Right, Insert; S-Tab\n"
      "                           with Shift), clicks (X,Y on the 640 x 480 screen; R:X,Y right) and\n"
      "                           text (text:WORD)\n"
      "  --menu PAGE              show this menu also when taking a screenshot: main, quick, flight,\n"
      "                           training, single, campaign, exit or network\n"
      "  --menu-keys \"K K ...\"    then press these keys and clicks, as --plan-keys\n"
      "  --destroy N              blow up the first N ground targets at the start\n"
      "  --look DEG               turn the seat view DEG degrees to the left (negative: right)\n"
      "  --look-pitch DEG         tilt the seat view DEG degrees up from its default (negative: down)\n"
      "  --virtual-mount X,Y[,F]  a virtual door gun mount at these axes (-1..1), F = 1 the trigger held\n"
      "                           (with door_device = Virtual Joystick in [joystick])\n"
      "  --orbit M,YAW,PITCH      the external view from M metres, YAW and PITCH degrees\n"
      "  --fail all|N,N...        systems knocked out from the start (0 NAV, 1 COM, 2 JAM, 3 TWI, 4 HUD,\n"
      "                           5 LLTV, 6 FLIR, 7 GUN, 8 HMTD, 9 LAS, 10 U/C)\n"
      "  --end-ask                show \"END MISSION? Y/N\" (Ctrl+Q) at once\n"
      "  --abort                  end the mission at once as with Ctrl+Q, Y (the debriefing)\n"
      "  --hour H                 take off at H o'clock (e.g. 13.5), whatever the mission says\n"
      "  --quickstart             fly the mission as Quickstart does (Ukraine: by day, on the patrol line)\n"
      "  --sight-zoom N           with --view gunner: the sight magnified N steps (1: 3.3x, 2: 10x)\n"
      "  --at-waypoint N          start on the ground at point N of the mission's plan (0 = take-off)\n"
      "  --doors                  start with the cargo doors open\n"
      "  --night-vision           start with the night vision on (V)\n"
      "  --hud-black              start with the HUD black (I)\n"
      "  --clean-shot             the picture alone, without the HUD and its texts (pictures for the menus)\n"
      "  --top-ten-score N        open the Top Ten with a line for this Arcade score\n"
      "  --virtual-joystick       attach a virtual stick (3 axes, a hat) and pedals (3 axes),\n"
      "                           moved by --menu-keys \"axis:D:A:V hat:D:H:BITS frame\"\n"
      "  --say K                  press Ctrl+K (1..9, 0) two seconds into the flight: a message to the others\n"
      "  --instrument-lights      start with the instruments' lamps on (L)\n"
      "  --wait-gunner            as --host, the flight standing still until the gunner's game has joined\n"
      "  --screenshot FILE.ppm    save frame 120 to FILE and quit\n"
      "  --shot-time S            ... or the frame after S seconds of flight\n"
      "  --shot-cam X,Y,Z[,door]  the camera X m right, Y up, Z back of the helicopter, looking at the nearest\n"
      "                           enemy drone (with door: at the right door gun)\n"
      "  --shot-pan T0,T1,FOV[,K] with --shot-cam ...,door: between T0 and T1 seconds turning to the drone,\n"
      "                           the field of view narrowing to FOV degrees (K < 1: only so far, 0.5 halfway)\n"
      "  --shot-eye X,Y,Z,T[,G]   with --shot-cam: till T seconds the camera standing at X,Y,Z (world; the shot\n"
      "                           with --shot-cam alone prints where its camera was), zoomed in on the Hind;\n"
      "                           the last G seconds gliding onto its place on the Hind\n"
      "  --shot-slow F            off screen F times slower (F frames to a 60th of a second): slow motion\n"
      "  --shot-frames N          ... and N frames in a row from it, 60 a second (FILE-0000.ppm ...)\n"
      "  --model-sheet FILE.ppm   render the Mi-24 from the side, top and front at 100 px/m and quit\n"
      "                           (FILE.obj: write the model's triangles as OBJ with vertex colours)\n"
      "  --sheet-model NAME       model of the sheet: mi24 (default), shahed, geran3, litavr, banderol, r60 or 9m114 (at 400 px/m) or obj:N\n"
      "                           (object N of the original game; obj:N@PPM at PPM pixels a metre);\n"
      "                           original: with FILE.obj the original's Mi-24 (+ FILE-atlas.ppm, its textures)\n");
}

struct Meshes {
  render::GpuMesh exterior, gear, rotor, rotorStopped, disc, tailRotor, chinGun, instruments, effects, doorView, litavrs, bow, arrows;
  // The original game's Mi-24V seen from outside (when chosen and there).
  bool original = false;
  render::GpuMesh origBody, origGearUp, origGearDown, origTurret, origDoorsShut, origDoorsOpen;
  // With door guns: the body with the doorways cut, the cabin, the gunners (left, right).
  render::GpuMesh origBodyOpen, origCabin, origGunner[2];
  // The pieces it breaks into in a crash (the original's parts and our rotors), about their
  // centres; empty without the original.
  std::vector<render::GpuMesh> pieces;
  std::vector<glm::vec3> pieceCentres;
  std::vector<float> pieceRadii;
  std::vector<bool> pieceCull;
  void buildOriginal(const gamedata::OriginalData& data) {
    render::OriginalMi24 o;
    if (!(original = o.build(data))) return;
    sim::Weapons::doorPivot = render::OriginalMi24::doorGunPivot(1);  // the rounds from the guns drawn
    for (size_t i = 0; i < o.pieces.size(); ++i) {
      pieces.emplace_back().upload(o.pieces[i].verts);
      pieceCentres.push_back(o.pieceCentres[i]);
      pieceRadii.push_back(o.pieceRadii[i]);
      pieceCull.push_back(true);
    }
    render::MeshBuilder mb;
    render::buildMainRotor(mb, glm::mat4(1.0f), 0.0f);
    pieces.emplace_back().upload(mb.verts);
    pieceCentres.push_back(render::kMainRotorHub);
    pieceRadii.push_back(0.3f);
    pieceCull.push_back(false);
    mb.verts.clear();
    render::buildTailRotor(mb, glm::mat4(1.0f));
    pieces.emplace_back().upload(mb.verts);
    pieceCentres.push_back(render::kTailRotorHub);
    pieceRadii.push_back(0.3f);
    pieceCull.push_back(false);
    origBody.upload(o.body.verts);
    origGearUp.upload(o.gearUp.verts);
    origGearDown.upload(o.gearDown.verts);
    origTurret.upload(o.turret.verts);
    origDoorsShut.upload(o.doorsShut.verts);
    origDoorsOpen.upload(o.doorsOpen.verts);
    origBodyOpen.upload(o.bodyOpen.verts);
    origCabin.upload(o.cabin.verts);
    for (int side = 0; side < 2; ++side) origGunner[side].upload(o.gunner[side].verts);
  }
  // The stores by kind, pylon and side, built when first carried.
  render::GpuMesh stores[sim::kStores][sim::kPylons][2];
  render::GpuMesh interceptorRacks[7][2];  // by the drones left on it (only the inner pylon carries one)
  void build() {
    render::MeshBuilder mb;
    glm::mat4 id(1.0f);
    render::buildMi24(mb, id, render::Mi24Parts::Exterior, false);
    exterior.upload(mb.verts);
    mb.verts.clear();
    render::buildChinGun(mb, id);
    chinGun.upload(mb.verts);
    mb.verts.clear();
    render::buildLandingGear(mb, id);
    gear.upload(mb.verts);
    mb.verts.clear();
    render::buildMainRotor(mb, id, 0.15f);
    rotor.upload(mb.verts);
    mb.verts.clear();
    render::buildMainRotor(mb, id, 0.7f);  // standing still its blades sag
    rotorStopped.upload(mb.verts);
    mb.verts.clear();
    render::buildMainRotorDisc(mb);
    disc.upload(mb.verts);
    mb.verts.clear();
    render::buildTailRotor(mb, id);
    tailRotor.upload(mb.verts);
  }
  // What hangs under the wings now: pods stay on when empty, bombs and missiles go.
  void drawStores(const sim::Weapons& w) {
    sim::WingArms::Stores s;
    for (int p = 0; p < sim::kPylons; ++p) {
      s.store[p] = uint8_t(w.loadout().at(sim::Pylon(p)));
      for (int side = 0; side < 2; ++side) s.rounds[p][side] = int8_t(std::clamp(w.sideRounds(sim::Pylon(p), side), 0, 127));
    }
    drawStores(s);
  }
  void drawStores(const sim::WingArms::Stores& set) {
    for (int p = 0; p < sim::kPylons; ++p) {
      const sim::Store st = sim::Store(set.store[p]);
      if (st == sim::Store::None || int(st) >= int(std::size(stores))) continue;
      const sim::StoreInfo& si = sim::storeInfo(st);
      for (int side = 0; side < 2; ++side) {
        if (si.available && si.emptyMass <= 0.0f && set.rounds[p][side] <= 0) continue;
        // A rack of interceptors shows the drones still on it.
        const bool drones = st == sim::Store::Interceptor;
        const int left = drones ? std::clamp(int(set.rounds[p][side]), 0, si.rounds) : -1;
        render::GpuMesh& mesh = drones ? interceptorRacks[left][side] : stores[int(st)][p][side];
        if (mesh.empty()) {
          render::MeshBuilder mb;
          render::buildStore(mb, glm::mat4(1.0f), st, sim::Pylon(p), side, left);
          mesh.upload(mb.verts);
        }
        mesh.draw();
      }
    }
  }
};

// Display for the window: an SDL display number, "left" / "right" for the outermost monitor,
// or -1 (empty) to let the system choose.
int pickDisplay(const std::string& want) {
  const int n = SDL_GetNumVideoDisplays();
  if (want.empty() || n <= 0) return -1;
  if (want == "left" || want == "right") {
    int best = 0;
    SDL_Rect bestR{};
    for (int i = 0; i < n; ++i) {
      SDL_Rect r;
      if (SDL_GetDisplayBounds(i, &r) != 0) continue;
      if (i == 0 || (want == "left" ? r.x < bestR.x : r.x > bestR.x)) best = i, bestR = r;
    }
    return best;
  }
  int i = std::atoi(want.c_str());
  return i >= 0 && i < n ? i : -1;
}

void saveScreenshot(const std::string& path, int w, int h) {
  std::vector<unsigned char> px(size_t(w) * h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * w * 3], 1, size_t(w) * 3, f);
  std::fclose(f);
  std::printf("Saved %s\n", path.c_str());
}

// F1's: the screen (the back buffer, before it is shown) as a BMP in `dir`, named by the time.
// Returns the file's path, or "" when it could not be written.
std::string saveScreenBmp(const std::string& dir, int w, int h) {
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(dir), ec);
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
  std::string path = dir + "/hind_" + stamp + ".bmp";
  for (int n = 2; std::filesystem::exists(std::filesystem::path(path), ec); ++n)
    path = dir + "/hind_" + stamp + "_" + std::to_string(n) + ".bmp";
  std::vector<unsigned char> px(size_t(w) * h * 3), rows(px.size());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < h; ++y) std::memcpy(&rows[size_t(y) * w * 3], &px[size_t(h - 1 - y) * w * 3], size_t(w) * 3);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(rows.data(), w, h, 24, w * 3, SDL_PIXELFORMAT_RGB24);
  const bool ok = s && SDL_SaveBMP(s, path.c_str()) == 0;
  if (s) SDL_FreeSurface(s);
  return ok ? path : std::string();
}

// The model as a Wavefront OBJ (vertex colours after the positions), for drawing it elsewhere: the
// Mi-24 with its stores, gun, gear and rotors (groups airframe, main_rotor, tail_rotor), or a drone
// ("shahed", "geran3", "litavr", "banderol").
void writeModelObj(const std::string& path, const std::string& drone, const sim::Loadout& loadout,
                   const gamedata::OriginalData* original) {
  const bool shahed = !drone.empty();
  render::MeshBuilder mb;
  std::vector<size_t> groups;  // where the main and the tail rotor start (OBJ groups)
  render::OriginalMi24 orig;
  if (shahed) {
    if (drone == "geran3") render::buildGeran3(mb, glm::mat4(1.0f));
    else if (drone == "litavr") render::buildLitavr(mb, glm::mat4(1.0f), 0.6f);
    else if (drone == "banderol") render::buildBanderol(mb, glm::mat4(1.0f));
    else render::buildShahed(mb, glm::mat4(1.0f));
  } else if (original && orig.build(*original)) {
    // The original's Mi-24 as it flies (gear up, doors shut), with our stores and rotors as the
    // game shows it; its textured faces keep their place in the atlas, written beside the OBJ.
    // With door guns the doors are open, the doorways cut into the cabin, a gunner at each gun.
    const bool doorGuns = loadout.doorGun[0] || loadout.doorGun[1];
    for (const render::MeshBuilder* part :
         {doorGuns ? &orig.bodyOpen : &orig.body, &orig.gearUp, doorGuns ? &orig.doorsOpen : &orig.doorsShut, &orig.turret})
      mb.verts.insert(mb.verts.end(), part->verts.begin(), part->verts.end());
    if (doorGuns) mb.verts.insert(mb.verts.end(), orig.cabin.verts.begin(), orig.cabin.verts.end());
    for (int side = 0; side < 2; ++side) {
      if (!loadout.doorGun[side]) continue;
      mb.verts.insert(mb.verts.end(), orig.gunner[side].verts.begin(), orig.gunner[side].verts.end());
      render::buildDoorGun(mb, side, {side ? 1.0f : -1.0f, -0.2f, -0.25f}, render::OriginalMi24::doorGunPivot(side), true, false);
    }
    const glm::mat4 lift = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, render::kOriginalMi24Lift, 0.0f));
    for (int p = 0; p < sim::kPylons; ++p)
      for (int side = 0; side < 2; ++side) render::buildStore(mb, lift, loadout.at(sim::Pylon(p)), sim::Pylon(p), side);
    groups.push_back(mb.verts.size());
    render::buildMainRotor(mb, glm::translate(glm::mat4(1.0f), render::kMainRotorHub), 0.15f);
    groups.push_back(mb.verts.size());
    render::buildTailRotor(mb, glm::translate(glm::mat4(1.0f), render::kTailRotorHub));
    const std::string atlas = path.substr(0, path.size() - 4) + "-atlas.ppm";
    if (FILE* a = std::fopen(atlas.c_str(), "wb")) {
      // The textures as the atlas has them (render::uploadAtlas), rows from the top, with the
      // Ukrainian missions' Czech camouflage (render/livery.hpp).
      int n = original->textureCount();
      const std::vector<uint8_t> textures = render::withLivery(original->textures(), n);
      const int s = render::kAtlasSize, t = render::kAtlasTile;
      std::vector<uint8_t> rgb(size_t(s) * s * 3, 255);
      for (int k = 0; k < std::min(n, render::kAtlasTiles * render::kAtlasTiles); ++k)
        for (int y = 0; y < t; ++y)
          for (int x = 0; x < t; ++x)
            for (int c = 0; c < 3; ++c)
              rgb[((size_t(k / render::kAtlasTiles * t + y)) * s + size_t(k % render::kAtlasTiles * t + x)) * 3 + c] =
                  textures[(size_t(k) * t * t + size_t(y) * t + x) * 4 + c];
      std::fprintf(a, "P6\n%d %d\n255\n", s, s);
      std::fwrite(rgb.data(), 1, rgb.size(), a);
      std::fclose(a);
      std::printf("Saved %s\n", atlas.c_str());
    }
  } else {
    render::buildMi24(mb, glm::mat4(1.0f), render::Mi24Parts::Exterior, false);
    for (int p = 0; p < sim::kPylons; ++p)
      for (int side = 0; side < 2; ++side) render::buildStore(mb, glm::mat4(1.0f), loadout.at(sim::Pylon(p)), sim::Pylon(p), side);
    render::buildChinGun(mb, glm::mat4(1.0f));
    render::buildLandingGear(mb, glm::mat4(1.0f));
    groups.push_back(mb.verts.size());
    render::buildMainRotor(mb, glm::translate(glm::mat4(1.0f), render::kMainRotorHub), 0.15f);
    groups.push_back(mb.verts.size());
    render::buildTailRotor(mb, glm::translate(glm::mat4(1.0f), render::kTailRotorHub));
  }
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) {
    std::fprintf(stderr, "Cannot write %s\n", path.c_str());
    return;
  }
  for (const render::Vertex& v : mb.verts)
    std::fprintf(f, "v %.4f %.4f %.4f %.3f %.3f %.3f\n", v.pos.x, v.pos.y, v.pos.z, v.color.r, v.color.g, v.color.b);
  // Texture coordinates in the atlas (v down from its top, as it lies in the file); the third
  // is 1 on a textured face, 0 on a plain one.
  const bool textured = std::any_of(mb.verts.begin(), mb.verts.end(), [](const render::Vertex& v) { return v.tex.z >= 0.0f; });
  if (textured)
    for (const render::Vertex& v : mb.verts) std::fprintf(f, "vt %.5f %.5f %d\n", v.tex.x, v.tex.y, v.tex.z >= 0.0f ? 1 : 0);
  static const char* names[] = {"main_rotor", "tail_rotor"};
  std::fprintf(f, "g %s\n", shahed ? "shahed" : "airframe");
  for (size_t i = 0; i + 2 < mb.verts.size(); i += 3) {
    for (size_t g = 0; g < std::min<size_t>(groups.size(), std::size(names)); ++g)
      if (groups[g] == i) std::fprintf(f, "g %s\n", names[g]);
    if (textured) std::fprintf(f, "f %zu/%zu %zu/%zu %zu/%zu\n", i + 1, i + 1, i + 2, i + 2, i + 3, i + 3);
    else std::fprintf(f, "f %zu %zu %zu\n", i + 1, i + 2, i + 3);
  }
  std::fclose(f);
  std::printf("Saved %s: %zu triangles\n", path.c_str(), mb.verts.size() / 3);
}

// Orthographic side, top and front views of the parked Mi-24 at 100 px per metre, laid out
// like a three-view drawing (nose to the left), for comparison with the reference drawings.
void renderModelSheet(render::Program& scene, const std::string& path, const std::string& model, const sim::Loadout& loadout,
                      const gamedata::OriginalData& original) {
  const bool shahed = model == "shahed" || model == "geran3" || model == "litavr" || model == "banderol" || model == "r60" || model == "9m114";
  // "obj:N": object N of the original game's 3DOBJS.BIN, centred in each view.
  const int object = model.rfind("obj:", 0) == 0 ? std::atoi(model.c_str() + 4) : 0;
  gamedata::Model orig = object > 0 ? original.model(object) : gamedata::Model();
  const float extent = object > 0 ? std::max(1.0f, glm::length(orig.hi - orig.lo)) : 1.0f;
  // "obj:N@PPM" draws it at PPM pixels a metre (to lay it over a drawing), otherwise filling the views.
  const size_t atSign = model.find('@');
  const int ppm = shahed ? 400 : object > 0 ? (atSign != std::string::npos ? std::atoi(model.c_str() + atSign + 1) : int(1300.0f / extent)) : 100;
  const int w = 2200, h = shahed ? 2400 : 2200;
  GLuint fbo, color, depth;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glGenRenderbuffers(1, &color);
  glBindRenderbuffer(GL_RENDERBUFFER, color);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
  glGenRenderbuffers(1, &depth);
  glBindRenderbuffer(GL_RENDERBUFFER, depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);

  // Airframe and gear in every view, rotors only from the side and front (elsewhere they hide the airframe).
  render::MeshBuilder mb;
  render::GpuMesh airframe, rotors;
  if (shahed) {
    if (model == "geran3") render::buildGeran3(mb, glm::mat4(1.0f));
    else if (model == "litavr") render::buildLitavr(mb, glm::mat4(1.0f), 0.6f);
    else if (model == "banderol") render::buildBanderol(mb, glm::mat4(1.0f));
    else if (model == "r60") render::buildR60(mb, glm::mat4(1.0f));
    else if (model == "9m114") render::buildShturm(mb, glm::mat4(1.0f));
    else render::buildShahed(mb, glm::mat4(1.0f));
    std::printf("%s: %zu triangles\n",
                model == "geran3" ? "Geran-3" : model == "litavr" ? "Litavr" : model == "banderol" ? "Banderol" : model == "r60" ? "R-60" : model == "9m114" ? "9M114" : "Shahed-136",
                mb.verts.size() / 3);
    airframe.upload(mb.verts);
  } else if (object > 0) {
    for (const gamedata::Polygon& p : orig.polys)
      mb.face(p.pts, p.color, p.oneSided, glm::mat4(1.0f), p.uv, p.texture);
    std::printf("%s: %zu polygons, %d not decoded, %.1f x %.1f x %.1f m\n", orig.name.c_str(), orig.polys.size(),
                original.skipped(), orig.hi.x - orig.lo.x, orig.hi.y - orig.lo.y, orig.hi.z - orig.lo.z);
    airframe.upload(mb.verts);
  } else {
    render::buildMi24(mb, glm::mat4(1.0f), render::Mi24Parts::Exterior, false);
    for (int p = 0; p < sim::kPylons; ++p)
      for (int side = 0; side < 2; ++side) render::buildStore(mb, glm::mat4(1.0f), loadout.at(sim::Pylon(p)), sim::Pylon(p), side);
    render::buildChinGun(mb, glm::mat4(1.0f));
    render::buildLandingGear(mb, glm::mat4(1.0f));
    std::printf("Mi-24: %zu triangles\n", mb.verts.size() / 3);
    airframe.upload(mb.verts);
    mb.verts.clear();
    render::buildMainRotor(mb, glm::translate(glm::mat4(1.0f), render::kMainRotorHub), 0.15f);
    render::buildTailRotor(mb, glm::translate(glm::mat4(1.0f), render::kTailRotorHub));
    rotors.upload(mb.verts);
  }

  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  scene.use();
  scene.set("uModel", glm::mat4(1.0f));
  scene.set("uFogColor", glm::vec3(1.0f));
  scene.set("uFogDist", 1.0e6f);
  scene.set("uLight", glm::vec3(1.0f));
  scene.set("uAmbient", 0.45f);
  scene.set("uDiffuse", 0.65f);
  struct View {
    int x, y, vw, vh;          // viewport in pixels (y from the bottom)
    glm::vec3 eye, up;         // looking at the origin
    float left, bottom;        // world extent at the viewport's lower left corner
    float scale = 0;           // pixels per metre (0: the sheet's)
  };
  const View shahedViews[] = {
      {0, 1700, 2200, 700, {-50, 0, 0}, {0, 1, 0}, -2.75f, -0.875f},
      {0, 600, 2200, 1100, {0, 50, 0}, {1, 0, 0}, -2.75f, -1.375f},
      {0, 0, 1100, 600, {0, 0, -50}, {0, 1, 0}, -1.375f, -0.75f},
      {1100, 0, 1100, 600, {-32, 22, -32}, {0, 1, 0}, -2.1f, -1.15f, 260.0f},
  };
  // The 5 m Banderol: the oblique view smaller.
  const View banderolViews[] = {shahedViews[0], shahedViews[1], shahedViews[2],
                                {1100, 0, 1100, 600, {-32, 22, -32}, {0, 1, 0}, -2.4f, -1.3f, 230.0f}};
  const View mi24Views[] = {
      {0, 1500, 2200, 700, {-50, 0, 0}, {0, 1, 0}, -7.0f, -2.3f},  // side, from the left
      {0, 700, 2200, 800, {0, 50, 0}, {1, 0, 0}, -7.0f, -4.0f},    // top
      {0, 0, 900, 700, {0, 0, -50}, {0, 1, 0}, -4.5f, -2.3f},      // front
      {900, 0, 1300, 700, {-32, 22, -32}, {0, 1, 0}, -9.75f, -4.6f, 66.0f},  // oblique, front left above
  };
  const glm::vec3 centre = object > 0 ? (orig.lo + orig.hi) * 0.5f : glm::vec3(0.0f);
  for (const View& v : model == "banderol" ? banderolViews : shahed || object > 0 ? shahedViews : mi24Views) {
    glViewport(v.x, v.y, v.vw, v.vh);
    const float scale = v.scale > 0 ? v.scale * (object > 0 ? float(ppm) / 400.0f : 1.0f) : ppm;
    glm::mat4 proj = object > 0 ? glm::ortho(-0.5f * v.vw / scale, 0.5f * v.vw / scale, -0.5f * v.vh / scale,
                                             0.5f * v.vh / scale, 1.0f, 100.0f + extent)
                                : glm::ortho(v.left, v.left + v.vw / scale, v.bottom, v.bottom + v.vh / scale, 1.0f, 100.0f);
    scene.set("uViewProj", proj * glm::lookAt(centre + v.eye, centre, v.up));
    scene.set("uCamPos", v.eye);
    scene.set("uSunDir", glm::normalize(glm::normalize(v.eye) + glm::vec3(0.3f, 0.6f, -0.3f)));
    if (object > 0) glEnable(GL_CULL_FACE);  // the original's one-sided polygons
    airframe.draw();
    glDisable(GL_CULL_FACE);
    if (v.eye.y == 0.0f) rotors.draw();
  }
  saveScreenshot(path, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteRenderbuffers(1, &color);
  glDeleteRenderbuffers(1, &depth);
  glDeleteFramebuffers(1, &fbo);
}

}  // namespace

struct ZoneInfo {
  const char* key;
  const char* dir;
  const char* airfield;  // home base; empty: the first named one, null: the middle of the map
  const char* missions;  // prefix of the mission files
  int number;            // in the missions' .MIS files
  float latitude;        // degrees north, for the stars
};
// Kazakhstan and Saratov carry a copy of Korea's AIRFIELD.BIN (their airfields are found by their
// runway markings instead: gamedata::airfieldsFromMarkings).
const ZoneInfo kZones[] = {{"afghan", "AFGHAN.WZ", "BAGRAM", "AFGHN", 3, 34.5f},
                           {"kazak", "KAZAK.WZ", nullptr, "KAZAK", 1, 47.0f},
                           {"korea", "KOREA.WZ", "", "KOREA", 2, 38.0f},
                           {"saratov", "SARATOV.WZ", nullptr, "SARAT", 4, 51.5f},
                           // Ours: the Ukrainian war on Saratov's steppe (data/missions/UKRAI*).
                           {"ukraine", "SARATOV.WZ", nullptr, "UKRAI", 4, 49.5f}};

// The remake's own missions: data/missions next to the build directory, or in the source tree.
// Our own data (the Ukrainian missions, their speech): the directory found, "" when none.
std::string ownDataDir;

// Runs this game again with `args` and waits for it (making a picture off screen).
void runSelf(const std::string& exe, const std::vector<std::string>& args) {
#ifdef _WIN32
  // cmd.exe drops the outer quotes of the whole line, so it gets a pair more.
  auto quote = [](const std::string& s) { return "\"" + s + "\""; };
  std::string line = "\"" + quote(exe);
  for (const std::string& a : args) line += " " + quote(a);
  line += "\"";
#else
  auto quote = [](const std::string& s) {
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
  };
  std::string line = quote(exe);
  for (const std::string& a : args) line += " " + quote(a);
  line += " >/dev/null 2>&1";
#endif
  std::printf("Drawing a picture: %s\n", args.back().c_str());
  std::fflush(stdout);
  if (std::system(line.c_str()) != 0) std::fprintf(stderr, "Could not draw %s\n", args.back().c_str());
}

void addOwnMissions() {
  std::vector<std::string> dirs;
  if (char* base = SDL_GetBasePath()) {
    dirs.push_back(std::string(base) + "../data/missions");
    dirs.push_back(std::string(base) + "data/missions");
    SDL_free(base);
  }
  dirs.push_back(HIND_DATA_DIR "/missions");
  for (const std::string& d : dirs)
    if (std::filesystem::is_directory(d)) {
      gamedata::addMissionDir(d);
      ownDataDir = std::filesystem::path(d).parent_path().string();
      return;
    }
}

const ZoneInfo* zoneInfo(const std::string& key) {
  for (const ZoneInfo& z : kZones)
    if (key == z.key) return &z;
  return nullptr;
}

// The landscape: a war zone of the original game (afghan, kazak, korea, saratov) around its main
// airfield, or the procedural one (also when the original is not installed). With `near` (m east
// and north in the zone, such as where a mission starts) it is around the airfield nearest to
// that point, or the point itself when no airfield is close. `origin` is set to the zone's point
// at the world origin, `objects` to the zone's buildings, bridges and scrub. With `scenery` (the
// original's objects, to stand about it) the airfield has the zone's own runways.
world::Terrain makeTerrain(const std::string& zone, const std::string& originalDir, const glm::vec2* near,
                           glm::vec2& origin, std::vector<gamedata::ZoneObject>& objects,
                           const gamedata::OriginalData* scenery) {
  origin = glm::vec2(0.0f);
  objects.clear();
  if (const ZoneInfo* z = zoneInfo(zone)) {
    gamedata::Zone data;
    std::string error;
    if (originalDir.empty() || !gamedata::loadZone(originalDir, z->dir, data, error)) {
      std::fprintf(stderr, "%s, using the procedural landscape\n",
                   originalDir.empty() ? "Original Hind not found" : error.c_str());
      return world::Terrain();
    }
    // Kazakhstan and Saratov have a copy of Korea's list: their own airfields where their runway
    // markings stand.
    if (!z->airfield && scenery && scenery->loaded()) data.airfields = gamedata::airfieldsFromMarkings(data, *scenery);
    const gamedata::ZoneAirfield* base = nullptr;
    if (near) {
      for (const gamedata::ZoneAirfield& a : data.airfields)
        if (glm::distance(a.pos, *near) < 5000.0f && (!base || glm::distance(a.pos, *near) < glm::distance(base->pos, *near)))
          base = &a;
    } else {
      base = z->airfield ? data.airfield(z->airfield) : data.airfields.empty() || !scenery ? nullptr : &data.airfields[0];
      for (const gamedata::ZoneAirfield& a : data.airfields)
        if (z->airfield && !base && a.name.rfind("AIRFIELD", 0) != 0) base = &a;
    }
    origin = base ? base->pos : near ? *near : glm::vec2(float(data.size - 1) * data.spacing * 0.5f);
    std::printf("Landscape: %s, home base %s\n", z->dir, base ? base->name.c_str() : near ? "(field)" : "(middle)");
    std::vector<world::Runway> runways;
    std::vector<world::Pad> pads;
    if (base && scenery && scenery->loaded()) {
      runways = gamedata::zoneRunways(data, *scenery, origin);
      // Its aprons and taxiways, the paved areas of the original's map (FEATURES.BIN) about it in
      // the zone palette's colour (the terrain keeps those on its levelled ground).
      gamedata::ZoneMap map;
      std::string mapError;
      std::vector<uint8_t> pal;
      if (std::ifstream in(gamedata::findOriginalFile(gamedata::findOriginalFile(originalDir, z->dir), "3DPAL.RGB"), std::ios::binary); in)
        pal.assign(std::istreambuf_iterator<char>(in), {});
      if (gamedata::loadZoneMap(originalDir, z->dir, map, mapError))
        for (const gamedata::ZoneMap::Shape& s : map.shapes) {
          if (!s.area || s.kind != gamedata::ZoneMap::Road || s.pts.size() < 3 ||
              glm::distance(s.pts[0], origin) > 6000.0f)
            continue;
          world::Pad p;
          for (const glm::vec2& q : s.pts) p.outline.push_back({q.x - origin.x, origin.y - q.y});
          if (size_t(s.colour) * 3 + 2 < pal.size())
            p.colour = 0.85f * glm::vec3(pal[size_t(s.colour) * 3], pal[size_t(s.colour) * 3 + 1], pal[size_t(s.colour) * 3 + 2]) / 255.0f;
          pads.push_back(std::move(p));
        }
    }
    objects = std::move(data.objects);
    // Valley floors are 0 in the original; lift them so that nothing counts as sea.
    return world::Terrain(data.heightMap(origin, 300.0f), std::move(runways), std::move(pads));
  }
  if (zone != "procedural") std::fprintf(stderr, "unknown zone '%s', using the procedural landscape\n", zone.c_str());
  return world::Terrain();
}

// Keys and clicks of the development options --plan-keys and --menu-keys: SDL key names ("Tab";
// "S-Tab" with Shift) and clicks "X,Y" on the 640 x 480 screen ("R:X,Y" with the right button),
// which `toWindow` turns into window pixels.
template <typename F>
std::vector<SDL_Event> scriptedEvents(const std::string& keys, F toWindow) {
  std::vector<SDL_Event> out;
  size_t at = 0;
  while (at < keys.size()) {
    size_t end = keys.find(' ', at);
    if (end == std::string::npos) end = keys.size();
    std::string k = keys.substr(at, end - at);
    at = end + 1;
    if (k.empty()) continue;
    SDL_Event ev{};
    // The virtual joysticks of --virtual-joystick: "axis:D:A:V" (device, axis, -1..1), "hat:D:H:BITS",
    // and "frame": the rest from the next frame on.
    if (int d, a, bits; std::sscanf(k.c_str(), "hat:%d:%d:%d", &d, &a, &bits) == 3) {
      ev.type = SDL_USEREVENT, ev.user.code = 2;
      ev.user.data1 = reinterpret_cast<void*>(intptr_t(d * 256 + a)), ev.user.data2 = reinterpret_cast<void*>(intptr_t(bits));
      out.push_back(ev);
      continue;
    }
    if (int d, a; std::sscanf(k.c_str(), "axis:%d:%d:", &d, &a) == 2) {
      ev.type = SDL_USEREVENT, ev.user.code = 1;
      ev.user.data1 = reinterpret_cast<void*>(intptr_t(d * 256 + a));
      ev.user.data2 = reinterpret_cast<void*>(intptr_t(std::lround(std::atof(k.c_str() + k.rfind(':') + 1) * 32767.0)));
      out.push_back(ev);
      continue;
    }
    if (k == "frame") {
      ev.type = SDL_USEREVENT, ev.user.code = 3;
      out.push_back(ev);
      continue;
    }
    if (k.rfind("text:", 0) == 0) {
      // Typed text (a name, an address).
      ev.type = SDL_TEXTINPUT;
      std::snprintf(ev.text.text, sizeof ev.text.text, "%s", k.c_str() + 5);
      out.push_back(ev);
      continue;
    }
    int x, y;
    const bool right = k.rfind("R:", 0) == 0;
    int x2, y2;
    if (std::sscanf(k.c_str(), "%d,%d>%d,%d", &x, &y, &x2, &y2) == 4) {
      // A drag: pressed at x,y, moved to x2,y2 in steps, let go there.
      const glm::vec2 a = toWindow(glm::vec2(float(x), float(y))), b = toWindow(glm::vec2(float(x2), float(y2)));
      ev.type = SDL_MOUSEMOTION;
      ev.motion.x = int(a.x), ev.motion.y = int(a.y);
      out.push_back(ev);
      ev = {};
      ev.type = SDL_MOUSEBUTTONDOWN;
      ev.button.x = int(a.x), ev.button.y = int(a.y), ev.button.button = SDL_BUTTON_LEFT, ev.button.clicks = 1;
      out.push_back(ev);
      for (int k2 = 1; k2 <= 8; ++k2) {
        const glm::vec2 p = a + (b - a) * (float(k2) / 8.0f);
        ev = {};
        ev.type = SDL_MOUSEMOTION;
        ev.motion.x = int(p.x), ev.motion.y = int(p.y);
        out.push_back(ev);
      }
      ev = {};
      ev.type = SDL_MOUSEBUTTONUP;
      ev.button.x = int(b.x), ev.button.y = int(b.y), ev.button.button = SDL_BUTTON_LEFT, ev.button.clicks = 1;
      out.push_back(ev);
      continue;
    }
    if (std::sscanf(k.c_str() + (right ? 2 : 0), "%d,%d", &x, &y) == 2) {
      const glm::vec2 wp = toWindow(glm::vec2(float(x), float(y)));
      ev.type = SDL_MOUSEMOTION;
      ev.motion.x = int(wp.x), ev.motion.y = int(wp.y);
      out.push_back(ev);
      ev = {};
      ev.button.x = int(wp.x), ev.button.y = int(wp.y);
      ev.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
      ev.button.clicks = 1;
      for (Uint32 t : {Uint32(SDL_MOUSEBUTTONDOWN), Uint32(SDL_MOUSEBUTTONUP)}) {
        ev.type = t;
        out.push_back(ev);
      }
      continue;
    }
    const bool shift = k.rfind("S-", 0) == 0;
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.scancode = SDL_GetScancodeFromName(k.c_str() + (shift ? 2 : 0));
    ev.key.keysym.mod = shift ? KMOD_SHIFT : KMOD_NONE;
    if (ev.key.keysym.scancode == SDL_SCANCODE_UNKNOWN) std::fprintf(stderr, "unknown key %s\n", k.c_str());
    else out.push_back(ev);
  }
  return out;
}

// The mission to fly: one named, or the zone's campaign mission the player has got to.
bool pickMission(const platform::Config& cfg, const std::string& originalDir, gamedata::Mission& mission) {
  std::string name = cfg.getString("world", "mission", "campaign");
  const std::string zone = cfg.getString("world", "zone", "afghan");
  if (originalDir.empty() || name.empty() || name == "none") return false;
  if (name == "campaign") {
    // The one the campaign's page picked for its level (or one picked now).
    if (!zoneInfo(zone)) return false;
    const gamedata::CampaignState c = gamedata::loadCampaign(cfg, zone, originalDir);
    name = !c.mission.empty() ? c.mission : gamedata::pickCampaignMission(c, originalDir, zone, unsigned(std::time(nullptr)));
    if (name.empty()) return false;
  }
  std::string error;
  if (!gamedata::loadMission(originalDir, name, mission, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return false;
  }
  return true;
}

namespace {
// The lone bowman's LAW rocket as the effects draw it: as a small rocket (an S-5's), its motor
// burning all the way (guided, it has to).
sim::Projectile lawRocket(const sim::Bowman::Rocket& r) {
  sim::Projectile p{sim::WeaponType::S5, r.pos, r.vel};
  p.age = std::min(r.age, 0.5f);
  p.law = true;
  return p;
}
}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // (Windows' C library takes line buffering for full)
#endif
  addOwnMissions();
  std::string configPath = platform::defaultConfigPath();
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[i + 1];

  platform::Config cfg;
  bool existed = cfg.load(configPath);
  cfg.applyDefaults();
  if (!existed && cfg.save(configPath)) std::printf("Wrote default configuration to %s\n", configPath.c_str());

  std::string screenshotPath, modelSheetPath, sheetModel = "mi24";
  float startAlt = -1.0f, startKmh = 0.0f, startBank = 0.0f, startPitch = 0.0f;
  glm::vec3 orbit{0.0f};  // --orbit: distance, yaw, pitch of the external view
  std::string startView = "pilot", autoFire;
  glm::vec2 startPos{0.0f};
  float startHeading = 0.0f;
  bool customStart = false;
  int destroyCount = 0;
  float startLook = 0.0f, startLookPitch = 0.0f;  // --look-pitch: up (+) from the seat's own
  // --virtual-mount X,Y[,FIRE]: an SDL virtual joystick moved there soon after the start, to try
  // the door gun mount's whole way through the game without one (door_device = Virtual Joystick).
  bool useVirtualMount = false;
  glm::vec2 virtualMount{0.0f};
  int virtualMountFire = 0;
  std::string bowPose;  // --bow ready|draw:D|fit:F|law|law-follow…: the lone bowman held so (development)
  int sightZoom = 0;
  float forceHour = -1.0f;
  bool quickDev = false;  // --quickstart: the mission as Quickstart flies it (development)
  int endTest = 0;
  std::string failSystems;  // --fail: systems knocked out from the start  // --end-ask 1: the game stopped with "END MISSION? Y/N"; --abort 2: ended at once  // --hour: the mission's take-off at this time instead  // --sight-zoom: steps of the gunner's sight's magnification
  bool armOnly = false;  // --arm: the weapon of --fire armed, the trigger left alone
  int atWaypoint = -1;     // --at-waypoint: on the ground at this point of the mission's plan
  bool debugDoor = false;  // --debug-door: why the door gunners fire or not, once a second on the console while on (L)
  bool doorLogging = false;  // switched by L
  double doorDebugAt = 0.0;
  float interceptSec = -1.0f;  // --intercept: in the air beside the mission's first drone, which catches up in this time
  glm::vec2 interceptAt{-150.0f, -60.0f};  // where it draws level then: m to our right, m above us (--intercept S,R,A)
  bool openDoors = false;  // --doors
  bool nightVision = false, nightVisionAtStart = false;  // V; each flight starts as --night-vision says
  // I: the HUD black instead of green. Each flight starts black by day and green at night, as
  // in the original (--hud-black: black whenever).
  bool hudBlack = false, hudBlackAlways = false, hudChosen = false;
  bool virtualJoystick = false;  // --virtual-joystick
  int topTenScore = 0;           // --top-ten-score: the Top Ten page entering it
  bool cleanShot = false;        // --clean-shot: no HUD nor texts over the picture
  int sayKey = 0;  // --say K: Ctrl+K two seconds into the flight (a network game's message)
  bool instrumentLights = false;  // L: the instruments' lamps on (they show at night)
  double shotTime = -1.0;
  int shotFrames = 0, shotSaved = 0;
  float shotSlow = 1.0f;  // --shot-slow: off screen, so many frames to a 60th of a second (slow motion)
  // --shot-cam: the camera at a point of the helicopter (metres right, up, back) looking at its
  // right door or at the nearest enemy drone (where it blew up, once gone).
  bool shotCam = false, shotCamDoor = false;
  glm::vec3 shotCamAt{0.0f}, shotCamDrone{0.0f};
  int shotCamUnit = -1;  // the drone it keeps to
  glm::vec4 shotEye{0.0f, 0.0f, 0.0f, -1.0f};  // --shot-eye: the camera standing at this point till then
  float shotEyeGlide = 0.0f;                    // ... gliding from it onto the helicopter in these last seconds
  glm::vec4 shotPan{-1.0f, -1.0f, 0.0f, 1.0f};  // --shot-pan: from the door to the drone between these seconds, narrowing to this field, this far  // --shot-frames: so many frames in a row (FILE-0000.ppm ...)
  bool listOnly = false;
  bool forceSetup = false;  // --setup: the first start's question where the original is
  std::string planScreen;  // the planning screen to show also when taking a screenshot
  std::string planKeys;    // keys and clicks for it
  std::string menuPage, menuKeys;  // the same for the menus
  bool direct = false;     // straight into the flying (or the planning), no menus
  bool hosting = false;    // --host: the pilot's side of a network game
  bool waitGunner = false; // --wait-gunner: the flight starts once the gunner is at his sight
  std::string joinAddress; // --join: the gunner's side, the pilot's game to join
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--width") cfg.set("video", "width", next());
    else if (a == "--height") cfg.set("video", "height", next());
    else if (a == "--fullscreen") cfg.set("video", "fullscreen", "1");
    else if (a == "--windowed") cfg.set("video", "fullscreen", "0");
    else if (a == "--display") cfg.set("video", "display", next());
    else if (a == "--model") cfg.set("flight", "model", next());
    else if (a == "--no-crash") cfg.set("flight", "no_crash", "1");
    else if (a == "--config") ++i;
    else if (a == "--original") cfg.set("data", "original_dir", next());
    else if (a == "--setup") forceSetup = true;
    else if (a == "--zone") cfg.set("world", "zone", next());
    else if (a == "--mission") cfg.set("world", "mission", next()), direct = true;
    else if (a == "--no-menu") direct = true;
    else if (a == "--host") hosting = true;
    else if (a == "--wait-gunner") waitGunner = hosting = true;
    else if (a == "--join") joinAddress = next(), direct = true;
    else if (a == "--menu") menuPage = next();
    else if (a == "--menu-keys") menuKeys = next();
    else if (a == "--missions") listOnly = true;
    else if (a == "--briefing") planScreen = "briefing", direct = true;
    else if (a == "--plan") planScreen = next(), direct = true;
    else if (a == "--plan-keys") planKeys = next();
    else if (a == "--start-pos") {
      startPos.x = float(std::atof(next().c_str()));
      startPos.y = float(std::atof(next().c_str()));
      startHeading = glm::radians(float(std::atof(next().c_str())));
      customStart = true;
      direct = true;
    }
    else if (a == "--screenshot") screenshotPath = next();
    else if (a == "--shot-time") shotTime = std::atof(next().c_str());
    else if (a == "--shot-cam") {
      char at[16] = "";
      shotCam = std::sscanf(next().c_str(), "%f,%f,%f,%15s", &shotCamAt.x, &shotCamAt.y, &shotCamAt.z, at) >= 3;
      shotCamDoor = !std::strcmp(at, "door");
    }
    else if (a == "--shot-pan") std::sscanf(next().c_str(), "%f,%f,%f,%f", &shotPan.x, &shotPan.y, &shotPan.z, &shotPan.w);
    else if (a == "--shot-eye") std::sscanf(next().c_str(), "%f,%f,%f,%f,%f", &shotEye.x, &shotEye.y, &shotEye.z, &shotEye.w, &shotEyeGlide);
    else if (a == "--shot-slow") shotSlow = std::max(1.0f, float(std::atof(next().c_str())));
    else if (a == "--shot-frames") shotFrames = std::max(0, std::atoi(next().c_str()));
    else if (a == "--model-sheet") modelSheetPath = next();
    else if (a == "--sheet-model") sheetModel = next();
    else if (a == "--view") startView = next();
    else if (a == "--fire") autoFire = next();
    else if (a == "--arm") autoFire = next(), armOnly = true;
    else if (a == "--destroy") destroyCount = std::atoi(next().c_str());
    else if (a == "--bow") bowPose = next();
    else if (a == "--look") startLook = glm::radians(float(std::atof(next().c_str())));
    else if (a == "--look-pitch") startLookPitch = glm::radians(float(std::atof(next().c_str())));
    else if (a == "--virtual-mount") {
      useVirtualMount = true;
      std::sscanf(next().c_str(), "%f,%f,%d", &virtualMount.x, &virtualMount.y, &virtualMountFire);
    }
    else if (a == "--orbit") std::sscanf(next().c_str(), "%f,%f,%f", &orbit.x, &orbit.y, &orbit.z);
    else if (a == "--sight-zoom") sightZoom = std::atoi(next().c_str());
    else if (a == "--end-ask") endTest = 1;
    else if (a == "--fail") failSystems = next();
    else if (a == "--abort") endTest = 2;
    else if (a == "--hour") forceHour = std::clamp(float(std::atof(next().c_str())), 0.0f, 24.0f);
    else if (a == "--at-waypoint") atWaypoint = std::atoi(next().c_str()), direct = true;
    else if (a == "--debug-door") debugDoor = true;
    else if (a == "--quickstart") quickDev = true;
    else if (a == "--intercept") {
      const std::string v = next();
      interceptSec = std::max(0.0f, float(std::atof(v.c_str()))), direct = true;
      float r = 0.0f, up = 0.0f;
      if (std::sscanf(v.c_str(), "%*f,%f,%f", &r, &up) == 2) interceptAt = {r, up};
    }
    else if (a == "--doors") openDoors = true;
    else if (a == "--night-vision") nightVision = nightVisionAtStart = true;
    else if (a == "--hud-black") hudBlackAlways = true;
    else if (a == "--virtual-joystick") virtualJoystick = true;
    else if (a == "--clean-shot") cleanShot = true;
    else if (a == "--top-ten-score") topTenScore = std::atoi(next().c_str());
    else if (a == "--say") sayKey = std::atoi(next().c_str());
    else if (a == "--instrument-lights") instrumentLights = true;
    else if (a == "--start-bank") startBank = float(std::atof(next().c_str()));
    else if (a == "--start-pitch") startPitch = float(std::atof(next().c_str()));
    else if (a == "--start-air") {
      startAlt = float(std::atof(next().c_str()));
      startKmh = float(std::atof(next().c_str()));
      direct = true;
    }
    else {
      usage();
      return a == "--help" || a == "-h" ? 0 : 1;
    }
  }

  if (listOnly) {
    const std::string dir = gamedata::findOriginalDir(cfg.getString("data", "original_dir", ""));
    if (dir.empty()) {
      std::fprintf(stderr, "Original Hind not found (set [data] original_dir in %s)\n", configPath.c_str());
      return 1;
    }
    for (const ZoneInfo& z : kZones)
      for (const std::string& n : gamedata::listMissions(dir, z.missions)) {
        gamedata::Mission m;
        std::string error;
        if (gamedata::loadMission(dir, n, m, error)) std::printf("%-9s %-8s %s\n", n.c_str(), z.key, m.title.c_str());
      }
    return 0;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  // A stick with a throttle and pedals of their own, for trying out the joystick's settings.
  std::vector<SDL_Joystick*> virtualJoys;
  if (virtualJoystick) {
    // (Moved also in a window without the focus: the screenshots'.)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    for (const char* name : {"Virtual stick", "Virtual pedals"}) {
      SDL_VirtualJoystickDesc desc{};
      desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
      desc.type = Uint16(SDL_JOYSTICK_TYPE_FLIGHT_STICK);
      desc.naxes = 3;
      desc.nbuttons = name[8] == 's' ? 4 : 0;
      desc.nhats = name[8] == 's' ? 1 : 0;
      desc.name = name;
      const int index = SDL_JoystickAttachVirtualEx(&desc);
      if (SDL_Joystick* j = index >= 0 ? SDL_JoystickOpen(index) : nullptr) virtualJoys.push_back(j);
      else std::fprintf(stderr, "--virtual-joystick: %s\n", SDL_GetError());
    }
    // The throttle down, the toe brakes let go (as a real one rests).
    if (virtualJoys.size() == 2) SDL_JoystickSetVirtualAxis(virtualJoys[0], 2, 32767);
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);  // the shadows darken each pixel once
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  int msaa = cfg.getInt("video", "msaa", 4);
  if (msaa > 0) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
  }

  bool fullscreen = cfg.getBool("video", "fullscreen", false);
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  // Screenshots and model sheets render off screen in a hidden window: nothing pops up or takes
  // the keyboard focus.
  const bool offscreen = !screenshotPath.empty() || !modelSheetPath.empty();
  if (debugDoor && !screenshotPath.empty()) doorLogging = true;  // no L to press off screen
  if (offscreen) flags = (flags & ~Uint32(SDL_WINDOW_RESIZABLE)) | SDL_WINDOW_HIDDEN;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  int ww = cfg.getInt("video", "width", 1600), wh = cfg.getInt("video", "height", 900);
  const int display = pickDisplay(cfg.getString("video", "display", ""));
  const int wpos = display >= 0 ? int(SDL_WINDOWPOS_CENTERED_DISPLAY(display)) : int(SDL_WINDOWPOS_CENTERED);
  SDL_Window* window = SDL_CreateWindow("HIND REBUILT", wpos, wpos, ww, wh, flags);
  if (!window && msaa > 0) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    window = SDL_CreateWindow("HIND REBUILT", wpos, wpos, ww, wh, flags);
  }
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  {
    // The window's icon (taskbar, window list): our Mi-24 on a red badge (tools/make_icon.py).
    static const unsigned char kIcon[] = {
#include "render/icon.inc"
    };
    static_assert(sizeof kIcon == 64 * 64 * 4);
    if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char*>(kIcon), 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32)) {
      SDL_SetWindowIcon(window, icon);
      SDL_FreeSurface(icon);
    }
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    std::fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError());
    return 1;
  }
  {
    std::string missing;
    if (!hindgl::load(SDL_GL_GetProcAddress, missing)) {
      std::fprintf(stderr, "OpenGL 3.3: the driver does not provide %s\n", missing.c_str());
      return 1;
    }
  }
  SDL_GL_SetSwapInterval(cfg.getBool("video", "vsync", true) ? 1 : 0);
  // Off-screen frame buffer for screenshots: multisampled, resolved before reading.
  GLuint shotFbo = 0, shotResolve = 0, shotRb[3] = {0, 0, 0};
  if (!screenshotPath.empty()) {
    glGenFramebuffers(1, &shotFbo);
    glGenFramebuffers(1, &shotResolve);
    glGenRenderbuffers(3, shotRb);
    glBindFramebuffer(GL_FRAMEBUFFER, shotFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, shotRb[0]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, std::max(msaa, 0), GL_RGBA8, ww, wh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, shotRb[0]);
    glBindRenderbuffer(GL_RENDERBUFFER, shotRb[1]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, std::max(msaa, 0), GL_DEPTH24_STENCIL8, ww, wh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, shotRb[1]);
    glBindFramebuffer(GL_FRAMEBUFFER, shotResolve);
    glBindRenderbuffer(GL_RENDERBUFFER, shotRb[2]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, ww, wh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, shotRb[2]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  {
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    std::printf("OpenGL %s on %s, antialiasing %dx (asked %dx)\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), int(samples), std::max(msaa, 0));
  }
  {  // GL objects below must be destroyed before the context.

  std::string originalDir = gamedata::findOriginalDir(cfg.getString("data", "original_dir", ""));
  // The first start (or --setup): where the original's data are, as without them the game
  // cannot be played. (The screenshots and model sheets made off screen go on without them:
  // the development's.)
  if (forceSetup && offscreen && !screenshotPath.empty()) {
    menu::SetupResult unused;
    menu::runSetup(window, "", "", unused, screenshotPath);
    std::printf("Saved %s\n", screenshotPath.c_str());
    return 0;
  }
  if ((originalDir.empty() || forceSetup) && !offscreen) {
    const std::string configured = cfg.getString("data", "original_dir", "");
    const std::string why = !forceSetup && !configured.empty()
                                ? "There is no Hind in " + configured + " (original_dir in " + configPath + ")."
                                : "";
    menu::SetupResult chosen;
    const std::string copyTo = (std::filesystem::path(configPath).parent_path() / "original").string();
    if (!menu::runSetup(window, copyTo, why, chosen)) {
      std::printf("Without the original Hind the game cannot run.\n");
      return 0;
    }
    originalDir = chosen.originalDir;
    platform::Config saved;
    saved.load(configPath);
    saved.applyDefaults();
    saved.set("data", "original_dir", originalDir);
    cfg.set("data", "original_dir", originalDir);
    if (!chosen.cd.empty()) saved.set("data", "cd", chosen.cd), cfg.set("data", "cd", chosen.cd);
    saved.save(configPath);
    std::printf("The original Hind: %s%s%s (in %s)\n", originalDir.c_str(), chosen.cd.empty() ? "" : ", CD ", chosen.cd.c_str(),
                configPath.c_str());
  }
  // The original's sounds (not for the screenshots and model sheets made off screen).
  audio::GameAudio sound;
  // The briefings' speech: on the original's CD (or its image), ours in data/speech.
  gamedata::Cd cd;
  if (!originalDir.empty() && cd.open(cfg.getString("data", "cd", ""), originalDir)) std::printf("Speech from the CD: %s\n", cd.where().c_str());
  else if (!originalDir.empty()) std::printf("The original's CD not found: the briefings are not spoken (set [data] cd)\n");
  // Says it (a name of SPEECH/*.RAW); "" stops it.
  const auto speak = [&](const std::string& name) {
    sound.stopSpeech();
    if (name.empty() || !cfg.getBool("audio", "speech", true)) return;
    std::vector<uint8_t> raw;
    const std::string own = ownDataDir.empty() ? "" : gamedata::findOriginalFile(ownDataDir, "speech/" + name + ".RAW");
    if (!own.empty()) {
      std::ifstream in(own, std::ios::binary);
      raw.assign(std::istreambuf_iterator<char>(in), {});
    }
    if (raw.empty() && !cd.read("SPEECH/" + name + ".RAW", raw)) return;
    sound.speak(raw);
  };
  if (!offscreen && cfg.getBool("audio", "enabled", true))
    sound.init(originalDir, cfg.getFloat("audio", "volume", 0.8f), cfg.getFloat("audio", "voices", 1.0f));
  // The game's menus come first (not for the development options that go straight into the
  // flying); flying a mission ends back in them.
  menu::FrontEnd front;
  const bool useMenu = !originalDir.empty() && (!menuPage.empty() || (!direct && screenshotPath.empty() && modelSheetPath.empty()));
  const bool menuReady = useMenu && front.init(originalDir);
  if (menuReady) front.setGunnerHints(cfg.getBool("hud", "gunner_hints", true));
  if (menuReady) front.setUkraineInterceptors(cfg.getString("weapons", "quick_ukraine", "r60") == "interceptor");
  // The Pilot's Log, beside hind.ini.
  sim::PilotLog pilotLog;
  const std::string pilotLogPath = (std::filesystem::path(configPath).parent_path() / "pilotlog.txt").string();
  // The Top Ten of Quickstart Arcade, beside it.
  sim::TopTen topTen;
  const std::string topTenPath = (std::filesystem::path(configPath).parent_path() / "topten.txt").string();
  topTen.load(topTenPath);
  if (!pilotLog.load(pilotLogPath)) std::fprintf(stderr, "%s: damaged lines left out\n", pilotLogPath.c_str());
  if (menuReady) front.setLog(&pilotLog, pilotLogPath);
  front.setTopTen(&topTen, topTenPath);
  front.setOwnData(ownDataDir);
  // The Ukrainian campaign's picture: our Hind at dusk over the war's steppe, as this game draws
  // it (the original's model when there), made once by the game itself off screen and kept
  // beside hind.ini.
  const std::string ukrainePicPath = (std::filesystem::path(configPath).parent_path() / "C_UKRAINE.PPM").string();
  front.setUkrainePicture([&, tried = false]() mutable -> std::string {
    if (!std::filesystem::exists(ukrainePicPath) && !tried) {
      tried = true;
      std::string exe = "hind";
      if (char* base = SDL_GetBasePath()) exe = std::string(base) + "hind", SDL_free(base);
#ifdef _WIN32
      exe += ".exe";
#endif
      runSelf(exe, {"--config", configPath, "--original", originalDir, "--mission", "UKRAIC02", "--hour", "20.0",
                    "--width", "1280", "--height", "536", "--start-air", "70", "170", "--start-bank", "20",
                    "--start-pitch", "-8", "--view", "external", "--orbit", "13,215,-7", "--clean-shot",
                    "--screenshot", ukrainePicPath});
    }
    return std::filesystem::exists(ukrainePicPath) ? ukrainePicPath : std::string();
  });
  front.setVoice([&sound](int id) { sound.announce(id); });
  front.setSpeech(speak);
  if (cd.isOpen()) front.setCd([&](const std::string& path, std::vector<uint8_t>& out) { return cd.read(path, out); });
  if (menuReady && topTenScore > 0) front.enterTopTen(topTenScore);
  if (menuReady && !menuPage.empty() && !front.openByName(menuPage)) std::fprintf(stderr, "unknown menu page %s\n", menuPage.c_str());
  // The menus' own loop; false: quit the game.
  // What the menus chose goes into the configuration of this run only (and not for good: the
  // next choice starts again from the configuration as it was, keeping the campaigns' progress).
  platform::Config baseCfg = cfg;
  // The preferences page changes it (and hind.ini); the window and the sound follow at once.
  front.setConfig(&baseCfg, configPath, [&](const std::string& section, const std::string& key) {
    if (section == "video" && key == "fullscreen") {
      fullscreen = baseCfg.getBool("video", "fullscreen", false);
      SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    if (section == "audio") {
      const bool on = baseCfg.getBool("audio", "enabled", true);
      const float volume = baseCfg.getFloat("audio", "volume", 0.8f), voices = baseCfg.getFloat("audio", "voices", 1.0f);
      if (on && !sound.ready() && !offscreen) sound.init(originalDir, volume, voices);
      sound.setVolumes(on ? volume : 0.0f, voices);
    }
  });
  // The music from the CD (SOUND/*.RAW): in the menus the theme over and over, the medal's tune on the
  // awards' page; the debriefing's by the result; "" stops it. Not in the flight.
  std::string musicNow;
  const auto menuMusic = [&](std::string name) {
    if (!baseCfg.getBool("audio", "music", true) || !baseCfg.getBool("audio", "enabled", true)) name.clear();
    if (name == musicNow) return;
    musicNow = name;
    sound.stopMusic();
    std::vector<uint8_t> raw;
    if (!name.empty() && cd.read("SOUND/" + name + ".RAW", raw)) sound.music(raw, name == "THEME");
  };
  bool infiniteWeapons = false, wsoAllies = false, skipPlanning = false, quitAll = false;
  bool huntInterceptors = false;  // Quickstart over Ukraine with the interceptor rack (the menu's toggle)
  bool arcadeRun = false;  // Quickstart Arcade: the flight scores for the Top Ten
  bool quickRun = false;   // Quickstart (Arcade or Invincible): straight into the fight
  // The network game: the pilot's game listens for the gunner's, which shows the pilot's flight.
  bool joining = !joinAddress.empty();
  bool joinedFromMenu = false;  // the menus' Network page joined: leaving goes back to them
  bool leaveGame = false;       // he quit the flight of the game he joined from the menus
  const int netPort = cfg.getInt("net", "port", net::kDefaultPort);
  net::Listener listener;
  net::Link link;
  int hostPort = netPort;  // where this game listens (the next free port when another game has it)
  // The names the network game goes by: the player's (the pilot of the log unless he is Comrade
  // Immortal) and the game's, remembered in hind.ini.
  std::string netName = cfg.getString("net", "name", ""), netGame = cfg.getString("net", "game", "");
  if (netName.empty()) netName = pilotLog.pilot().immortal ? "Pilot" : pilotLog.pilot().name;
  if (netGame.empty()) netGame = netName + "'s game";
  std::string netNameUpper = netName;
  for (char& c : netNameUpper) c = char(std::toupper(static_cast<unsigned char>(c)));
  front.setNetNames(netName, netGame);
  // The other players: the hosted game's, or the crew of our Hind when we fly a wingman's.
  std::vector<std::unique_ptr<Peer>> peers;
  bool crewHost = false;  // a wingman's game taking a crew of its own (listening)
  std::string joinGame;   // the game joined (a wingman's adverts say it)
  net::Beacon beacon;
  net::Finder finder;
  double advertAt = -10.0;
  const auto startHosting = [&]() {
    std::string error;
    for (hostPort = netPort; hostPort < netPort + 10; ++hostPort)
      if (listener.listen(hostPort, error)) break;
    if (!listener.open()) {
      std::fprintf(stderr, "Network game: cannot listen on %s\n", error.c_str());
      return false;
    }
    std::printf("Network game \"%s\": waiting for the other players on port %d\n", netGame.c_str(), hostPort);
    hosting = true;
    advertAt = -10.0;
    return true;
  };
  // A wingman's game takes a crew too: it listens, and says so on the network.
  const auto startCrewHost = [&]() {
    std::string error;
    for (hostPort = netPort; hostPort < netPort + 10; ++hostPort)
      if (listener.listen(hostPort, error)) break;
    crewHost = listener.open();
    advertAt = -10.0;
    if (crewHost) std::printf("Network game: our Hind takes a crew on port %d\n", hostPort);
  };
  const auto closePeers = [&]() {
    peers.clear();
    listener.close();
    crewHost = false;
  };
  if (hosting && !startHosting()) hosting = false;
  // Our game said on the network every second: the game, its pilot, who is in it (the host's,
  // or a wingman's taking a crew).
  const auto announce = [&]() {
    const double now = double(SDL_GetTicks64()) / 1000.0;
    if ((!hosting && !crewHost) || now - advertAt < 1.0) return;
    advertAt = now;
    net::Advert a;
    a.game = hosting ? netGame : joinGame, a.pilot = netName, a.host = hosting, a.port = hostPort;
    if (!hosting) {
      const size_t colon = joinAddress.rfind(':');
      a.hostAddress = colon == std::string::npos ? joinAddress : joinAddress.substr(0, colon);
    }
    for (const auto& p : peers) {
      if (!p->link.open() || p->name.empty() || p->bowman()) continue;
      if (p->wingman()) a.wingmen.push_back(p->name);
      else a.crew[p->view == "door-left" ? 1 : p->view == "door-right" ? 2 : 0] = p->name;
    }
    beacon.announce(a);
  };
  const auto hostStatus = [&]() {
    std::string s = "Hosting \"" + netGame + "\"";
    std::string who;
    for (const auto& p : peers)
      if (p->link.open() && !p->name.empty())
        who += (who.empty() ? " - " : ", ") + p->name +
               (p->wingman() ? " (Hind)" : p->bowman() ? " (on foot)" : p->view == "door-left" ? " (left door)" : p->view == "door-right" ? " (right door)" : " (gunner)");
    return s + (who.empty() ? " - nobody joined yet" : who);
  };
  // A Hello: who he is, where he sits. A wingman gets a Hind of his own, if one is left.
  const auto readHello = [&](Peer& p, net::Reader& r) {
    std::string name, view;
    r.str(name), r.str(view);
    if (!r.ok()) return false;
    const bool changed = name != p.name || view != p.view;
    const int slotWas = p.slot;
    p.name = name, p.view = view;
    if (p.wingman() && p.slot < 0) {
      for (int k = 0; k < kMaxWingmen && p.slot < 0; ++k)
        if (std::none_of(peers.begin(), peers.end(), [&](const auto& o) { return o->slot == k; })) p.slot = k;
    }
    if (!p.wingman()) p.slot = -1;
    if (p.slot != slotWas) p.startSent = false;  // his flight set up again, in his place
    if (changed) std::printf("Network game: %s is in, seat %s%s\n", name.c_str(), view.c_str(), p.slot >= 0 ? (" " + std::to_string(p.slot + 1)).c_str() : "");
    advertAt = -10.0;
    return true;
  };
  const auto sendLines = [](net::Link& l, const std::vector<std::string>& lines) {
    net::Writer w;
    w.pod(uint32_t(lines.size()));
    for (const std::string& s : lines) w.str(s);
    l.send(net::Wait, w.bytes);
  };
  // Outside the flights: newcomers let in and told to wait (`lines`), Hellos read, the gone dropped.
  const auto serveWaiting = [&](const std::vector<std::string>& lines) {
    if (!hosting && !crewHost) return;
    announce();
    if (peers.size() < size_t(kMaxPeers)) {
      auto p = std::make_unique<Peer>();
      if (listener.accept(p->link)) {
        std::printf("Network game: a player joined from %s\n", p->link.peer().c_str());
        sendLines(p->link, lines);
        p->waitSent = true;
        peers.push_back(std::move(p));
      }
    }
    for (auto& p : peers) {
      uint8_t type;
      std::vector<uint8_t> bytes;
      while (p->link.receive(type, bytes)) {
        net::Reader r(bytes);
        if (type == net::Hello) readHello(*p, r);
      }
      p->link.flush();
    }
    const size_t was = peers.size();
    std::erase_if(peers, [](const auto& p) { return !p->link.open(); });
    if (peers.size() != was) advertAt = -10.0;
  };
  const auto rememberNames = [&]() {
    netName = front.netChoice().name, netGame = front.netChoice().game;
    platform::Config saved;
    saved.load(configPath);
    saved.applyDefaults();
    saved.set("net", "name", netName);
    saved.set("net", "game", netGame);
    saved.save(configPath);
    baseCfg.set("net", "name", netName), baseCfg.set("net", "game", netGame);
  };
  bool menuJoin = false;  // the menus' Network page chose a game to join
  // The hosted game while its pilot is in the menus: the others let in, told to wait.
  const auto serveInMenus = [&]() { serveWaiting({netGame, "THE PILOT IS IN THE GAME'S MENUS."}); };
  auto runMenu = [&]() {
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    menuJoin = false;
    const Uint64 menuStart = SDL_GetTicks64();
    if (!finder.isOpen()) {
      std::string error;
      if (!finder.open(error)) std::fprintf(stderr, "Network game: cannot hear the games on the network (%s)\n", error.c_str());
    }
    std::vector<SDL_Event> script;  // --menu-keys, after the first frame
    for (int frames = 0;; ++frames) {
      menu::FrontResult r = menu::FrontResult::None;
      bool scriptWaits = false;
      for (size_t k = 0; k < script.size() && r == menu::FrontResult::None; ++k) {
        if (script[k].type == SDL_USEREVENT) {
          const SDL_UserEvent& u = script[k].user;
          const int d = int(reinterpret_cast<intptr_t>(u.data1)) / 256, a = int(reinterpret_cast<intptr_t>(u.data1)) % 256;
          const int v = int(reinterpret_cast<intptr_t>(u.data2));
          if (u.code == 3) {
            script.erase(script.begin(), script.begin() + long(k) + 1);
            scriptWaits = true;
            break;
          }
          if (d >= 0 && d < int(virtualJoys.size()) && u.code == 1) SDL_JoystickSetVirtualAxis(virtualJoys[size_t(d)], a, Sint16(std::clamp(v, -32768, 32767)));
          if (d >= 0 && d < int(virtualJoys.size()) && u.code == 2) SDL_JoystickSetVirtualHat(virtualJoys[size_t(d)], a, Uint8(v));
          continue;
        }
        r = front.handleEvent(script[k]);
        if (r != menu::FrontResult::None) script.erase(script.begin(), script.begin() + long(k) + 1);
      }
      if (r == menu::FrontResult::None && !scriptWaits) script.clear();
      SDL_Event e;
      while (r == menu::FrontResult::None && SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        r = front.handleEvent(e);
      }
      const Uint64 now = SDL_GetPerformanceCounter();
      const float dt = shotFbo ? 1.0f / 60.0f : float(std::min((now - last) / freq, 0.1));
      last = now;
      if (r == menu::FrontResult::None) r = front.update(dt);
      // (The animations without it, as the original's.)
      const menu::Page shown = front.page();
      menuMusic(r != menu::FrontResult::None || shown == menu::Page::Animation ? "" : shown == menu::Page::Award ? "MEDAL" : "THEME");
      sound.duckMusic(sound.speaking());
      // The network game: hosting one, closing it, joining one.
      if (r == menu::FrontResult::Host) {
        rememberNames();
        if (startHosting()) front.note("Now choose the mission to fly"), front.open(menu::Page::Flight);
        else front.note("Cannot host: the port is taken");
        r = menu::FrontResult::None;
      } else if (r == menu::FrontResult::StopHost) {
        closePeers();
        hosting = false;
        r = menu::FrontResult::None;
      } else if (r == menu::FrontResult::Join) {
        rememberNames();
        menuJoin = true;
        return true;
      }
      if (r != menu::FrontResult::None) return r == menu::FrontResult::Fly;
      serveInMenus();
      finder.update(double(SDL_GetTicks64()) / 1000.0);
      front.setNetGames(finder.games());
      front.setHosting(hosting, hostStatus());
      int dw, dh, winW, winH;
      SDL_GL_GetDrawableSize(window, &dw, &dh);
      SDL_GetWindowSize(window, &winW, &winH);
      if (shotFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, shotFbo);
        dw = ww, dh = wh;
      }
      front.setMouseScale(shotFbo || winW <= 0 ? glm::vec2(1.0f) : glm::vec2(float(dw) / float(winW), float(dh) / float(winH)));
      front.draw(dw, dh);
      if (frames == 0 && !menuKeys.empty()) script = scriptedEvents(menuKeys, [&](glm::vec2 p) { return front.windowPoint(p); });
      // (The network page's shot once the games on the network have been heard.)
      if (shotFbo && frames >= 30 && (front.page() != menu::Page::Network || SDL_GetTicks64() - menuStart > 1500)) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, shotFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shotResolve);
        glBlitFramebuffer(0, 0, dw, dh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, shotResolve);
        saveScreenshot(screenshotPath, dw, dh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
      }
      if (!shotFbo) SDL_GL_SwapWindow(window);
    }
  };
  net::StartInfo joined;            // the gunner's: the flight as the pilot's game started it
  std::string pilotGone;            // the gunner's: why the last flight ended under him
  if (joining && startView == "pilot") startView = "gunner";  // he comes to fight
  const auto seatName = [](const std::string& view) {
    return view == "door-left" ? "LEFT DOOR GUN" : view == "door-right" ? "RIGHT DOOR GUN" : view == "wingman" ? "PILOT OF THE WINGMAN'S HIND" : view == "bowman" ? "ON FOOT, WITH A BOW" : "GUNNER'S COCKPIT";
  };
  bool joinedPending = false;       // a start for another mission came during the flight
  render::Hud waitHud;
  bool waitHudReady = false;
  // The gunner's game until the pilot's starts a flight; false: quit.
  auto waitForPilot = [&]() {
    if (joinedPending) {
      joinedPending = false;
      return true;
    }
    if (!waitHudReady) waitHudReady = waitHud.init();
    std::vector<std::string> lines;
    double unanswered = 0.0;  // s connecting without the pilot's game answering
    const auto waiting = [&]() {
      std::vector<std::string> l;
      l.push_back("NETWORK GAME");
      if (!pilotGone.empty()) l.push_back(pilotGone);
      l.push_back("WAITING FOR THE PILOT'S GAME AT " + joinAddress + ".");
      if (unanswered > 8.0)
        l.push_back("NO ANSWER: A FIREWALL ON THE PILOT'S COMPUTER MAY BLOCK TCP " + std::to_string(netPort) + "-" + std::to_string(netPort + 9) + ".");
      l.push_back("ESC: QUIT.");
      return l;
    };
    lines = waiting();
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    double retry = 0.0, trying = 0.0;
    bool helloSent = false;
    const auto hello = [&]() {
      net::Writer w;
      w.str(front.netChoice().name.empty() ? netName : front.netChoice().name);
      w.str(startView);
      link.send(net::Hello, w.bytes);
    };
    for (int frames = 0;; ++frames) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) return false;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        // Where he sits when the flight starts (F3 and F4 change it in the flight).
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
          const SDL_Scancode k = e.key.keysym.scancode;
          if (k == SDL_SCANCODE_1 || k == SDL_SCANCODE_KP_1) startView = "gunner";
          if (k == SDL_SCANCODE_2 || k == SDL_SCANCODE_KP_2) startView = "door-left";
          if (k == SDL_SCANCODE_3 || k == SDL_SCANCODE_KP_3) startView = "door-right";
          if (k == SDL_SCANCODE_4 || k == SDL_SCANCODE_KP_4) startView = "wingman";
          if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_4) helloSent = false;
          if (k == SDL_SCANCODE_R && (e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_SHIFT)) startView = "bowman", helloSent = false;
        }
      }
      const Uint64 now = SDL_GetPerformanceCounter();
      const double dt = std::min((now - last) / freq, 0.1);
      last = now;
      // A wingman takes a crew of his own; they wait with him.
      if (startView == "wingman" && !crewHost) startCrewHost();
      if (startView != "wingman" && crewHost) closePeers();
      serveWaiting({"YOUR PILOT WAITS FOR THE GAME'S HOST."});
      // Connecting, again every half a second until the pilot's game answers.
      if (!link.open() && (retry -= dt) <= 0.0) {
        retry = 0.5, trying = 0.0;
        link.connect(joinAddress, netPort);
      }
      if (link.open() && !link.connected() && (trying += dt) > 3.0) link.close();
      unanswered = link.connected() ? 0.0 : unanswered + dt;
      if (!link.open()) helloSent = false;
      else if (!helloSent && link.connected()) hello(), helloSent = true;
      uint8_t type;
      std::vector<uint8_t> bytes;
      while (link.receive(type, bytes)) {
        net::Reader r(bytes);
        if (type == net::Start && joined.read(r)) {
          pilotGone.clear();
          if (!joined.game.empty()) joinGame = joined.game;
          std::printf("Network game: the pilot starts %s\n", joined.mission.empty() ? "a free flight" : joined.mission.c_str());
          return true;
        }
        if (type == net::Wait) {
          lines.clear();
          const uint32_t n = r.get<uint32_t>();
          for (uint32_t i = 0; i < n && r.ok(); ++i) r.str(lines.emplace_back());
        }
      }
      if ((!link.open() && lines.size() > 1 && lines[1].rfind("WAITING", 0) != 0) || unanswered > 8.0) lines = waiting();
      int dw, dh;
      SDL_GL_GetDrawableSize(window, &dw, &dh);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, dw, dh);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      if (waitHudReady) {
        render::HudData hd;
        hd.cockpit = true;
        hd.page = lines;
        hd.page.push_back("");
        hd.page.push_back(std::string("YOUR SEAT: ") + seatName(startView) + ".");
        hd.page.push_back("1: GUNNER'S COCKPIT   2: LEFT DOOR   3: RIGHT DOOR   4: A HIND OF YOUR OWN");
        waitHud.draw(hd, dw, dh);
      }
      SDL_GL_SwapWindow(window);
      if (frames > 0 && !SDL_GL_GetSwapInterval()) SDL_Delay(15);
    }
  };
  for (bool again = true; again;) {
  again = false;
  nightVision = nightVisionAtStart;
  if (menuReady && !joining) {
    if (!runMenu()) break;
    if (menuJoin) {
      // Into another's game, in the seat chosen.
      joining = joinedFromMenu = true;
      joinAddress = front.netChoice().address;
      joinGame = front.netChoice().joinGame;
      startView = menu::netSeatView(front.netChoice().seat);
      pilotGone.clear();
      link.close();
    }
  }
  arcadeRun = false, quickRun = quickDev;
  if (menuReady && !joining) {
    cfg = baseCfg;
    const menu::Choice& c = front.choice();
    cfg.set("world", "zone", c.zone);
    cfg.set("world", "mission", c.mission);
    if (c.arcade) cfg.set("flight", "model", "novice");
    if (c.noCrash) cfg.set("flight", "no_crash", "1");
    infiniteWeapons = c.infiniteWeapons;
    arcadeRun = c.arcade && !c.noCrash;
    quickRun = c.arcade;
    huntInterceptors = c.ukraineInterceptors;
    wsoAllies = c.wsoAllies;
    skipPlanning = !c.plan;
    if (c.gunnerHints != cfg.getBool("hud", "gunner_hints", true)) {
      // Remembered for the next time.
      const std::string v = c.gunnerHints ? "1" : "0";
      baseCfg.set("hud", "gunner_hints", v);
      cfg.set("hud", "gunner_hints", v);
      platform::Config saved;
      saved.load(configPath);
      saved.applyDefaults();
      saved.set("hud", "gunner_hints", v);
      saved.save(configPath);
    }
  }
  if (joining) {
    if (!waitForPilot()) {
      if (!joinedFromMenu) break;
      // Back to the menus.
      link.close();
      closePeers();
      joining = joinedFromMenu = false;
      again = true;
      continue;
    }
    cfg = baseCfg;
    cfg.set("world", "zone", joined.zone);
    cfg.set("world", "mission", joined.mission.empty() ? "none" : joined.mission);
    cfg.set("world", "hour", std::to_string(joined.hour));
    cfg.set("world", "weather", std::to_string(joined.weather));
    skipPlanning = true;
  }
  // The second player: in the pilot's Hind (its gunner, the game a mirror of the pilot's), or the
  // pilot of a Hind of his own, the wingman's.
  const bool wingmanGame = joining && startView == "wingman";
  const bool mirrored = joining && !wingmanGame;
  // A crewman joined in the pilot's Hind keeps the seat he took (the gunner's sight or his door):
  // the views from outside, yes, another seat no.
  render::ViewMode crewSeat = render::ViewMode::Gunner;
  int crewDoor = 0;
  // The lone bowman's: a mirror of the host's game too, seen through his eyes on the ground.
  // (--view bowman without --join: his alone, by our Hind on the ground; development.)
  const bool bowmanGame = startView == "bowman";
  // Seen from outside, the camera round him (F6, F2 back to his eyes; from the start with --orbit).
  bool bowOrbit = bowmanGame && orbit.x > 0.0f;
  // F7: the camera round his arrow in the air, a moment on where it went off, then back.
  bool bowArrow = false;
  double bowArrowGone = -1.0;
  glm::vec3 bowArrowAt{0.0f}, bowArrowDir{0.0f, 0.0f, -1.0f};
  {
  const bool gunnerHints = cfg.getBool("hud", "gunner_hints", true);  // the door gunners' shot on the HUD
  // A mission of the original game decides the war zone, and the landscape is laid out around
  // the airfield it starts from.
  gamedata::Mission mission;
  bool haveMission = pickMission(cfg, originalDir, mission);
  if (haveMission && forceHour >= 0.0f) mission.startHour = forceHour;
  // Quickstart over Ukraine: the raid by day (the nights are the campaign's).
  const bool quickHunt = quickRun && haveMission && mission.name.rfind("UKRAIC", 0) == 0;
  if (quickHunt && (mission.startHour < 8.0f || mission.startHour > 17.0f)) mission.startHour = 11.0f;
  for (const ZoneInfo& z : kZones)
    if (haveMission && z.number == mission.zone) cfg.set("world", "zone", z.key);
  if (haveMission && !zoneInfo(cfg.getString("world", "zone", ""))) haveMission = false;
  std::vector<gamedata::Formation> formations;
  if (haveMission) {
    std::string error;
    if (!gamedata::loadFormations(originalDir, formations, error)) std::fprintf(stderr, "%s\n", error.c_str());
    std::printf("Mission %s: %s\n", mission.name.c_str(), mission.title.c_str());
  }
  glm::vec2 zoneOrigin{0.0f};
  const glm::vec2 missionStart = haveMission ? mission.player()->route[0].pos : glm::vec2(0.0f);
  // The vehicles are modelled after the original game when the player has it installed.
  gamedata::OriginalData original;
  if (originalDir.empty())
    std::fprintf(stderr, "Original Hind not found (set [data] original_dir in %s), vehicles are boxes\n",
                 configPath.c_str());
  else if (!original.open(originalDir))
    std::fprintf(stderr, "%s\n", original.error().c_str());
  else
    std::printf("Original game data: %s\n", originalDir.c_str());

  // The zone's buildings, bridges and copses (and its airfields' runways) unless turned off.
  const bool showScenery = cfg.getBool("world", "scenery", true);
  std::vector<gamedata::ZoneObject> scenery;
  world::Terrain terrain = makeTerrain(cfg.getString("world", "zone", "afghan"), originalDir,
                                       haveMission ? &missionStart : nullptr, zoneOrigin, scenery,
                                       showScenery ? &original : nullptr);
  const std::vector<gamedata::ZoneObject> zoneObjects = scenery;  // what the waypoints are
  if (!showScenery) scenery.clear();
  const auto& af = terrain.airfield();
  // The runways' lights at night: white along the edges every 30 m, a green row across each end.
  std::vector<glm::vec4> runwayLights;
  {
    std::vector<world::Runway> rws = af.runways;
    if (rws.empty())
      rws.push_back({af.center - glm::vec2(0.0f, af.runwayLength * 0.5f), af.center + glm::vec2(0.0f, af.runwayLength * 0.5f), af.runwayWidth});
    for (const world::Runway& r : rws) {
      const float len = glm::distance(r.a, r.b);
      if (len < 100.0f) continue;
      const glm::vec2 along = (r.b - r.a) / len, across{-along.y, along.x};
      auto light = [&](glm::vec2 p, float kind) { runwayLights.push_back({p.x, terrain.groundHeight(p.x, p.y) + 0.4f, p.y, kind}); };
      const int n = int(len / 30.0f);
      for (int k = 0; k <= n; ++k)
        for (float side : {-0.5f, 0.5f}) light(r.a + along * (len * float(k) / float(n)) + across * (r.width * side + side * 2.0f), 0.0f);
      for (const glm::vec2 end : {r.a - along * 3.0f, r.b + along * 3.0f})
        for (float x = -0.5f; x <= 0.5f; x += 0.1f) light(end + across * (r.width * x), 1.0f);
    }
  }
  // Where the player starts: on the apron, or where the mission says when that is away from the
  // airfield (or on a zone's airfield, which has no apron), facing the first waypoint.
  glm::vec2 startPad = customStart ? startPos : af.runways.empty() ? glm::vec2{af.apronCenter.x - 40.0f, 60.0f} : glm::vec2(0.0f);
  auto missionStartPad = [&](const gamedata::Mission& m) {
    const auto& r = m.player()->route;
    glm::vec2 a{r[0].pos.x - zoneOrigin.x, zoneOrigin.y - r[0].pos.y}, b{r[1].pos.x - zoneOrigin.x, zoneOrigin.y - r[1].pos.y};
    if (!customStart) {
      startPad = glm::length(a) < 2500.0f && af.runways.empty() ? glm::vec2{af.apronCenter.x - 40.0f, 60.0f} : a;
      startHeading = std::atan2(b.x - startPad.x, -(b.y - startPad.y));
    }
  };
  if (haveMission) missionStartPad(mission);

  sim::Helicopter heli(terrain);
  if (!sim::parseFlightModel(cfg.getString("flight", "model", "stable"), heli.model))
    std::fprintf(stderr, "unknown flight model, using stable\n");
  heli.noCrash = cfg.getBool("flight", "no_crash", false);
  heli.resetOnGround(startPad, startHeading);
  if (startAlt >= 0.0f) {
    glm::vec3 p{startPad.x, terrain.groundHeight(startPad.x, startPad.y) + startAlt, startPad.y};
    heli.resetAirborne(p, startHeading, startKmh / 3.6f);
    heli.orient = heli.orient * glm::angleAxis(glm::radians(startPitch), glm::vec3(1, 0, 0)) *
                  glm::angleAxis(-glm::radians(startBank), glm::vec3(0, 0, -1));
    heli.gearDown = false;
    heli.gearPos = 0.0f;
  }
  // The autorotation drill starts in the air over the base, in a model with a free rotor.
  bool noviceBeforeDrill = false;
  const auto drillStart = [&]() {
    if (!haveMission || mission.autorotation <= 0) {
      if (noviceBeforeDrill) heli.model = sim::FlightModel::Novice;
      noviceBeforeDrill = false;
      return;
    }
    if (heli.model == sim::FlightModel::Novice) noviceBeforeDrill = true, heli.model = sim::FlightModel::Stable;
    if (startAlt >= 0.0f) return;
    heli.resetAirborne({startPad.x, terrain.groundHeight(startPad.x, startPad.y) + float(mission.autorotation), startPad.y}, startHeading, 200.0f / 3.6f);
    heli.gearDown = false, heli.gearPos = 0.0f;
  };
  drillStart();

  // The stores: from hind.ini (or the mission's), then as the payload screen sets them.
  sim::Loadout loadout;
  for (int p = 0; p < sim::kPylons; ++p) {
    static const char* keys[sim::kPylons] = {"inner_pylons", "outer_pylons", "wingtip_pylons"};
    const std::string v = cfg.getString("weapons", keys[p], p == 2 ? "9m114" : "b8v20");
    sim::Store st;
    std::string why;
    if (!sim::parseStore(v, st)) std::fprintf(stderr, "hind.ini: unknown store '%s' for %s\n", v.c_str(), keys[p]);
    else if (!sim::storeFits(st, sim::Pylon(p), &why)) std::fprintf(stderr, "hind.ini: %s\n", why.c_str());
    else loadout.at(sim::Pylon(p)) = st;
  }
  if (loadout.outerTaken() && loadout.outer != sim::Store::None)
    std::fprintf(stderr, "hind.ini: the interceptor rack takes the outer pylons too, nothing on them\n");
  loadout.settle();
  if (const std::string v = cfg.getString("weapons", "door_guns", "none"); !sim::parseDoorGuns(v, loadout))
    std::fprintf(stderr, "hind.ini: door_guns is none, left, right or both, not '%s'\n", v.c_str());
  // Quickstart over Ukraine: R-60s on the outer pylons (for the Banderols in the stream; reloaded
  // in the air, Weapons::r60Reloads), or the interceptor drones' rack as the Quickstart page's
  // toggle has it; and both door guns.
  const auto huntLoadout = [&](sim::Loadout& l) {
    if (!quickHunt) return;
    if (huntInterceptors) {
      l.inner = sim::Store::Interceptor;
    } else {
      if (l.inner == sim::Store::Interceptor) l.inner = sim::Store::None;
      l.outer = sim::Store::R60;
    }
    l.doorGun[0] = l.doorGun[1] = true;
    l.settle();
  };
  // A mission loads its own (the payload screen can change them).
  if (haveMission) sim::missionLoadout(mission, loadout), huntLoadout(loadout);
  // --fire: the gun, or a store fitted on the inner pylons for the purpose.
  sim::Store fireStore = sim::Store::None;
  if (autoFire == "upk") fireStore = sim::Store::Upk23;
  else if (autoFire == "s5") fireStore = sim::Store::UB32;
  else if (autoFire == "9m114") fireStore = sim::Store::Shturm;
  else if (autoFire != "s8" && autoFire != "yakb") sim::parseStore(autoFire, fireStore);
  if (fireStore == sim::Store::Shturm) loadout.wingtip = fireStore;
  else if (fireStore == sim::Store::R60) loadout.outer = fireStore;
  else if (fireStore != sim::Store::None) loadout.inner = fireStore;
  sim::Weapons weapons(terrain);
  weapons.load(loadout);
  weapons.infiniteAmmo = infiniteWeapons;
  weapons.doorReloads = arcadeRun;
  weapons.r60Reloads = quickHunt;
  if (!autoFire.empty()) {
    sim::WeaponType want = fireStore != sim::Store::None ? sim::storeInfo(fireStore).weapon
                           : autoFire == "s8" ? sim::WeaponType::S8 : sim::WeaponType::YakB;
    weapons.select(want);
  }
  std::vector<sim::Impact> impacts;
  render::Effects effects;
  sim::Targets targets(terrain);
  sim::MissionState missionState;
  sim::Troops troops;
  sim::Debris debris;  // our helicopter in pieces after a crash
  // The game's other Hinds come down the same way (each game breaks them up itself): their wrecks,
  // and which of them we have seen come down.
  std::vector<sim::Debris> wrecks;
  std::map<const sim::Helicopter*, bool> othersDown;
  // The battlefield's vehicles and aircraft in pieces.
  struct UnitWreck {
    sim::Debris debris;
    sim::VehicleKind kind;
  };
  std::vector<UnitWreck> unitWrecks;
  bool wasCrashed = false;
  // The mission's forces, or the exercise area's vehicles; the troops riding in the helicopters
  // and those waiting for them, as the mission has them.
  auto deploy = [&]() {
    troops.reset();
    if (!haveMission) {
      targets.clear(), targets.placeExercise();
      sim::addScenery(scenery, original, zoneOrigin, targets);
      return;
    }
    missionState.start(mission, formations, original, zoneOrigin, targets, &scenery);
    troops.start(missionState.passengers(), missionState.carriers(), mission.humansDeployed, mission.humansLoaded,
                 missionState.route().empty() ? glm::vec2(0.0f) : missionState.route()[0], targets);
  };
  deploy();
  sim::Wso wso(terrain);
  wso.allies = wsoAllies;
  // The enemy shoots back in the missions (the exercise area is a range).
  sim::EnemyFire enemy(terrain);
  enemy.enabled = haveMission && cfg.getBool("enemy", "fire", true);
  enemy.skill = std::clamp(cfg.getFloat("enemy", "skill", 1.0f), 0.2f, 3.0f);
  // And fights our units there.
  sim::GroundBattle battle(terrain);
  battle.enabled = enemy.enabled;
  sim::AirCombat airCombat(terrain);  // the enemy's fighters and gunships come for us
  airCombat.enabled = enemy.enabled;
  // The Mi-17 of a drone-hunting flight calls the drones out.
  sim::Spotter spotter(terrain);
  // The player's orders to our other helicopters (Ctrl+1..4 the wingman, Shift+1..4 all).
  sim::Orders orders;
  // Over Ukraine the AI Hinds of the flight carry what the player does not (R-60s or interceptors).
  sim::WingArms wingArms(terrain);
  // Our rounds in the air and theirs (for the smoke trails and the drawing).
  // In a network game joined, the AI Hinds' stores and rounds as the pilot's game sends them.
  std::vector<sim::WingArms::Stores> netWingStores;
  std::vector<sim::Projectile> netWingShots;
  const auto wingShots = [&]() { return wingArms.hinds().empty() ? netWingShots : wingArms.shots(); };
  const auto wingStores = [&]() { return wingArms.hinds().empty() ? netWingStores : wingArms.stores(); };
  // The lone bowmen's M60 rounds: his own in his game, the others' tracers as they report them.
  std::vector<sim::Projectile> footShots;
  const auto allShots = [&]() {
    std::vector<sim::Projectile> all = weapons.shots, wing = wingShots();
    all.insert(all.end(), wing.begin(), wing.end());
    all.insert(all.end(), footShots.begin(), footShots.end());
    return all;
  };
  // The network game's other Hind: the wingman's in the pilot's game, the pilot's in the
  // wingman's (with what the snapshots carry of it).
  sim::Helicopter peerHeli(terrain);
  sim::Weapons peerWeapons(terrain);
  sim::Wso peerWso(terrain);
  sim::Troops peerTroops;
  bool peerFlying = false;            // the wingman's: the pilot's Hind is in the snapshots
  // The host's: the wingmen's Hinds; the others': the game's other Hinds as the snapshots say.
  std::vector<std::unique_ptr<Wing>> wings;
  for (int k = 0; k < kMaxWingmen; ++k) wings.push_back(std::make_unique<Wing>(terrain));
  std::vector<std::unique_ptr<sim::Helicopter>> otherHelis;
  std::vector<int8_t> otherHeliIds;  // whose each is (sim::Weapons::OtherHind)
  uint8_t doorsTaken = 0;  // the doors others of our crew are at (net::Frame)
  // Tells the pilot's game who we are and where we sit (again when that changes).
  const auto sayHello = [&]() {
    net::Writer w;
    w.str(front.netChoice().name.empty() ? netName : front.netChoice().name);
    w.str(startView);
    link.send(net::Hello, w.bytes);
  };
  glm::vec2 crewHome = startPad;  // where our Hind takes off (our crew's games set up there)
  bool airStarted = false;         // the joined game's flight started in the air (Quickstart over Ukraine)
  // The players' messages (Ctrl+1..0): the last one heard or sent, on the display under the HUD
  // until then (ms).
  std::string chatLine;
  Uint64 chatUntil = 0;
  const auto showChat = [&](const std::string& who, int number) {
    std::string w = who;
    for (char& c : w) c = char(std::toupper(static_cast<unsigned char>(c)));
    chatLine = w + ": " + net::chatText(number);
    chatUntil = SDL_GetTicks64() + 5000;
    if (who != "YOU") sound.radio(gamedata::snd::TplrFirst + number - 1);
  };
  const auto sendChat = [&](net::Link& l, int number, const std::string& who) {
    net::Writer w;
    w.pod(uint8_t(number)), w.str(who);
    l.send(net::Chat, w.bytes);
  };
  // What the wingman's rounds hit: his game's, for the next report; the pilot's, for the next frame.
  std::vector<sim::Impact> peerHits;
  std::vector<sim::EnemyEvent> enemyEvents;
  glm::vec3 sightEye{0.0f}, sightDir{0.0f, 0.0f, -1.0f};  // last frame's view, for T
  // The other external views: the weapon last launched (where it flies, or came down), the unit
  // watched, where the camera was last frame (the spectator stays there).
  glm::vec3 weaponAt{0.0f}, weaponDir{0.0f, 0.0f, -1.0f}, weaponVel{0.0f}, lastEye{0.0f};
  bool weaponSeen = false, weaponFlying = false;
  double weaponDown = -1e9;  // when the one followed came down
  int watchUnit = -1;
  // The interceptor drone a player at a door flies (G): the camera on it while it flies, then a
  // moment on where it went off before back at the door.
  double droneGoneAt = -1.0, droneLaunchedAt = -1.0;
  int droneSeat = 2;  // where the interceptor in the camera was sent from (its owner: 1 the sight, 2 / 3 a door)
  // Flying a drone the mouse is a stick: moved off centre it stays there, the drone turning on
  // until it is moved back (kDroneStickTravel rad of mouse to full deflection).
  glm::vec2 droneStick{0.0f};
  constexpr float kDroneStickTravel = 0.3f, kDroneStickDead = 0.06f;
  glm::vec3 droneEye{0.0f}, droneDir{0.0f, 0.0f, -1.0f};
  // The enemy fliers the drone's camera marks (within kInterceptorCamera, in its picture), the
  // nearest to the middle first, and the one the player has picked of them (B: the next).
  int droneSel = -1;
  bool droneFireWas = false;
  const auto droneMarked = [&]() {
    std::vector<std::pair<float, int>> seen;
    for (size_t i = 0; i < targets.list.size(); ++i) {
      const sim::Vehicle& v = targets.list[i];
      const float dist = glm::distance(v.pos, droneEye);
      if (!sim::Weapons::interceptorTarget(v) || dist > sim::Weapons::kInterceptorCamera || dist < 1.0f) continue;
      if (const float c = glm::dot((v.pos - droneEye) / dist, droneDir); c > 0.75f) seen.push_back({-c, int(i)});
    }
    std::sort(seen.begin(), seen.end());
    std::vector<int> out;
    for (const auto& [c, i] : seen) out.push_back(i);
    return out;
  };
  // The lone bowman's game: him, where his arrows went off (for his next report), what he is
  // told, the route's point he went to last (N).
  sim::Bowman bowman;
  std::vector<glm::vec3> bowmanBursts;
  std::vector<sim::Impact> bowmanHits;  // where his M60's rounds went, for his next report
  render::Casings casings;              // the spent cases his M60 throws out (in his own view)
  int casingShots = 0;                  // his M60's rounds whose cases are out
  float bowBloodSeed = 0.0f;            // the shape of the blood over his eyes (render::shaders::kBloodFS)
  std::string bowMessage;
  Uint64 bowMessageUntil = 0;
  int bowTurns = 0;  // of the warhead being screwed on, begun (each creaks)
  bool screenWanted = false;  // F1: the frame saved before it is shown
  int bowWaypoint = -1;
  for (int i = 0; i < destroyCount && i < int(targets.list.size()); ++i) {
    sim::Vehicle& v = targets.list[size_t(i)];
    targets.destroy(i, sim::Killer::Player);
    effects.impact({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), i, true});
  }


  // --virtual-mount: before Input looks for its devices.
  SDL_Joystick* virtualMountJoy = nullptr;
#if SDL_VERSION_ATLEAST(2, 0, 14)
  if (useVirtualMount) {
    const int at = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 2, 3, 0);
    if (at >= 0) virtualMountJoy = SDL_JoystickOpen(at);
    if (!virtualMountJoy) std::fprintf(stderr, "--virtual-mount: %s\n", SDL_GetError());
  }
#endif
  platform::Input input;
  input.configure(cfg);
  if (startAlt >= 0.0f) {
    float c = sim::Helicopter::levelFlightCollective(startKmh / 3.6f, heli.mass);
    input.setCollective(heli.model == sim::FlightModel::Novice ? 0.5f : c);
  } else if (heli.weightOnWheels <= 0.0f) {
    input.setCollective(0.6f);  // (the autorotation drill: in the air from the first moment)
  }

  render::Program scene;
  render::Sky sky;
  // The stars over the war zone (the Ukrainian missions under Ukraine's), on a midsummer night
  // as long as the original's days are.
  {
    const ZoneInfo* z = zoneInfo(haveMission && mission.name.rfind("UKRAI", 0) == 0 ? "ukraine" : cfg.getString("world", "zone", ""));
    sky.setPlace(z ? z->latitude : 50.0f, 182);
  }
  render::Hud hud;
  render::ScreenPass nightVisionView{"night vision", render::shaders::kNightVisionFS};
  render::ScreenPass hurtView{"bowman hurt", render::shaders::kHurtFS};
  render::ScreenPass bloodView{"bowman blood", render::shaders::kBloodFS};
  bool bowJumpWas = false;  // the jump key, last frame
  render::ScreenPass droneFeed{"drone feed", render::shaders::kDroneFeedFS};
  render::ScreenPass pictureFade{"cockpit fade", render::shaders::kFadeFS};
  if (!scene.build("scene", render::shaders::kSceneVS, render::shaders::kSceneFS) || !sky.init() || !hud.init())
    return 1;
  // The original's object textures (the aircraft's camouflage) for the scene shader.
  {
    int count = original.textureCount();
    const std::vector<uint8_t> atlas = render::withLivery(original.textures(), count);
    render::uploadAtlas(atlas, count);
  }
  // The Ukrainian missions' Hinds in the colours of the Czech Mi-24s Ukraine flies, and so the
  // drone hunters' training (a mission with Shaheds or Gerberas).
  bool ukrainianLivery = haveMission ? mission.name.rfind("UKRAI", 0) == 0 : cfg.getString("world", "zone", "") == "ukraine";
  if (haveMission)
    for (const gamedata::MissionTask& t : mission.tasks)
      for (const gamedata::MissionDrone& d : t.drones)
        if (d.name == "Shahed_136" || d.name == "Gerbera" || d.name == "Geran_3" || d.name == "Banderol") ukrainianLivery = true;
  scene.use();
  glUniform1i(scene.loc("uAtlas"), render::kAtlasUnit);
  if (!modelSheetPath.empty()) {
    if (modelSheetPath.size() > 4 && modelSheetPath.compare(modelSheetPath.size() - 4, 4, ".obj") == 0)
      writeModelObj(modelSheetPath,
                    sheetModel == "shahed" || sheetModel == "geran3" || sheetModel == "litavr" || sheetModel == "banderol" ? sheetModel : "", loadout, sheetModel == "original" ? &original : nullptr);
    else
      renderModelSheet(scene, modelSheetPath, sheetModel, loadout, original);
  }
  render::Palette pal;
  // The time of day and the weather: the mission's, or in free flight as hind.ini sets them.
  const float freeHour = std::clamp(cfg.getFloat("world", "hour", 10.0f), 0.0f, 24.0f);
  const int freeWeather = std::clamp(int(cfg.getFloat("world", "weather", 0.0f)), 0, 4);
  // The overcast sky of the original, from the zenith down to the horizon.
  render::SkyColours skyColours;
  {
    gamedata::PaletteRange ramp;
    std::string error;
    const std::string path = originalDir.empty() ? "" : gamedata::findOriginalFile(originalDir, "3DDATA/OVERCAST.RGB");
    if (!path.empty() && gamedata::loadPalette(path, 0, ramp, error) && ramp.rgb.size() >= 6) {
      const size_t last = ramp.rgb.size() / 3 - 1;
      skyColours.overcastZenith = glm::vec3(ramp.rgb[0], ramp.rgb[1], ramp.rgb[2]) / 255.0f;
      skyColours.overcastHorizon = glm::vec3(ramp.rgb[last * 3], ramp.rgb[last * 3 + 1], ramp.rgb[last * 3 + 2]) / 255.0f;
    }
  }
  float viewDistance = cfg.getFloat("video", "view_distance", 16000.0f);
  render::TerrainRenderer terrainRenderer(terrain);
  terrainRenderer.init();
  terrainRenderer.setViewDistance(viewDistance);
  Meshes meshes;
  meshes.build();
  // Our Mi-24 as the original game had it (our own simple one without its data).
  meshes.buildOriginal(original);
  render::VehicleRenderer vehicles;
  vehicles.build(original);
  render::FigureRenderer figures;
  figures.build(original);
  // Cockpit pictures of the original for each seat while looking roughly level; without them (or
  // looking up, down or behind) the modelled cockpit is shown.
  render::CockpitOverlay pictures[2];
  render::PanelGauges gauges;
  const bool gaugesReady = gauges.init(terrain);
  // The map shows the area of the mission's route.
  auto mapMission = [&]() {
    if (!gaugesReady || missionState.route().empty()) return;
    glm::vec2 lo = missionState.route()[0], hi = lo;
    for (glm::vec2 p : missionState.route()) lo = glm::min(lo, p), hi = glm::max(hi, p);
    gauges.buildMap(terrain, (lo + hi) * 0.5f);
  };
  if (haveMission) mapMission();
  // The game sets the last 32 colours of the cockpit pictures from COCKPITS.RGB.
  gamedata::PaletteRange cockpitPalette;
  {
    std::string error, path = originalDir.empty() ? "" : gamedata::findOriginalFile(originalDir, "COCKPITS/COCKPITS.RGB");
    if (!path.empty() && !gamedata::loadPalette(path, 224, cockpitPalette, error)) std::fprintf(stderr, "%s\n", error.c_str());
  }
  if (!originalDir.empty()) gauges.loadParts(originalDir, cockpitPalette);
  for (int i = 0; i < 2 && !originalDir.empty(); ++i) {
    // Text displays in the views ahead: under the pilot's HUD, at the top of the gunner's sight ring.
    const glm::vec4 display = i == 0 ? glm::vec4(277, 240, 391, 262) : glm::vec4(262, 3, 376, 23);
    std::string path = gamedata::findOriginalFile(originalDir, i == 0 ? "COCKPITS/PILOT.LBM" : "COCKPITS/GUNNER.LBM");
    gamedata::Picture pic;
    std::string error;
    const bool loaded = !path.empty() && gamedata::loadPbm(path, pic, 0, error, cockpitPalette.rgb.empty() ? nullptr : &cockpitPalette);
    // The gunner's picture has a small cross painted in the sight ring, above its middle: off our
    // sight (in the middle of the view), so it goes.
    if (loaded && i == 1 && pic.width >= 979 && pic.height >= 219)
      for (int y = 181; y <= 218; ++y)
        for (int x = 941; x <= 978; ++x) pic.rgba[(size_t(y) * size_t(pic.width) + size_t(x)) * 4 + 3] = 0;
    // Over it the rows we painted on (data/cockpit), seen as the crew member looks up.
    int top = 0;
    gamedata::Picture over;
    const std::string overPath = ownDataDir.empty() ? "" : ownDataDir + (i == 0 ? "/cockpit/pilot-top.pam" : "/cockpit/gunner-top.pam");
    if (loaded && !overPath.empty() && std::filesystem::exists(overPath)) {
      if (gamedata::loadPam(overPath, over, error) && gamedata::stackOver(over, pic)) top = over.height;
      else std::fprintf(stderr, "%s\n", error.empty() ? (overPath + ": not as wide as the cockpit picture").c_str() : error.c_str());
    }
    if (!loaded || !pictures[i].load(pic, display, top))
      std::fprintf(stderr, "%s, using the modelled cockpit\n", path.empty() ? "No cockpit picture" : error.c_str());
  }
  render::Camera camera;
  // The interceptor drone of the seat the camera is at (sim::Projectile::owner: 1 the gunner's
  // sight, 2 the left door, 3 the right), or the one in the drone's camera.
  const auto droneOwner = [&]() {
    return camera.mode == render::ViewMode::Drone ? droneSeat : camera.mode == render::ViewMode::Gunner ? 1 : 2 + camera.doorSide;
  };
  // Back from the drone's camera to where it was sent from.
  const auto droneHome = [&]() { return droneSeat == 1 ? render::ViewMode::Gunner : render::ViewMode::Door; };
  if (startView == "external") camera.mode = render::ViewMode::External;
  // The other external views from the start (development): the weapon's once one is launched.
  if (startView == "weapon") camera.mode = render::ViewMode::Weapon;
  if (startView == "target") camera.mode = render::ViewMode::Target;
  if (startView == "lock") camera.mode = render::ViewMode::Lock;
  if (startView == "watch") camera.mode = render::ViewMode::Watch;
  if (startView == "remote") camera.mode = render::ViewMode::Remote, camera.anchor = heli.pos - heli.forward() * 20.0f + glm::vec3(0.0f, 4.0f, 0.0f);
  if (startView == "gunner") camera.mode = render::ViewMode::Gunner;
  if (bowmanGame) camera.mode = render::ViewMode::Bowman;
  // At a door gun: the one there is (the right one of two), or the one asked for.
  // --view drone (development): at the right door, an interceptor out as soon as we fly.
  bool autoDrone = startView == "drone";
  if (startView.rfind("door", 0) == 0 || autoDrone) {
    camera.mode = render::ViewMode::Door;
    camera.doorSide = startView == "door-left" ? 0 : startView == "door-right" ? 1 : loadout.doorGun[1] || !loadout.doorGun[0];
  }
  camera.setLookYaw(startLook);
  if (startLookPitch != 0.0f) camera.tiltLook(startLookPitch);
  if (orbit.x > 0.0f) camera.setOrbit(orbit.x, glm::radians(orbit.y), glm::radians(orbit.z));
  for (int k = 0; k < sightZoom; ++k) camera.zoom(0.8f);
  const float fovY = glm::radians(cfg.getFloat("video", "fov", 65.0f));
  // The pilot looks left and right, as in the original, as far as its cockpit picture goes, and
  // up as far as the gunner does: the picture slides down, the rows painted over it and then the
  // open sky coming into view (the modelled cockpit is only for when the original's pictures are
  // not there). Picture pixels per unit of the tangent off the view's middle:
  const float picturePerTan = 240.0f / std::tan(fovY * 0.5f);
  if (pictures[0].loaded()) camera.limitSeat(render::ViewMode::Pilot, pictures[0].maxPan() / picturePerTan, glm::radians(60.0f));

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  if (msaa > 0) glEnable(GL_MULTISAMPLE);

  bool running = modelSheetPath.empty(), paused = false, firstFrame = true;
  // P as in the original: the game stops at once and the picture darkens to about half over a
  // second; P again brightens it back first, then the game runs on (unpausing).
  bool keyPause = false, unpausing = false;
  float pauseDim = 0.0f;  // 0 clear .. 1 darkened
  int arcadeScore = 0;  // Quickstart Arcade's: the targets the player destroyed, the flight's result
  std::map<std::string, sim::ArcadeKill> arcadeKills;  // Quickstart's: what the player destroyed, for the debriefing
  Uint64 last = SDL_GetPerformanceCounter();
  const double freq = double(SDL_GetPerformanceFrequency());
  double accumulator = 0.0, simTime = 0.0, fpsTimer = 0.0;
  int fpsFrames = 0, frameCount = 0, totalFrames = 0;
  const Uint64 startCounter = last;
  glm::vec3 prevPos = heli.pos;
  glm::quat prevOrient = heli.orient;
  glm::vec3 lastVel = heli.vel;
  float gLoad = 1.0f;  // smoothed load factor for the g meter

  // The text display keeps the previous message on its top line for a while.
  std::string lastMessage, prevMessage;
  double prevMessageUntil = 0.0;

  std::vector<std::string> page;  // over the game: the debriefing
  // The network game. The pilot's: the start of each flight goes to the gunner's game, then
  // snapshots of it; what happens in between is collected for them.
  net::Mirror mirror;
  net::Events netEvents;
  net::Frame netFrame;           // the gunner's: the clock, pause and page of the pilot's game
  uint32_t netGeneration = 1;
  double netTimer = 0.0;
  // The gunner: what he does, in his game (sent to the pilot's) and as the pilot's has it.
  net::GunnerInput gunnerIn;
  std::vector<uint8_t> gunnerActions;
  Uint64 snapshotAt = 0;      // the gunner's: ms of the last snapshot
  auto radio = [&](int id) {
    sound.radio(id);
    if (!peers.empty()) netEvents.radio.push_back(id);
  };
  auto effect = [&](int id) {
    sound.effect(id);
    if (!peers.empty()) netEvents.effects.push_back(id);
  };
  // D in the air with door guns: the door gunners fire by themselves, or hold fire (and their
  // guns are only the player's).
  auto toggleDoorGunners = [&]() {
    weapons.doorGunnersFree = !weapons.doorGunnersFree;
    heli.setMessage(weapons.doorGunnersFree ? "DOOR GUNNERS FREE FIRE" : "DOOR GUNNERS HOLD FIRE", 2.0f);
  };
  // Back to the start of the mission (or of the exercise).
  // --intercept: in the air beside the mission's first enemy drone (at the start, after the
  // planning's take-off and on every restart).
  auto intercept = [&]() {
    if (interceptSec < 0.0f) return;
    // Where the first enemy drone will be after interceptSec seconds (a copy of the battlefield run
    // ahead); the helicopter flies its course there 150 m to its right and 60 m above, 30 km/h
    // slower, so that the drone draws level just then.
    int drone = -1;
    for (size_t i = 0; i < targets.list.size() && drone < 0; ++i) {
      const sim::Vehicle& v = targets.list[i];
      if (v.side == sim::Side::Enemy && !v.destroyed && sim::vehicleType(v.kind).cls == sim::UnitClass::Drone) drone = int(i);
    }
    if (drone < 0) {
      std::fprintf(stderr, "--intercept: no enemy drone in this mission\n");
    } else {
      sim::Targets ahead = targets;
      for (float t = 0.0f; t < interceptSec; t += 0.05f) ahead.step(0.05f);
      const sim::Vehicle& d = ahead.list[size_t(drone)];
      const float speed = std::max(5.0f, glm::length(glm::vec2(d.vel.x, d.vel.z)) - 30.0f / 3.6f);
      const glm::vec3 fwd{std::sin(d.heading), 0.0f, -std::cos(d.heading)}, right{std::cos(d.heading), 0.0f, std::sin(d.heading)};
      const glm::vec3 p = d.pos - right * interceptAt.x - glm::vec3(0.0f, interceptAt.y, 0.0f) - fwd * (speed * interceptSec);
      heli.resetAirborne(p, d.heading, speed);
      heli.gearDown = false;
      heli.gearPos = 0.0f;
      prevPos = heli.pos, prevOrient = heli.orient;
      input.setCollective(heli.model == sim::FlightModel::Novice ? 0.5f : sim::Helicopter::levelFlightCollective(speed, heli.mass));
    }
  };
  // Quickstart over Ukraine: no flight out to the patrol line. The battlefield runs ahead until
  // the first enemy drone is kHuntRange from the line's middle, and the flight starts there in the
  // air: our Hind and the other helicopters of the flight (the wingman, the Hip), facing it. Then
  // no more waiting: kHuntDrones drones in a steady stream, each coming in from some kilometres
  // ahead of our Hind and past it; shot down or gone by, it comes again, as long as the player
  // wants to fly.
  std::vector<std::pair<size_t, sim::Vehicle>> huntWave;  // the raid's drones as the mission starts them
  double huntTick = 0.0;  // when the stream was last looked at
  // Each run of the game a stream of its own (random_device, and the clock should that be a
  // fixed sequence); off screen always the same, for repeatable pictures.
  uint32_t huntRng = offscreen ? 20261107u
                              : std::random_device{}() ^ uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
  constexpr float kHuntRange = 7000.0f, kHuntStep = 0.25f;
  constexpr int kHuntDrones = 16;
  const auto huntDrone = [](const sim::Vehicle& v) {
    return v.side == sim::Side::Enemy && sim::vehicleType(v.kind).cls == sim::UnitClass::Drone;
  };
  // As many drones as the stream wants: more of the mission's own, the same in every game of the
  // network game (each has the units by their places in the list).
  const auto huntFill = [&]() {
    std::vector<size_t> own;
    for (size_t i = 0; i < targets.list.size(); ++i)
      if (huntDrone(targets.list[i])) own.push_back(i);
    if (own.empty()) return;
    for (size_t k = own.size(); k < size_t(kHuntDrones); ++k) {
      const sim::Vehicle copy = targets.list[own[k % own.size()]];
      targets.list.push_back(copy);
    }
    // And a Banderol among them (huntSend sends it from far, fast).
    if (std::none_of(own.begin(), own.end(), [&](size_t i) { return sim::isCruiseMissile(targets.list[i].kind); })) {
      sim::Vehicle b = targets.list[own[0]];
      b.kind = sim::VehicleKind::Banderol;
      b.health = sim::vehicleType(b.kind).health;
      targets.list.push_back(b);
    }
  };
  // The drone (flying) nearest `at`, and how far.
  const auto nearestDrone = [&](const sim::Targets& ts, glm::vec2 at, float& dist) {
    int best = -1;
    dist = 1e9f;
    for (size_t i = 0; i < ts.list.size(); ++i) {
      const sim::Vehicle& v = ts.list[i];
      if (!huntDrone(v) || !v.moving() || sim::isCruiseMissile(v.kind)) continue;  // (the Banderol comes from far)
      const float d = glm::distance(glm::vec2(v.pos.x, v.pos.z), at);
      if (d < dist) dist = d, best = int(i);
    }
    return best;
  };
  // Seconds the battlefield must run until a drone is kHuntRange from `at` (or, when none ever
  // is, nearest it); < 0: no drone flies.
  const auto huntAhead = [&](sim::Targets ahead, glm::vec2 at) {
    float when = -1.0f, closest = 1e9f;
    for (float t = 0.0f; t < 1800.0f; t += kHuntStep, ahead.step(kHuntStep)) {
      float dist;
      if (nearestDrone(ahead, at, dist) < 0) continue;
      if (dist < closest) closest = dist, when = t;
      if (dist < kHuntRange) break;
    }
    return when;
  };
  // Where Quickstart over Ukraine put our flight in the air (for the wingmen's games too).
  bool huntAir = false;
  glm::vec2 huntHome{0.0f};
  float huntHeading = 0.0f;
  constexpr float kHuntAltitude = 150.0f, kHuntKmh = 120.0f;
  // Sends a drone (as the mission started it) in from `far` metres ahead of our Hind, a little to
  // a side, on past it at its height and speed.
  glm::vec2 huntBanderolSide{0.0f};  // how far to a side of our Hind the Banderol passes
  const auto huntSend = [&](const std::pair<size_t, sim::Vehicle>& w, float far) {
    const auto rnd = [&]() {
      huntRng = huntRng * 1664525u + 1013904223u;
      return float(huntRng >> 8) / float(1u << 24);  // 0..1
    };
    float alt = 80.0f, speed = 50.0f;
    for (const sim::RoutePoint& q : w.second.route) alt = q.alt > 0.0f ? q.alt : alt, speed = std::max(speed, q.speed);
    // The Banderol: from 15 to 22 km out (some two minutes' flight), low, at 500 km/h.
    const bool banderol = sim::isCruiseMissile(w.second.kind);
    if (banderol) alt = 100.0f, speed = 500.0f / 3.6f, far = 15000.0f + 7000.0f * rnd();
    const glm::vec3 f = heli.forward();
    const float a = std::atan2(f.x, -f.z) + (rnd() - 0.5f) * 2.6f;  // within 75 degrees of our nose
    const glm::vec2 out{std::sin(a), -std::cos(a)}, side{-out.y, out.x};
    const glm::vec2 here{heli.pos.x, heli.pos.z};
    const glm::vec2 from = here + out * far + side * ((rnd() - 0.5f) * 1200.0f);
    sim::Vehicle& v = targets.list[w.first];
    v = w.second;
    std::vector<sim::RoutePoint> route{{from, alt, speed, 0.0f}};
    if (banderol) {
      // Its own way, not straight: a turn off to a side halfway, then past us a little to a side.
      route.push_back({here + out * (0.5f * far) + side * ((rnd() < 0.5f ? -1.0f : 1.0f) * (1500.0f + 1500.0f * rnd())), alt, speed, 0.0f});
      huntBanderolSide = side * ((rnd() - 0.5f) * 800.0f);
      route.push_back({here + huntBanderolSide, alt, speed, 0.0f});
      const glm::vec2 last = glm::normalize(route[2].pos - route[1].pos);
      route.push_back({route[2].pos + last * 40000.0f, alt, speed, 0.0f});
    } else {
      route.push_back({from - out * 40000.0f, alt, speed, 0.0f});
    }
    targets.setRoute(int(w.first), route, false, 0.0f);
    v.pos = {from.x, terrain.groundHeight(from.x, from.y) + alt, from.y};
    v.heading = a + glm::pi<float>();
  };
  auto huntStart = [&]() {
    huntWave.clear();
    huntAir = false;
    if (!quickHunt || joining) return;
    const std::vector<glm::vec2>& r = missionState.route();
    if (r.size() < 3) return;
    huntFill();
    for (size_t i = 0; i < targets.list.size(); ++i)
      if (huntDrone(targets.list[i])) huntWave.emplace_back(i, targets.list[i]);
    heli.infiniteFuel = true;  // (the stream does not end)
    const glm::vec2 mid = (r[1] + r[2]) * 0.5f;
    const float when = huntAhead(targets, mid);
    if (when < 0.0f) return;
    for (float u = 0.0f; u + kHuntStep * 0.5f < when; u += kHuntStep) targets.step(kHuntStep);
    float dist;
    const int drone = nearestDrone(targets, mid, dist);
    if (drone < 0) return;
    const sim::Vehicle& d = targets.list[size_t(drone)];
    const glm::vec2 to = glm::vec2(d.pos.x, d.pos.z) - mid;
    const float heading = std::atan2(to.x, -to.y);
    const glm::vec2 fwd{std::sin(heading), -std::cos(heading)}, right{std::cos(heading), std::sin(heading)};
    const float speed = kHuntKmh / 3.6f;
    heli.resetAirborne({mid.x, terrain.groundHeight(mid.x, mid.y) + kHuntAltitude, mid.y}, heading, speed);
    huntAir = true, huntHome = mid, huntHeading = heading;
    heli.gearDown = false;
    heli.gearPos = 0.0f;
    prevPos = heli.pos, prevOrient = heli.orient;
    input.setCollective(0.8f);  // (rather climbing than sinking, the first thing the player sees)
    // The others of the flight: beside and behind us, at their own height above the ground.
    int k = 0;
    for (sim::Vehicle& v : targets.list) {
      if (v.side != sim::Side::Ours || v.destroyed || sim::vehicleType(v.kind).cls != sim::UnitClass::Helicopter || v.route.empty())
        continue;
      const float above = std::max(v.pos.y - terrain.groundHeight(v.pos.x, v.pos.z), 120.0f);
      const glm::vec2 at = mid + right * (k % 2 ? -250.0f : 250.0f) * float(k / 2 + 1) - fwd * 200.0f * float(k / 2 + 1);
      v.pos = {at.x, terrain.groundHeight(at.x, at.y) + above, at.y};
      v.heading = heading, v.waiting = 0.0f;
      v.vel = glm::vec3(fwd.x, 0.0f, fwd.y) * speed;
      ++k;
    }
    // And with us on the hunt, not off along the mission's route (the wingman's R-60s within
    // reach of what comes at us); the player may send them back onto it (resume the flight plan).
    std::string said;
    orders.give(sim::Order::Follow, true, heli, targets, -1, said);
    std::printf("Quickstart: the flight on the patrol line, %s %.1f km ahead\n", sim::vehicleType(d.kind).name.c_str(),
                glm::length(to) / 1000.0f);
    // The first of the stream, one behind another.
    float far = 2500.0f;
    for (const auto& w : huntWave) huntSend(w, far), far += 400.0f;
  };
  // Shift+T: time compression, round x2, x4, x8 and back to x1 (Ctrl+T off).
  // Only on the quiet legs: it stops by itself when there is anything to fight.
  int timeScale = 1;
  // The Pilot's Log: seconds in the air this flight, the campaign won with it.
  double airborne = 0.0;
  bool campaignWon = false;
  auto timeHalt = [&]() -> const char* {
    if (wso.target() >= 0 || spotter.designated() >= 0) return "TARGET";
    if (!enemy.shots().empty()) return "UNDER FIRE";
    for (const sim::Vehicle& v : targets.list)
      if (!v.destroyed && v.side == sim::Side::Enemy && glm::distance(v.pos, heli.pos) < kTimeHaltRange) return "ENEMY NEAR";
    return "";
  };
  std::string collidedWith;  // what the Hind flew into (for the debriefing)
  // Over Ukraine our AI Hinds armed against what the player carries (after huntStart: the
  // flight in its places in the air).
  const auto armWings = [&]() {
    if (haveMission && mission.name.rfind("UKRAI", 0) == 0 && !joining)
      wingArms.arm(targets, weapons.loadout(), weapons.infiniteAmmo, weapons.r60Reloads);
    else
      wingArms.clear();
  };
  auto restart = [&]() {
    collidedWith.clear();
    timeScale = 1;
    nightVision = nightVisionAtStart;
    hudChosen = false;
    airborne = 0.0;
    ++netGeneration;
    crewHome = startPad;
    for (auto& p : peers) p->startSent = false;
    heli.resetOnGround(startPad, startHeading);
    drillStart();
    input.resetControls();
    if (heli.weightOnWheels <= 0.0f) input.setCollective(0.6f);
    weapons.load(loadout);
    peerWeapons.load(loadout);
    // The wingmen's Hinds are armed as ours; the battlefield anew: their next reports take the
    // AI wingmen's places again.
    for (auto& w : wings) {
      w->weapons.load(loadout), w->weapons.shots.clear();
      w->heli.damage = {};
      w->flying = w->down = false;
      w->standIns.clear();
    }
    effects.clear();
    debris.reset(), wrecks.clear(), unitWrecks.clear(), othersDown.clear();
    camera.stopCrashOrbit();
    wasCrashed = false;
    deploy();
    wso.reset();
    enemy.reset();
    battle.reset();
    airCombat.reset();
    spotter.reset();
    orders.reset();
    sound.startFlight(haveMission, haveMission && mission.name.rfind("UKRAI", 0) == 0);
    prevPos = heli.pos;
    prevOrient = heli.orient;
    intercept();
    huntStart();
    armWings();
  };
  // The bowman stands 350 m ahead of the take-off point and a little aside, facing it.
  const auto placeBowman = [&]() {
    if (!bowmanGame) return;
    const glm::vec2 fwd{std::sin(startHeading), -std::cos(startHeading)}, right{std::cos(startHeading), std::sin(startHeading)};
    // (His alone, by our Hind: 60 m off.)
    const float off = joining ? 350.0f : 60.0f;
    bowman.place(startPad + fwd * off - right * (off * 0.23f), startHeading + glm::pi<float>(), terrain);
    // (--start-pos X Z and --look DEG: there, facing that way; development.)
    if (customStart) bowman.place(startPos, startLook, terrain);
    bowman.pitch = startLookPitch;  // (--look-pitch)
    std::printf("Network game: on foot at %.0f %.0f, the Hinds take off at %.0f %.0f\n", bowman.pos.x, bowman.pos.z, startPad.x, startPad.y);
    bowmanBursts.clear();
    bowWaypoint = -1;
  };
  // The gunner's game: the flight starts (again) as the pilot's did.
  auto applyStart = [&](const net::StartInfo& st) {
    startPad = st.home, startHeading = st.heading;
    loadout = st.loadout;
    if (haveMission) mission.startHour = st.hour;
    heli.model = sim::FlightModel(st.flightModel);
    heli.noCrash = st.noCrash != 0;
    weapons.infiniteAmmo = peerWeapons.infiniteAmmo = st.infiniteWeapons != 0;
    weapons.doorReloads = peerWeapons.doorReloads = st.doorReloads != 0;
    // The wingmen's Hinds stand in a row 30 m apart to the right of the pilot's.
    const float right = wingmanGame ? 30.0f * float(st.slot + 1) : 0.0f;
    crewHome = startPad + right * glm::vec2(std::cos(startHeading), std::sin(startHeading));
    heli.resetOnGround(crewHome, startHeading);
    heli.infiniteFuel = st.infiniteFuel != 0;
    if (st.airStart) {
      // In the air by the pilot's Hind: the wingmen in echelon to the right, a little behind.
      const glm::vec2 fwd{std::sin(startHeading), -std::cos(startHeading)}, side{std::cos(startHeading), std::sin(startHeading)};
      const float n = wingmanGame ? float(st.slot + 1) : 0.0f;
      crewHome = startPad + side * (100.0f * n) - fwd * (50.0f * n);
      const float speed = kHuntKmh / 3.6f;
      heli.resetAirborne({crewHome.x, terrain.groundHeight(crewHome.x, crewHome.y) + st.airAltitude, crewHome.y}, startHeading, speed);
      heli.gearDown = false, heli.gearPos = 0.0f;
      input.setCollective(0.8f);  // (as the pilot's: huntStart)
    }
    airStarted = st.airStart != 0;
    for (auto& p : peers) p->startSent = false;  // our crew's games start again with ours
    otherHelis.clear();
    weapons.load(loadout);
    peerWeapons.load(loadout);
    effects.clear();
    debris.reset(), wrecks.clear(), unitWrecks.clear(), othersDown.clear();
    camera.stopCrashOrbit();
    wasCrashed = false;
    deploy();
    if (st.airStart) huntFill();  // (the same units as in the pilot's game)
    if (st.targets != targets.list.size())
      std::fprintf(stderr, "Network game: the pilot's battlefield has %u units, ours %zu (not the same game data?)\n", st.targets,
                   targets.list.size());
    net::Mirror::setRoute(missionState, st.route, st.routeAlts);
    mapMission();
    sound.startFlight(haveMission, haveMission && mission.name.rfind("UKRAI", 0) == 0);
    page.clear();
    netFrame = {};
    netWingStores.clear(), netWingShots.clear();
    prevPos = heli.pos;
    prevOrient = heli.orient;
    placeBowman();
  };
  // Planning before the mission (briefing, map, payload); the debriefing after it is a page over
  // the paused game until Enter.
  menu::Planner planner;
  GLuint view3DFbo = 0, view3DDepth = 0;  // the planner's 3D view is drawn into its picture
  bool planning = false;
  planner.setUiScale(cfg.getInt("video", "ui_scale", 2));
  if (haveMission && !planner.init(originalDir, cfg.getString("world", "zone", "afghan"))) haveMission = false;
  auto plan = [&]() {
    planning = paused = true;
    speak(gamedata::speechName(mission.name, gamedata::SpeechPart::Briefing));
    sim::missionLoadout(mission, loadout);
    huntLoadout(loadout);
    planner.start(mission, zoneOrigin, terrain, targets, loadout, sim::waypointTargets(mission, original, zoneObjects));
  };
  // Off the planning screens into the helicopter: the route and the stores as planned.
  auto takeOff = [&]() {
    speak("");
    menuMusic("");
    planner.applyRoute(mission);
    loadout = planner.loadout();
    missionStartPad(mission);
    restart();
    mapMission();
    planning = paused = false;
    // The stores are remembered for the next time (not this run's command line options).
    platform::Config saved;
    saved.load(configPath);
    saved.applyDefaults();
    static const char* keys[sim::kPylons] = {"inner_pylons", "outer_pylons", "wingtip_pylons"};
    for (int p = 0; p < sim::kPylons; ++p) saved.set("weapons", keys[p], sim::storeInfo(loadout.at(sim::Pylon(p))).key);
    saved.set("weapons", "door_guns", sim::doorGunsKey(loadout));
    saved.save(configPath);
  };
  bool debriefing = false, campaignFlown = false;  // (a campaign's mission counts once)
  // Logs the flight just debriefed for the pilot flying; what it brought is shown in the menus.
  auto logFlight = [&]() {
    sim::FlightRecord f;
    f.airborneSeconds = airborne;
    f.combat = !haveMission || mission.name.rfind("SARATT", 0) != 0;
    f.campaign = cfg.getString("world", "mission", "campaign") == "campaign";
    f.zone = cfg.getString("world", "zone", "afghan");
    if (haveMission) f.mission = mission.name;
    const sim::MissionResult r = missionState.result();
    f.result = r == sim::MissionResult::Outstanding ? 2 : r == sim::MissionResult::Success ? 1 : 0;
    f.campaignWon = campaignWon;
    f.crashed = heli.crashed;
    // A hard crash or one shot down is not walked away from.
    f.died = heli.crashed && (heli.damage.shotDown || glm::length(heli.crashVel) > 12.0f);  // (as the debriefing's music)
    for (const sim::Vehicle& v : targets.list)
      if (!v.destroyed && v.side == sim::Side::Enemy && !v.scenery && glm::distance(v.pos, heli.pos) < 5000.0f) f.enemyTerritory = true;
    const sim::LogOutcome o = pilotLog.record(f, int64_t(std::time(nullptr)));
    if (!o.logged) std::printf("Pilot's log: %s is not active, the mission is not logged\n", pilotLog.pilot().name.c_str());
    else if (!pilotLog.save(pilotLogPath)) std::fprintf(stderr, "Cannot write the pilot's log %s\n", pilotLogPath.c_str());
    if (menuReady) front.showAwards(o);
  };
  bool endAsk = false;  // Ctrl+Q: "END MISSION? Y/N" over the stopped game
  // Our Hind down in a network game: the game's other Hinds still flying (with what to call
  // them), the one the camera goes round instead of our wreck (F5 the next) and since when.
  const sim::Helicopter* watchedHind = nullptr;
  double downSince = -1.0;
  const auto hindsLeft = [&]() {
    std::vector<std::pair<const sim::Helicopter*, std::string>> v;
    if (joining) {
      if (peerFlying && !peerHeli.crashed) v.push_back({&peerHeli, "THE PILOT"});
      for (size_t k = 0; k < otherHelis.size(); ++k)
        if (!otherHelis[k]->crashed) v.push_back({otherHelis[k].get(), "HIND " + std::to_string(v.size() + 1)});
    }
    for (size_t k = 0; k < wings.size(); ++k) {
      if (!wings[k]->flying || wings[k]->heli.crashed) continue;
      std::string name = "WINGMAN";
      for (const auto& p : peers)
        if (p->slot == int(k)) name = p->label();
      for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
      v.push_back({&wings[k]->heli, name});
    }
    return v;
  };
  std::vector<std::string> debriefLines;
  if (haveMission && !skipPlanning && (screenshotPath.empty() || !planScreen.empty())) {
    plan();
    if (planScreen == "payload") planner.open(menu::Screen::Payload);
    else if (planScreen == "briefing") planner.open(menu::Screen::Briefing);
  }
  sound.startFlight(haveMission, haveMission && mission.name.rfind("UKRAI", 0) == 0);
  if (atWaypoint >= 0 && atWaypoint < int(missionState.route().size())) {
    heli.resetOnGround(missionState.route()[size_t(atWaypoint)], startHeading);
    prevPos = heli.pos, prevOrient = heli.orient;
  }
  intercept();
  huntStart();
  armWings();
  if (!joining) placeBowman();
  if (openDoors) troops.toggleDoors(heli);
  if (joining) {
    applyStart(joined);
    // The wingman's own Hind: in the air with --start-air, its weapon as --fire says (development).
    if (wingmanGame && startAlt >= 0.0f) {
      heli.resetAirborne(heli.pos + glm::vec3(0.0f, startAlt, 0.0f), startHeading, startKmh / 3.6f);
      heli.gearDown = false, heli.gearPos = 0.0f;
    }
    if (wingmanGame && !autoFire.empty()) {
      sim::WeaponType want = fireStore != sim::Store::None ? sim::storeInfo(fireStore).weapon
                             : autoFire == "s8" ? sim::WeaponType::S8 : sim::WeaponType::YakB;
      weapons.select(want);
    }
    // The seat he chose; a door without a gun this time: the other door, or the gunner's cockpit.
    if (camera.mode == render::ViewMode::Door && !loadout.doorGun[camera.doorSide]) {
      if (loadout.doorGun[1 - camera.doorSide]) camera.doorSide = 1 - camera.doorSide;
      else camera.mode = render::ViewMode::Gunner;
      heli.setMessage("NO DOOR GUN THERE", 3.0f);
    }
    crewSeat = camera.mode, crewDoor = camera.doorSide;
    snapshotAt = SDL_GetTicks64();
  }
  if (endTest == 1) endAsk = paused = true;
  for (int k = 0; k < sim::kSystems; ++k)
    if (failSystems == "all" || ("," + failSystems + ",").find("," + std::to_string(k) + ",") != std::string::npos)
      heli.damage.failed[k] = true;
  if (endTest == 2 && haveMission) missionState.abort();
  // --fire in the gunner's game: his weapon armed and the trigger held (development).
  if (mirrored && !autoFire.empty()) gunnerIn.armed = 1, gunnerIn.weapon = autoFire == "9m114" ? sim::WeaponType::Shturm : sim::WeaponType::YakB;
  while (running) {
    SDL_Event e;
    if (sayKey > 0 && simTime > 2.0) {
      SDL_Event ev{};
      ev.type = SDL_KEYDOWN;
      ev.key.keysym.scancode = SDL_Scancode(SDL_SCANCODE_1 + (sayKey + 9) % 10);
      ev.key.keysym.mod = KMOD_LCTRL;
      SDL_PushEvent(&ev);
      sayKey = 0;
    }
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false, quitAll = true;
      if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F11) {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
      }
      if (planning) {
        if (input.screenshotKey(e)) screenWanted = true;
        const menu::Result r = planner.handleEvent(e);
        if (r == menu::Result::TakeOff) takeOff();
        if (r == menu::Result::Quit) {
          // Out of the debriefing: into the Pilot's Log, as the original's. A campaign's mission
          // is logged as it went; a single one only if the pilot wants it (or flown again at
          // once). An aborted mission, a network game and the free flight are not logged.
          // (Quickstart's has its Top Ten instead: the two do not compare.)
          const bool loggable = debriefing && haveMission && !quickRun && !joining && !link.open() && peers.empty() &&
                                missionState.result() != sim::MissionResult::Aborted && screenshotPath.empty();
          if (loggable && cfg.getString("world", "mission", "campaign") != "campaign") {
            planner.askLog();
            continue;
          }
          if (loggable) logFlight();
          running = false;
        }
        if (r == menu::Result::LogYes) logFlight(), running = false;
        if (r == menu::Result::LogNo) running = false;
        if (r == menu::Result::LogRetry) {
          menuMusic("");
          restart();
          debriefing = planning = paused = false;
        }
        continue;
      }
      // --debug-door: L starts and stops the log.
      if (debugDoor && e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_L) {
        doorLogging = !doorLogging;
        doorDebugAt = 0.0;
        heli.setMessage(doorLogging ? "DOOR GUNNER LOG ON" : "DOOR GUNNER LOG OFF", 2.0f);
        std::printf("--- door gunner log %s ---\n", doorLogging ? "on" : "off");
        std::fflush(stdout);
      }
      if (!page.empty() && e.type == SDL_KEYDOWN) {
        const SDL_Scancode sc = e.key.keysym.scancode;
        // The gunner's page is the pilot's to turn.
        if (!joining && !e.key.repeat && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)) {
          page.clear();
          paused = false;
        }
        // Only Escape (quit) gets through to the game while a page is shown.
        if (sc != SDL_SCANCODE_ESCAPE) continue;
      }
      // "END MISSION? Y/N": Y ends it (aborted: the debriefing), N or Esc flies on.
      if (endAsk && e.type == SDL_KEYDOWN && !e.key.repeat) {
        const SDL_Scancode sc = e.key.keysym.scancode;
        if (sc == SDL_SCANCODE_Y || sc == SDL_SCANCODE_Z) {  // Z: Y on a Czech keyboard
          endAsk = false;
          if (haveMission && missionState.active()) missionState.abort();
          else running = false;
        } else if (sc == SDL_SCANCODE_N || sc == SDL_SCANCODE_ESCAPE) {
          endAsk = false;
          paused = false;
        }
        continue;
      }
      if (endAsk && e.type == SDL_KEYDOWN) continue;
      // Paused, the game answers to P alone (and to leaving it, and the screenshot).
      const bool frozen = keyPause || (mirrored && paused);
      input.setFrozen(frozen);
      platform::Action action = input.handleEvent(e);
      if (action == platform::Action::Screenshot) {
        screenWanted = true;
        continue;
      }
      if (frozen && action != platform::Action::Pause && action != platform::Action::Quit && action != platform::Action::EndMission)
        action = platform::Action::None;
      if (bowmanGame) {
        using platform::Action;
        // N and Shift+N (W walks him): off to the next (or the one before) point of the flight's
        // route, 150 m short of it on the way there and a little aside, facing where the Hinds
        // come from.
        const std::vector<glm::vec2>& route = missionState.route();
        if (!frozen && e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_N) {
          const int n = int(route.size()), step = e.key.keysym.mod & KMOD_SHIFT ? -1 : 1;
          if (n == 0) {
            bowMessage = "NO ROUTE TO GO ALONG", bowMessageUntil = SDL_GetTicks64() + 2000;
          } else {
            bowWaypoint = bowWaypoint < 0 ? (step > 0 ? 0 : n - 1) : (bowWaypoint + step + n) % n;
            const glm::vec2 at = route[size_t(bowWaypoint)], from = bowWaypoint > 0 ? route[size_t(bowWaypoint - 1)] : startPad;
            const glm::vec2 d = glm::length(at - from) > 1.0f ? glm::normalize(at - from) : glm::vec2(0.0f, -1.0f);
            const glm::vec2 to = at - d * 150.0f + glm::vec2(-d.y, d.x) * 40.0f;
            bowman.pos = {to.x, terrain.groundHeight(to.x, to.y), to.y};
            bowman.airborne = false;
            bowman.yaw = std::atan2(-d.x, d.y), bowman.pitch = 0.0f;
            bowMessage = std::string("WAYPOINT ") + sim::MissionState::letter(bowWaypoint), bowMessageUntil = SDL_GetTicks64() + 2000;
          }
        }
        if (action == Action::ViewExternal) bowOrbit = true;
        if (action == Action::ViewPilot) bowOrbit = false;
        if (action == Action::ViewExternal || action == Action::ViewPilot) bowArrow = false;
        if (action == Action::ViewWeapon) {
          if (bowman.rocket.flying || bowman.state == sim::Bowman::State::Flying) bowArrow = true, bowArrowGone = -1.0;
          else bowMessage = bowman.weapon == sim::Bowman::Weapon::Law ? "NO ROCKET IN THE AIR" : "NO ARROW IN THE AIR",
               bowMessageUntil = SDL_GetTicks64() + 2000;
        }
        if ((bowOrbit || bowArrow) && (action == Action::ZoomIn || action == Action::ZoomOut)) camera.zoom(action == Action::ZoomIn ? 0.8f : 1.25f);
        // Enter (as the Hind's weapons): the bow for the M60 and back.
        if (action == Action::WeaponSelect && !bowman.dead()) bowman.change();
        if (action != Action::Quit) action = Action::None;
      }
      // The gunner's game only looks: its own view, and quitting.
      if (mirrored) {
        using platform::Action;
        // The gunner's weapons (the turret gun, the 9M114s, and the stores too) and his keys for
        // the silicon WSO.
        if (action == Action::WeaponSelect) {
          const std::vector<sim::WeaponType> mine = weapons.gunnerChoices();
          size_t from = 0;
          if (gunnerIn.armed) from = size_t(std::find(std::begin(mine), std::end(mine), gunnerIn.weapon) - std::begin(mine)) + 1;
          gunnerIn.armed = 0;
          for (size_t k = from; k < std::size(mine); ++k)
            if (weapons.rounds(mine[k]) > 0) {
              gunnerIn.weapon = mine[k], gunnerIn.armed = 1;
              break;
            }
        }
        if (action == Action::WeaponSafe) gunnerIn.armed = 0;
        if (action == Action::Wso) gunnerActions.push_back(uint8_t(net::GunnerAction::Wso));
        if (action == Action::Brakes && camera.mode != render::ViewMode::Drone) gunnerActions.push_back(uint8_t(net::GunnerAction::Rescan));
        if (action == Action::TrackTarget) gunnerActions.push_back(uint8_t(net::GunnerAction::Track));
        if (action == Action::Doors) gunnerActions.push_back(uint8_t(net::GunnerAction::DoorGunners));
        const Action own[] = {Action::Quit, Action::ViewPilot, Action::ViewGunner, Action::ViewDoor, Action::ViewExternal, Action::ViewCenter,
                              Action::ZoomIn, Action::ZoomOut, Action::NightVision, Action::ViewWeapon, Action::ViewTarget,
                              Action::ViewLock, Action::ViewRemote, Action::ViewSpectator, Action::ViewWatch, Action::ViewWatchBack,
                              Action::CrewMessage, Action::PilotMessage, Action::HudColour, Action::InstrumentLights,
                              Action::Interceptor};
        // (B in a drone's camera picks its next target here.)
        if (std::find(std::begin(own), std::end(own), action) == std::end(own) &&
            !(action == Action::Brakes && camera.mode == render::ViewMode::Drone))
          action = Action::None;
      }
      switch (action) {
        case platform::Action::Quit: running = false, quitAll = quitAll || (joining && !joinedFromMenu), leaveGame = joinedFromMenu; break;
        case platform::Action::Screenshot: break;
        case platform::Action::EndMission:
          // As in the original: the game stops and asks (the gunner's game just leaves).
          if (joining) running = false, quitAll = !joinedFromMenu, leaveGame = joinedFromMenu;
          else if (page.empty()) endAsk = paused = true;
          break;
        case platform::Action::Pause:
          if (keyPause && !unpausing) unpausing = true;
          else if (unpausing) unpausing = false;
          else if (!paused) paused = keyPause = true;
          else paused = false;
          break;
        case platform::Action::FormationOrder:
        case platform::Action::CrewMessage:
        case platform::Action::PilotMessage: {
          // An order to the wingman (Ctrl+1..4 outside a network game) or to all (Shift+1..4).
          const int key = input.messageNumber();
          const bool order = action == platform::Action::FormationOrder ||
                             (action == platform::Action::CrewMessage && key <= 4 && peers.empty() && !joining);
          if (order) {
            if (joining) break;
            static const sim::Order kOrders[4] = {sim::Order::Home, sim::Order::Follow, sim::Order::Attack, sim::Order::Plan};
            const int target = wso.target() >= 0 ? wso.target() : spotter.designated();
            std::string said;
            const int answer = orders.give(kOrders[key - 1], action == platform::Action::FormationOrder, heli, targets, target, said);
            heli.setMessage(said, 3.0f);
            if (answer >= 0 && heli.works(sim::System::Com)) radio(answer);
            break;
          }
          // A gunner tells his pilot; a pilot his crew, or (Shift, or without a crew) the other pilots.
          const bool crew = std::any_of(peers.begin(), peers.end(), [](const auto& p) { return !p->wingman() && !p->bowman() && !p->view.empty(); });
          int number = 0;
          bool heard = false;
          if (mirrored) {
            number = key;
            if (link.open()) sendChat(link, number, netName), heard = true;
          } else if (action == platform::Action::CrewMessage && crew) {
            number = key + 10;
            for (auto& p : peers)
              if (!p->wingman() && !p->bowman() && !p->view.empty()) sendChat(p->link, number, netName), heard = true;
          } else {
            number = key + 20;
            if (wingmanGame && link.open()) sendChat(link, number, netName), heard = true;
            for (auto& p : peers)
              if (p->wingman()) sendChat(p->link, number, netName), heard = true;
          }
          if (heard) showChat("YOU", number);
          else chatLine = "NOBODY TO HEAR YOU", chatUntil = SDL_GetTicks64() + 2000;
          break;
        }
        case platform::Action::Gear: heli.toggleGear(); break;
        case platform::Action::Engines: heli.toggleEngines(); break;
        case platform::Action::Brakes:
          // As in the original, B is the brakes on the ground and "rescan targets" in the air.
          // In a drone's camera: the next enemy flier it marks.
          if (camera.mode == render::ViewMode::Drone) {
            const std::vector<int> marked = droneMarked();
            const auto at = std::find(marked.begin(), marked.end(), droneSel);
            droneSel = marked.empty() ? -1 : at == marked.end() || at + 1 == marked.end() ? marked.front() : *(at + 1);
          } else if (heli.weightOnWheels > 0.0f) {
            heli.brakes = !heli.brakes;
            heli.setMessage(heli.brakes ? "BRAKES ON" : "BRAKES OFF", 1.5f);
          } else {
            wso.rescan(heli, targets);
          }
          break;
        case platform::Action::Wso: wso.toggle(heli); break;
        case platform::Action::TimeMax:
          // In the gunner's seat it tracks the target along his sight, as T; elsewhere it speeds time.
          if (camera.mode != render::ViewMode::Gunner) {
            const char* why = hosting || joining ? "NOT IN A NETWORK GAME" : heli.crashed ? "" : timeHalt();
            if (*why) heli.setMessage(std::string("NO TIME COMPRESSION - ") + why, 2.0f);
            else if (!heli.crashed) {
              timeScale = timeScale >= 8 ? 1 : timeScale * 2;
              heli.setMessage(timeScale == 1 ? std::string("TIME NORMAL") : "TIME COMPRESSION X" + std::to_string(timeScale), 2.0f);
            }
            break;
          }
          [[fallthrough]];
        case platform::Action::TrackTarget:
          // Along the line of sight of the helmet designator.
          if (!heli.works(sim::System::Hmtd)) heli.setMessage(sim::systemFailure(sim::System::Hmtd), 2.0f);
          else wso.track(heli, targets, sightEye, sightDir);
          break;
        case platform::Action::TimeNormal:
          if (timeScale > 1) heli.setMessage("TIME NORMAL", 2.0f);
          timeScale = 1;
          break;
        case platform::Action::Reset:
          // In the network game the pilot starts the flight over, for both.
          if (joining) heli.setMessage("THE PILOT RESTARTS THE FLIGHT", 2.0f);
          else restart();
          break;
        case platform::Action::Recon: missionState.startRecon(heli); break;
        case platform::Action::Doors:
          // With the door guns in the doors there is nothing to shut in the air: D tells their
          // gunners to fire by themselves or hold fire (on the ground it is the doors, for the troops).
          if ((weapons.loadout().doorGun[0] || weapons.loadout().doorGun[1]) && heli.weightOnWheels <= 0.0f) toggleDoorGunners();
          else troops.toggleDoors(heli);
          break;
        case platform::Action::NextWaypoint: missionState.nextWaypoint(heli); break;
        case platform::Action::PrevWaypoint: missionState.nextWaypoint(heli, -1); break;
        case platform::Action::Flares:
          enemy.autoFlares = !enemy.autoFlares;
          heli.setMessage(enemy.autoFlares ? "AUTO FLARES ON " + std::to_string(enemy.flares) : "AUTO FLARES OFF", 1.5f);
          break;
        case platform::Action::Chaff:
          enemy.autoChaff = !enemy.autoChaff;
          heli.setMessage(enemy.autoChaff ? "AUTO CHAFF ON " + std::to_string(enemy.chaff) : "AUTO CHAFF OFF", 1.5f);
          break;
        case platform::Action::Jammer:
          enemy.jammer = !enemy.jammer;
          heli.setMessage(!heli.works(sim::System::Jam) ? sim::systemFailure(sim::System::Jam) : enemy.jammer ? "JAMMER ON" : "JAMMER OFF", 1.5f);
          break;
        case platform::Action::HudColour:
          hudBlack = !hudBlack;
          break;
        case platform::Action::InstrumentLights:
          instrumentLights = !instrumentLights;
          break;
        case platform::Action::Interceptor:
          // At a door or the gunner's sight: an interceptor drone out, the camera on it to fly
          // it; again (or F4, F3), let it go to find its own way, back at the door (the sight).
          // Watching one locked on its target, the next one out (his hands are free).
          if (camera.mode == render::ViewMode::Drone && weapons.interceptorOf(droneOwner())) {
            // (Only this lets it go: a player of the network game leaving its camera by another
            // view leaves it flying straight on, his to come back to.)
            if (mirrored) ++gunnerIn.releases;
            else weapons.releaseInterceptor(droneOwner());
            camera.mode = droneHome();
          } else if (camera.mode == render::ViewMode::Door || camera.mode == render::ViewMode::Drone ||
                     camera.mode == render::ViewMode::Gunner) {
            if (weapons.loadout().inner != sim::Store::Interceptor) {
              heli.setMessage("NO INTERCEPTORS LOADED", 2.0f);
            } else if (mirrored) {
              ++gunnerIn.launches;
              droneSeat = droneOwner(), camera.mode = render::ViewMode::Drone, droneLaunchedAt = simTime, droneStick = glm::vec2(0.0f), droneGoneAt = -1.0, droneEye = glm::vec3(0.0f);
            } else if (weapons.launchInterceptor(heli, droneOwner())) {
              droneSeat = droneOwner(), camera.mode = render::ViewMode::Drone, droneLaunchedAt = simTime, droneStick = glm::vec2(0.0f), droneGoneAt = -1.0, droneEye = glm::vec3(0.0f);
            } else {
              heli.setMessage(weapons.rounds(sim::WeaponType::Interceptor) == 0       ? "NO INTERCEPTORS LEFT"
                              : heli.crashed || heli.weightOnWheels > 0.0f ? "NOT IN THE AIR"
                                                                           : "INTERCEPTOR STILL FLYING",
                              2.0f);
            }
          } else if (!mirrored) {
            // The pilot's order to the door gunners: one out at his target when it is a drone,
            // else at the nearest (by themselves they send them at the jet ones only).
            const std::string why = weapons.orderInterceptor(heli, wso.target() >= 0 ? wso.target() : spotter.designated());
            if (!why.empty()) heli.setMessage(why, 2.0f);
          }
          break;
        case platform::Action::NightVision:
          // The night vision is the low light TV's picture.
          if (!heli.works(sim::System::Lltv) && !nightVision) heli.setMessage(sim::systemFailure(sim::System::Lltv), 2.0f);
          else nightVision = !nightVision;
          break;
        case platform::Action::FlightModel:
          heli.model = sim::FlightModel((int(heli.model) + 1) % 3);
          heli.setMessage(std::string("FLIGHT MODEL ") + sim::flightModelName(heli.model), 2.0f);
          break;
        case platform::Action::ViewPilot: camera.mode = render::ViewMode::Pilot; break;
        case platform::Action::ViewGunner: camera.mode = render::ViewMode::Gunner; break;
        case platform::Action::ViewDoor: {
          // From a drone, back to its door: the drone let go.
          if (camera.mode == render::ViewMode::Drone && droneSeat != 1) {
            if (!mirrored) weapons.releaseInterceptor(droneOwner());
            camera.mode = render::ViewMode::Door;
            break;
          }
          // To a door gun; again, to the other one if there is one.
          const sim::Loadout& l = weapons.loadout();
          // A door gunner of the network game: over to the other door, when nobody is at it.
          if (mirrored && crewSeat == render::ViewMode::Door) {
            const int other = 1 - crewDoor;
            if (camera.mode == render::ViewMode::Door) {
              if (!l.doorGun[other]) heli.setMessage("NO OTHER DOOR GUN", 2.0f);
              else if (doorsTaken & (1 << other)) heli.setMessage("THE OTHER DOOR IS TAKEN", 2.0f);
              else {
                crewDoor = other;
                // (Everybody's menus and the pilot's game go by what he says he is.)
                startView = crewDoor ? "door-right" : "door-left";
                sayHello();
              }
            }
            camera.mode = render::ViewMode::Door, camera.doorSide = crewDoor;
            break;
          }
          if (!l.doorGun[0] && !l.doorGun[1]) {
            if (!mirrored) heli.setMessage("NO DOOR GUNS", 2.0f);
            break;
          }
          const bool there = camera.mode == render::ViewMode::Door;
          if (there ? l.doorGun[1 - camera.doorSide] : !l.doorGun[camera.doorSide]) camera.doorSide = 1 - camera.doorSide;
          camera.mode = render::ViewMode::Door;
          break;
        }
        case platform::Action::ViewExternal: camera.mode = render::ViewMode::External; break;
        case platform::Action::ViewWeapon:
          if (weaponSeen) camera.mode = render::ViewMode::Weapon;
          else heli.setMessage("NO WEAPON LAUNCHED", 2.0f);
          break;
        case platform::Action::ViewTarget: camera.mode = render::ViewMode::Target; break;
        case platform::Action::ViewLock:
          if (wso.target() >= 0) camera.mode = render::ViewMode::Lock;
          else heli.setMessage("NO TARGET LOCKED", 2.0f);
          break;
        case platform::Action::ViewRemote:
          // Where the helicopter is now (a little behind it, not inside it); it flies on.
          camera.anchor = heli.pos - heli.forward() * 20.0f + glm::vec3(0.0f, 4.0f, 0.0f);
          camera.mode = render::ViewMode::Remote;
          break;
        case platform::Action::ViewSpectator:
          camera.anchor = lastEye;
          camera.mode = render::ViewMode::Spectator;
          break;
        case platform::Action::ViewWatch:
        case platform::Action::ViewWatchBack: {
          const int step = action == platform::Action::ViewWatch ? 1 : -1;
          if (watchedHind) {
            // Our Hind down: the next (or the one before) of the Hinds still flying.
            const auto left = hindsLeft();
            const int m = int(left.size());
            int at = 0;
            for (int k = 0; k < m; ++k)
              if (left[size_t(k)].first == watchedHind) at = k;
            const auto& next = left[size_t((at + step + m) % m)];
            watchedHind = next.first;
            camera.mode = render::ViewMode::External;
            heli.setMessage("WATCHING " + next.second, 2.0f);
            break;
          }
          // The next (or the one before) of the units about, ours and the enemy's.
          const int n = int(targets.list.size());
          int found = -1;
          for (int k = 1; k <= n && found < 0; ++k) {
            const int i = ((watchUnit < 0 ? (step > 0 ? -1 : 0) : watchUnit) + step * k + n * 2) % std::max(n, 1);
            const sim::Vehicle& v = targets.list[size_t(i)];
            if (!v.destroyed && !v.aboard && !v.scenery) found = i;
          }
          if (found < 0) {
            heli.setMessage("NOTHING TO WATCH", 2.0f);
            break;
          }
          watchUnit = found;
          camera.mode = render::ViewMode::Watch;
          heli.setMessage(sim::vehicleType(targets.list[size_t(found)].kind).name, 2.0f);
          break;
        }
        case platform::Action::ViewCenter: camera.center(); break;
        case platform::Action::ZoomIn: camera.zoom(0.8f); break;
        case platform::Action::ZoomOut: camera.zoom(1.25f); break;
        case platform::Action::WeaponSelect: weapons.selectNext(); break;
        case platform::Action::WeaponSafe: weapons.makeSafe(); break;
        case platform::Action::None: break;
      }
      if (mirrored) {
        const bool seat = camera.mode == render::ViewMode::Pilot || camera.mode == render::ViewMode::Gunner || camera.mode == render::ViewMode::Door;
        if (seat && (camera.mode != crewSeat || (crewSeat == render::ViewMode::Door && camera.doorSide != crewDoor)))
          camera.mode = crewSeat, camera.doorSide = crewDoor;
      }
    }

    Uint64 now = SDL_GetPerformanceCounter();
    // Off screen there is no display to keep time: steady 60 frames per simulated second.
    float frameDt = shotFbo ? 1.0f / 60.0f / shotSlow : float(std::min((now - last) / freq, 0.1));
    last = now;
    // Time compression ends by itself when the fight comes (or the mission ends, the menu opens).
    if (timeScale > 1 && (planning || !page.empty() || heli.crashed || *timeHalt())) {
      const char* why = heli.crashed ? "" : timeHalt();
      heli.setMessage(*why ? std::string("TIME NORMAL - ") + why : std::string("TIME NORMAL"), 2.0f);
      timeScale = 1;
    }
    // The world's own clock: the flight, the targets, the crews (the controls and the view keep
    // the real one).
    const float simDt = frameDt * float(timeScale);

    // The mouse aims the gun in the gunner's seat and at a door (caught in the window there).
    if (autoDrone && !mirrored && simTime > 0.3 && camera.mode == render::ViewMode::Door &&
        weapons.launchInterceptor(heli, droneOwner()))
      droneSeat = droneOwner(), camera.mode = render::ViewMode::Drone, droneLaunchedAt = simTime, droneStick = glm::vec2(0.0f), droneGoneAt = -1.0, droneEye = glm::vec3(0.0f), autoDrone = false;
    const bool droneView = camera.mode == render::ViewMode::Drone;
    const bool aimView = camera.mode == render::ViewMode::Gunner || camera.mode == render::ViewMode::Door || droneView;
    // The pause darkening in or out (real time); out, the game runs on.
    if (keyPause && !paused) keyPause = unpausing = false;
    pauseDim = std::clamp(pauseDim + ((keyPause && !unpausing) || (mirrored && paused) ? frameDt : -frameDt), 0.0f, 1.0f);
    if (unpausing && pauseDim <= 0.0f) paused = keyPause = unpausing = false;
    input.setMouseAim(!planning && page.empty() && (aimView || bowmanGame) && !shotFbo && !camera.crashOrbit() && !paused);
    bool mouseTurned = false;
    glm::vec2 droneSteer{0.0f};  // how the player flies his interceptor drone (-1..1, x left, y up)
    // The planning screens have the keyboard to themselves.
    input.setStickToDrone(droneView);
    input.setAtDoor(camera.mode == render::ViewMode::Door || droneView);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (virtualMountJoy && simTime > 0.2) {
      SDL_JoystickSetVirtualAxis(virtualMountJoy, 0, Sint16(virtualMount.x * 32767.0f));
      SDL_JoystickSetVirtualAxis(virtualMountJoy, 1, Sint16(virtualMount.y * 32767.0f));
      SDL_JoystickSetVirtualButton(virtualMountJoy, 0, virtualMountFire ? 1 : 0);
      static double reported = 0.0;
      if (camera.mode == render::ViewMode::Door && simTime >= reported + 1.0) {
        reported = simTime;
        std::printf("mount %.1f s: aim %.1f %.1f, view %.1f %.1f degrees, %d rounds, fire %d\n", simTime, glm::degrees(camera.doorAimYaw()), glm::degrees(camera.doorAimPitch()),
                    glm::degrees(camera.lookYaw()), glm::degrees(camera.lookPitch()), weapons.doorRounds(camera.doorSide), int(input.fire()));
      }
    }
#endif
    if (!planning) {
      input.update(keyPause || (mirrored && paused) ? 0.0f : frameDt);
      // The lone bowman: the mouse (or Alt + arrows) turns his eyes, W A S D (or the arrows) walk
      // him, Ctrl runs (Shift looks about); the fire button held draws his bow, let go looses the arrow.
      if (bowmanGame) {
        const glm::vec2 m = input.takeMouse();
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        const SDL_Keymod mods = SDL_GetModState();
        const bool arrows = input.viewYaw() == 0.0f && input.viewPitch() == 0.0f;  // (else they look about)
        const auto key = [&](SDL_Scancode a, SDL_Scancode b) { return ks[a] || (arrows && ks[b]); };
        const glm::vec2 move{float(key(SDL_SCANCODE_D, SDL_SCANCODE_RIGHT)) - float(key(SDL_SCANCODE_A, SDL_SCANCODE_LEFT)),
                             float(key(SDL_SCANCODE_W, SDL_SCANCODE_UP)) - float(key(SDL_SCANCODE_S, SDL_SCANCODE_DOWN))};
        if (!paused) {
          // What he cannot walk through and what his arrow's fuse feels: the game's Hinds and
          // everything else on the battlefield (vehicles, aircraft, buildings) near him or it, as
          // the snapshots have them.
          std::vector<sim::HindShape> hinds;
          // (The jammers: the host's Hind's as its snapshots say; the others' not known here,
          // taken as on, as they start.)
          if (!heli.crashed) hinds.push_back({heli.pos, heli.orient, int8_t(0), enemy.jammer && heli.works(sim::System::Jam)});
          for (size_t k = 0; k < otherHelis.size(); ++k)
            if (!otherHelis[k]->crashed)
              hinds.push_back({otherHelis[k]->pos, otherHelis[k]->orient, k < otherHeliIds.size() ? otherHeliIds[k] : int8_t(-1),
                               otherHelis[k]->works(sim::System::Jam)});
          const auto near = [&](glm::vec3 at, float range) {
            std::vector<sim::UnitShape> u;
            for (size_t k = 0; k < targets.list.size(); ++k) {
              const sim::Vehicle& v = targets.list[k];
              if (v.destroyed || v.aboard || glm::distance(v.pos, at) > range) continue;
              const sim::VehicleType& t = sim::vehicleType(v.kind);
              u.push_back({glm::inverse(v.toWorld()), t.lo, t.hi, t.faces.empty() ? nullptr : &t.faces, int(k)});
            }
            // The men walking to and from the Hinds.
            for (const sim::Walker& w : (joining ? peerTroops : troops).walkers())
              if (!w.gone && glm::distance(w.pos, at) <= range)
                u.push_back({glm::translate(glm::mat4(1.0f), -w.pos), glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 1.8f, 0.3f)});
            return u;
          };
          const std::vector<sim::UnitShape> around = near(bowman.pos, 120.0f);
          // (Slower while he guides the LAW's rocket by his look.)
          const float lawSlow = bowman.rocket.guided() ? sim::Bowman::kLawMouse : 1.0f;
          if (!bowman.dead()) bowman.turn((-m.x + input.viewYaw() * 1.6f * frameDt) * lawSlow, (m.y + input.viewPitch() * 1.6f * frameDt) * lawSlow);
          // Space jumps, the bow is the mouse's and the stick's.
          if (input.fireKey() && !bowJumpWas && !bowman.dead()) bowman.jump();
          bowJumpWas = input.fireKey();
          bowman.walk(move, mods & KMOD_CTRL, frameDt, terrain, hinds, around);
          bowman.fall(frameDt, terrain, around);
          const std::vector<sim::UnitShape> units =
              bowman.state == sim::Bowman::State::Flying ? near(bowman.arrowPos, 150.0f) : std::vector<sim::UnitShape>();
          // --fire (development): he aims at the host's Hind himself (over the arrow's drop) and
          // looses one every 14 seconds.
          bool pull = input.fireNoKey();
          const bool law = bowman.weapon == sim::Bowman::Weapon::Law;
          if (input.takeRightClick()) bowman.letDown();
          // (--fire still: as moving, but once locked on he holds still, and fires when it is out
          // of the circle.)
          static bool heldStill = false;
          if (autoFire == "still" && heldStill) {
            pull = bowman.locked && glm::dot(glm::normalize(bowman.aimAt - bowman.eye()), bowman.look()) < std::cos(sim::Bowman::kKeepCone * 0.7f);
            if (bowman.rocket.flying) heldStill = false;
          } else if (!autoFire.empty() && bowman.state != sim::Bowman::State::Flying && bowman.state != sim::Bowman::State::Fitting) {
            glm::vec3 to = heli.pos - bowman.eye();
            // (--fire moving: at the nearest aircraft on the move instead, as the An-12 taxiing.)
            if (autoFire == "moving" || autoFire == "still") {
              float best = 1200.0f;
              for (const sim::Vehicle& v : targets.list)
                if (v.moving() && sim::vehicleType(v.kind).cls == sim::UnitClass::Aircraft && glm::distance(v.pos, bowman.pos) < best) best = glm::distance(v.pos, bowman.pos), to = v.pos + glm::vec3(0, 2, 0) - bowman.eye();
            }
            const float speed = bowman.weapon == sim::Bowman::Weapon::M60 ? sim::Bowman::kM60Speed : sim::Bowman::kSpeedMax;
            const float dist = std::max(1.0f, glm::length(glm::vec2(to.x, to.z))), tof = dist / speed;
            bowman.yaw = std::atan2(to.x, -to.z);
            bowman.pitch = std::atan2(to.y + (law ? 0.0f : 0.5f * sim::Helicopter::kGravity * tof * tof), dist);
            pull = std::fmod(simTime, 14.0) > 2.0 && std::fmod(simTime, 14.0) < 5.0;
            // (The LAW once it has locked on.)
            if (bowman.weapon == sim::Bowman::Weapon::Law) pull = bowman.locked && autoFire != "still";
            heldStill = autoFire == "still" && bowman.locked && !bowman.rocket.flying;
          }
          if (!autoFire.empty() && std::fmod(simTime, 14.0) > 6.0) bowman.walk({0.0f, 1.0f}, false, frameDt, terrain, hinds, around);
          // The M60 (the trigger its, while it is in his hands): where its rounds went, to the host's
          // game (his alone: here, as there).
          std::vector<sim::Bowman::Hit> m60Hits;
          const bool gun = bowman.weapon == sim::Bowman::Weapon::M60;
          const int beltBefore = bowman.belt;
          bowman.stepM60(pull && gun, frameDt, terrain, hinds, bowman.rounds.empty() && !(pull && gun) ? std::vector<sim::UnitShape>() : near(bowman.pos, 1600.0f), m60Hits);
          if (bowman.belt < beltBefore) sound.fired(sim::WeaponType::DoorGun);
          for (const sim::Bowman::Hit& h : m60Hits) {
            const sim::Impact i{sim::WeaponType::DoorGun, h.pos, h.target, false, h.hind};
            if (joining) {
              bowmanHits.push_back(i);
              continue;
            }
            peerHits.push_back(i);
            if (h.hind == 0) sim::roundOnHind(heli, sim::WeaponType::DoorGun, h.pos, "A LONE BOWMAN");
            if (h.target >= 0)
              for (int k : targets.damage(h.target, sim::WeaponType::DoorGun, h.pos, sim::Killer::None)) {
                const sim::Vehicle& v = targets.list[size_t(k)];
                peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
              }
          }
          // The LAW: locking on, its rocket's flight; where it went off, to the host's game (his
          // alone: here, as there), as a 9M114's (sim::lawBlast on the Hinds).
          {
            const bool rocketWas = bowman.rocket.flying;
            sim::Bowman::Hit lawHit;
            const std::vector<sim::UnitShape> lawUnits = law || bowman.rocket.flying ? near(bowman.pos, sim::Bowman::kLawRange + 300.0f) : std::vector<sim::UnitShape>();
            if (bowman.stepLaw(pull && law, frameDt, terrain, hinds, lawUnits, lawHit, enemy.flaresInAir())) {
              const sim::Impact i{sim::WeaponType::Shturm, lawHit.pos, lawHit.target, false, lawHit.hind};
              std::printf("Network game: the LAW's rocket went off %.0f m away%s\n", glm::distance(lawHit.pos, bowman.pos),
                          lawHit.hind >= 0 ? " by a Hind" : lawHit.target >= 0 ? " on a unit" : "");
              if (joining) bowmanHits.push_back(i);
              else {
                peerHits.push_back({sim::WeaponType::Shturm, lawHit.pos});
                sim::lawBlast(heli, lawHit.pos);
                for (int k : targets.damage(lawHit.target, sim::WeaponType::Shturm, lawHit.pos, sim::Killer::None)) {
                  const sim::Vehicle& v = targets.list[size_t(k)];
                  peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
                }
              }
            }
            if (bowman.rocket.flying && !rocketWas) {
              sound.fired(sim::WeaponType::S8);
              // Fired without a lock (steered by his look): straight behind the rocket, as F7.
              if (bowman.rocket.guided()) bowArrow = true, bowArrowGone = -1.0;
            }
          }
          footShots = bowman.rounds;
          if (bowman.rocket.flying) footShots.push_back(lawRocket(bowman.rocket));
          glm::vec3 burst;
          if (bowman.step(pull && bowman.weapon == sim::Bowman::Weapon::Bow && !bowman.dead(), frameDt, terrain, hinds, units, burst)) {
            if (joining) {
              bowmanBursts.push_back(burst);
            } else {
              // His alone: it goes off here, as in the host's game.
              peerHits.push_back({sim::WeaponType::S8, burst});
              sim::arrowBlast(heli, burst);
              for (int k : targets.damage(-1, sim::WeaponType::S8, burst, sim::Killer::None)) {
                const sim::Vehicle& v = targets.list[size_t(k)];
                peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
              }
            }
          }
          // --bow: held in one pose.
          if (const size_t colon = bowPose.find(':'); !bowPose.empty()) {
            const float v = colon == std::string::npos ? 0.0f : std::clamp(float(std::atof(bowPose.c_str() + colon + 1)), 0.0f, 1.0f);
            if (bowPose.rfind("draw", 0) == 0) bowman.state = sim::Bowman::State::Drawing, bowman.draw = v;
            else if (bowPose.rfind("fit", 0) == 0) bowman.state = sim::Bowman::State::Fitting, bowman.fit = (1.0f - v) * sim::Bowman::kFitTime;
            else if (bowPose == "walk") bowman.state = sim::Bowman::State::Ready, bowman.walking = true, bowman.stride = std::fmod(float(simTime) * 15.8f, 6.2832f);
            else if (bowPose == "m60") bowman.weapon = sim::Bowman::Weapon::M60;
            else if (bowPose == "law") bowman.weapon = sim::Bowman::Weapon::Law;
            else if (bowPose == "law-follow") bowman.weapon = sim::Bowman::Weapon::Law, bowArrow = bowArrow || bowman.rocket.flying;
            else if (bowPose.rfind("hurt", 0) == 0) bowman.health = sim::Bowman::kHealth * (1.0f - v), bowman.blood = v, bowBloodSeed = 3.0f;
            else bowman.state = sim::Bowman::State::Ready;
          }
          // Each turn of the warhead onto its arrow creaks.
          const int turns = bowman.state == sim::Bowman::State::Fitting ? render::warheadTurns(bowman.fitting()) : 0;
          if (turns > bowTurns) sound.squeak();
          bowTurns = turns;
        } else {
          bowman.walking = false;
        }
      }
      // Paused, the view and the sights stay as they are.
      if (const glm::vec2 m = paused ? (input.takeMouse(), glm::vec2(0.0f)) : input.takeMouse(); m != glm::vec2(0.0f) && droneView) {
        droneStick = glm::clamp(droneStick + glm::vec2(-m.x, m.y) / kDroneStickTravel, glm::vec2(-1.0f), glm::vec2(1.0f));
      } else if (m != glm::vec2(0.0f)) {
        camera.turn(-m.x, m.y);
        mouseTurned = true;
        if (mirrored) gunnerIn.aiming = 1;
      }
      float yawIn = paused ? 0.0f : input.viewYaw(), pitchIn = paused ? 0.0f : input.viewPitch();
      // The gunner's game: in his seat the stick (the arrows) turns the sight; forward is down.
      if (mirrored && !paused && (camera.mode == render::ViewMode::Gunner || camera.mode == render::ViewMode::Door)) {
        yawIn = std::clamp(yawIn + input.controls().cyclicX, -1.0f, 1.0f);
        pitchIn = std::clamp(pitchIn - input.controls().cyclicY, -1.0f, 1.0f);
        if (yawIn != 0.0f || pitchIn != 0.0f) gunnerIn.aiming = 1;
      }
      // Flying the drone, the look keys and the hat steer it; the view at the door stays as it was.
      if (droneView) {
        // The stick held (with a little dead zone round the centre) and the keys on top of it.
        // Gentle round the centre for fine aiming, full at the edge (an expo curve, as on a model).
        // The joystick flies it as an aeroplane's: forward is down.
        const glm::vec2 held = glm::length(droneStick) > kDroneStickDead ? droneStick * (0.35f + 0.65f * glm::abs(droneStick)) : glm::vec2(0.0f);
        const glm::vec2 joy = input.droneStick();
        droneSteer = glm::clamp(held + glm::vec2(-yawIn - joy.x, pitchIn - joy.y), glm::vec2(-1.0f), glm::vec2(1.0f));
        if (mirrored && (yawIn != 0.0f || pitchIn != 0.0f || joy != glm::vec2(0.0f))) gunnerIn.aiming = 1;
      }
      else if (!paused) {
        // The door gun mount says where the gun is (and stays so: keys on top would only fight it).
        glm::vec2 mount;
        if (camera.mode == render::ViewMode::Door && input.doorMount(mount)) {
          camera.setDoorAim(mount.x, mount.y);
          yawIn = pitchIn = 0.0f;
          if (mirrored) gunnerIn.aiming = 1;
        }
        camera.update(frameDt, yawIn, pitchIn);
      }
      // The fire button in a drone's camera locks it on the target picked, once near enough.
      const bool fireNow = droneView && !paused && input.fire();
      if (fireNow && !droneFireWas) {
        const std::vector<int> marked = droneMarked();
        if (const sim::Projectile* d = weapons.interceptorOf(droneOwner())) {
          if (droneSel < 0 || std::find(marked.begin(), marked.end(), droneSel) == marked.end())
            heli.setMessage("NO TARGET", 1.5f);
          else if (glm::distance(targets.list[size_t(droneSel)].pos, d->pos) > sim::Weapons::kInterceptorLock)
            heli.setMessage("TOO FAR TO LOCK", 1.5f);
          else if (mirrored)
            ++gunnerIn.locks, gunnerIn.lockTarget = int16_t(droneSel), heli.setMessage("TARGET LOCKED", 2.0f);
          else if (weapons.lockInterceptor(droneOwner(), targets, droneSel))
            heli.setMessage("TARGET LOCKED", 2.0f);
        }
      }
      droneFireWas = fireNow;
    }
    // The WSO lays the gun on his target. Without one the gunner aims the turret with his helmet
    // sight (where he looks), and from the pilot's seat it points ahead. Turning the gunner's
    // view by hand takes over from the silicon WSO.
    // In the network game the gunner in the other game does all that, here only the pilot flies.
    const bool gunnerView = camera.mode == render::ViewMode::Gunner;
    // The crew over the network: the gunner (the first at the sight, else the first on board but
    // not at a door) has the gunner's weapons; each door's first man that door's gun.
    const Peer* gunnerPeer = nullptr;
    const Peer* doorPeer[2] = {nullptr, nullptr};
    if (!mirrored)
      for (const auto& p : peers) {
        if (p->wingman() || !p->aboard || !p->link.open()) continue;
        const uint8_t seat = p->input.seat;
        if (seat >= 2 && seat <= 3 && !doorPeer[seat - 2]) doorPeer[seat - 2] = p.get();
        if (seat == 1 && (!gunnerPeer || gunnerPeer->input.seat != 1)) gunnerPeer = p.get();
        if (seat == 0 && !gunnerPeer) gunnerPeer = p.get();
      }
    const bool manned = gunnerPeer != nullptr;
    static const net::GunnerInput kNobody{};
    const net::GunnerInput& remoteGunner = manned ? gunnerPeer->input : kNobody;
    weapons.setGunner(manned, remoteGunner.armed, remoteGunner.weapon, remoteGunner.trigger && !paused);
    weapons.clearDoorGunners();
    for (int side = 0; side < 2; ++side)
      if (doorPeer[side]) weapons.setDoorGunner(side, doorPeer[side]->input.dirBody, doorPeer[side]->input.trigger && !paused);
    // The interceptor drones: a player at a door flies them (the crew's over the network, sent
    // out and let go by their counts, steered as they say; ours at F4 or in the drone's view), and
    // while one is there the computer launches none.
    if (!mirrored) {
      // A drone of a player of the network game stays his: out of its camera it flies straight on
      // (no computer takes it over) until he comes back to it with G, lets it go, locks it or leaves.
      bool netFlown[4] = {};
      for (auto& p : peers) {
        if (p->wingman() || !p->aboard || !p->link.open()) continue;
        for (int owner : {1, 2, 3}) {
          if (p->droneOf[owner] && !weapons.interceptorOf(owner)) p->droneOf[owner] = false;
          if (!p->droneOf[owner]) continue;
          netFlown[owner] = true;
          if (p->input.seat != owner) weapons.steerInterceptor(owner, glm::vec2(0.0f));
        }
      }
      for (auto& p : peers) {
        if (p->wingman() || !p->aboard || p->input.seat < 1 || p->input.seat > 3) continue;
        const int owner = p->input.seat;
        if (p->input.launches != p->launchesSeen) {
          p->launchesSeen = p->input.launches;
          weapons.launchInterceptor(heli, owner);
          if (weapons.interceptorOf(owner)) p->droneOf[owner] = true, netFlown[owner] = true;
        }
        if (p->input.releases != p->releasesSeen) p->releasesSeen = p->input.releases, weapons.releaseInterceptor(owner);
        if (p->input.locks != p->locksSeen) p->locksSeen = p->input.locks, weapons.lockInterceptor(owner, targets, p->input.lockTarget);
        weapons.steerInterceptor(owner, paused ? glm::vec2(0.0f) : p->input.steer);
      }
      if (droneView) weapons.steerInterceptor(droneOwner(), paused ? glm::vec2(0.0f) : droneSteer);
      // Ours left for another view (F1, F6, ...) or the other door: let go, the crew takes it.
      const bool sightPeer = gunnerPeer && gunnerPeer->input.seat == 1;
      for (int owner : {1, 2, 3})
        if (!netFlown[owner] && !(owner == 1 ? sightPeer : doorPeer[owner - 2] != nullptr) && !(droneView && droneOwner() == owner))
          weapons.releaseInterceptor(owner);
      weapons.interceptorsCrewed = doorPeer[0] || doorPeer[1] || camera.mode == render::ViewMode::Door || droneView;
    } else {
      gunnerIn.steer = droneView ? droneSteer : glm::vec2(0.0f);
    }
    // The player at a door nobody else holds (F4) has that gun: it points where he looks and his
    // trigger fires it (and nothing else); back in his seat the gunner there fires by himself again.
    const bool ownDoor = !mirrored && camera.mode == render::ViewMode::Door && weapons.loadout().doorGun[camera.doorSide] &&
                         !weapons.doorManned(camera.doorSide);
    // --debug-door: the nearest enemy drone as each door gunner sees it.
    if (debugDoor && doorLogging && simTime >= doorDebugAt) {
      doorDebugAt = std::floor(simTime) + 1.0;
      int best = -1;
      for (size_t k = 0; k < targets.list.size(); ++k) {
        const sim::Vehicle& v = targets.list[k];
        if (v.destroyed || v.side != sim::Side::Enemy || sim::vehicleType(v.kind).cls != sim::UnitClass::Drone) continue;
        if (best < 0 || glm::distance(v.pos, heli.pos) < glm::distance(targets.list[size_t(best)].pos, heli.pos)) best = int(k);
      }
      if (best < 0) {
        std::printf("door %.0f s: no enemy drone\n", simTime);
      } else {
        const sim::Vehicle& v = targets.list[size_t(best)];
        const glm::vec3 rel = v.pos - heli.pos, rv = v.vel - heli.vel, body = glm::inverse(heli.orient) * rel;
        std::printf("door %.0f s: drone %.0f m (body x %.0f up %.0f fwd %.0f), %.0f m below, speed diff %.1f m/s, quality %.2f%s%s\n",
                    simTime, glm::length(rel), body.x, body.y, -body.z, -rel.y, glm::length(rv), sim::Weapons::doorShot(rel, rv),
                    weapons.doorGunnersFree ? "" : ", HOLD FIRE", weapons.anyDoorManned() ? ", door gun manned" : "");
        for (int side = 0; side < 2; ++side)
          if (weapons.loadout().doorGun[side])
            std::printf("  %s gun: %d rounds, %s\n", side ? "right" : "left", weapons.doorRounds(side),
                        sim::Weapons::doorField(side, body) ? "in its field" : "out of its field");
      }
      std::fflush(stdout);
    }
    const bool turning = manned ? remoteGunner.aiming != 0 : gunnerView && (input.viewYaw() != 0.0f || input.viewPitch() != 0.0f || mouseTurned);
    for (auto& p : peers) p->input.aiming = 0;
    if (turning && !mirrored && (wso.enabled() || wso.target() >= 0)) {
      wso.setEnabled(false, heli);
      wso.reset();
    }
    // The gunner's sight: where he looks from his seat (the remote gunner's, or ours there).
    const glm::vec3 gunnerEye = manned ? heli.pos + heli.orient * render::kGunnerEye : sightEye;
    const glm::vec3 gunnerDir = manned ? heli.orient * remoteGunner.dirBody : sightDir;
    const bool gunnerSighting = manned ? remoteGunner.seat != 0 : gunnerView;
    // Systems knocked out: no night vision without the low light TV, no magnified sight without
    // the infra-red sensor, nobody heard over a dead radio (the Hip's calls too).
    if (!heli.works(sim::System::Lltv)) nightVision = false;
    while (!heli.works(sim::System::Flir) && camera.magnification() > 1.01f) camera.zoom(1.25f);
    if (!heli.works(sim::System::Com)) spotter.reset();
    if (!paused && !mirrored && heli.works(sim::System::Com)) {
      // The Hip calls the drones to the pilot (the wingman hears it in the snapshots).
      if (!joining) spotter.step(heli, targets, simDt);
      if (wso.enabled() && wso.target() < 0) wso.designate(heli, targets, spotter.designated());
      wso.update(heli, weapons, targets, simDt);
    }
    // The R-60 as in the original: on the WSO's target, the WSO after fliers while it is selected.
    wso.airFirst = weapons.selected() == sim::WeaponType::R60;
    weapons.r60Track = wso.target();
    if (wso.target() < 0 && !mirrored) {
      // The turret follows the gunner's look by the helmet designator.
      const bool hmtd = heli.works(sim::System::Hmtd);
      if (manned && remoteGunner.seat && hmtd) weapons.aimTurret(remoteGunner.az, remoteGunner.el);
      else if (gunnerView && !manned && hmtd) weapons.aimTurret(camera.lookYaw(), camera.lookPitch());
      else weapons.aimTurret(0.0f, 0.0f);
    }
    // 9M114 guidance: the gunner's sight holds the WSO's target, or points where the gunner looks
    // (from the pilot's seat: along the nose). The sight turns at most 60 degrees off the nose.
    {
      const glm::vec3 sightAt = heli.pos + heli.orient * render::kGunnerEye;
      glm::vec3 los = heli.forward();
      if (wso.target() >= 0) los = glm::normalize(wso.aimPoint(targets) - sightAt);
      else if (gunnerSighting) los = gunnerDir;
      if (mirrored) {
      } else if (glm::dot(los, heli.forward()) > 0.5f && !heli.crashed) weapons.guide(sightAt, los);
      else weapons.unguided();
    }
    // Computer-controlled bomb release: with the silicon WSO on a target the bombs go, while the
    // trigger is held, when the point they would hit passes the target.
    bool bombComputer = false;
    float bombCountdown = -1.0f, bombLateral = 0.0f, bombDistance = 1.0f;
    if (weapons.armed() && sim::isBomb(weapons.selected()) && wso.enabled() && wso.target() >= 0) {
      glm::vec3 hit;
      const glm::vec2 v{heli.vel.x, heli.vel.z};
      const float gs = glm::length(v);
      if (gs > 5.0f && weapons.bombImpact(heli, hit)) {
        const glm::vec3 aim = wso.aimPoint(targets);
        const glm::vec2 to = glm::vec2(aim.x, aim.z) - glm::vec2(hit.x, hit.z), f = v / gs, right{-f.y, f.x};
        const float along = glm::dot(to, f);
        if (along > -30.0f) {
          bombComputer = true;
          bombCountdown = std::max(0.0f, along / gs);
          bombLateral = glm::dot(to, right);
          bombDistance = std::max(50.0f, glm::length(glm::vec2(aim.x, aim.z) - glm::vec2(heli.pos.x, heli.pos.z)));
        }
      }
    }
    bool trigger = (input.fire() || (!autoFire.empty() && !armOnly && simTime > 0.5)) && (!bombComputer || bombCountdown <= 0.05f);
    if (ownDoor) {
      weapons.setDoorGunner(camera.doorSide, glm::inverse(heli.orient) * sightDir, trigger && !paused);
      trigger = false;
    }
    if (droneView) trigger = false;  // there it locks the drone
    // The laser on a target, for a bomber's laser-guided bombs: the WSO's target, or without one
    // the ground where the sight looks, within the designator's field (manual: 14 degrees either
    // side of the nose, 20 up, 60 down).
    {
      const bool laser = heli.works(sim::System::Las);
      bool lit = laser && wso.target() >= 0 && wso.lasing(heli, targets);
      glm::vec3 spot = lit ? wso.aimPoint(targets) : glm::vec3(0.0f);
      if (!lit && !heli.crashed && laser) {
        const glm::vec3 body = glm::inverse(heli.orient) * gunnerDir;
        const float side = std::atan2(body.x, -body.z), up = std::atan2(body.y, glm::length(glm::vec2(body.x, body.z)));
        if (std::fabs(side) < glm::radians(14.0f) && up < glm::radians(20.0f) && up > -glm::radians(60.0f))
          for (float d = 20.0f; d < 6000.0f; d += 10.0f) {
            const glm::vec3 p = gunnerEye + gunnerDir * d;
            if (p.y < terrain.groundHeight(p.x, p.z)) {
              lit = true, spot = p;
              break;
            }
          }
      }
      if (!joining) missionState.laser(lit, spot);
    }
    impacts.clear();
    // The pilot's game: what the wingman's rounds did since the last frame.
    if (!joining) impacts.insert(impacts.end(), peerHits.begin(), peerHits.end()), peerHits.clear();
    net::World netWorld{heli, weapons, targets, enemy, troops, wso, spotter, missionState};
    // The wingman's game takes the pilot's Hind into its own objects, its own it flies.
    net::World peerWorld{peerHeli, peerWeapons, targets, enemy, peerTroops, peerWso, spotter, missionState};
    if (joining) {
      // The gunner's game: things move on as they were moving, until the next snapshot says how
      // they are.
      if (!paused) {
        net::Mirror::advance(wingmanGame ? peerWorld : netWorld, frameDt);
        for (sim::Projectile& p : netWingShots) p.pos += p.vel * float(frameDt), p.age += float(frameDt);
      }
      uint8_t type;
      std::vector<uint8_t> bytes;
      bool waiting = !page.empty() && netFrame.page.empty();
      while (running && link.receive(type, bytes)) {
        net::Reader r(bytes);
        if (type == net::Start) {
          net::StartInfo st;
          if (!st.read(r)) continue;
          joined = st;
          if (!st.game.empty()) joinGame = st.game;
          // Another mission: its world is set up anew.
          if (st.zone != cfg.getString("world", "zone", "") || st.mission != (haveMission ? mission.name : "")) {
            joinedPending = true;
            running = false;
          } else {
            applyStart(st);
            waiting = false;
          }
        } else if (type == net::Chat) {
          uint8_t number = 0;
          std::string who;
          r.pod(number), r.str(who);
          if (r.ok()) showChat(who, number);
        } else if (type == net::Wait) {
          page.clear();
          const uint32_t n = r.get<uint32_t>();
          for (uint32_t i = 0; i < n && r.ok(); ++i) r.str(page.emplace_back());
          waiting = true;
        } else if (type == net::Snapshot) {
          uint32_t generation = 0;
          net::Events ev;
          net::Frame frame;
          const std::string hipWas = peerHeli.message;
          // The wingman's dispensers and jammer are his own (the snapshot carries the pilot's).
          const bool ownFlares = enemy.autoFlares, ownChaff = enemy.autoChaff, ownJammer = enemy.jammer;
          if (!net::Mirror::read(r, generation, wingmanGame ? peerWorld : netWorld, frame, ev)) {
            std::fprintf(stderr, "Network game: a snapshot we cannot read (not the same game data?)\n");
            continue;
          }
          if (generation != joined.generation) continue;
          netFrame = frame;
          netWingStores = frame.wingStores, netWingShots = frame.wingShots;
          waiting = false;
          snapshotAt = SDL_GetTicks64();
          peerFlying = wingmanGame;
          // The game's other Hinds.
          while (otherHelis.size() < frame.others.size()) otherHelis.push_back(std::make_unique<sim::Helicopter>(terrain));
          otherHelis.resize(frame.others.size());
          doorsTaken = frame.doorsTaken;
          if (frame.doorMove && mirrored) {
            if (frame.doorMove == 3) {
              const char* why = crewSeat == render::ViewMode::Door ? "Both door guns are taken" : "The gunner's seat is taken";
              running = false, quitAll = !joinedFromMenu, leaveGame = joinedFromMenu;
              std::printf("Network game: %s\n", why);
              front.note(why);
            } else if (crewSeat == render::ViewMode::Door && crewDoor != frame.doorMove - 1) {
              crewDoor = frame.doorMove - 1;
              if (camera.mode == render::ViewMode::Door) camera.doorSide = crewDoor;
              startView = crewDoor ? "door-right" : "door-left";
              sayHello();
              heli.setMessage("THE DOOR WAS TAKEN - YOU ARE AT THE OTHER ONE", 3.0f);
            }
          }
          otherHeliIds.resize(frame.others.size());
          for (size_t k = 0; k < frame.others.size(); ++k) frame.others[k].apply(*otherHelis[k]), otherHeliIds[k] = frame.others[k].id;
          // Our crew hears the radio too.
          if (!peers.empty()) netEvents.radio.insert(netEvents.radio.end(), ev.radio.begin(), ev.radio.end());
          if (!wingmanGame) simTime = frame.simTime;  // the wingman's game keeps its own clock
          for (const sim::Impact& i : ev.impacts) impacts.push_back(i);
          for (const sim::EnemyEvent& e : ev.enemy) enemyEvents.push_back(e);
          // (The bowman hears none of the Hind's radio and cockpit.)
          if (!bowmanGame)
            for (int id : ev.radio) sound.radio(id);
          if (!wingmanGame && !bowmanGame) {
            for (sim::WeaponType w : ev.fired) weapons.fired.push_back(w);
            for (int id : ev.effects) sound.effect(id);
          } else {
            // The Hip's calls are for the whole flight; the enemy's hits on us the pilot's game counts.
            if (peerHeli.message != hipWas && peerHeli.message.rfind("HIP", 0) == 0) heli.setMessage(peerHeli.message, 3.0f);
            heli.damage = frame.wingmanDamage;
            enemy.autoFlares = ownFlares, enemy.autoChaff = ownChaff, enemy.jammer = ownJammer;
            enemy.flares = frame.wingmanFlares, enemy.chaff = frame.wingmanChaff;
            if (!frame.wingmanMessage.empty()) heli.setMessage(frame.wingmanMessage, 4.0f);
          }
        }
      }
      if (!waiting) page = netFrame.page;
      paused = waiting || netFrame.paused;
      if (mirrored) prevPos = heli.pos, prevOrient = heli.orient, accumulator = 0.0;
      // In the flight the snapshots come 60 times a second: none for long, the line is dead
      // (a cable out, the other computer asleep) even if the connection has not said so.
      if (link.open() && !waiting && SDL_GetTicks64() - snapshotAt > kNetSilenceMs) {
        std::printf("Network game: nothing from the pilot's game for %d s\n", int(kNetSilenceMs / 1000));
        link.close();
      }
      if (!link.open()) {
        std::printf("Network game: the pilot's game is gone\n");
        pilotGone = "THE CONNECTION TO THE PILOT'S GAME WAS LOST.";
        running = false;
      }
    }
    const bool someoneIn = std::any_of(peers.begin(), peers.end(), [](const auto& p) { return p->aboard || p->wingman(); });
    // What our rounds can hit besides the battlefield: the game's other Hinds.
    weapons.otherHinds.clear();
    if (!joining)
      for (size_t k = 0; k < wings.size(); ++k)
        if (wings[k]->flying) weapons.otherHinds.push_back({&wings[k]->heli, int8_t(k + 1)});
    if (wingmanGame) {
      if (peerFlying) weapons.otherHinds.push_back({&peerHeli, int8_t(0)});
      for (size_t k = 0; k < otherHelis.size(); ++k)
        if (otherHeliIds[k] >= 0) weapons.otherHinds.push_back({otherHelis[k].get(), otherHeliIds[k]});
    }
    // The pilot's game counts what a round (of the Hind `shooter`) did to the other Hinds.
    const auto onHinds = [&](const sim::Impact& i, int shooter, const std::string& by) {
      if (i.destroyed) return;
      const auto one = [&](int id, sim::Helicopter& h) {
        if (id == shooter || h.crashed) return;
        const bool direct = i.hind == id;
        if (direct) sim::roundOnHind(h, i.type, i.pos, by);
        if ((direct || (i.hind < 0 && sim::blastOnHind(h, i.type, i.pos, by))) && id > 0) wings[size_t(id - 1)]->message = h.message;
      };
      one(0, heli);
      for (size_t k = 0; k < wings.size(); ++k)
        if (wings[k]->flying) one(int(k + 1), wings[k]->heli);
    };
    if (!mirrored && !paused && !(waitGunner && !someoneIn)) {
      accumulator += simDt;
      int steps = 0;
      while (accumulator >= kPhysicsDt && steps < 48 * timeScale) {
        prevPos = heli.pos;
        prevOrient = heli.orient;
        heli.mass = sim::Helicopter::kRefMass - sim::referenceStoresMass() + weapons.storesMass();
        heli.step(input.controls(), kPhysicsDt);
        if (heli.weightOnWheels <= 0.0f && !heli.crashed) airborne += kPhysicsDt;
        const size_t before = impacts.size();
        weapons.step(heli, trigger, kPhysicsDt, impacts, &targets);
        if (!joining && !wings.empty())
          for (size_t k = before; k < impacts.size(); ++k) onHinds(impacts[k], 0, netNameUpper);
        if (quickRun)
          for (size_t k = before; k < impacts.size(); ++k)
            if (impacts[k].destroyed && impacts[k].target >= 0 && impacts[k].target < int(targets.list.size())) {
              const sim::Vehicle& v = targets.list[size_t(impacts[k].target)];
              const int points = sim::arcadePoints(v);
              if (arcadeRun) arcadeScore = std::max(0, arcadeScore + points);
              sim::ArcadeKill& kill = arcadeKills[sim::vehicleType(v.kind).name];
              ++kill.count, kill.points += points;
            }
        if (wingmanGame) {
          // The wingman's rounds: what they hit goes to the pilot's game, which applies it and
          // sends the blasts back with the rest (here nothing is drawn of them twice).
          for (size_t k = before; k < impacts.size(); ++k)
            if (!impacts[k].destroyed) peerHits.push_back(impacts[k]);
          impacts.resize(before);
        }
        weapons.groundReload(heli, kPhysicsDt);
        if (!joining) targets.step(kPhysicsDt), orders.step(heli, targets, terrain, kPhysicsDt, impacts);
        if (!joining) wingArms.step(targets, &weapons, kPhysicsDt, impacts);
        // A mid-air collision: both come down (as in the original), a drone's warhead goes off.
        if (!heli.crashed && !heli.noCrash) {
          std::vector<glm::vec3> pts;
          for (const glm::vec3& b : sim::Helicopter::outline()) pts.push_back(heli.pos + heli.orient * b);
          if (const int k = targets.airCollision(pts); k >= 0) {
            sim::Vehicle& v = targets.list[size_t(k)];
            targets.destroy(k, sim::Killer::Player);
            impacts.push_back({sim::WeaponType::S8, v.pos, k, true});
            if (!heli.crashed) collidedWith = sim::vehicleType(v.kind).name;
            heli.collide("MID-AIR COLLISION");
            std::printf("Mid-air collision with %s\n", sim::vehicleType(v.kind).name.c_str());
          }
        }
        if (!joining) {
          // The wingmen's Hinds are under fire too: what each is told (a launch, a failure) goes to him.
          std::vector<sim::Helicopter*> others;
          for (auto& w : wings) {
            others.push_back(w->flying ? &w->heli : nullptr);
            if (w->flying) w->heli.message.clear();
          }
          std::vector<const sim::Helicopter*> ourHelis{&heli};
          ourHelis.insert(ourHelis.end(), others.begin(), others.end());
          airCombat.step(targets, ourHelis, kPhysicsDt);
          enemy.step(heli, targets, kPhysicsDt, enemyEvents, others);
          for (auto& w : wings)
            if (w->flying && !w->heli.message.empty()) w->message = w->heli.message;
          battle.step(targets, heli.pos, kPhysicsDt, enemyEvents);
          // Our Hind down, the mission waits for the wingmen still flying (not those landed home).
          bool aloft = false;
          for (const auto& w : wings)
            if (w->flying && !w->heli.crashed &&
                !(missionState.atHome(w->heli.pos) && w->heli.weightOnWheels > 0.0f && glm::length(w->heli.vel) < 1.0f))
              aloft = true;
          missionState.othersAloft(aloft);
          missionState.step(heli, targets, kPhysicsDt);
        }
        accumulator -= kPhysicsDt;
        simTime += kPhysicsDt;
        ++steps;
      }
    }
    // The rest of the pilot's game: the mission, the troops, the mines (the gunner's gets what
    // came of them in the snapshots).
    if (!joining) {
    // Ground control and the bombers are on the radio.
    const bool com = heli.works(sim::System::Com);
    if (const int recon = missionState.reconReport(); recon && com) radio(recon > 0 ? gamedata::snd::GoodRecon : gamedata::snd::BadRecon);
    for (int line : missionState.takeRadio())
      if (com) radio(line);
    if (!paused) troops.step(heli, targets, terrain, simDt);
    for (int id : troops.takeRadio()) (id >= gamedata::snd::CargoDoorsOpen ? radio(id) : effect(id));
    missionState.setTroopsTask(troops.deployTask() || troops.evacuationTask(),
                               (!troops.deployTask() || troops.deployed()) && (!troops.evacuationTask() || troops.evacuated()),
                               troops.evacuationTask() && !troops.deployTask());
    // A bomber's laser-guided bombs coming down.
    for (glm::vec3 at : missionState.takeBombs()) {
      at.y = terrain.groundHeight(at.x, at.z);
      impacts.push_back({sim::WeaponType::Fab500, at});
      for (int k : targets.damage(-1, sim::WeaponType::Fab500, at, sim::Killer::Allied)) {
        const sim::Vehicle& v = targets.list[size_t(k)];
        impacts.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
      }
    }
    }
    for (sim::WeaponType w : weapons.fired) sound.fired(w);
    if (!peers.empty()) netEvents.fired.insert(netEvents.fired.end(), weapons.fired.begin(), weapons.fired.end());
    weapons.fired.clear();
    if (!joining) {
    // Mines: where a compartment was let go (the WSO says if it was on target), the mines lying
    // where they fell, and the enemy blown up on them.
    for (const sim::MineRelease& r : weapons.mineReleases) {
      if (!missionState.active()) continue;
      const bool good = missionState.mineRelease(r) > 0;
      radio(good ? gamedata::snd::GoodMineLaying : gamedata::snd::MinesOffTarget);
      heli.setMessage(good ? "GOOD MINE LAYING" : "MINES LAID OFF TARGET", 3.0f);
    }
    weapons.mineReleases.clear();
    for (const sim::Impact& i : impacts)
      if (i.type == sim::WeaponType::Mine && i.target < 0) targets.addMine(i.pos);
    // A drone diving onto its target: the blast of its 50 kg warhead.
    for (const glm::vec3& at : targets.takeStrikes()) impacts.push_back({sim::WeaponType::Fab250, at + glm::vec3(0, 0.5f, 0)});
    for (int k : targets.takeMineKills()) {
      const sim::Vehicle& v = targets.list[size_t(k)];
      impacts.push_back({sim::WeaponType::S5, v.pos + glm::vec3(0, 0.3f, 0)});
      impacts.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
    }
    // Quickstart over Ukraine: the raid over, the next one after a breath.
    // The stream of drones: one down a few seconds ago, or well past us, comes in again.
    if (!huntWave.empty() && !paused && simTime >= huntTick && (!heli.crashed || watchedHind)) {
      huntTick = simTime + 0.5;
      const glm::vec2 at{heli.pos.x, heli.pos.z};
      for (const auto& w : huntWave) {
        // The Banderol comes for where our Hind is now, not where it was when it set out.
        sim::Vehicle& m = targets.list[w.first];
        if (sim::isCruiseMissile(m.kind) && !m.destroyed && m.route.size() == 4 && m.next <= 2) {
          m.route[2].pos = at + huntBanderolSide;
          const glm::vec2 on = m.route[2].pos - m.route[1].pos;
          if (glm::length(on) > 1.0f) m.route[3].pos = m.route[2].pos + glm::normalize(on) * 40000.0f;
        }
        const sim::Vehicle& v = targets.list[w.first];
        const glm::vec2 to = at - glm::vec2(v.pos.x, v.pos.z);
        const bool gone = glm::length(to) > 9000.0f && glm::dot(to, glm::vec2(v.vel.x, v.vel.z)) < 0.0f;
        if ((v.destroyed && v.sinceDestroyed > 6.0f) || v.struck || v.spent || (!v.destroyed && (!v.moving() || gone))) {
          huntRng = huntRng * 1664525u + 1013904223u;
          huntSend(w, 3500.0f + 3500.0f * float(huntRng >> 8) / float(1u << 24));
        }
      }
    }
    if (missionState.finished() && !debriefing) {
      // Debriefing; a campaign goes on to its next mission after a success.
      debriefing = true;
      paused = true;
      // (Quickstart's flight is judged by itself, the mission's targets do not count.)
      const sim::MissionResult r = quickRun ? sim::arcadeResult(heli.crashed, arcadeScore) : missionState.result();
      const int arcadeBonus = !arcadeRun ? 0 : r == sim::MissionResult::Outstanding ? sim::kArcadeOutstanding : r == sim::MissionResult::Success ? sim::kArcadeSuccess : 0;
      arcadeScore += arcadeBonus;
      const std::string zone = cfg.getString("world", "zone", "afghan");
      const bool campaign = cfg.getString("world", "mission", "campaign") == "campaign" && !joining && !campaignFlown;
      if (campaign) {
        // Up a level, or a life less; the campaign's page tells it after the debriefing. Only
        // the progress goes to hind.ini, not this run's command line options.
        campaignFlown = true;
        gamedata::CampaignState c = gamedata::loadCampaign(cfg, zone, originalDir);
        c.mission = mission.name;
        gamedata::campaignResult(c, r == sim::MissionResult::Success || r == sim::MissionResult::Outstanding, gamedata::campaignLevels(originalDir, zone));
        campaignWon = c.show == gamedata::CampaignShow::Won;
        gamedata::saveCampaign(cfg, zone, c);
        gamedata::saveCampaign(baseCfg, zone, c);
        platform::Config saved;
        saved.load(configPath);
        saved.applyDefaults();
        gamedata::saveCampaign(saved, zone, c);
        saved.save(configPath);
      }
      // As the original's: the map, the Debrief button and its window, Exit back to the menus.
      // How the Hind was lost, in the original's words.
      std::string lost;
      if (heli.damage.shotDown) lost = heli.damage.lastHitBy.empty() ? "Hind was shot down." : "Hind was shot down by " + heli.damage.lastHitBy + ".";
      else if (!collidedWith.empty()) lost = "Hind collided with " + collidedWith + ".";
      else if (heli.crashCause == "DITCHED") lost = "Hind ditched in the water.";
      else if (heli.crashCause == "CRASHED") lost = "Hind crash landed. Your crash was a result of your vertical speed at the time of landing.";
      else if (heli.crashCause == "CRASHED - SIDEWAYS LANDING") lost = "Hind crash landed. Your crash was a result of your lateral speed at the time of landing.";
      else if (heli.crashCause == "ROLLED OVER") lost = "Hind crash landed. Your crash was a result of your roll at the time of landing.";
      else if (heli.crashed) lost = "Hind crashed.";
      debriefLines = quickRun ? sim::arcadeDebrief(heli.crashed, lost, arcadeRun, arcadeScore, arcadeBonus, airborne, arcadeKills) : missionState.debriefing(targets, lost);
      planner.start(mission, zoneOrigin, terrain, targets, loadout, sim::waypointTargets(mission, original, zoneObjects));
      planner.debrief((quickRun ? std::string(arcadeRun ? "Arcade" : "Invincible") : mission.title) + " - " + sim::MissionState::resultName(r), debriefLines);
      // (The mission's words on its own result would not fit Quickstart's.)
      if (!quickRun)
        speak(gamedata::speechName(mission.name, r == sim::MissionResult::Outstanding ? gamedata::SpeechPart::Outstanding
                                                 : r == sim::MissionResult::Success   ? gamedata::SpeechPart::Success
                                                                                       : gamedata::SpeechPart::Failure));
      // And the original's tune of it (the death's when the pilot did not live through it).
      const bool died = heli.crashed && (heli.damage.shotDown || glm::length(heli.crashVel) > 12.0f);
      menuMusic(died ? "DEATH" : r == sim::MissionResult::Outstanding ? "OUTSTAND" : r == sim::MissionResult::Success ? "SUCCESS" : "FAIL");
      planning = true;
      for (auto& p : peers) p->startSent = p->waitSent = false;
    }
    }
    // A crash: a fireball, and the helicopter breaks up into its parts, which fly apart.
    if (heli.crashed && !wasCrashed) {
      const glm::vec3 at = heli.pos + glm::vec3(0.0f, 1.0f, 0.0f);
      effects.impact({sim::WeaponType::Fab250, at});
      effects.impact({sim::WeaponType::S8, at, -1, true});
      if (meshes.original)
        debris.start(heli.pos, heli.orient, heli.crashVel, meshes.pieceCentres, meshes.pieceRadii);
      // Seen from outside, the camera circling round (the bowman sees it from where he stands).
      if (!bowmanGame) camera.startCrashOrbit();
    }
    wasCrashed = heli.crashed;
    // Down, a moment on our wreck; then round the Hinds still flying, the next when it is gone.
    if (heli.crashed && !bowmanGame) {
      if (downSince < 0.0) downSince = simTime;
      const auto left = hindsLeft();
      const bool still = std::any_of(left.begin(), left.end(), [&](const auto& h) { return h.first == watchedHind; });
      if (left.empty()) watchedHind = nullptr;
      else if (!still && simTime - downSince > 3.0) {
        watchedHind = left.front().first;
        camera.mode = render::ViewMode::External;
        heli.setMessage("WATCHING " + left.front().second, 3.0f);
      }
    } else {
      downSince = -1.0, watchedHind = nullptr;
    }
    if (!paused) debris.step(terrain, simDt);
    {
      std::vector<const sim::Helicopter*> others;
      for (const auto& w : wings)
        if (w->flying) others.push_back(&w->heli);
      if (peerFlying) others.push_back(&peerHeli);
      for (const auto& h : otherHelis) others.push_back(h.get());
      for (const sim::Helicopter* h : others) {
        bool& was = othersDown[h];
        if (h->crashed && !was) {
          const glm::vec3 at = h->pos + glm::vec3(0.0f, 1.0f, 0.0f);
          effects.impact({sim::WeaponType::Fab250, at});
          effects.impact({sim::WeaponType::S8, at, -1, true});
          // (Over the network only its pose comes, not how it hit: as if falling.)
          const glm::vec3 v = glm::length(h->crashVel) > 0.1f ? h->crashVel : glm::vec3(0.0f, -8.0f, 0.0f);
          if (meshes.original) wrecks.emplace_back().start(h->pos, h->orient, v, meshes.pieceCentres, meshes.pieceRadii);
        }
        was = h->crashed;
      }
      // A mission's vehicles, aircraft and helicopters break up the same way, wherever they were
      // hit (buildings, men and drones not). The Hinds by the pieces of ours.
      for (sim::Vehicle& v : targets.list) {
        if (!meshes.original || !v.destroyed || v.brokenUp || v.turretOff || v.sinceDestroyed > 0.5f || v.aboard || v.scenery) continue;
        const sim::VehicleType& t = sim::vehicleType(v.kind);
        if (t.cls != sim::UnitClass::Vehicle && t.cls != sim::UnitClass::Aircraft && t.cls != sim::UnitClass::Helicopter) continue;
        const glm::quat o = glm::quat_cast(v.toWorld());
        const glm::vec3 fall = v.vel + glm::vec3(0.0f, -2.0f, 0.0f);
        if (t.original.find("Mi_24") != std::string::npos) {
          v.brokenUp = true;
          // (On the ground its model has its wheels at its place, in the air its middle.)
          const bool aloft = v.pos.y > terrain.groundHeight(v.pos.x, v.pos.z) + 1.5f;
          const glm::vec3 middle = v.pos + glm::vec3(0.0f, aloft ? 0.0f : sim::Helicopter::kRestHeight, 0.0f);
          effects.impact({sim::WeaponType::Fab250, middle});
          effects.impact({sim::WeaponType::S8, middle, -1, true});
          wrecks.emplace_back().start(middle, o, fall, meshes.pieceCentres, meshes.pieceRadii);
        } else if (const render::VehicleRenderer::Pieces& pieces = vehicles.pieces(v.kind); !pieces.meshes.empty()) {
          // A tank stays where it is, burnt out; a Russian one's turret goes up in the air.
          const bool tank = t.arms == sim::Arms::TankGun;
          (tank ? v.turretOff : v.brokenUp) = true;
          unitWrecks.push_back({sim::Debris(), v.kind});
          unitWrecks.back().debris.start(v.pos, o, tank ? glm::vec3(0.0f, 22.0f, 0.0f) : fall, pieces.centres, pieces.radii);
        }
      }
      if (!paused) {
        for (sim::Debris& w : wrecks) w.step(terrain, simDt);
        for (UnitWreck& w : unitWrecks) w.debris.step(terrain, simDt);
      }
    }
    // The host's: the lone bowmen's M60 tracers as they report them.
    if (!joining && !bowmanGame) {
      footShots.clear();
      for (const auto& p : peers)
        if (p->bowman() && p->onFoot && p->link.open()) footShots.insert(footShots.end(), p->tracers.begin(), p->tracers.end());
    }
    if (!paused && bowmanGame) {
      // The lone bowman: what of the battlefield's rounds and blasts got him (his own game counts
      // it): those that went off near him, those that flew through him.
      float harm = 0.0f;
      for (const sim::Impact& i : impacts) harm += bowman.damageBy(i);
      for (const std::vector<sim::Projectile>& shots : {weapons.shots, wingShots()})
        for (const sim::Projectile& p : shots)
          if (p.age < 1e9f) harm += bowman.damageBy(p.type, p.pos - p.vel * float(frameDt), p.pos);
      if (harm > 0.0f) {
        if (bowman.blood < 0.2f) bowBloodSeed = float(std::fmod(simTime * 7.31, 97.0));
        if (bowman.hurt(harm)) {
          std::printf("Network game: the lone bowman is dead\n");
          bowMessage = "", bowMessageUntil = 0;
        }
      }
      // Dead a while, he is back where he started.
      if (bowman.stepDead(float(frameDt))) {
        placeBowman();
        bowMessage = "BACK AT THE START", bowMessageUntil = SDL_GetTicks64() + 3000;
      }
    }
    if (!paused) {
      for (const sim::Impact& i : impacts) effects.impact(i), sound.impact(i, targets, terrain);
      if (!peers.empty()) {
        netEvents.impacts.insert(netEvents.impacts.end(), impacts.begin(), impacts.end());
        netEvents.enemy.insert(netEvents.enemy.end(), enemyEvents.begin(), enemyEvents.end());
      }
      effects.follow(targets);
      effects.update(simDt, allShots());
      for (const sim::EnemyEvent& e : enemyEvents) effects.enemy(e), sound.enemyEvent(e, heli);
      effects.updateEnemy(simDt, enemy.shots(), enemy.flaresInAir());
      // Smoke from a damaged engine, flames from a burning one.
      for (int k = 0; k < 2; ++k)
        if (heli.damage.engine[k] < 60.0f && !heli.crashed)
          effects.engineSmoke(heli.pos + heli.orient * glm::vec3(k ? 0.9f : -0.9f, 1.0f, 1.4f), heli.vel, heli.engineFire(k));
    }
    enemyEvents.clear();
    if (!paused && frameDt > 0.0f) {
      glm::vec3 accel = (heli.vel - lastVel) / simDt + glm::vec3(0, sim::Helicopter::kGravity, 0);
      float g = std::clamp(glm::dot(accel, heli.up()) / sim::Helicopter::kGravity, -3.0f, 5.0f);
      gLoad += (g - gLoad) * std::min(1.0f, frameDt * 4.0f);
      lastVel = heli.vel;
    }
    float alpha = paused ? 1.0f : float(accumulator / kPhysicsDt);
    glm::vec3 pos = glm::mix(prevPos, heli.pos, alpha);
    glm::quat orient = glm::slerp(prevOrient, heli.orient, alpha);

    int dw, dh;
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    if (shotFbo) {
      glBindFramebuffer(GL_FRAMEBUFFER, shotFbo);
      dw = ww;
      dh = wh;
    }
    audio::Scene heard;
    heard.paused = paused || planning;
    heard.gLoad = gLoad;
    heard.wso = wso.enabled();
    if (wso.target() >= 0) {
      const sim::Vehicle& tv = targets.list[size_t(wso.target())];
      heard.wsoTarget = wso.target();
      heard.wsoTargetObject = sim::vehicleType(tv.kind).original;
      heard.wsoTargetEnemy = tv.side == sim::Side::Enemy;
    }
    // The threat warning knocked out lights nothing and says nothing.
    if (heli.works(sim::System::Twi)) heard.threat = sim::greatestThreat(heli, targets, terrain);
    if (missionState.active()) {
      const glm::vec2 to = missionState.steerPoint() - glm::vec2(heli.pos.x, heli.pos.z);
      heard.waypoint = heli.works(sim::System::Nav) ? missionState.waypoint() : -1;
      heard.waypointBearing = glm::degrees(std::atan2(to.x, -to.y));
      heard.waypointDistance = glm::length(to);
    }
    if (bowmanGame) {
      audio::Scene quiet;
      quiet.paused = heard.paused;
      heard = quiet;
    }
    float clockHour = 0.0f;
    {
      const float startHour = haveMission ? mission.startHour : freeHour;
      clockHour = std::fmod(startHour + float(simTime) / 3600.0f, 24.0f);
      pal = render::skyAt(clockHour, haveMission ? mission.weather : freeWeather, skyColours);
    }
    if (!hudChosen) hudBlack = hudBlackAlways || render::cockpitLight(pal).g > 0.99f, hudChosen = true;
    if (planning) {
      int winW, winH;
      SDL_GetWindowSize(window, &winW, &winH);
      planner.setMouseScale(shotFbo || winW <= 0 ? glm::vec2(1.0f) : glm::vec2(float(dw) / float(winW), float(dh) / float(winH)));
      // The planner's 3D view: the world drawn from its camera into its window's picture.
      if (const menu::Planner::View3D v = planner.view3D(); v.open && v.size.x > 0 && v.size.y > 0) {
        render::Texture& tex = planner.view3DTarget();
        GLint drawn = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawn);
        if (!view3DFbo) glGenFramebuffers(1, &view3DFbo), glGenRenderbuffers(1, &view3DDepth);
        if (!tex.loaded() || tex.width != v.size.x || tex.height != v.size.y) {
          tex.uploadRgb(v.size.x, v.size.y, std::vector<unsigned char>(size_t(v.size.x) * size_t(v.size.y) * 3, 0));
          glBindRenderbuffer(GL_RENDERBUFFER, view3DDepth);
          glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, v.size.x, v.size.y);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, view3DFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, view3DDepth);
        glViewport(0, 0, v.size.x, v.size.y);
        glClearColor(pal.horizon.r, pal.horizon.g, pal.horizon.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float aspect3D = float(v.size.x) / float(v.size.y);
        const glm::mat4 proj = v.ortho ? glm::ortho(-0.5f * v.span, 0.5f * v.span, -0.5f * v.span / aspect3D,
                                                    0.5f * v.span / aspect3D, 10.0f, 10000.0f)
                                       : glm::perspective(glm::radians(50.0f), aspect3D, 1.5f, viewDistance * 1.15f);
        const glm::mat4 vp = proj * glm::lookAt(v.eye, v.at, v.up);
        glEnable(GL_DEPTH_TEST);
        if (!v.ortho) sky.draw(vp, pal, clockHour, float(v.size.y) / 1080.0f);
        scene.use();
        scene.set("uViewProj", vp);
        scene.set("uSunDir", pal.sunDir);
        scene.set("uFogColor", pal.horizon);
        scene.set("uFogDist", v.ortho ? 1e6f : std::min(viewDistance * 0.75f, pal.fogDist));
        scene.set("uLight", pal.light);
        scene.set("uAmbient", pal.ambient);
        scene.set("uDiffuse", pal.diffuse);
        scene.set("uCamPos", v.eye);
        terrainRenderer.draw(scene, v.ortho ? v.at : v.eye, vp);
        vehicles.draw(scene, targets, v.ortho ? v.at : v.eye, viewDistance, float(simTime));
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(drawn));
        glViewport(0, 0, dw, dh);
      }
      planner.draw(dw, dh, frameDt);
    } else {
      glViewport(0, 0, dw, dh);
      float aspect = dh > 0 ? float(dw) / float(dh) : 1.0f;
      bool cockpit = camera.inside();
      bool gunnerSeat = camera.mode == render::ViewMode::Gunner;
      render::CockpitOverlay& picture = pictures[gunnerSeat ? 1 : 0];
      // The picture slides sideways as the view turns, at the rate the world does in the middle.
      const float pan = -camera.lookYaw() * (picture.loaded() ? picturePerTan : 0.0f);
      // ... and slides down as it tilts up (up as it tilts down), as the world does there.
      const float lift = picture.loaded() ? picturePerTan * std::tan(camera.lookPitch() - camera.seatPitch()) : 0.0f;
      // Through the gunner's sight magnified only the world is seen, narrower.
      const float magnification = camera.magnification();
      const bool sightOnly = magnification > 1.01f;
      float fov = 2.0f * std::atan(std::tan(fovY * 0.5f) / magnification);
      const bool door = camera.mode == render::ViewMode::Door;
      // The picture fades as the view tilts off level or (the gunner's, his view turns further
      // than the picture goes) nears its end aside: the modelled cockpit, or for the gunner none,
      // under it. (The pilot's view stops at the picture's ends.)
      const float pictureShown = cockpit && !door && !sightOnly && picture.loaded()
                                     ? camera.pictureShown() * std::clamp((picture.maxPan() - std::fabs(pan)) / (gunnerSeat ? 80.0f : 1.0f) + (gunnerSeat ? 0.0f : 1.0f), 0.0f, 1.0f)
                                     : 0.0f;
      bool usePicture = pictureShown > 0.0f;
      const bool pictureWhole = pictureShown >= 1.0f;
      // The gunner looking off his cockpit picture (up, down, far aside to aim) sees through his
      // helmet sight only, not a modelled cockpit.
      const bool noCockpit = sightOnly || (gunnerSeat && cockpit && !door && !pictureWhole && picture.loaded());
      if (heli.message != lastMessage) {
        if (!lastMessage.empty()) prevMessage = lastMessage, prevMessageUntil = simTime + 6.0;
        lastMessage = heli.message;
      }
      std::string displayText = heli.message.empty() ? sim::flightModelName(heli.model) : heli.message;
      if (!heli.message.empty() && !prevMessage.empty() && simTime < prevMessageUntil && prevMessage != heli.message)
        displayText = prevMessage + "\n" + displayText;
      // A player's message over the rest, the last of those under it.
      if (SDL_GetTicks64() < chatUntil) displayText = (heli.message.empty() ? std::string() : heli.message + "\n") + chatLine;
      // The Hip's call keeps the upper line while its drone flies, worked out afresh each frame;
      // the rest take turns on the lower one.
      if (const std::string hip = heli.works(sim::System::Com) ? spotter.line(heli, targets) : std::string(); !hip.empty()) {
        const std::string last = SDL_GetTicks64() < chatUntil ? chatLine : heli.message.empty() ? sim::flightModelName(heli.model) : heli.message;
        displayText = hip + "\n" + last;
      }
      // The weapon last launched (rockets, missiles, bombs; not the guns' rounds): the one
      // followed stays followed to where it comes down (found again each frame where it should
      // have flown to), shown there for two seconds; then the newest in the air.
      {
        auto gun = [](sim::WeaponType t) { return t == sim::WeaponType::YakB || t == sim::WeaponType::Upk23 || t == sim::WeaponType::DoorGun; };
        const sim::Projectile* best = nullptr;
        if (weaponFlying) {
          const glm::vec3 want = weaponAt + weaponVel * frameDt;
          float d = 3.0f + glm::length(weaponVel) * frameDt * 0.5f;
          for (const sim::Projectile& s : weapons.shots)
            if (!gun(s.type) && glm::distance(s.pos, want) < d) d = glm::distance(s.pos, want), best = &s;
          if (!best) weaponFlying = false, weaponDown = simTime;
        }
        if (!best && (!weaponSeen || simTime - weaponDown > 2.0))
          for (auto s = weapons.shots.rbegin(); s != weapons.shots.rend() && !best; ++s)
            if (!gun(s->type)) best = &*s;
        if (best) {
          weaponAt = best->pos, weaponVel = best->vel, weaponFlying = weaponSeen = true;
          if (glm::length(best->vel) > 1.0f) weaponDir = glm::normalize(best->vel);
        }
      }
      render::CameraState cs = camera.compute(pos, orient, terrain);
      // The original's other external views.
      switch (camera.mode) {
        case render::ViewMode::Weapon:
          if (weaponSeen) cs = camera.orbit(weaponAt, std::atan2(weaponDir.x, -weaponDir.z), terrain, 0.3f);
          break;
        case render::ViewMode::Drone: {
          // From the interceptor this door flies, along its flight; when it is gone (off by its
          // target, into the ground, its battery flat; not yet out over the network), a moment
          // on where it was, then back at the door.
          if (const sim::Projectile* d = weapons.interceptorWatched(droneOwner())) {
            droneEye = d->pos;
            if (glm::length(d->vel) > 1.0f) droneDir = glm::normalize(d->vel);
            droneGoneAt = -1.0;
          } else if (simTime - droneLaunchedAt > 2.0 && droneGoneAt < 0.0) {
            droneGoneAt = simTime;
          }
          if (droneGoneAt >= 0.0 && simTime - droneGoneAt > 1.5) camera.mode = droneHome();
          if (droneEye != glm::vec3(0.0f)) cs = render::Camera::look(droneEye, droneEye + droneDir * 100.0f, terrain);
          break;
        }
        case render::ViewMode::Target: {
          // From behind the target (the WSO's, or where the weapon is going) towards the weapon
          // coming, or towards us without one in the air.
          const glm::vec3 from = weaponFlying ? weaponAt : pos;
          const glm::vec3 tgt = wso.target() >= 0 ? wso.aimPoint(targets) : weaponFlying ? weaponAt + weaponDir * 150.0f : pos + heli.forward() * 300.0f;
          const glm::vec3 away = glm::normalize(glm::vec3(tgt.x - from.x, 0.0f, tgt.z - from.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
          cs = render::Camera::look(tgt + away * 25.0f + glm::vec3(0.0f, 6.0f, 0.0f), from, terrain);
          break;
        }
        case render::ViewMode::Lock:
          if (wso.target() < 0) {
            camera.mode = render::ViewMode::External;
          } else {
            // Past the helicopter towards its target: heading straight at it, we see ourselves from behind.
            const glm::vec3 tgt = wso.aimPoint(targets);
            const glm::vec3 back = glm::normalize(pos - tgt + glm::vec3(1e-4f, 0.0f, 0.0f));
            cs = render::Camera::look(pos + back * camera.distance() + glm::vec3(0.0f, 5.0f, 0.0f), tgt, terrain);
          }
          break;
        case render::ViewMode::Bowman:
          cs.eye = bowman.eye();
          cs.view = glm::lookAt(cs.eye, cs.eye + bowman.look(), bowman.up());
          if (bowOrbit) cs = camera.orbit(bowman.pos + glm::vec3(0.0f, 1.0f, 0.0f), bowman.yaw, terrain, 0.12f);
          if (bowArrow) {
            // (Behind the LAW's rocket while one flies, else the arrow.)
            const bool rocket = bowman.rocket.flying;
            if (rocket || bowman.state == sim::Bowman::State::Flying) {
              const glm::vec3 vel = rocket ? bowman.rocket.vel : bowman.arrowVel;
              bowArrowAt = rocket ? bowman.rocket.pos : bowman.arrowPos, bowArrowGone = -1.0;
              if (glm::length(vel) > 1.0f) bowArrowDir = glm::normalize(vel);
            } else if (bowArrowGone < 0.0) {
              bowArrowGone = simTime;
            } else if (simTime - bowArrowGone > 2.0) {
              bowArrow = false;
            }
            const bool lawView = bowman.rocket.flying || bowman.weapon == sim::Bowman::Weapon::Law;
            if (bowArrow && lawView) {
              // The LAW's rocket: from behind it, a little above and aside (its smoke not in the
              // way), looking ahead along its flight (what it flies at in sight, either side);
              // about its middle never nearer than 3.5 m, however the view is turned or zoomed (the
              // scene's near plane is 1.5 m off).
              const glm::vec3 mid = bowArrowAt - bowArrowDir * (render::kLawRocketLength * 0.5f);
              const float heading = std::atan2(bowArrowDir.x, -bowArrowDir.z);
              cs = camera.orbit(mid, heading - 0.45f + render::Camera::kDefaultOrbitYaw, terrain, std::max(0.06f, 4.0f / camera.distance()));
              cs.eye.y += 1.0f;
              // (Turned round by him towards its front: the less ahead, the more at the rocket.)
              const float ahead = std::max(0.0f, std::cos(camera.orbitYaw() - render::Camera::kDefaultOrbitYaw));
              cs.view = glm::lookAt(cs.eye, glm::mix(mid, bowArrowAt + bowArrowDir * 10.0f, ahead * ahead), glm::vec3(0, 1, 0));
            } else if (bowArrow) {
              cs = camera.orbit(bowArrowAt, std::atan2(bowArrowDir.x, -bowArrowDir.z), terrain, 0.12f);
            }
          }
          break;
        case render::ViewMode::Remote:
        case render::ViewMode::Spectator:
          cs = render::Camera::look(camera.anchor, pos + glm::vec3(0.0f, 1.0f, 0.0f), terrain);
          break;
        case render::ViewMode::Watch:
          if (watchUnit < 0) {
            // None chosen yet: the nearest.
            float best = 1e30f;
            for (size_t k = 0; k < targets.list.size(); ++k) {
              const sim::Vehicle& v = targets.list[k];
              if (v.destroyed || v.aboard || v.scenery) continue;
              if (const float d = glm::distance(v.pos, pos); d < best) best = d, watchUnit = int(k);
            }
          }
          if (watchUnit >= 0 && watchUnit < int(targets.list.size())) {
            const sim::Vehicle& v = targets.list[size_t(watchUnit)];
            const sim::VehicleType& t = sim::vehicleType(v.kind);
            cs = camera.orbit(glm::vec3(v.toWorld() * glm::vec4((t.lo + t.hi) * 0.5f, 1.0f)), v.heading, terrain);
          } else {
            camera.mode = render::ViewMode::External;
          }
          break;
        default: break;
      }
      if (watchedHind && camera.mode == render::ViewMode::External) {
        const glm::vec3 f = watchedHind->forward();
        cs = camera.orbit(watchedHind->pos + glm::vec3(0.0f, 1.0f, 0.0f), std::atan2(f.x, -f.z), terrain);
      }
      if (shotCam) {
        float best = 1e30f;
        if (shotCamUnit < 0)
          for (size_t k = 0; k < targets.list.size(); ++k) {
            const sim::Vehicle& v = targets.list[k];
            if (!v.destroyed && v.side == sim::Side::Enemy && sim::vehicleType(v.kind).cls == sim::UnitClass::Drone)
              if (const float d = glm::distance(v.pos, pos); d < best) best = d, shotCamUnit = int(k);
          }
        if (shotCamUnit >= 0 && (!targets.list[size_t(shotCamUnit)].destroyed || targets.list[size_t(shotCamUnit)].fallingDrone()))
          shotCamDrone = targets.list[size_t(shotCamUnit)].pos;
        // Till then standing still where it will be (--shot-eye): the Hind flies up to it.
        const glm::vec3 onHind = pos + orient * shotCamAt, door = pos + orient * sim::Weapons::doorPivot;
        glm::vec3 eye = onHind;
        if (simTime < shotEye.w) {
          const float k = shotEyeGlide > 0.0f ? glm::smoothstep(shotEye.w - shotEyeGlide, shotEye.w, float(simTime)) : 0.0f;
          eye = glm::mix(glm::vec3(shotEye), onHind, k);
          // From afar zoomed in on the Hind, its 18 m filling the picture's height or so, out to
          // the field of view as the camera comes up to it.
          fov = std::min(fov, std::max(glm::radians(6.0f), 2.0f * std::atan(14.0f / glm::distance(eye, door))));
        }
        glm::vec3 at = shotCamDoor || shotCamDrone == glm::vec3(0.0f) ? door : shotCamDrone;
        if (shotCamDoor && shotPan.y > shotPan.x && shotCamDrone != glm::vec3(0.0f)) {
          // Turning by the direction, not the point: the drone is much further than the door.
          const float k = glm::smoothstep(shotPan.x, shotPan.y, float(simTime)) * shotPan.w;
          const glm::vec3 d = glm::normalize(glm::mix(glm::normalize(door - eye), glm::normalize(shotCamDrone - eye), k));
          at = eye + d * 10.0f;
          fov = glm::mix(fov, glm::radians(shotPan.z), k);
        }
        cs = render::Camera::look(eye, at, terrain);
      }
      lastEye = cs.eye;
      sightEye = cs.eye;
      sightDir = -glm::vec3(glm::inverse(cs.view)[2]);
      heard.ear = cs.eye;
      heard.earRight = glm::vec3(glm::inverse(cs.view)[0]);
      heard.inside = cockpit;

      glClearColor(pal.horizon.r, pal.horizon.g, pal.horizon.b, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      // Pass 1: the world, with a near plane far enough out for good depth precision.
      glm::mat4 projWorld = glm::perspective(fov, aspect, 1.5f, viewDistance * 1.15f);
      glm::mat4 vpWorld = projWorld * cs.view;
      sky.draw(vpWorld, pal, clockHour, float(dh) / 1080.0f);
      scene.use();
      scene.set("uViewProj", vpWorld);
      scene.set("uSunDir", pal.sunDir);
      scene.set("uFogColor", pal.horizon);
      scene.set("uFogDist", std::min(viewDistance * 0.75f, pal.fogDist));
      scene.set("uLight", pal.light);
      scene.set("uAmbient", pal.ambient);
      scene.set("uDiffuse", pal.diffuse);
      scene.set("uCamPos", cs.eye);
      terrainRenderer.draw(scene, cs.eye, vpWorld, firstFrame);
      firstFrame = false;
      vehicles.draw(scene, targets, cs.eye, viewDistance, float(simTime), ukrainianLivery, &terrain);
      // What our AI Hinds carry (sim::WingArms) under their wings, as on ours: flying, their centre
      // is where ours has it (the original's wings higher on its longer gear); parked, their
      // wheels stand at 0.
      for (const sim::WingArms::Stores& ws : wingStores()) {
        if (ws.index < 0 || size_t(ws.index) >= targets.list.size()) continue;
        const sim::Vehicle& v = targets.list[size_t(ws.index)];
        if (v.destroyed || v.aboard || glm::distance(v.pos, cs.eye) > 3000.0f) continue;
        const bool flying = (v.moving() && v.delay <= 0.0f && glm::length(v.vel) > 1.0f) || v.hover;
        const float lift = (meshes.original ? render::kOriginalMi24Lift : 0.0f) + (flying ? 0.0f : sim::Helicopter::kRestHeight);
        scene.set("uModel", glm::translate(v.toWorld(), glm::vec3(0.0f, lift, 0.0f)));
        meshes.drawStores(ws);
      }
      scene.set("uModel", glm::mat4(1.0f));
      {
        // The men on foot, and the lone bowmen about (his own game draws not him) with their
        // arrows in the air.
        const std::vector<sim::Walker>& men = troops.walkers();
        std::vector<net::BowmanPose> bowmen = joining ? netFrame.bowmen : std::vector<net::BowmanPose>();
        if (!joining)
          for (const auto& p : peers)
            if (p->bowman() && p->onFoot && p->link.open()) bowmen.push_back(p->pose);
        if (bowmanGame) bowmen.push_back(net::BowmanPose::of(bowman));  // (last: his arrow only)
        render::MeshBuilder ab;
        for (size_t k = 0; k < bowmen.size(); ++k) {
          const net::BowmanPose& b = bowmen[k];
          // (Dead, on his back.)
          const glm::mat4 lying = b.dead ? glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.15f, 0.0f)) * glm::rotate(glm::mat4(1.0f), 1.5708f, glm::vec3(1, 0, 0))
                                         : glm::mat4(1.0f);
          if (!(bowmanGame && !bowOrbit && !bowArrow && k + 1 == bowmen.size()))
            render::buildBowmanFigure(ab, glm::translate(glm::mat4(1.0f), b.pos) * glm::rotate(glm::mat4(1.0f), -b.yaw, glm::vec3(0, 1, 0)) * lying,
                                      b.stride, b.walking && !b.dead ? 1.0f : 0.0f, b.dead ? 0.0f : b.draw, b.dead ? 0.0f : b.pitch, b.weapon);
          if (!b.flying || glm::length(b.arrowVel) < 1.0f) continue;
          const glm::vec3 f = glm::normalize(b.arrowVel);
          const glm::vec3 r = glm::normalize(glm::cross(f, std::fabs(f.y) < 0.98f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
          render::buildArrow(ab, glm::mat4(glm::vec4(r, 0.0f), glm::vec4(glm::cross(r, f), 0.0f), glm::vec4(-f, 0.0f), glm::vec4(b.arrowPos, 1.0f)));
        }
        figures.draw(scene, men);
        if (!ab.verts.empty()) {
          meshes.arrows.upload(ab.verts);
          scene.set("uModel", glm::mat4(1.0f));
          meshes.arrows.draw();
        }
      }
      // The network game's other Hinds: the wingmen's in the host's game; the host's and the
      // other wingmen's in a wingman's; the wingmen's to a crew. Their stores when we know them.
      std::vector<std::pair<const sim::Helicopter*, const sim::Weapons*>> otherHinds;
      for (const auto& w : wings)
        if (w->flying) otherHinds.push_back({&w->heli, &w->weapons});
      if (peerFlying) otherHinds.push_back({&peerHeli, &peerWeapons});
      if (bowmanGame) otherHinds.push_back({&heli, &weapons});  // the host's Hind, far off among the rest
      for (const auto& h : otherHelis) otherHinds.push_back({h.get(), nullptr});
      for (const auto& [hp, hw] : otherHinds) {
        if (hp->crashed) continue;
        const sim::Helicopter& h = *hp;
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), h.pos) * glm::mat4_cast(h.orient);
        scene.set("uModel", m);
        // Every Hind of the game is armed as the host's: door guns in open doors, their gunners
        // where we know where they aim (else straight out).
        const sim::Loadout& lo = hw ? hw->loadout() : weapons.loadout();
        const bool gunsOut = lo.doorGun[0] || lo.doorGun[1];
        if (meshes.original) {
          glEnable(GL_CULL_FACE);
          scene.set("uLivery", ukrainianLivery ? 1.0f : 0.0f);
          (h.gearPos > 0.5f ? meshes.origGearDown : meshes.origGearUp).draw();
          (gunsOut ? meshes.origDoorsOpen : meshes.origDoorsShut).draw();
          (gunsOut ? meshes.origBodyOpen : meshes.origBody).draw();
          meshes.origTurret.draw();
          scene.set("uLivery", 0.0f);
          if (gunsOut) {
            meshes.origCabin.draw();
            for (int side = 0; side < 2; ++side)
              if (lo.doorGun[side]) meshes.origGunner[side].draw();
          }
          glDisable(GL_CULL_FACE);
          scene.set("uModel", glm::translate(m, glm::vec3(0.0f, render::kOriginalMi24Lift, 0.0f)));
        } else {
          meshes.exterior.draw();
          meshes.chinGun.draw();
          if (h.gearPos > 0.5f) meshes.gear.draw();
        }
        if (hw) meshes.drawStores(*hw);
        if (gunsOut) {
          scene.set("uModel", m);
          render::MeshBuilder gb;
          for (int side = 0; side < 2; ++side) {
            if (!lo.doorGun[side]) continue;
            const glm::vec3 aim = hw ? hw->doorAim(side) : glm::vec3(side ? 1.0f : -1.0f, -0.1f, 0.0f);
            if (meshes.original) render::buildDoorGun(gb, side, aim, render::OriginalMi24::doorGunPivot(side), true, false);
            else render::buildDoorGun(gb, side, aim);
          }
          meshes.doorView.upload(gb.verts);
          meshes.doorView.draw();
        }
        const glm::mat4 hub = m * glm::translate(glm::mat4(1.0f), render::kMainRotorHub);
        scene.set("uModel", hub * glm::rotate(glm::mat4(1.0f), -h.rotorAngle, glm::vec3(0, 1, 0)));
        (h.rotorRpm < 30.0f ? meshes.rotorStopped : meshes.rotor).draw();
        scene.set("uModel", m * glm::translate(glm::mat4(1.0f), render::kTailRotorHub) *
                                glm::rotate(glm::mat4(1.0f), h.tailRotorAngle, glm::vec3(1, 0, 0)));
        meshes.tailRotor.draw();
        scene.set("uModel", glm::mat4(1.0f));
      }
      // The other Hinds' wrecks (and to the bowman the host's).
      std::vector<const sim::Debris*> wrecked;
      for (const sim::Debris& w : wrecks) wrecked.push_back(&w);
      if (bowmanGame) wrecked.push_back(&debris);
      for (const UnitWreck& w : unitWrecks) vehicles.drawPieces(scene, w.debris, w.kind, cs.eye, viewDistance);
      for (const sim::Debris* w : wrecked)
        for (size_t i = 0; i < w->pieces().size() && i < meshes.pieces.size(); ++i) {
          scene.set("uModel", glm::translate(glm::mat4(1.0f), w->pieces()[i].pos) * glm::mat4_cast(w->pieces()[i].orient));
          if (meshes.pieceCull[i]) glEnable(GL_CULL_FACE);
          meshes.pieces[i].draw();
          glDisable(GL_CULL_FACE);
        }
      scene.set("uModel", glm::mat4(1.0f));
      {
        // Interceptor drones in the air near enough to make out (further off a dark streak
        // among the effects), along their flight and banked into their turn; not the one whose
        // camera we look through.
        const sim::Projectile* flown = camera.mode == render::ViewMode::Drone ? weapons.interceptorOf(droneOwner()) : nullptr;
        render::MeshBuilder lb;
        auto litavrs = [&](const std::vector<sim::Projectile>& shots) {
          for (const sim::Projectile& s : shots) {
            if (s.type != sim::WeaponType::Interceptor || &s == flown || glm::distance(s.pos, cs.eye) > render::kInterceptorModelRange ||
                glm::length(s.vel) < 1.0f)
              continue;
            const glm::vec3 f = glm::normalize(s.vel);
            const glm::vec3 r = glm::normalize(glm::cross(f, std::fabs(f.y) < 0.98f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
            const glm::mat4 frame(glm::vec4(r, 0.0f), glm::vec4(glm::cross(r, f), 0.0f), glm::vec4(-f, 0.0f), glm::vec4(s.pos, 1.0f));
            render::buildLitavr(lb, glm::rotate(frame, 1.2f * s.turning.x, glm::vec3(0, 0, 1)), s.age * 400.0f);
          }
        };
        // And the missiles near enough, the same (their flames among the effects): the R-60, and
        // the 9M114 rolling on its fins.
        auto r60s = [&](const std::vector<sim::Projectile>& shots) {
          for (const sim::Projectile& s : shots) {
            const bool r60 = s.type == sim::WeaponType::R60;
            if ((!r60 && s.type != sim::WeaponType::Shturm) || glm::distance(s.pos, cs.eye) > render::kMissileModelRange || glm::length(s.vel) < 1.0f)
              continue;
            const glm::vec3 f = glm::normalize(s.vel);
            const glm::vec3 r = glm::normalize(glm::cross(f, std::fabs(f.y) < 0.98f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
            const glm::vec3 mid = s.pos - f * (r60 ? render::kR60Half : render::kShturmHalf);  // (its position is its nose)
            const glm::mat4 frame(glm::vec4(r, 0.0f), glm::vec4(glm::cross(r, f), 0.0f), glm::vec4(-f, 0.0f), glm::vec4(mid, 1.0f));
            if (r60) render::buildR60(lb, frame);
            else render::buildShturm(lb, glm::rotate(frame, s.age * render::kShturmSpin * glm::two_pi<float>(), glm::vec3(0, 0, 1)),
                                     render::kShturmSpin * glm::two_pi<float>() / 60.0f);
          }
        };
        // The lone bowmen's LAW rockets (among footShots), nose first.
        for (const sim::Projectile& s : footShots) {
          if (!s.law || glm::distance(s.pos, cs.eye) > 400.0f || glm::length(s.vel) < 1.0f) continue;
          const glm::vec3 f = glm::normalize(s.vel);
          const glm::vec3 r = glm::normalize(glm::cross(f, std::fabs(f.y) < 0.98f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
          render::buildLawRocket(lb, glm::mat4(glm::vec4(r, 0.0f), glm::vec4(glm::cross(r, f), 0.0f), glm::vec4(-f, 0.0f), glm::vec4(s.pos, 1.0f)));
        }
        for (const std::vector<sim::Projectile>& shots : {weapons.shots, wingShots()}) litavrs(shots), r60s(shots);
        for (const auto& [hp, hw] : otherHinds)
          if (hw && hw != &weapons) litavrs(hw->shots), r60s(hw->shots);
        if (!lb.verts.empty()) {
          meshes.litavrs.upload(lb.verts);
          scene.set("uModel", glm::mat4(1.0f));
          meshes.litavrs.draw();
        }
      }
      if (pal.shadow > 0.01f) {
        // Shadows: the ground under them multiplied darker, each pixel once (the stencil counts
        // the pixels darkened: where shadows overlap, the second leaves them).
        scene.set("uShadowDark", 1.0f - 0.55f * pal.shadow);
        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0f, -8.0f);
        glDepthMask(GL_FALSE);
        glClear(GL_STENCIL_BUFFER_BIT);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, 0, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        vehicles.drawShadows(scene, targets, terrain, cs.eye, std::min(viewDistance, kShadowRange), pal.sunDir);
        // A Hind's: ours, and the other player's in the network game.
        auto hindShadow = [&](glm::vec3 p, glm::quat o, const sim::Helicopter& h, const sim::Weapons* w) {
          glm::vec3 at, normal;
          if (h.crashed || !render::shadowPlane(terrain, p, pal.sunDir, at, normal)) return;
          const glm::mat4 flat = render::shadowMatrix(at, normal, pal.sunDir) * glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(o);
          scene.set("uModel", flat);
          scene.set("uShadow", 1.0f);
          if (meshes.original) {
            (h.gearPos > 0.5f ? meshes.origGearDown : meshes.origGearUp).draw();
            meshes.origDoorsShut.draw();
            meshes.origBody.draw();
            meshes.origTurret.draw();
            scene.set("uModel", flat * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, render::kOriginalMi24Lift, 0.0f)));
            if (w) meshes.drawStores(*w);
          } else {
            meshes.exterior.draw();
            meshes.chinGun.draw();
            if (h.gearPos > 0.5f) meshes.gear.draw();
            if (w) meshes.drawStores(*w);
          }
          // The rotor: a dithered disc while it turns, the blades as they stand when it stops.
          const glm::mat4 hub = flat * glm::translate(glm::mat4(1.0f), render::kMainRotorHub);
          if (h.rotorRpm > 20.0f) {
            scene.set("uShadow", 2.0f);
            scene.set("uModel", hub);
            meshes.disc.draw();
          } else {
            scene.set("uModel", hub * glm::rotate(glm::mat4(1.0f), -h.rotorAngle, glm::vec3(0, 1, 0)));
            meshes.rotorStopped.draw();
          }
        };
        if (!debris.active() && !bowmanGame) hindShadow(pos, orient, heli, &weapons);
        for (const auto& [hp, hw] : otherHinds) hindShadow(hp->pos, hp->orient, *hp, hw);
        scene.set("uShadow", 0.0f);
        scene.set("uModel", glm::mat4(1.0f));
        glDisable(GL_STENCIL_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_BLEND);
      }
      {
        // Tracers, rockets, smoke and explosions: unlit, translucent, not hiding each other.
        render::MeshBuilder eb;
        effects.build(eb, allShots(), cs.eye, pos - heli.pos);
        for (const auto& [hp, hw] : otherHinds)
          if (hw && hw != &weapons) effects.build(eb, hw->shots, cs.eye);
        effects.buildEnemy(eb, enemy.shots(), enemy.flaresInAir(), cs.eye);
        effects.buildChaff(eb, enemy.chaffInAir(), cs.eye);
        effects.buildLights(eb, runwayLights, cs.eye, pal.lamps);
        // (The drone's camera sees in the infra-red at night: as night vision, the engines glowing.)
        std::vector<glm::mat4> hindFrames;
        if (!cockpit && !heli.crashed && !bowmanGame) hindFrames.push_back(glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(orient));
        for (const auto& [hp, hw] : otherHinds)
          if (!hp->crashed) hindFrames.push_back(glm::translate(glm::mat4(1.0f), hp->pos) * glm::mat4_cast(hp->orient));
        effects.buildEngineGlows(eb, targets, hindFrames, cs.eye, pal.lamps,
                                 nightVision || (camera.mode == render::ViewMode::Drone && pal.lamps > 0.2f));
        if (!eb.verts.empty()) {
          meshes.effects.upload(eb.verts);
          scene.set("uModel", glm::mat4(1.0f));
          scene.set("uUnlit", 1.0f);
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glDepthMask(GL_FALSE);
          meshes.effects.draw();
          glDepthMask(GL_TRUE);
          glDisable(GL_BLEND);
          scene.set("uUnlit", 0.0f);
        }
      }

      // Pass 2: our own helicopter (cockpit frame is centimetres from the eye).
      glClear(GL_DEPTH_BUFFER_BIT);
      glm::mat4 projNear = glm::perspective(fov, aspect, cockpit ? 0.05f : 0.5f, cockpit ? 80.0f : 600.0f);
      scene.set("uViewProj", projNear * cs.view);
      glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(orient);
      scene.set("uModel", model);
      const bool brokenUp = debris.active() && !bowmanGame;
      if (brokenUp) {
        const auto& ps = debris.pieces();
        for (size_t i = 0; i < ps.size() && i < meshes.pieces.size(); ++i) {
          scene.set("uModel", glm::translate(glm::mat4(1.0f), ps[i].pos) * glm::mat4_cast(ps[i].orient));
          if (meshes.pieceCull[i]) glEnable(GL_CULL_FACE);
          meshes.pieces[i].draw();
          glDisable(GL_CULL_FACE);
        }
        scene.set("uModel", model);
      } else if (!pictureWhole && !noCockpit && !bowmanGame) {
        // From a door the outside of the original's airframe (its one-sided polygons show only
        // what is out of the window: the wings, the stores), and the cabin around.
        const bool orig = meshes.original && (!cockpit || door);
        // The original's polygons are one-sided.
        if (orig) glEnable(GL_CULL_FACE);
        if (orig) scene.set("uLivery", ukrainianLivery ? 1.0f : 0.0f);
        if (orig) (heli.gearPos > 0.5f ? meshes.origGearDown : meshes.origGearUp).draw();
        // With door guns the doors are open, the gunners at them in the cabin.
        const bool gunsOut = orig && !door && (weapons.loadout().doorGun[0] || weapons.loadout().doorGun[1]);
        if (orig) (troops.doorsOpen() || gunsOut ? meshes.origDoorsOpen : meshes.origDoorsShut).draw();
        if (door) {
          if (orig) meshes.origBody.draw();
          scene.set("uLivery", 0.0f);
          glDisable(GL_CULL_FACE);
          // The gun points where its gunner looks: ours, or the computer's where he aims.
          const glm::vec3 gunDir = mirrored || ownDoor ? glm::inverse(orient) * sightDir
                                                                                     : weapons.doorAim(camera.doorSide);
          render::MeshBuilder db;
          render::buildDoorView(db, camera.doorSide, gunDir);
          meshes.doorView.upload(db.verts);
          meshes.doorView.draw();
          if (orig) glEnable(GL_CULL_FACE);
          if (orig) scene.set("uLivery", ukrainianLivery ? 1.0f : 0.0f);
        } else {
          if (!cockpit) (orig ? (gunsOut ? meshes.origBodyOpen : meshes.origBody) : meshes.exterior).draw();
          if (!cockpit && gunsOut) {
            scene.set("uLivery", 0.0f);
            meshes.origCabin.draw();
            for (int side = 0; side < 2; ++side)
              if (weapons.loadout().doorGun[side]) meshes.origGunner[side].draw();
            scene.set("uLivery", ukrainianLivery ? 1.0f : 0.0f);
          }
        }
        const glm::vec3 pivot = sim::Weapons::kTurretPivot;
        scene.set("uModel", model * glm::translate(glm::mat4(1.0f), pivot) *
                                glm::rotate(glm::mat4(1.0f), weapons.turretAz(), glm::vec3(0, 1, 0)) *
                                glm::rotate(glm::mat4(1.0f), weapons.turretEl(), glm::vec3(1, 0, 0)) *
                                glm::translate(glm::mat4(1.0f), -pivot));
        (orig ? meshes.origTurret : meshes.chinGun).draw();
        scene.set("uLivery", 0.0f);
        glDisable(GL_CULL_FACE);
        // The stores hang from the original's wings, which stand higher on their longer gear.
        scene.set("uModel", orig ? glm::translate(model, glm::vec3(0.0f, render::kOriginalMi24Lift, 0.0f)) : model);
        meshes.drawStores(weapons);
        scene.set("uModel", model);
        // The door guns from outside, pointing where their gunners have them.
        if (!cockpit && (weapons.loadout().doorGun[0] || weapons.loadout().doorGun[1])) {
          render::MeshBuilder gb;
          // (On the original's model in its doorway, on a post by the floor.)
          for (int side = 0; side < 2; ++side) {
            if (!weapons.loadout().doorGun[side]) continue;
            if (meshes.original) render::buildDoorGun(gb, side, weapons.doorAim(side), render::OriginalMi24::doorGunPivot(side), true, false);
            else render::buildDoorGun(gb, side, weapons.doorAim(side));
          }
          meshes.doorView.upload(gb.verts);
          meshes.doorView.draw();
        }
      }
      render::InstrumentData id;
      if (cockpit) {
        id.speedKmh = heli.trueAirspeed() * 3.6f;
        id.pitchDeg = heli.pitchDeg();
        id.rollDeg = heli.rollDeg();
        id.headingDeg = heli.headingDeg();
        id.verticalSpeed = heli.vel.y;
        float radarAlt = heli.radarAltitude();
        id.radarAltitude = radarAlt < 700.0f ? radarAlt : -1.0f;
        id.baroAltitude = heli.baroAltitude();
        id.torque = heli.torque;
        id.gLoad = gLoad;
        glm::vec3 vb = glm::inverse(heli.orient) * heli.vel;
        id.slipDeg = glm::length(vb) > 3.0f ? glm::degrees(std::atan2(vb.x, -vb.z)) : 0.0f;
        id.yawRateDeg = -glm::degrees(heli.omega.y);
        id.clockSeconds = (haveMission ? mission.startHour : freeHour) * 3600.0f + float(simTime);
        id.gearDown = heli.gearDown;
        id.gearMoving = heli.gearPos > 0.0f && heli.gearPos < 1.0f;
        id.brakes = heli.brakes;
        id.position = {heli.pos.x, heli.pos.z};
        id.home = startPad;
        id.steer = missionState.active() ? missionState.steerPoint() : startPad;
        id.route = missionState.route();
        id.waypoint = missionState.active() && heli.works(sim::System::Nav) ? missionState.waypoint() : -1;
        for (int k = 0; k < sim::kSystems; ++k) id.failed[k] = !heli.works(sim::System(k));
        id.fuelDamaged = heli.damage.fuelLeak > 0.0f;
        id.text = displayText;
        id.fuelKg = heli.fuel;
        id.mapLight = pal.mapLight;
        id.dialLight = instrumentLights ? glm::max(render::cockpitLight(pal), kInstrumentLamps) : render::cockpitLight(pal);
      for (int k = 0; k < 2; ++k) {
        id.engineFailed[k] = heli.engineOut(k);
        id.engineWarning[k] = heli.damage.engine[k] < 100.0f;
      }
      id.fire = heli.engineFire(0) || heli.engineFire(1);
      id.jammer = enemy.jammer;
        id.engineRpm = heli.engineRpm;
        id.rotorRpm = heli.rotorRpm;
        const sim::Threat& threat = heard.threat;
        id.threat = threat.active;
        id.threatClass = int(threat.cls);
        id.threatBearing = threat.bearingDeg;
        id.threatProximity = threat.proximity;
        const sim::WeaponType sel = weapons.selected();
        for (int p = 0; p < sim::kPylons; ++p) {
          const sim::StoreInfo& si = sim::storeInfo(weapons.loadout().at(sim::Pylon(p)));
          id.stationRounds[p] = weapons.pylonRounds(sim::Pylon(p));
          id.stationSelected[p] = si.available && si.rounds > 0 && si.weapon == sel;
        }
        id.stationRounds[3] = weapons.rounds(sim::WeaponType::YakB);
        id.stationSelected[3] = sel == sim::WeaponType::YakB;
        id.weapon = sim::weaponName(sel);
      }
      if (cockpit && !pictureWhole && !door && !noCockpit) {
        render::MeshBuilder ib;
        if (gunnerSeat) render::buildGunnerCockpit(ib, id);
        else render::buildPilotCockpit(ib, id);
        meshes.instruments.upload(ib.verts);
        scene.set("uUnlit", 1.0f);
        meshes.instruments.draw();
        scene.set("uUnlit", 0.0f);
      }
      if (heli.gearPos > 0.5f && !meshes.original && !(cockpit && !door) && !brokenUp && !noCockpit && !bowmanGame) meshes.gear.draw();
      glm::mat4 hub = model * glm::translate(glm::mat4(1.0f), render::kMainRotorHub);
      // Russian rotors turn clockwise seen from above.
      // From the cockpit the blades are only a blur (the disc below).
      if ((!cockpit || heli.crashed) && !brokenUp && !bowmanGame) {
        scene.set("uModel", hub * glm::rotate(glm::mat4(1.0f), -heli.rotorAngle, glm::vec3(0, 1, 0)));
        (heli.rotorRpm < 30.0f ? meshes.rotorStopped : meshes.rotor).draw();
      }
      scene.set("uModel", model * glm::translate(glm::mat4(1.0f), render::kTailRotorHub) *
                              glm::rotate(glm::mat4(1.0f), heli.tailRotorAngle, glm::vec3(1, 0, 0)));
      if (!brokenUp && !bowmanGame) meshes.tailRotor.draw();
      if (!heli.crashed && !bowmanGame) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        scene.set("uModel", hub);
        meshes.disc.draw();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
      }
      if (bowmanGame) {
        // The cases of the rounds fired since the last frame, out of the M60's port as he holds it.
        const glm::mat4 toWorld = glm::inverse(cs.view);
        const glm::mat4 gun = toWorld * render::m60View(bowman.kick, -1.0f);
        const glm::vec3 port(gun * glm::vec4(render::kM60Port, 1.0f));
        const glm::vec3 right = glm::normalize(glm::mat3(gun) * glm::vec3(1, 0, 0)), up = glm::normalize(glm::mat3(gun) * glm::vec3(0, 1, 0));
        const glm::vec3 back = glm::normalize(glm::mat3(gun) * glm::vec3(0, 0, 1));
        for (int n = std::min(bowman.shots() - casingShots, 8); n > 0 && !bowOrbit && !bowArrow; --n)
          casings.eject(port, right, up, back, glm::vec3(bowman.vel.x, 0.0f, bowman.vel.y));
        casingShots = bowman.shots();
        casings.step(frameDt, terrain);
      }
      if (bowmanGame && !bowOrbit && !bowArrow) {
        // His bow and hands, right before his eyes.
        render::MeshBuilder bb;
        casings.build(bb, cs.view);
        const bool nocked = bowman.state == sim::Bowman::State::Ready || bowman.state == sim::Bowman::State::Drawing;
        if (bowman.dead()) {}
        else if (bowman.weapon == sim::Bowman::Weapon::M60)
          render::buildM60View(bb, bowman.kick, bowman.reload > 0.0f ? 1.0f - bowman.reload / sim::Bowman::kM60Reload : -1.0f);
        else if (bowman.weapon == sim::Bowman::Weapon::Law)
          render::buildLawView(bb, bowman.kick, 1.0f - bowman.lawReady / sim::Bowman::kLawReady);
        else render::buildBowView(bb, bowman.draw, nocked, bowman.state == sim::Bowman::State::Fitting ? bowman.fitting() : -1.0f);
        glClear(GL_DEPTH_BUFFER_BIT);
        scene.set("uViewProj", glm::perspective(fov, aspect, 0.02f, 5.0f) * cs.view);
        scene.set("uModel", glm::inverse(cs.view));
        meshes.bow.upload(bb.verts);
        meshes.bow.draw();
        scene.set("uModel", glm::mat4(1.0f));
      }
      if (usePicture) {
        if (!pictureWhole) pictureFade.capture(dw, dh);
        const float left = picture.frontLeft(dw, dh, pan), scale = picture.scale(dh), drop = lift * scale;
        if (gaugesReady) gauges.drawUnder(id, gunnerSeat, dw, dh, left, scale, drop);
        // The instruments' lamps light the dials' marks on the panel below the glare shield.
        picture.draw(dw, dh, pan, lift, glm::min(render::cockpitLight(pal), glm::vec3(1.0f)),
                     instrumentLights ? kInstrumentLamps : glm::vec3(0.0f), gunnerSeat ? kGunnerPanelRow : kPilotPanelRow);
        if (gaugesReady) gauges.drawOver(id, gunnerSeat, dw, dh, left, scale, drop);
        if (!pictureWhole) pictureFade.drawBack(0.0f, 1.0f - pictureShown);
      }

      // HUD.
      render::HudData hd;
      hd.cockpit = cockpit;
      hd.gunner = gunnerSeat;
      hd.black = hudBlack;
      hd.fovY = fov;
      hd.magnification = magnification;
      // The HUD is on the collimator glass: readable only when the pilot looks roughly ahead.
      // The pilot's HUD knocked out is dark (the gunner's sight is his own).
      // With the cockpit picture the HUD is there as long as its glass is (it slides with the
      // picture); in the modelled cockpit only looking about straight ahead.
      hd.headUp = !door && (gunnerSeat || heli.works(sim::System::Hud)) &&
                  (usePicture || (std::fabs(camera.lookYaw()) < glm::radians(4.0f) && std::fabs(camera.lookPitch()) < glm::radians(25.0f)));
      if (usePicture && !gunnerSeat) hd.hudShiftX = -pan * picture.scale(dh), hd.hudShiftY = -lift * picture.scale(dh);
      // The door gunners' shot at the nearest enemy drone within a kilometre.
      if (gunnerHints && !heli.crashed && (weapons.loadout().doorGun[0] || weapons.loadout().doorGun[1])) {
        const sim::Vehicle* drone = nullptr;
        for (const sim::Vehicle& v : targets.list)
          if (!v.destroyed && v.side == sim::Side::Enemy && sim::vehicleType(v.kind).cls == sim::UnitClass::Drone &&
              glm::distance(v.pos, heli.pos) < 1000.0f && (!drone || glm::distance(v.pos, heli.pos) < glm::distance(drone->pos, heli.pos)))
            drone = &v;
        if (drone) {
          const glm::vec3 rel = drone->pos - heli.pos, rv = drone->vel - heli.vel, body = glm::inverse(heli.orient) * rel;
          glm::vec3 fwd = heli.forward();
          fwd.y = 0.0f;
          fwd = glm::length(fwd) > 0.01f ? glm::normalize(fwd) : glm::vec3(0.0f, 0.0f, -1.0f);
          const sim::Weapons::DoorShotParts parts = sim::Weapons::doorShotParts(rel, rv);
          hd.doorShotShown = true;
          hd.doorShotSide = 0;
          for (int side = 0; side < 2; ++side)
            if (weapons.loadout().doorGun[side] && sim::Weapons::doorField(side, body)) hd.doorShotSide = side ? 'R' : 'L';
          hd.doorShot = parts.near * parts.matched * parts.below;
          hd.doorNear = parts.near, hd.doorMatched = parts.matched, hd.doorBelow = parts.below;
          hd.doorOpenFire = sim::Weapons::kDoorOpenFire;
          hd.doorShotHold = !weapons.doorGunnersFree;
          hd.doorRange = glm::length(rel);
          hd.doorAlongKmh = glm::dot(rv, fwd) * 3.6f;
          hd.doorUpMs = rv.y;
          hd.doorBelowM = -rel.y;
        }
      }
      if (door) {
        // At the door gun: its ring sight where it points, its rounds.
        const int side = camera.doorSide;
        hd.doorGun = std::string("GSHG ") + (side ? "RIGHT " : "LEFT ") + (weapons.doorReloading(side) ? std::string("RELOADING") : std::to_string(weapons.doorRounds(side))) +
                     (weapons.doorGunnersFree ? "" : "  HOLD FIRE");
        const bool mine = mirrored || ownDoor;
        const glm::vec3 dir = mine ? sightDir : orient * weapons.doorAim(side);
        glm::vec4 clip = vpWorld * glm::vec4(cs.eye + dir * 1000.0f, 1.0f);
        if (clip.w > 0.0f) {
          hd.doorMark = {(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
          hd.doorMarkShown = true;
        }
      }
      if (camera.mode == render::ViewMode::Drone) {
        // Flying an interceptor: its speed and battery, the enemy fliers its camera sees marked,
        // the one picked (or locked on) bracketed.
        hd.droneView = true;
        const sim::Projectile* d = weapons.interceptorWatched(droneOwner());
        char buf[96];
        if (d)
          std::snprintf(buf, sizeof buf, "INTERCEPTOR %.0f KM/H  BATTERY %.0f S  %d LEFT", glm::length(d->vel) * 3.6f,
                        std::max(0.0f, sim::Weapons::kInterceptorLife - d->age), weapons.rounds(sim::WeaponType::Interceptor));
        else
          std::snprintf(buf, sizeof buf, "%s", simTime - droneLaunchedAt < 2.0 ? "INTERCEPTOR - LAUNCHING" : "INTERCEPTOR - SIGNAL LOST");
        hd.droneInfo = buf;
        hd.droneStick = droneStick;
        hd.droneLocked = d && d->locked;
        hd.droneKeys = hd.droneLocked ? "LOCKED - FLYING ITSELF   G NEXT INTERCEPTOR" : "B NEXT TARGET   FIRE LOCK   G LET GO";
        std::vector<int> marked = d ? droneMarked() : std::vector<int>();
        if (hd.droneLocked) {
          droneSel = d->target;
          if (droneSel >= 0 && std::find(marked.begin(), marked.end(), droneSel) == marked.end()) marked.push_back(droneSel);
        } else if (std::find(marked.begin(), marked.end(), droneSel) == marked.end()) droneSel = marked.empty() ? -1 : marked.front();
        // The picked one first.
        if (const auto at = std::find(marked.begin(), marked.end(), droneSel); at != marked.end()) std::rotate(marked.begin(), at, at + 1);
        for (int i : marked) {
          const glm::vec4 clip = vpWorld * glm::vec4(targets.list[size_t(i)].pos, 1.0f);
          if (clip.w <= 0.0f) continue;
          const glm::vec2 at{(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
          if (i == droneSel) {
            const sim::Vehicle& v = targets.list[size_t(i)];
            const float dist = glm::distance(v.pos, cs.eye);
            hd.droneSelected = true;
            hd.droneLockable = !hd.droneLocked && dist <= sim::Weapons::kInterceptorLock;
            hd.droneMarkText = sim::vehicleType(v.kind).name + " " + std::to_string(int(std::lround(dist / 10.0f)) * 10) + " M" +
                               (hd.droneLocked ? "  LOCKED" : hd.droneLockable ? "  FIRE TO LOCK" : "");
            hd.droneMarks.insert(hd.droneMarks.begin(), {at});
          } else {
            hd.droneMarks.push_back({at});
          }
        }
      }
      // (The pilot's HUD slides down with his cockpit picture as he looks up: drawn as from his
      // seat's default pitch, the boresight comes about where it is.)
      hd.boresightUp = (dh * 0.5f) / std::tan(fov * 0.5f) * std::tan(usePicture && !gunnerSeat ? -camera.seatPitch() : -camera.lookPitch());
      hd.speedKmh = heli.trueAirspeed() * 3.6f;
      // Tail first: going against where the nose points (in space: nose up in a climb it is
      // forwards whatever little it drifts back over the ground; sideways or still, forwards).
      hd.backwards = glm::dot(heli.forward(), heli.vel) < -0.5f;
      {
        // Over the ground, in the horizontal plane of the heading (not tilted with the airframe).
        glm::vec3 f = orient * glm::vec3(0, 0, -1);
        glm::vec2 fh = glm::length(glm::vec2(f.x, f.z)) > 1e-3f ? glm::normalize(glm::vec2(f.x, f.z)) : glm::vec2(0, -1);
        glm::vec2 vh{heli.vel.x, heli.vel.z};
        hd.forwardKmh = glm::dot(vh, fh) * 3.6f;
        hd.sideKmh = glm::dot(vh, glm::vec2(-fh.y, fh.x)) * 3.6f;
      }
      float radar = heli.radarAltitude();
      hd.radarAltitude = radar < 700.0f;
      hd.altitude = hd.radarAltitude ? radar : heli.baroAltitude();
      hd.verticalSpeed = heli.vel.y;
      hd.torque = heli.torque;
      hd.rotorFree = heli.rotorFree, hd.rotorRpm = heli.rotorRpm;
      hd.headingDeg = heli.headingDeg();
      hd.waypointShown = missionState.active() && heli.works(sim::System::Nav);
      if (hd.waypointShown) {
        const glm::vec2 to = missionState.steerPoint() - glm::vec2(heli.pos.x, heli.pos.z);
        const float dist = glm::length(to), gs = glm::length(glm::vec2(heli.vel.x, heli.vel.z));
        const float off = std::remainder(glm::degrees(std::atan2(to.x, -to.y)) - heli.headingDeg(), 360.0f);
        const float climb = glm::degrees(std::atan2(missionState.steerAltitude() - heli.radarAltitude(), std::max(dist, 1500.0f)));
        hd.waypointLetter = sim::MissionState::letter(missionState.waypoint());
        hd.waypointKm = dist / 1000.0f;
        // Seconds to it at the speed made good towards it.
        const float closing = dist > 1.0f ? glm::dot(glm::vec2(heli.vel.x, heli.vel.z), to / dist) : 0.0f;
        hd.waypointSeconds = closing > 5.0f && gs > 5.0f ? dist / closing : -1.0f;
        hd.pathMarkerDeg = {off, climb * 4.0f};
      }
      if (hd.waypointShown) {
        const glm::vec2 to = missionState.steerPoint() - glm::vec2(heli.pos.x, heli.pos.z);
        hd.waypointBearing = glm::degrees(std::atan2(to.x, -to.y));
      }
      hd.pitchDeg = heli.pitchDeg();
      hd.rollDeg = heli.rollDeg();
      const sim::Controls& ctl = mirrored ? netFrame.controls : input.controls();
      hd.cyclicX = ctl.cyclicX;
      hd.cyclicY = ctl.cyclicY;
      hd.cyclicKeys = mirrored ? netFrame.cyclicKeys : input.cyclicKeys();
      hd.pedal = ctl.pedal;
      hd.collective = ctl.collective;
      hd.flightModel = sim::flightModelName(heli.model);
      hd.gearDown = heli.gearDown;
      hd.gearMoving = heli.gearPos > 0.0f && heli.gearPos < 1.0f;
      hd.brakes = heli.brakes;
      hd.dim = pauseDim;
      hd.timeScale = timeScale;
      if (arcadeRun) hd.score = arcadeScore;
      hd.joystick = input.joystickActive();
      hd.message = heli.message;
      if (usePicture) {
        hd.displayText = displayText;
        hd.displayRect = picture.displayRect(dw, dh, pan, lift);
        hd.message.clear();  // shown on the text display instead
      }
      hd.weapon = weapons.status();
    hd.airframe = int(std::lround(heli.damage.airframe));
    hd.flares = enemy.flares;
    hd.chaff = enemy.chaff;
    hd.jammer = enemy.jammer && heli.works(sim::System::Jam);
      if (gunnerSeat && cockpit) {
        // Where the turret actually points (it lags the sight and stops at its limits).
        glm::vec3 dir = orient * weapons.turretDirBody();
        glm::vec4 clip = vpWorld * glm::vec4(cs.eye + dir * 1000.0f, 1.0f);
        if (clip.w > 0.0f) {
          hd.turretMark = {(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
          hd.turretShown = true;
        }
      }
      if (wso.target() >= 0) {
        const sim::Vehicle& tv = targets.list[size_t(wso.target())];
        hd.lasing = wso.lasing(heli, targets) && heli.works(sim::System::Las);
        hd.r60 = weapons.selected() == sim::WeaponType::R60 && weapons.armed();
        hd.r60Lock = hd.r60 && sim::Weapons::r60Seek(heli, targets, wso.target()) >= 0;
        hd.target = std::string(wso.enabled() ? "WSO " : "TRK ") + sim::vehicleType(tv.kind).name;
        if (hd.lasing) {
          const float range = wso.range(heli, targets);
          hd.target += " " + std::to_string(int(std::lround(range / 10.0f)) * 10);
          // Too far for the weapon chosen: rounds wasted.
          // (An R-60's seeker by what it is and from where: a Shahed's piston engine is faint.)
          const float reach = weapons.selected() == sim::WeaponType::R60 ? sim::Weapons::r60LockRange(tv, heli.pos)
                                                                           : sim::Weapons::effectiveRange(weapons.selected());
          if (weapons.armed() && range > reach) hd.target += " FAR";
        }
        // A 9M114 on its way: seconds to go.
        const float tof = weapons.missileTimeTo(wso.aimPoint(targets));
        if (tof >= 0.0f) hd.target += " T" + std::to_string(int(std::ceil(tof)));
        glm::vec4 clip = vpWorld * glm::vec4(wso.aimPoint(targets), 1.0f);
        if (clip.w > 0.0f && (!cockpit || gunnerSeat || hd.headUp)) {
          hd.targetMark = {(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
          hd.targetShown = std::fabs(clip.x / clip.w) < 1.0f && std::fabs(clip.y / clip.w) < 1.0f;
        }
      } else if (wso.enabled()) {
        hd.target = "WSO";
      }
      // The drone the Mi-17 called out, unless the WSO's mark is on it already.
      if (const int cue = spotter.designated(); cue >= 0 && (cue != wso.target() || !hd.targetShown)) {
        glm::vec4 clip = vpWorld * glm::vec4(targets.list[size_t(cue)].pos, 1.0f);
        if (clip.w > 0.0f && std::fabs(clip.x / clip.w) < 1.0f && std::fabs(clip.y / clip.w) < 1.0f) {
          hd.cueMark = {(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
          hd.cueShown = true;
        }
      }
      // Our other Hinds within 5 km: the AI wingman, the network game's.
      {
        std::vector<glm::vec3> ours;
        if (const int w = sim::Orders::wingman(targets); w >= 0) ours.push_back(targets.list[size_t(w)].pos);
        for (const auto& wg : wings)
          if (wg->flying && !wg->heli.crashed) ours.push_back(wg->heli.pos);
        if (peerFlying && !peerHeli.crashed) ours.push_back(peerHeli.pos);
        for (const auto& o : otherHelis)
          if (!o->crashed) ours.push_back(o->pos);
        for (const glm::vec3& p : ours) {
          if (glm::distance(p, heli.pos) > 5000.0f || glm::distance(p, heli.pos) < 20.0f) continue;
          const glm::vec4 clip = vpWorld * glm::vec4(p + glm::vec3(0.0f, 1.5f, 0.0f), 1.0f);
          if (clip.w > 0.0f && std::fabs(clip.x / clip.w) < 1.0f && std::fabs(clip.y / clip.w) < 1.0f)
            hd.friendMarks.push_back({(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh});
        }
      }
      if (weapons.armed()) {
        // Weapons mode.
        const sim::WeaponType w = weapons.selected();
        hd.weaponsMode = true;
        hd.sight = sim::isBomb(w) || w == sim::WeaponType::Mine ? render::HudData::Sight::Bombs
                   : w == sim::WeaponType::Shturm ? render::HudData::Sight::Missile
                   : w == sim::WeaponType::S5 || w == sim::WeaponType::S8 || w == sim::WeaponType::R60
                       ? render::HudData::Sight::Rockets
                                                                          : render::HudData::Sight::Gun;
        // As the original names them: "TurretGun:1470", "FAB250:4".
        std::string name = w == sim::WeaponType::YakB ? "TurretGun" : sim::weaponName(w);
        name.erase(std::remove(name.begin(), name.end(), '-'), name.end());
        hd.weaponLabel = name + ":" + std::to_string(weapons.rounds(w));
        hd.minRange = sim::Weapons::minimumRange(w);
        hd.maxRange = sim::isBomb(w) ? 3000.0f : sim::Weapons::effectiveRange(w);
        if (wso.target() >= 0 && hd.lasing) hd.targetRange = wso.range(heli, targets);
        if (bombComputer) {
          hd.bombComputer = true;
          hd.countdown = bombCountdown;
          hd.bombLateral = (dh * 0.5f) / std::tan(fov * 0.5f) * bombLateral / bombDistance;
        } else if (w == sim::WeaponType::Shturm && wso.target() >= 0) {
          hd.countdown = weapons.missileTimeTo(wso.aimPoint(targets));
        }
      }
      if (weapons.armed() && sim::isBomb(weapons.selected()) && cockpit && (gunnerSeat || hd.headUp)) {
        glm::vec3 hit;
        if (weapons.bombImpact(heli, hit)) {
          glm::vec4 clip = vpWorld * glm::vec4(hit, 1.0f);
          if (clip.w > 0.0f) {
            hd.bombMark = {(clip.x / clip.w * 0.5f + 0.5f) * dw, (clip.y / clip.w * 0.5f + 0.5f) * dh};
            hd.bombShown = std::fabs(clip.x / clip.w) < 1.0f && std::fabs(clip.y / clip.w) < 1.0f;
          }
        }
      }
      hd.time = float(simTime);
      hd.page = page;
      if (endAsk && !gaugesReady) hd.message = "END MISSION? Y/N";
      if (bowmanGame) {
        // Through his eyes only a small cross, and what he is about.
        render::HudData bh;
        bh.bowView = true, bh.bowCross = !bowOrbit && !bowArrow;
        char buf[96];
        // Drawing: where the arrow would come down, how far, and the ring of its blast round it.
        glm::vec3 lands;
        std::vector<glm::vec3> flight;
        // (What its fuse would feel on the way: the Hinds, and the units within its reach.)
        std::vector<sim::HindShape> fuseHinds;
        std::vector<sim::UnitShape> fuseUnits;
        bool struck = false;
        if (bowman.state == sim::Bowman::State::Drawing) {
          if (!heli.crashed) fuseHinds.push_back({heli.pos, heli.orient});
          for (const auto& h : otherHelis)
            if (!h->crashed) fuseHinds.push_back({h->pos, h->orient});
          for (const sim::Vehicle& v : targets.list) {
            if (v.destroyed || v.aboard || glm::distance(v.pos, bowman.pos) > 800.0f) continue;
            const sim::VehicleType& t = sim::vehicleType(v.kind);
            fuseUnits.push_back({glm::inverse(v.toWorld()), t.lo, t.hi, t.faces.empty() ? nullptr : &t.faces});
          }
        }
        const bool landsKnown = bowman.state == sim::Bowman::State::Drawing && !bowOrbit && !bowArrow &&
                                bowman.landing(bowman.draw, terrain, lands, &flight, fuseHinds, fuseUnits, &struck);
        const float range = landsKnown ? glm::length(glm::vec2(lands.x - bowman.pos.x, lands.z - bowman.pos.z)) : 0.0f;
        if (landsKnown) {
          const auto onScreen = [&](glm::vec3 p, glm::vec2& at) {
            const glm::vec4 clip = vpWorld * glm::vec4(p, 1.0f);
            if (clip.w <= 0.1f) return false;
            at = {(clip.x / clip.w * 0.5f + 0.5f) * float(dw), (clip.y / clip.w * 0.5f + 0.5f) * float(dh)};
            return true;
          };
          // Its flight, in short dashes (each a tenth of a second's): drawn from a few metres to his
          // left (straight from his eye an arc shows only as a line down the middle), coming in to
          // where it comes down, where it does. The arrow itself goes from his bow.
          const glm::vec3 from = flight.front(), left{-std::cos(bowman.yaw), 0.0f, -std::sin(bowman.yaw)};
          const float span = std::max(1.0f, glm::length(glm::vec2(lands.x - from.x, lands.z - from.z)));
          const float aside = 3.0f + 0.15f * span;
          std::vector<glm::vec2> way;
          for (glm::vec3 p : flight) {
            p += left * (aside * (1.0f - std::clamp(glm::length(glm::vec2(p.x - from.x, p.z - from.z)) / span, 0.0f, 1.0f)));
            glm::vec2 at;
            if (!onScreen(p, at)) {
              if (way.size() > 1) bh.bowPath.push_back(way);
              way.clear();
              continue;
            }
            way.push_back(at);
          }
          if (way.size() > 1) bh.bowPath.push_back(way);
          // Anything its blast would reach: a Hind, a unit, whosever (he is on nobody's side).
          bool hot = !heli.crashed && sim::hindDistance(lands, {heli.pos, heli.orient}) < sim::Bowman::kBlastReach;
          for (const auto& h : otherHelis) hot = hot || (!h->crashed && sim::hindDistance(lands, {h->pos, h->orient}) < sim::Bowman::kBlastReach);
          for (const sim::Vehicle& v : targets.list) {
            if (hot) break;
            if (v.destroyed || v.aboard || glm::distance(v.pos, lands) > sim::Bowman::kBlastReach + 30.0f) continue;
            const sim::VehicleType& t = sim::vehicleType(v.kind);
            const glm::vec3 local(glm::inverse(v.toWorld()) * glm::vec4(lands, 1.0f));
            hot = glm::distance(local, glm::clamp(local, t.lo, t.hi)) < sim::Bowman::kBlastReach;
          }
          bh.bowRingHot = hot || struck;
          constexpr int kSides = 64;
          std::vector<glm::vec2> part;
          for (int k = 0; k <= kSides; ++k) {
            const float a = float(k) / float(kSides) * glm::two_pi<float>();
            const float x = lands.x + std::cos(a) * sim::Bowman::kBlastReach, z = lands.z + std::sin(a) * sim::Bowman::kBlastReach;
            // (Going off by something in the air: the ring level round where it would.)
            const glm::vec4 clip = vpWorld * glm::vec4(x, struck ? lands.y : terrain.groundHeight(x, z) + 0.3f, z, 1.0f);
            if (clip.w <= 0.1f) {
              if (part.size() > 1) bh.bowRing.push_back(part);
              part.clear();
              continue;
            }
            part.push_back({(clip.x / clip.w * 0.5f + 0.5f) * float(dw), (clip.y / clip.w * 0.5f + 0.5f) * float(dh)});
          }
          if (part.size() > 1) bh.bowRing.push_back(part);
        }
        if (bowman.dead()) {
          std::snprintf(buf, sizeof buf, "BACK AT THE START IN %.0f S", std::ceil(sim::Bowman::kDeadTime - bowman.deadFor));
          bh.bowDead = "YOU ARE DEAD";
        } else if (bowman.weapon == sim::Bowman::Weapon::M60) {
          if (bowman.reload > 0.0f) std::snprintf(buf, sizeof buf, "M60 RELOADING   ENTER LAW");
          else std::snprintf(buf, sizeof buf, "M60  %d   LEFT BUTTON FIRES   ENTER LAW   SPACE JUMP", bowman.belt);
        } else if (bowman.weapon == sim::Bowman::Weapon::Law) {
          const bool homing = bowman.rocket.hind >= 0 || bowman.rocket.unit >= 0;
          if (bowman.rocket.flying && bowman.rocket.decoyed())
            std::snprintf(buf, sizeof buf, bowman.rocket.flare >= 0 ? "ROCKET TOOK A FLARE" : "ROCKET LOST THE TARGET");
          else if (bowman.rocket.flying && homing) std::snprintf(buf, sizeof buf, bowArrow ? "ROCKET LOCKED ON - ON ITS WAY" : "ROCKET LOCKED ON - ON ITS WAY   F7 FOLLOW IT");
          else if (bowman.rocket.flying && bowArrow) std::snprintf(buf, sizeof buf, "ROCKET IN THE AIR");
          else if (bowman.rocket.flying) std::snprintf(buf, sizeof buf, "ROCKET IN THE AIR - KEEP THE CROSS ON THE TARGET   F7 FOLLOW IT");
          else if (bowman.lawReady > 0.0f) std::snprintf(buf, sizeof buf, "M72 LAW - NEW TUBE IN %.0f S   ENTER BOW", std::ceil(bowman.lawReady));
          else if (bowman.locked) std::snprintf(buf, sizeof buf, "M72 LAW LOCKED - LEFT BUTTON FIRES   ENTER BOW");
          else std::snprintf(buf, sizeof buf, "M72 LAW - HOLD A TARGET IN THE CIRCLE TO LOCK ON   ENTER BOW");
        } else {
          switch (bowman.state) {
            case sim::Bowman::State::Ready: std::snprintf(buf, sizeof buf, "ARROW READY - HOLD LEFT BUTTON TO DRAW   ENTER M60   SPACE JUMP   N NEXT WAYPOINT"); break;
            case sim::Bowman::State::Drawing:
              if (landsKnown) std::snprintf(buf, sizeof buf, "DRAWN %.0f %%   RANGE %.0f M   RIGHT BUTTON LETS IT DOWN", bowman.draw * 100.0f, range);
              else std::snprintf(buf, sizeof buf, "DRAWN %.0f %%   RIGHT BUTTON LETS IT DOWN", bowman.draw * 100.0f);
              break;
            case sim::Bowman::State::Flying: std::snprintf(buf, sizeof buf, bowArrow ? "ARROW IN THE AIR" : "ARROW IN THE AIR   F7 FOLLOW IT   ENTER M60"); break;
            case sim::Bowman::State::Fitting: std::snprintf(buf, sizeof buf, "SCREWING ON THE WARHEAD - %.0f S   ENTER M60", std::ceil(bowman.fit)); break;
          }
        }
        bh.bowInfo = buf;
        if (bowman.weapon == sim::Bowman::Weapon::Law && !bowman.dead() && !bowOrbit && !bowArrow) {
          // The lock cone on the screen; what it is locking on.
          bh.lawCone = std::tan(sim::Bowman::kLockCone) / std::tan(fov * 0.5f) * float(dh) * 0.5f;
          if (bowman.aimHind >= 0 || bowman.aimUnit >= 0) {
            const glm::vec4 clip = vpWorld * glm::vec4(bowman.aimAt, 1.0f);
            if (clip.w > 0.1f) {
              bh.lawBox = {(clip.x / clip.w * 0.5f + 0.5f) * float(dw), (clip.y / clip.w * 0.5f + 0.5f) * float(dh)};
              bh.lawLock = std::min(1.0f, bowman.aimFor / sim::Bowman::kLockTime);
            }
          }
        }
        bh.bowHealth = bowman.health / sim::Bowman::kHealth;
        if (SDL_GetTicks64() < bowMessageUntil) bh.message = bowMessage;
        bh.page = page;
        bh.dim = pauseDim;
        hd = bh;
      }
      if (watchedHind) {
        const std::string k = input.watchKeyName();
        hd.watchHint = k + " / SHIFT+" + k + " NEXT HIND    CTRL+Q " + (joining ? "LEAVE THE GAME" : "END THE MISSION");
      }
      // (Blood over his eyes, under what the HUD says: his health is seen through it.)
      if (bowmanGame && bowman.blood > 0.0f) bloodView.apply(dw, dh, bowBloodSeed, bowman.blood);
      if (!cleanShot) hud.draw(hd, dw, dh);
      if (endAsk && gaugesReady) gauges.endMission(dw, dh);
      if (nightVision || (camera.mode == render::ViewMode::Drone && pal.lamps > 0.2f)) nightVisionView.apply(dw, dh);
      if (bowmanGame && (bowman.pain > 0.0f || bowman.dead())) hurtView.apply(dw, dh, 0.0f, bowman.dead() ? 0.7f : bowman.pain);
      if (camera.mode == render::ViewMode::Drone) {
        // Its picture over the radio: worse with the range from us (clean within 300 m, poor at 4 km).
        const float range = droneEye != glm::vec3(0.0f) ? glm::distance(droneEye, heli.pos) : 0.0f;
        droneFeed.apply(dw, dh, float(SDL_GetTicks64() % 600000) / 1000.0f, std::clamp((range - 300.0f) / 3700.0f, 0.0f, 1.0f));
      }
    }

    sound.update(frameDt, heard, heli, weapons, enemy);
    sound.duckMusic(sound.speaking());  // (the debriefing's tune under its speech)
    sound.drones(targets, heli.vel);
    // The wingman's game: his Hind, his rounds and what they hit, 60 times a second.
    if (wingmanGame && link.open()) {
      netTimer += frameDt;
      if (netTimer >= 1.0 / 60.0) {
        netTimer = 0.0;
        net::Writer w;
        net::Mirror::writeHeli(w, heli);
        w.vec(weapons.shots), w.vec(peerHits);
        w.pod(enemy.autoFlares), w.pod(enemy.autoChaff), w.pod(enemy.jammer);
        link.send(net::Wingman, w.bytes);
        peerHits.clear();
      }
      link.flush();
    }
    // The gunner's game: what he does goes to the pilot's, up to 60 times a second.
    // The lone bowman's game: where he is and his arrow, where it went off, 60 times a second.
    if (bowmanGame && link.open()) {
      netTimer += frameDt;
      if (netTimer >= 1.0 / 60.0 || !bowmanBursts.empty()) {
        netTimer = 0.0;
        net::Writer w;
        // (Of his M60's rounds only the tracers: the others see no more.)
        std::vector<sim::Projectile> tracers;
        for (const sim::Projectile& p : bowman.rounds)
          if (p.tracer && p.age < 1e9f) tracers.push_back(p);
        if (bowman.rocket.flying) tracers.push_back(lawRocket(bowman.rocket));
        w.pod(net::BowmanPose::of(bowman)), w.vec(bowmanBursts), w.vec(tracers), w.vec(bowmanHits);
        link.send(net::Bowman, w.bytes);
        bowmanBursts.clear(), bowmanHits.clear();
      }
      link.flush();
    }
    if (mirrored && !bowmanGame && link.open()) {
      // Flying his interceptor he is still at his door or sight (its gun not fired meanwhile).
      const bool droneSight = camera.mode == render::ViewMode::Drone && droneSeat == 1;
      const bool atDoor = camera.mode == render::ViewMode::Door || (camera.mode == render::ViewMode::Drone && !droneSight);
      gunnerIn.seat = camera.mode == render::ViewMode::Gunner || droneSight ? 1 : atDoor ? 2 + camera.doorSide : 0;
      gunnerIn.trigger = camera.mode != render::ViewMode::Drone && (input.fire() || (!autoFire.empty() && !armOnly));
      gunnerIn.az = camera.lookYaw(), gunnerIn.el = camera.lookPitch();
      if (camera.mode != render::ViewMode::Drone) gunnerIn.dirBody = glm::inverse(orient) * sightDir;
      netTimer += frameDt;
      if (netTimer >= 1.0 / 60.0 || !gunnerActions.empty()) {
        netTimer = 0.0;
        net::Writer w;
        w.pod(gunnerIn), w.vec(gunnerActions);
        link.send(net::Input, w.bytes);
        gunnerIn.aiming = 0;
        gunnerActions.clear();
      }
      link.flush();
    }
    // Our game's other players (the host's, or our crew when we fly a wingman's Hind): newcomers
    // let in, the start of each flight and then its snapshots to each, what they do read back.
    if ((hosting && !joining) || crewHost) {
      announce();
      if (peers.size() < size_t(kMaxPeers)) {
        auto np = std::make_unique<Peer>();
        if (listener.accept(np->link)) {
          std::printf("Network game: a player joined from %s\n", np->link.peer().c_str());
          peers.push_back(std::move(np));
        }
      }
      const auto upper = [](std::string t) {
        for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
        return t;
      };
      // What a snapshot carries of the game's other Hinds: not the one it is of, nor the
      // receiver's own.
      const auto othersFor = [&](int slot) {
        std::vector<net::HindPose> v;
        if (joining) {
          if (peerFlying) v.push_back(net::HindPose::of(peerHeli));
          for (const auto& h : otherHelis) v.push_back(net::HindPose::of(*h));
        }
        for (size_t k = 0; k < wings.size(); ++k)
          if (wings[k]->flying && int(k) != slot) v.push_back(net::HindPose::of(wings[k]->heli)), v.back().id = int8_t(k + 1);
        return v;
      };
      // Up to 60 snapshots a second, the same moment to all (the events since the last go to each).
      netTimer += frameDt;
      const bool snapshotDue = netTimer >= 1.0 / 60.0;
      if (snapshotDue) netTimer = 0.0;
      const int64_t now = int64_t(SDL_GetTicks64());
      for (auto& pp : peers) {
        Peer& p = *pp;
        if (!p.link.open()) continue;
        if (planning) {
          if (!p.waitSent) {
            if (debriefing) {
              std::vector<std::string> lines{mission.title + " - " + sim::MissionState::resultName(missionState.result())};
              lines.insert(lines.end(), debriefLines.begin(), debriefLines.end());
              sendLines(p.link, lines);
            } else {
              sendLines(p.link, {haveMission ? mission.title : std::string("NETWORK GAME"), "THE PILOT IS PLANNING THE FLIGHT."});
            }
          }
          p.waitSent = true, p.startSent = false;
        } else if (p.view.empty()) {
          // Nothing until his Hello says where he sits.
        } else if (!p.wingman() || p.slot >= 0) {
          if (!p.startSent) {
            net::StartInfo st;
            st.generation = joining ? joined.generation : netGeneration;
            st.zone = cfg.getString("world", "zone", "");
            st.mission = haveMission ? mission.name : "";
            st.loadout = loadout;
            st.home = crewHome, st.heading = startHeading;
            if (huntAir || (joining && airStarted)) {
              // In the air: by our Hind where it flies now (a wingman joining later too).
              st.airStart = 1, st.infiniteFuel = 1, st.airAltitude = kHuntAltitude;
              if (!heli.crashed) {
                const glm::vec3 f = heli.forward();
                st.home = {heli.pos.x, heli.pos.z}, st.heading = std::atan2(f.x, -f.z);
                st.airAltitude = std::max(heli.pos.y - terrain.groundHeight(heli.pos.x, heli.pos.z), 50.0f);
              } else if (huntAir) {
                st.home = huntHome, st.heading = huntHeading;
              }
            }
            st.flightModel = int(heli.model);
            st.hour = haveMission ? mission.startHour : freeHour, st.weather = freeWeather;
            net::Mirror::route(missionState, st.route, st.routeAlts);
            st.targets = uint32_t(targets.list.size());
            st.slot = uint32_t(std::max(p.slot, 0));
            st.game = hosting ? netGame : joinGame;
            st.infiniteWeapons = weapons.infiniteAmmo ? 1 : 0, st.doorReloads = weapons.doorReloads ? 1 : 0, st.noCrash = heli.noCrash ? 1 : 0;
            net::Writer w;
            st.write(w);
            p.link.send(net::Start, w.bytes);
            p.inputAt = now + 20000;  // a new world takes him a while to set up
            p.mirror.reset();
            p.startSent = true, p.waitSent = false;
          }
          // None while his game is behind with reading them.
          if (snapshotDue && p.link.backlog() < (1u << 20)) {
            net::Frame fr;
            fr.simTime = simTime, fr.paused = paused, fr.controls = input.controls(), fr.cyclicKeys = input.cyclicKeys();
            fr.page = page;
            // Who is at which door, by the seat he said (he may be looking from outside just now);
            // whoever comes to a door taken already moves to the other one, or out with none left.
            const auto doorOf = [](const Peer& q) { return q.wingman() || !q.aboard || !q.link.open() ? -1 : q.view == "door-left" ? 0 : q.view == "door-right" ? 1 : -1; };
            bool first = true, late = false;
            for (const auto& q : peers) {
              if (q.get() == &p) {
                first = false;
                continue;
              }
              if (doorOf(*q) >= 0) fr.doorsTaken |= uint8_t(1 << doorOf(*q));
              if (first && doorOf(*q) == doorOf(p) && doorOf(p) >= 0) late = true;
              if (first && p.view == "gunner" && q->view == "gunner" && !q->wingman() && q->aboard && q->link.open()) fr.doorMove = 3;
            }
            if (late) {
              const int other = 1 - doorOf(p);
              fr.doorMove = weapons.loadout().doorGun[other] && !(fr.doorsTaken & (1 << other)) ? uint8_t(other + 1) : uint8_t(3);
            }
            if (p.slot >= 0 && wings[size_t(p.slot)]->flying) {
              Wing& wg = *wings[size_t(p.slot)];
              fr.wingmanDamage = wg.heli.damage, fr.wingmanMessage = wg.message;
              if (size_t(p.slot) < enemy.wingmen.size())
                fr.wingmanFlares = enemy.wingmen[size_t(p.slot)].flares, fr.wingmanChaff = enemy.wingmen[size_t(p.slot)].chaff;
            }
            fr.others = othersFor(p.slot);
            if (joining) fr.bowmen = netFrame.bowmen;
            fr.wingStores = wingStores(), fr.wingShots = wingShots();
            for (const auto& o : peers)
              if (o.get() != &p && o->bowman() && o->onFoot && o->link.open()) {
                fr.bowmen.push_back(o->pose);
                // (His M60's tracers drawn with the AI Hinds' rounds.)
                fr.wingShots.insert(fr.wingShots.end(), o->tracers.begin(), o->tracers.end());
              }
            net::Writer w;
            p.mirror.write(w, joining ? joined.generation : netGeneration, netWorld, fr, netEvents);
            p.link.send(net::Snapshot, w.bytes);
          }
        } else if (!p.waitSent) {
          sendLines(p.link, {"NETWORK GAME", "NO HIND IS LEFT FOR YOU IN THIS GAME."});
          p.waitSent = true;
        }
        p.link.flush();
        uint8_t type;
        std::vector<uint8_t> bytes;
        while (p.link.receive(type, bytes)) {
          net::Reader r(bytes);
          if (type == net::Chat) {
            // A message: heard here, and passed on to the others it is for (the crew's to the
            // rest of the crew, a pilot's to the other pilots).
            uint8_t number = 0;
            std::string who;
            r.pod(number), r.str(who);
            if (!r.ok()) continue;
            showChat(who, number);
            for (auto& o : peers)
              if (o.get() != &p && !o->view.empty() && !o->bowman() && (number <= 10 ? !o->wingman() : number > 20 && o->wingman()))
                sendChat(o->link, number, who);
            continue;
          }
          if (type == net::Hello) {
            const bool known = !p.name.empty();
            if (readHello(p, r) && !known) heli.setMessage(upper(p.label()) + " JOINED", 3.0f);
            continue;
          }
          if (type == net::Wingman && p.slot >= 0 && hosting) {
            // A Hind of his own: drawn where he flies it, what his rounds hit applied here. The
            // hits it takes are counted here, not in his game.
            Wing& wg = *wings[size_t(p.slot)];
            const sim::Helicopter::Damage taken = wg.heli.damage;
            net::Mirror::readHeli(r, wg.heli);
            if (wg.flying) {
              wg.heli.damage = taken;
              if (wg.heli.crashed && !wg.down) {
                heli.setMessage(upper(p.label()) + " DOWN", 4.0f);
                missionState.wingmanLost(wg.heli.damage.shotDown ? wg.heli.damage.lastHitBy : "");
              }
            }
            wg.down = wg.heli.crashed;
            std::vector<sim::Impact> hits;
            if (enemy.wingmen.size() <= size_t(p.slot)) enemy.wingmen.resize(size_t(p.slot) + 1);
            sim::EnemyFire::Launchers& l = enemy.wingmen[size_t(p.slot)];
            r.vec(wg.weapons.shots), r.vec(hits);
            r.pod(l.autoFlares), r.pod(l.autoChaff), r.pod(l.jammer);
            if (!r.ok()) continue;
            p.inputAt = now;
            if (!wg.flying) {
              heli.setMessage(upper(p.label()) + " IN THE AIR", 3.0f);
              // A mission's AI wingman makes way for him.
              for (size_t k = 0; k < targets.list.size(); ++k)
                if (sim::vehicleType(targets.list[k].kind).original == "Mi_24_Hind_Wingman" && !targets.list[k].aboard) {
                  targets.list[k].aboard = true, wg.standIns.push_back(int(k));
                  break;
                }
            }
            wg.flying = true;
            // (Kept for the next frame's impacts: the blasts, the mines, the snapshots.)
            for (const sim::Impact& i : hits) {
              if (i.target >= int(targets.list.size())) continue;
              onHinds(i, p.slot + 1, upper(p.label()));
              peerHits.push_back(i);
              const bool blast = i.target >= 0 || (i.type != sim::WeaponType::YakB && i.type != sim::WeaponType::Upk23 && i.type != sim::WeaponType::Mine);
              if (!blast) continue;
              for (int k : targets.damage(i.target, i.type, i.pos, sim::Killer::Wingman, p.label())) {
                const sim::Vehicle& v = targets.list[size_t(k)];
                peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
              }
            }
            continue;
          }
          if (type == net::Bowman && p.bowman() && hosting) {
            // The lone bowman: drawn where he says he is; his warheads go off here, a rocket's
            // blast on the units, and what arrowBlast does to the Hinds near.
            std::vector<glm::vec3> bursts;
            std::vector<sim::Impact> hits;
            const net::BowmanPose was = p.pose;
            r.pod(p.pose), r.vec(bursts), r.vec(p.tracers), r.vec(hits);
            if (!r.ok()) continue;
            p.inputAt = now, p.onFoot = true;
            // His LAW locked on a Hind, its rocket off at one: the crew told (as of a Stinger's).
            const auto warn = [&](int8_t id, const char* what) {
              sim::Helicopter* h = id == 0 ? &heli : id > 0 && size_t(id - 1) < wings.size() && wings[size_t(id - 1)]->flying ? &wings[size_t(id - 1)]->heli : nullptr;
              if (!h) return;
              h->setMessage(std::string(what) + sim::clockOf(*h, p.pose.pos) + " O'CLOCK", 4.0f);
              std::printf("Network game: %s's LAW to Hind %d: %s\n", p.label().c_str(), int(id), h->message.c_str());
              if (id > 0) wings[size_t(id - 1)]->message = h->message;
            };
            if (p.pose.lockHind >= 0 && p.pose.lockHind != was.lockHind) warn(p.pose.lockHind, "MISSILE LOCK ");
            if (p.pose.rocketHind >= 0 && p.pose.rocketHind != was.rocketHind) warn(p.pose.rocketHind, "MISSILE LAUNCH ");
            // His M60's rounds: on a Hind, on a unit, in the ground.
            if (const long onHind = std::count_if(hits.begin(), hits.end(), [](const sim::Impact& i) { return i.hind >= 0 && i.type != sim::WeaponType::Shturm; }); onHind > 0)
              std::printf("Network game: %ld of %s's M60 rounds hit a Hind\n", onHind, p.label().c_str());
            for (sim::Impact& i : hits) {
              if (i.target >= int(targets.list.size())) continue;
              if (i.type == sim::WeaponType::Shturm) {
                // His LAW's rocket: on the Hinds near, on the units.
                std::printf("Network game: %s's LAW rocket went off %.0f m from our Hind\n", p.label().c_str(), glm::distance(i.pos, heli.pos));
                peerHits.push_back({sim::WeaponType::Shturm, i.pos});
                sim::lawBlast(heli, i.pos);
                for (auto& w : wings)
                  if (w->flying && sim::lawBlast(w->heli, i.pos)) w->message = w->heli.message;
                for (int k : targets.damage(i.target, sim::WeaponType::Shturm, i.pos, sim::Killer::None, p.label())) {
                  const sim::Vehicle& v = targets.list[size_t(k)];
                  peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
                }
                continue;
              }
              i.type = sim::WeaponType::DoorGun, i.destroyed = false;
              onHinds(i, -1, "A LONE BOWMAN");
              peerHits.push_back(i);
              if (i.target < 0) continue;
              for (int k : targets.damage(i.target, i.type, i.pos, sim::Killer::None, p.label())) {
                const sim::Vehicle& v = targets.list[size_t(k)];
                peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
              }
            }
            for (const glm::vec3& at : bursts) {
              std::printf("Network game: %s's arrow went off %.0f m from our Hind\n", p.label().c_str(), glm::distance(at, heli.pos));
              peerHits.push_back({sim::WeaponType::S8, at});
              sim::arrowBlast(heli, at);
              for (auto& w : wings)
                if (w->flying && sim::arrowBlast(w->heli, at)) w->message = w->heli.message;
              for (int k : targets.damage(-1, sim::WeaponType::S8, at, sim::Killer::None, p.label())) {
                const sim::Vehicle& v = targets.list[size_t(k)];
                peerHits.push_back({sim::WeaponType::S8, v.pos + glm::vec3(0, 1.2f, 0), k, true});
              }
            }
            continue;
          }
          if (type != net::Input) continue;
          const uint8_t aiming = p.input.aiming;
          std::vector<uint8_t> acts;
          r.pod(p.input), r.vec(acts);
          p.input.aiming |= aiming;
          if (!r.ok()) continue;
          p.inputAt = now;
          if (!p.aboard || p.input.seat != p.seat) {
            // Where he is: the pilot hears it as the crew would tell him.
            const uint8_t st = p.input.seat;
            heli.setMessage(upper(p.label()) + (st == 2 ? " AT THE LEFT DOOR" : st == 3 ? " AT THE RIGHT DOOR" : st == 1 ? " AT THE SIGHT" : " ON BOARD"), 3.0f);
            p.seat = st;
          }
          p.aboard = true;
          const glm::vec3 eye = heli.pos + heli.orient * render::kGunnerEye, dir = heli.orient * p.input.dirBody;
          for (uint8_t a : acts) {
            if (a == uint8_t(net::GunnerAction::Wso)) wso.toggle(heli);
            if (a == uint8_t(net::GunnerAction::Rescan)) wso.rescan(heli, targets);
            if (a == uint8_t(net::GunnerAction::Track)) wso.track(heli, targets, eye, dir);
            if (a == uint8_t(net::GunnerAction::DoorGunners)) toggleDoorGunners();
          }
        }
        const bool active = p.aboard || p.onFoot || (p.slot >= 0 && wings[size_t(p.slot)]->flying);
        if (p.link.open() && active && !planning && p.startSent && now > p.inputAt + int64_t(kNetSilenceMs)) {
          std::printf("Network game: nothing from %s for %d s\n", p.label().c_str(), int(kNetSilenceMs / 1000));
          p.link.close();
        }
      }
      if (snapshotDue) {
        netEvents.clear();
        for (auto& w : wings) w->message.clear();
      }
      // The gone: a wingman's Hind leaves the sky, a crewman's place is the computer's again.
      bool crewLeft = false;
      for (auto& pp : peers) {
        Peer& p = *pp;
        if (p.link.open()) continue;
        if (p.slot >= 0) {
          Wing& wg = *wings[size_t(p.slot)];
          if (wg.flying) {
            std::printf("Network game: %s's Hind left\n", p.label().c_str());
            heli.setMessage(upper(p.label()) + "'S HIND LEFT", 3.0f);
          }
          wg.flying = wg.down = false;
          wg.weapons.shots.clear();
          for (int k : wg.standIns) targets.list[size_t(k)].aboard = false;
          wg.standIns.clear();
        } else if (p.aboard) {
          std::printf("Network game: %s left\n", p.label().c_str());
          heli.setMessage(upper(p.label()) + " LEFT", 3.0f);
          crewLeft = true;
        }
      }
      const size_t was = peers.size();
      std::erase_if(peers, [](const auto& p) { return !p->link.open(); });
      if (peers.size() != was) advertAt = -10.0;
      // The silicon WSO takes the sight again when nobody is at it.
      if (crewLeft && !wso.enabled() && std::none_of(peers.begin(), peers.end(), [](const auto& p) { return p->aboard && !p->wingman(); }))
        wso.toggle(heli);
    }
    // Scripted planning (development): after the first frame, when the screen layout is known.
    if (planning && frameCount == 0 && !planKeys.empty())
      for (const SDL_Event& ev : scriptedEvents(planKeys, [&](glm::vec2 p) { return planner.windowPoint(p); }))
        if (planner.handleEvent(ev) == menu::Result::TakeOff) takeOff();
    ++frameCount;
    // The shot at its time, or at the debriefing when the mission ends before (the game pauses).
    if (!screenshotPath.empty() && (shotTime >= 0.0 ? simTime >= shotTime || debriefing : frameCount == 120)) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, shotFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shotResolve);
      glBlitFramebuffer(0, 0, dw, dh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, shotResolve);
      if (shotCam) {
        const glm::vec3 f = heli.forward(), r = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
        std::printf("Shot camera at %.3f,%.3f,%.3f (%.4f s), the Hind's forward %.3f,%.3f,%.3f, right %.3f,%.3f,%.3f\n", lastEye.x,
                    lastEye.y, lastEye.z, simTime, f.x, f.y, f.z, r.x, r.y, r.z);
      }
      if (shotFrames > 0) {
        const std::filesystem::path base(screenshotPath);
        char num[16];
        std::snprintf(num, sizeof num, "-%04d", shotSaved);
        saveScreenshot((base.parent_path() / (base.stem().string() + num + base.extension().string())).string(), dw, dh);
      } else {
        saveScreenshot(screenshotPath, dw, dh);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (++shotSaved >= shotFrames || debriefing) running = false;
    }
    if (screenWanted && !shotFbo) {
      screenWanted = false;
      std::string dir = cfg.getString("data", "screenshots", "");
      if (dir.empty()) dir = platform::defaultScreenshotDir();
      const std::string saved = saveScreenBmp(dir, dw, dh);
      if (saved.empty()) {
        std::fprintf(stderr, "Screenshot: cannot write to %s\n", dir.c_str());
        heli.setMessage("SCREENSHOT FAILED", 2.0f);
      } else {
        std::printf("Saved %s\n", saved.c_str());
        heli.setMessage("SCREENSHOT SAVED", 2.0f);
      }
    }
    if (!shotFbo) SDL_GL_SwapWindow(window);

    fpsTimer += frameDt;
    ++fpsFrames;
    if (fpsTimer >= 0.5) {
      char title[96];
      std::snprintf(title, sizeof title, "HIND REBUILT - %dx%d - %.0f fps", dw, dh, fpsFrames / fpsTimer);
      SDL_SetWindowTitle(window, title);
      fpsTimer = 0.0;
      fpsFrames = 0;
    }
    ++totalFrames;
  }
  double seconds = (SDL_GetPerformanceCounter() - startCounter) / freq;
  std::printf("%d frames in %.1f s, average %.0f fps\n", totalFrames, seconds, totalFrames / seconds);
  speak("");  // the briefing's or the debriefing's, left by Exit
  sound.stop();
  input.setMouseAim(false);
  for (auto& p : peers) {
    sendLines(p->link, {"NETWORK GAME", joining ? "YOUR PILOT WAITS FOR THE GAME'S HOST." : "THE PILOT IS IN THE GAME'S MENUS."});
    p->waitSent = true, p->startSent = false;
    p->aboard = false, p->seat = 0;
  }
  if (leaveGame) {
    // Out of the game joined, back to the menus.
    link.close();
    closePeers();
    joining = joinedFromMenu = leaveGame = false;
  }
  // After a campaign's mission: its page, how the campaign stands (and on to the next).
  if (campaignFlown && menuReady && !quitAll) front.openCampaign(cfg.getString("world", "zone", "afghan"));
  if (arcadeRun && menuReady && arcadeScore > 0) {
    std::printf("Arcade score: %d\n", arcadeScore);
    front.enterTopTen(arcadeScore, heli.crashed);
  }
  again = (menuReady || joining) && !quitAll && screenshotPath.empty();
  }
  }
  if (shotFbo) {
    glDeleteFramebuffers(1, &shotFbo);
    glDeleteFramebuffers(1, &shotResolve);
    glDeleteRenderbuffers(3, shotRb);
  }
  }
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
