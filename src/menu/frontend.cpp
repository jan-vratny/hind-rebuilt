#include "menu/frontend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#include "gamedata/original.hpp"
#include "gamedata/picture.hpp"

namespace menu {

namespace {

// What can be pressed. The stone buttons come first, in the order of kBrushes (their pressed
// pictures).
enum Id {
  kQuickstart, kFlight, kPrefs, kLog,
  kArcade, kInvincible, kTopTen,
  kTraining, kSingle, kCampaign, kNetwork, kTwoPlayer,
  kKazak, kKorea, kAfghan, kUkraine,
  kYes, kNo,
  kRecLast, kRecNext, kRecAir, kRecVehicles, kRecShips,
  kGoTo, kBack, kCommit, kInfinite, kNoCrash, kWsoAllies, kGunnerHints, kUkraineStores,
  kHelp, kGoPrefs, kGoLogs, kGoQuit, kGoRecognition, kGoAnimations,
  kLogCreate, kLogRename, kLogDelete,
  kPref0, kPrefLast = kPref0 + 13, kPrefJoyDevice, kPrefJoyLearn,
  kNetName, kNetGame, kNetHost, kNetSeat, kNetJoin, kNetAddress,
};
const char* const kBrushes[] = {"QUICK.BBM",    "FLIGHT.BBM",   "PREFS.BBM",    "LOG.BBM",     "ARCADE.BBM",
                                "INVIN.BBM",    "TOPTEN.BBM",   "TRAINING.BBM", "SINGLE.BBM",  "CAMPAIGN.BBM",
                                "NETWORK.BBM",  "TWO_PLYR.BBM", "W_KAZAK.BBM",  "W_KOREA.BBM", "W_AFGHAN.BBM",
                                nullptr,  // UKRAINE: ours, engraved from the others (FrontEnd::init)
                                "YES.BBM",      "NO.BBM",       "REC_LAST.BBM", "REC_NEXT.BBM", "REC_AIR.BBM",
                                "REC_VEHC.BBM", "REC_SHIP.BBM"};
constexpr int kStoneButtons = int(sizeof kBrushes / sizeof kBrushes[0]);

// Where the stone buttons are on their pictures (measured: the pressed brushes match there).
struct Stone {
  int id;
  glm::vec2 at;
  SDL_Scancode key;
  bool available;
};
const Stone kMainButtons[] = {{kQuickstart, {416, 42}, SDL_SCANCODE_Q, true},
                              {kFlight, {416, 100}, SDL_SCANCODE_F, true},
                              {kPrefs, {416, 159}, SDL_SCANCODE_P, true},
                              {kLog, {416, 217}, SDL_SCANCODE_L, true}};
const Stone kQuickButtons[] = {{kArcade, {415, 44}, SDL_SCANCODE_A, true},
                               {kInvincible, {415, 102}, SDL_SCANCODE_I, true},
                               {kTopTen, {415, 161}, SDL_SCANCODE_T, true}};
const Stone kFlightButtons[] = {{kTraining, {416, 42}, SDL_SCANCODE_T, true},
                                {kSingle, {416, 100}, SDL_SCANCODE_S, true},
                                {kCampaign, {416, 159}, SDL_SCANCODE_C, true},
                                {kNetwork, {416, 217}, SDL_SCANCODE_N, true},
                                {kTwoPlayer, {416, 276}, SDL_SCANCODE_W, true}};
const Stone kWorldButtons[] = {{kKazak, {415, 41}, SDL_SCANCODE_K, true},
                               {kKorea, {415, 101}, SDL_SCANCODE_R, true},
                               {kAfghan, {415, 158}, SDL_SCANCODE_A, true},
                               {kUkraine, {415, 216}, SDL_SCANCODE_U, true}};
const Stone kRecButtons[] = {{kRecLast, {416, 42}, SDL_SCANCODE_LEFT, true},
                             {kRecNext, {520, 42}, SDL_SCANCODE_RIGHT, true},
                             {kRecAir, {416, 99}, SDL_SCANCODE_A, true},
                             {kRecVehicles, {416, 159}, SDL_SCANCODE_V, true},
                             {kRecShips, {416, 217}, SDL_SCANCODE_S, true}};
const Stone kExitButtons[] = {{kYes, {267, 429}, SDL_SCANCODE_Y, true}, {kNo, {370, 429}, SDL_SCANCODE_N, true}};
const glm::vec2 kQuickOptionsAt{398, 2};  // QUICK_OP.BBM over the title menu's buttons

// The red buttons in the corner and the GoTo menu above them.
const glm::vec2 kGoToLo{583, 442}, kGoToHi{639, 458}, kBackLo{583, 464}, kBackHi{639, 480};
const glm::vec2 kGoToMenuLo{566, 347};
constexpr float kGoToMenuW = 73.0f, kGoToLine = 19.0f;

// The mission list: the steel panel on the left with a line per mission, the briefing on the
// right with the Commit button under it, the two options below.
const glm::vec2 kListLo{17, 27}, kListHi{323, 452};
const glm::vec2 kInfoLo{350, 27}, kInfoHi{620, 365};
constexpr float kRowTop = 34.0f, kRowH = 20.0f;
constexpr int kRows = 20;
const glm::vec2 kCommitLo{532, 341}, kCommitHi{612, 357};
const glm::vec2 kInfiniteLo{352, 375}, kInfiniteHi{484, 391};
const glm::vec2 kNoCrashLo{486, 375}, kNoCrashHi{618, 391};
const glm::vec2 kWsoAlliesLo{352, 393}, kWsoAlliesHi{484, 409};
const glm::vec2 kGunnerHintsLo{486, 393}, kGunnerHintsHi{618, 409};
// Quickstart's choice of the war: what the Hind carries over Ukraine, under the Ukraine stone.
const glm::vec2 kUkraineStoresLo{436, 272}, kUkraineStoresHi{602, 288};
constexpr float kInfoLine = 16.0f;
const glm::vec2 kTextLo{357, 36}, kTextHi{614, 334};

// The Pilot's Log (LOG.LBM): its three panes of glass, the pilots, the rank and medals, the
// details; its buttons over the picture's lower right.
const glm::vec2 kPilotsLo{20, 36}, kPilotsHi{328, 257};
const glm::vec2 kRankLo{345, 36}, kRankHi{623, 185};
const glm::vec2 kDetailLo{20, 272}, kDetailHi{288, 455};
constexpr float kPilotRow = 20.0f;
const glm::vec2 kLogCreateLo{486, 375}, kLogCreateHi{618, 391};
const glm::vec2 kLogRenameLo{486, 393}, kLogRenameHi{618, 409};
const glm::vec2 kLogDeleteLo{486, 411}, kLogDeleteHi{618, 427};
const char* const kMedalBrushes[3] = {"MED_RED.BBM", "MED_LENI.BBM", "MED_GOLD.BBM"};
const char* const kMedalPictures[3] = {"MED_RED.LBM", "MED_LENI.LBM", "MED_GOLD.LBM"};
const char* const kPromotionPictures[5] = {"", "PRM_LT.LBM", "PRM_CPT.LBM", "PRM_MJR.LBM", "PRM_LCOL.LBM"};

// The network game (NETWORK.LBM): the games on the glass on the left, a line a pilot's game
// (SERVER.BBM by the hosts'), the steel bars on the right: the player's name, the game's name,
// Host, the seat, Join, and the short one an address to join by hand.
const glm::vec2 kGamesLo{20, 20}, kGamesHi{320, 460};
constexpr float kGameRowTop = 46.0f, kGameRowH = 50.0f;
constexpr int kGameRows = 8;
const glm::vec2 kNetNameLo{350, 194}, kNetNameHi{622, 216};
const glm::vec2 kNetGameLo{350, 235}, kNetGameHi{622, 257};
const glm::vec2 kNetHostLo{350, 266}, kNetHostHi{622, 288};
const glm::vec2 kNetSeatLo{350, 297}, kNetSeatHi{622, 319};
const glm::vec2 kNetJoinLo{350, 329}, kNetJoinHi{622, 351};
const glm::vec2 kNetAddressLo{483, 370}, kNetAddressHi{621, 392};

// The recognition library (V_RECON.LBM): the units of the manual's Vehicle Inventory (pp. 88-91)
// as the original game has them, and ours; the model on the left, its name under it.
struct RecUnit {
  const char* name;
  const char* object;  // in 3DOBJS.BIN (or our own, render::ModelViewer)
  const char* who;
  int voice;  // the WSO naming it (SOUND/MAXIMAL.BLK block), -1 none
};
const RecUnit kRecAircraftUnits[] = {
    {"Mil Mi-24V Hind-E", "Mi_24_Hind_Wingman", "Allies; enemy in Kazakstan", 177},
    {"Mil Mi-17 Hip-H", "Mi_17_Hip_H", "Allies; enemy in Kazakstan and Pakistan", 178},
    {"AH-64D Apache Longbow", "AH64D_LUX", "Enemy in Korea", 201},
    {"UH-60 Blackhawk", "UH_60_Blackhawk", "Enemy in Korea", 202},
    {"AH-1W SuperCobra", "AH_1W_SuperCobra", "Enemy in Pakistan", 203},
    {"MiG-29 Fulcrum", "MiG_29_Fulcrum", "Allies; enemy in Kazakstan", 180},
    {"MiG-27 Flogger", "MiG_27_Flogger", "Allies; enemy in Kazakstan", 179},
    {"MiG-21bis Fishbed", "MIG_21_bis", "Allies", -1},
    {"Su-25 Frogfoot", "Su_25_Frogfoot", "Allies; enemy in Kazakstan", 181},
    {"Su-24 Fencer", "Su_24", "Allies", -1},
    {"An-12BP Cub-A", "Antonov_An_12BP_Cub_A", "Allies; enemy in Kazakstan", 182},
    {"A-50 Mainstay", "A50_Mainstay", "Allies", -1},
    {"F-15 Eagle", "F15", "Enemy in Korea", 205},
    {"F-16 Fighting Falcon", "F16", "Enemy in Korea and Pakistan", 206},
    {"F/A-18 Hornet", "F18_Hornet", "Enemy in Korea", 207},
    {"F-14A Tomcat", "F_14A_Tomcat", "Enemy in Korea", 204},
    {"A-10 Thunderbolt", "A10_Thunderbolt", "Enemy in Korea", 208},
    {"C-130 Hercules", "C130_Hercules", "Enemy in Korea and Pakistan", 209},
    {"E-3 Sentry AWACS", "Boeing_E3_Sentry_AWACS", "Enemy in Korea", -1},
    {"Shahed-136", "Shahed_136", "Enemy in Ukraine (also the Gerbera decoy)", 230},
    {"Geran-3 (jet Shahed)", "Geran_3", "Enemy in Ukraine", 230},
    {"S8000 Banderol cruise missile", "Banderol", "Enemy in Ukraine", -1},
    {"Litavr interceptor drone", "Litavr", "Ours in Ukraine", -1},
};
const RecUnit kRecVehicleUnits[] = {
    {"T-62 tank", "T62_MBT", "Allies; enemy in Kazakstan and Pakistan", 183},
    {"T-72 tank", "T72_MBT", "Allies", -1},
    {"T-80 tank", "T80_MBT", "Allies", 184},
    {"M1A1 Abrams tank", "M1A1_Abrams_MBT", "Enemy in Korea", 210},
    {"Challenger 1 tank", "Challenger_1_MBT", "Enemy", 211},
    {"Leopard 1 tank", "Leopard_1_MBT", "Enemy", 212},
    {"Merkava Mk3 tank", "Merkava_Mk3_MBT", "Enemy", -1},
    {"BMP-2 APC", "BMP2_APC", "Allies; enemy in Kazakstan", 185},
    {"BRDM-2 APC", "BRDM_2_APC", "Allies; enemy in Kazakstan", 186},
    {"BRDM-2 with Sagger", "BRDM_2_Sagger", "Allies; enemy in Kazakstan", 187},
    {"MT-LBu command APC", "MT_LBU_Command_APC", "Allies; enemy in Kazakstan", 188},
    {"MT-LBus ECM", "MT_LBus_Jamming_Vehicle", "Allies; enemy in Kazakstan", 189},
    {"M113 APC", "M113APC_basic", "Enemy in Korea and Pakistan", 213},
    {"Warrior APC", "Warrior_APC", "Enemy", 214},
    {"SA-8 Gecko (Romb) SAM", "SA8_ROMB_Mobile_SAM", "Allies; enemy in Kazakstan", 193},
    {"SA-9 Gaskin SAM", "SA_9_Gaskin_SP_SAM", "Allies; enemy in Kazakstan", 194},
    {"SA-13 Gopher SAM", "SA13_Strela_Mobile_SAM", "Allies", 195},
    {"ZSU-23-4 Shilka AAA", "ZSU23_Shilka_AAA", "Allies; enemy in Kazakstan", 196},
    {"M163 Vulcan AAA", "M163_Vulcan", "Enemy in Korea", 223},
    {"35mm Oerlikon GDF SPAAG", "GDF_D03_SPAAG", "Enemy in Pakistan", 224},
    {"Tracked Rapier SAM", "Tracked_Rapier_SPSAM", "Enemy in Pakistan", 222},
    {"Patriot SAM launcher", "Patriot_Launcher", "Enemy in Korea", 221},
    {"Long Track radar", "Long_Track_Mobile_EWR", "Allies; enemy in Kazakstan", 190},
    {"Giraffe mobile radar", "Giraffe_Mobile_Radar", "Enemy in Korea", 218},
    {"D-30 howitzer", "D_30_Gun_Howitzer", "Allies; enemy in Kazakstan and Afghanistan", 192},
    {"M109 howitzer", "M109_SPH", "Enemy in Korea and Pakistan", 220},
    {"MLRS", "MLRS_AFV", "Enemy in Korea", 215},
    {"BM-21 Grad", "BM_21_MRL", "Allies", -1},
    {"MAZ-543 SCUD", "maz543", "Allies; enemy in Kazakstan", 197},
    {"M37 82mm mortar", "M37_82mm_Mortar", "Allies; enemy in Kazakstan and Afghanistan", -1},
    {"Silkworm launcher", "Silkworm_Launcher", "Enemy", 198},
    {"UAZ-469 light utility vehicle", "UAZ_469_Russian_Jeep", "Allies; enemy in Kazakstan", 219},
    {"HMMWV", "HMMWV_Utility_Vehicle", "Enemy in Korea", 191},
    {"Military truck", "Truck_Military_A", "Everywhere", 249},
    {"Fuel bowser", "Fuel_Bowser_Small", "Everywhere", 250},
    {"Train", "Generic_Diesel_Loco", "Everywhere", 258},
    {"12.7mm DShK heavy machine gun", "DShK_HMG_Tripod", "Enemy in Kazakstan and Afghanistan", 291},
    {"12.7mm M2 heavy machine gun", "M2_HMG_Tripod", "Enemy in Korea", 292},
};
const RecUnit kRecShipUnits[] = {
    {"LHA Tarawa assault ship", "LHA_Tarawa", "Enemy in Korea", 225},
    {"Perry class frigate", "Frigate_OHP_Class", "Enemy in Korea", 226},
    {"MEKO 200 frigate", "MEKO_200_Class_FFG", "Enemy", 227},
    {"Romeo class submarine", "Romeo_Class_Sub", "Allies", 199},
    {"Type 209 submarine", "Type_209_SSK", "Enemy", 229},
    {"Osa missile boat", "Missile_Boat_OSA1", "Allies", 200},
    {"FPB-57 patrol boat", "FPB_57_Patrol_Boat", "Enemy", 228},
    {"Mine layer", "Mine_Layer", "Allies", 233},
    {"Mine sweeper", "Mine_Sweeper", "Enemy", 232},
    {"Landing craft", "USN_LCM6_Landing_Craft", "Enemy in Korea", -1},
    {"Oil tanker", "Oil_Tanker_250000dwt", "Everywhere", 251},
    {"Cargo ship", "Cargo_Ship_7500dwt", "Everywhere", 252},
    {"Tug", "Generic_Tug", "Everywhere", 253},
    {"Supply barge", "Military_Supply_Barge", "Everywhere", 231},
};
struct RecList {
  const RecUnit* units;
  int count;
  const char* title;
};
const RecList kRecLists[3] = {{kRecAircraftUnits, int(std::size(kRecAircraftUnits)), "Aircraft"},
                              {kRecVehicleUnits, int(std::size(kRecVehicleUnits)), "Vehicles"},
                              {kRecShipUnits, int(std::size(kRecShipUnits)), "Ships"}};
const glm::vec2 kRecModelLo{12, 12}, kRecModelHi{396, 420};

// The preferences (PREFSB.LBM): our settings on its steel bars, each click the next value (the
// right button the previous), saved to hind.ini at once; the glass on the top left tells what
// the setting chosen does, the panel on the bottom right shows the joystick.
struct Pref {
  const char* label;
  const char* section;
  const char* key;
  std::vector<std::pair<const char*, const char*>> options;  // value in hind.ini, shown
  const char* help;
};
const Pref kPrefList[14] = {
    // The left column: sound and picture.
    {"Sound", "audio", "enabled", {{"1", "On"}, {"0", "Off"}},
     "The original game's sounds and voices. At once."},
    {"Volume", "audio", "volume", {{"0.2", "20 %"}, {"0.4", "40 %"}, {"0.6", "60 %"}, {"0.8", "80 %"}, {"1.0", "100 %"}},
     "How loud everything is. At once."},
    {"Voices", "audio", "voices", {{"0.5", "Quiet"}, {"0.75", "Lower"}, {"1.0", "Normal"}, {"1.25", "Louder"}},
     "The voices (the WSO, the radio, the warnings) against the rest. At once."},
    {"Scenery", "world", "scenery", {{"1", "On"}, {"0", "Off"}},
     "The war zones' buildings, bridges, copses and runways. From the next flight."},
    {"View distance", "video", "view_distance", {{"8000", "8 km"}, {"12000", "12 km"}, {"16000", "16 km"}, {"24000", "24 km"}},
     "How far the land is drawn. From the next flight."},
    {"Full screen", "video", "fullscreen", {{"0", "Off"}, {"1", "On"}}, "The game in a window or the whole screen (also F11). At once."},
    {"Music", "audio", "music", {{"1", "On"}, {"0", "Off"}},
     "The original game's music from its CD (or its image) in the menus, not in the flight. At once."},
    // The right column: the flying and the fight.
    {"Flight model", "flight", "model", {{"novice", "Novice"}, {"stable", "Stable"}, {"realistic", "Realistic"}},
     "Novice flies itself level, Stable helps a little, Realistic is the Hind as it is. Also M in the flight."},
    {"Crashes", "flight", "no_crash", {{"0", "On"}, {"1", "Off"}}, "Off: the ground and the enemy cannot bring you down. From the next flight."},
    {"Enemy fire", "enemy", "fire", {{"1", "On"}, {"0", "Off"}}, "Whether the enemy shoots back in the missions. From the next flight."},
    {"Enemy skill", "enemy", "skill", {{"0.5", "Poor"}, {"1.0", "Average"}, {"1.5", "Sharp"}},
     "How fast the enemy reacts and how well he aims. From the next flight."},
    {"Gunner hints", "hud", "gunner_hints", {{"1", "On"}, {"0", "Off"}},
     "The door gunners' shot at the nearest drone on the HUD: how good it is, what to do for a better one."},
    {"Mouse aim", "mouse", "aim", {{"1", "On"}, {"0", "Off"}}, "The mouse turns the gunner's sight and the door guns in their views."},
    {"Antialiasing", "video", "msaa", {{"0", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}},
     "Smooths the edges of the polygons, at some cost in speed. From the next start of the game."},
};
constexpr int kPrefCount = 14;
// The bars: the left column under the glass, the right one in two groups.
const glm::vec2 kPrefBarLo[kPrefCount] = {{20, 251},  {20, 292},  {20, 323},  {20, 354},  {20, 385},  {20, 416},  {20, 447},
                                          {330, 24},  {330, 55},  {330, 86},  {330, 127}, {330, 158}, {330, 189}, {330, 220}};
constexpr float kPrefBarW = 290.0f, kPrefBarH = 22.0f;
const glm::vec2 kPrefGlassLo{20, 24}, kPrefGlassHi{310, 242};
// The joystick's panel: the device's name on top, under it the stick, the collective, the pedals
// and the hat; the setting of the focus after the bars.
const glm::vec2 kJoyPanelLo{332, 252}, kJoyPanelHi{619, 372};
constexpr float kJoyNameBottom = 282.0f;
constexpr int kPrefJoystick = kPrefCount;
// The steps of learning the axes: each control asked to move (the last one the hat).
const char* const kLearnAsk[platform::joy::kControls + 1] = {
    "Move the stick to the right", "Push the stick forward", "Raise the collective", "Push the right pedal",
    "Look to the right with an axis", "Look up with an axis", "Push the looking hat up"};

const glm::vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};
const glm::vec4 kInk{0.08f, 0.08f, 0.09f, 1.0f};  // engraved on the steel bars
const glm::vec4 kInkDim{0.30f, 0.30f, 0.32f, 1.0f};
const glm::vec4 kRed{142 / 255.0f, 56 / 255.0f, 56 / 255.0f, 1.0f};
const glm::vec4 kRedLight{190 / 255.0f, 77 / 255.0f, 77 / 255.0f, 1.0f};
const glm::vec4 kRedDark{80 / 255.0f, 30 / 255.0f, 30 / 255.0f, 1.0f};
const glm::vec4 kDimText{0.62f, 0.45f, 0.45f, 1.0f};

bool inside(glm::vec2 p, glm::vec2 lo, glm::vec2 hi) { return p.x >= lo.x && p.y >= lo.y && p.x < hi.x && p.y < hi.y; }

const char* listPrefix(const std::string& zone) {
  return zone == "kazak" ? "KAZAKS" : zone == "korea" ? "KOREAS" : zone == "ukraine" ? "UKRAIC" : "AFGHNS";
}

// The UKRAINE button is not in the original: its plate is KOREA's with the letters wiped off
// the face, engraved with letters cut from the original's buttons (each letter's engraving is
// what it adds to the face of its own button, row by row). The letters: where they are on a
// button (columns, from its left edge), on which button; the faces lie in rows 6..37.
struct Letter {
  char c;
  int button;  // 0 KOREA, 1 QUICK, 2 TRAINING
  int x0, x1;
};
const Letter kLetters[] = {{'K', 0, 64, 81}, {'R', 0, 100, 117}, {'E', 0, 117, 131}, {'A', 0, 131, 149},
                           {'U', 1, 42, 58}, {'I', 1, 59, 66},   {'N', 2, 100, 117}};
constexpr int kFaceTop = 6, kFaceBottom = 38;

// One of the three buttons as a picture and where it is on it.
struct Plate {
  const gamedata::Picture* pic = nullptr;
  glm::ivec2 at{0};
  glm::ivec3 px(int x, int y) const {
    const size_t i = (size_t(at.y + y) * size_t(pic->width) + size_t(at.x + x)) * 4;
    return {pic->rgba[i], pic->rgba[i + 1], pic->rgba[i + 2]};
  }
};

gamedata::Picture engrave(const Plate plates[3], const std::string& word, int width, int height) {
  gamedata::Picture out;
  out.width = width, out.height = height;
  out.rgba.resize(size_t(width * height) * 4);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const glm::ivec3 c = plates[0].px(x, y);
      const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 4;
      out.rgba[i] = uint8_t(c.r), out.rgba[i + 1] = uint8_t(c.g), out.rgba[i + 2] = uint8_t(c.b), out.rgba[i + 3] = 255;
    }
  auto set = [&](int x, int y, glm::ivec3 c) {
    const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 4;
    c = glm::clamp(c, glm::ivec3(0), glm::ivec3(255));
    out.rgba[i] = uint8_t(c.r), out.rgba[i + 1] = uint8_t(c.g), out.rgba[i + 2] = uint8_t(c.b);
  };
  // The face wiped: in each row the shade going over from its letterless left part to its
  // letterless right part (the lamp lights the world page from the left), with the grain of
  // the left part mirrored on across.
  const int src0 = 14, src1 = 60, right0 = 160, right1 = 192;
  for (int y = kFaceTop; y < kFaceBottom; ++y) {
    glm::ivec3 left{0}, right{0};
    for (int k = src0; k < src1; ++k) left += plates[0].px(k, y);
    for (int k = right0; k < right1; ++k) right += plates[0].px(k, y);
    left /= src1 - src0, right /= right1 - right0;
    for (int x = src1; x < right0; ++x) {
      const int span = src1 - src0, k = (x - src1) % (2 * span - 2);
      const int from = src0 + (k < span ? k : 2 * span - 2 - k);
      const float t = float(x - (src0 + src1) / 2) / float((right0 + right1) / 2 - (src0 + src1) / 2);
      const glm::ivec3 shade = glm::ivec3(glm::mix(glm::vec3(left), glm::vec3(right), t));
      set(x, y, shade + plates[0].px(from, y) - left);
    }
  }
  // The engraving of each letter: its button's pixels less that face's own colour in the row.
  std::vector<const Letter*> letters;
  int total = 0;
  for (char ch : word)
    for (const Letter& l : kLetters)
      if (l.c == ch) letters.push_back(&l), total += l.x1 - l.x0 + 1;
  int x = (width - total) / 2;
  for (const Letter* l : letters) {
    const Plate& p = plates[l->button];
    for (int y = kFaceTop; y < kFaceBottom; ++y) {
      glm::ivec3 sum{0};
      for (int k = 12; k < 24; ++k) sum += p.px(k, y);
      const glm::ivec3 face = sum / 12;
      for (int c = l->x0; c < l->x1; ++c) {
        const size_t i = (size_t(y) * size_t(width) + size_t(x + c - l->x0)) * 4;
        const glm::ivec3 under{out.rgba[i], out.rgba[i + 1], out.rgba[i + 2]};
        set(x + c - l->x0, y, under + p.px(c, y) - face);
      }
    }
    x += l->x1 - l->x0 + 1;
  }
  return out;
}

}  // namespace

const char* netSeatName(NetSeat s) {
  static const char* names[] = {"Pilot (own Hind)", "Gunner", "Left door gun", "Right door gun", "Lone bowman"};
  return names[int(s)];
}

const char* netSeatView(NetSeat s) {
  static const char* views[] = {"wingman", "gunner", "door-left", "door-right", "bowman"};
  return views[int(s)];
}

bool FrontEnd::init(const std::string& originalDir) {
  if (originalDir.empty()) return false;
  dir_ = originalDir;
  if (!canvas_.init(gamedata::findOriginalFile(originalDir, "FONTS/11X15.LBM"))) return false;
  canvas_.setBackdrop(gamedata::findOriginalFile(originalDir, "MENUSCRN/TWO_PLYR.LBM"));
  bool ok = true;
  auto load = [&](const std::string& name, render::Texture& t, int transparent) {
    gamedata::Picture pic;
    std::string error;
    const std::string path = gamedata::findOriginalFile(originalDir, "MENUSCRN/" + name);
    if (!path.empty() && gamedata::loadPbm(path, pic, transparent, error)) {
      t.upload(pic);
    } else {
      std::fprintf(stderr, "MENUSCRN/%s: %s\n", name.c_str(), path.empty() ? "not found" : error.c_str());
      ok = false;
    }
  };
  load("MAIN.LBM", main_, -1);
  load("FLIGHT.LBM", flight_, -1);
  load("WORLD.LBM", world_, -1);
  load("AFGHAN.LBM", briefing_, -1);
  load("EXIT.LBM", exit_, -1);
  load("QUICK_OP.BBM", quickOptions_, -1);
  load("LOG.LBM", log_bg_, -1);
  load("NETWORK.LBM", network_, -1);
  load("SERVER.BBM", server_, 0);
  load("V_RECON.LBM", recognition_, -1);
  load("PREFSB.LBM", prefs_, -1);
  load("TOPTEN.LBM", topTenBg_, -1);
  for (int i = 0; i < 3; ++i) load(kMedalBrushes[i], medals_[i], -1);
  brushes_.resize(kStoneButtons);
  for (int i = 0; i < kStoneButtons; ++i)
    if (kBrushes[i]) load(kBrushes[i], brushes_[size_t(i)], 0);
  // UKRAINE, on the world page as it is and pressed.
  std::vector<gamedata::Picture> pics(6);
  const char* names[6] = {"WORLD.LBM", "MAIN.LBM", "FLIGHT.LBM", "W_KOREA.BBM", "QUICK.BBM", "TRAINING.BBM"};
  bool have = true;
  for (int i = 0; i < 6; ++i) {
    std::string error;
    have = have && gamedata::loadPbm(gamedata::findOriginalFile(originalDir, std::string("MENUSCRN/") + names[i]), pics[size_t(i)], -1, error);
  }
  if (have) {
    const Plate up[3] = {{&pics[0], {415, 100}}, {&pics[1], {416, 42}}, {&pics[2], {416, 42}}};
    const Plate down[3] = {{&pics[3], {0, 0}}, {&pics[4], {0, 0}}, {&pics[5], {0, 0}}};
    const int w = std::min(pics[3].width, pics[4].width), h = std::min(pics[3].height, pics[4].height);
    ukraine_.upload(engrave(up, "UKRAINE", w, h));
    brushes_[kUkraine].upload(engrave(down, "UKRAINE", w, h));
  }
  return ok;
}

void FrontEnd::open(Page p) {
  if (p == Page::Exit && page_ != Page::Exit) exitFrom_ = page_;
  page_ = p;
  pressed_ = pending_ = -1;
  focus_ = -1;
  goTo_ = false;
}

bool FrontEnd::openByName(const std::string& name) {
  if (name == "main") open(Page::Main);
  else if (name == "quick") open(Page::Quick);
  else if (name == "flight") open(Page::Flight);
  else if (name == "training") openList(List::Training, "saratov");
  else if (name == "single") openList(List::Single, "afghan");
  else if (name == "campaign") worldForCampaign_ = true, open(Page::World);
  else if (name == "exit") open(Page::Exit);
  else if (name == "network") open(Page::Network);
  else if (name == "recognition") open(Page::Recognition);
  else if (name == "animations") playAnimation();
  else if (name == "prefs" || name == "preferences") open(Page::Prefs);
  else if (name == "topten") topFrom_ = Page::Quick, open(Page::TopTen);
  else return false;
  return true;
}

void FrontEnd::openList(List l, const std::string& zone) {
  list_ = l;
  zone_ = zone;
  entries_.clear();
  for (const std::string& n : gamedata::listMissions(dir_, l == List::Training ? "SARATT" : listPrefix(zone))) {
    Entry e;
    std::string error;
    if (gamedata::loadMission(dir_, n, e.mission, error)) e.name = n, entries_.push_back(std::move(e));
    else std::fprintf(stderr, "%s\n", error.c_str());
  }
  selected_ = -1;
  top_ = textLine_ = 0;
  open(Page::Missions);
}

// ---------------------------------------------------------------- what is on the page

std::vector<FrontEnd::Item> FrontEnd::stoneButtons() const {
  std::vector<Item> v;
  auto add = [&](const Stone* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const render::Texture& b = brushes_[size_t(s[i].id)];
      v.push_back({s[i].id, s[i].at, s[i].at + glm::vec2(b.width, b.height), s[i].id, "", s[i].key, s[i].available});
    }
  };
  switch (page_) {
    case Page::Main: add(kMainButtons, std::size(kMainButtons)); break;
    case Page::Quick: add(kQuickButtons, std::size(kQuickButtons)); break;
    case Page::Flight: add(kFlightButtons, std::size(kFlightButtons)); break;
    case Page::World: add(kWorldButtons, std::size(kWorldButtons)); break;
    case Page::Exit: add(kExitButtons, std::size(kExitButtons)); break;
    case Page::Recognition: add(kRecButtons, std::size(kRecButtons)); break;
    case Page::Missions:
    case Page::Prefs:
    case Page::TopTen:
    case Page::Campaign:
    case Page::Log:
    case Page::Award:
    case Page::Animation:
    case Page::Network: break;
  }
  return v;
}

std::vector<FrontEnd::Item> FrontEnd::items() const {
  std::vector<Item> v;
  if (goTo_) {
    // The open GoTo menu covers everything else.
    static const std::pair<int, const char*> lines[] = {{kHelp, "Help"},         {kGoPrefs, "Preferences"},
                                                        {kGoLogs, "Logs"},       {kGoQuit, "Quit"},
                                                        {kGoRecognition, "Recognition"}, {kGoAnimations, "Animations"}};
    float y = kGoToMenuLo.y;
    for (const auto& l : lines) {
      v.push_back({l.first, {kGoToMenuLo.x, y}, {kGoToMenuLo.x + kGoToMenuW, y + kGoToLine}, -1, l.second,
                   SDL_SCANCODE_UNKNOWN, l.first != kHelp});
      y += kGoToLine;
    }
    v.push_back({kBack, kBackLo, kBackHi, -1, page_ == Page::Main ? "Quit" : "Exit", SDL_SCANCODE_UNKNOWN, true});
    return v;
  }
  v = stoneButtons();
  if (page_ == Page::Award || page_ == Page::Animation) return v;  // a click or a key goes on
  if (page_ == Page::Log && log_) {
    const bool someone = !log_->pilots.empty();
    v.push_back({kLogCreate, kLogCreateLo, kLogCreateHi, -1, "Create", SDL_SCANCODE_C, int(log_->pilots.size()) < sim::PilotLog::kMaxPilots});
    v.push_back({kLogRename, kLogRenameLo, kLogRenameHi, -1, "Rename", SDL_SCANCODE_R, someone && !log_->pilot().immortal});
    v.push_back({kLogDelete, kLogDeleteLo, kLogDeleteHi, -1, "Delete", SDL_SCANCODE_D, someone && !log_->pilot().immortal});
  }
  if (page_ == Page::Prefs && cfg_) {
    for (int i = 0; i < kPrefCount; ++i)
      v.push_back({kPref0 + i, kPrefBarLo[i], kPrefBarLo[i] + glm::vec2(kPrefBarW, kPrefBarH), -2, kPrefList[i].label, SDL_SCANCODE_UNKNOWN, learnStep_ < 0});
    v.push_back({kPrefJoyDevice, kJoyPanelLo, {kJoyPanelHi.x, kJoyNameBottom}, -2, "Joystick", SDL_SCANCODE_UNKNOWN, learnStep_ < 0});
    v.push_back({kPrefJoyLearn, {kJoyPanelLo.x, kJoyNameBottom}, kJoyPanelHi, -2, "Axes", SDL_SCANCODE_UNKNOWN, learnStep_ < 0});
  }
  if (page_ == Page::Network) {
    // The steel bars (no brush, no red face: drawNetwork draws them).
    const net::Advert* row = netRow_ >= 0 && netRow_ < int(netRows().size()) ? netRows()[size_t(netRow_)] : nullptr;
    v.push_back({kNetName, kNetNameLo, kNetNameHi, -2, "Your name", SDL_SCANCODE_N, true});
    v.push_back({kNetGame, kNetGameLo, kNetGameHi, -2, "Game", SDL_SCANCODE_M, !hosting_});
    v.push_back({kNetHost, kNetHostLo, kNetHostHi, -2, hosting_ ? "Stop hosting" : "Host this game", SDL_SCANCODE_H, true});
    v.push_back({kNetSeat, kNetSeatLo, kNetSeatHi, -2, "Seat", SDL_SCANCODE_S, true});
    v.push_back({kNetJoin, kNetJoinLo, kNetJoinHi, -2, row ? "Join " + row->pilot : "Join", SDL_SCANCODE_J, row && !hosting_});
    v.push_back({kNetAddress, kNetAddressLo, kNetAddressHi, -2, "Address", SDL_SCANCODE_A, !hosting_});
  }
  if (page_ == Page::Missions) {
    v.push_back({kCommit, kCommitLo, kCommitHi, -1, "Commit", SDL_SCANCODE_UNKNOWN, selected_ >= 0});
    v.push_back({kInfinite, kInfiniteLo, kInfiniteHi, -1, "Infinite weapons", SDL_SCANCODE_I, true});
    v.push_back({kNoCrash, kNoCrashLo, kNoCrashHi, -1, "No crashes", SDL_SCANCODE_N, true});
    v.push_back({kWsoAllies, kWsoAlliesLo, kWsoAlliesHi, -1, "WSO targets allies", SDL_SCANCODE_A, true});
    v.push_back({kGunnerHints, kGunnerHintsLo, kGunnerHintsHi, -1, "Gunner hints", SDL_SCANCODE_H, true});
  }
  if (page_ == Page::World && quickWorld_)
    v.push_back({kUkraineStores, kUkraineStoresLo, kUkraineStoresHi, -1,
                 ukraineInterceptors_ ? "Ukraine: Interceptors" : "Ukraine: R-60", SDL_SCANCODE_W, true});
  if (page_ != Page::Exit) {
    v.push_back({kGoTo, kGoToLo, kGoToHi, -1, "GoTo", SDL_SCANCODE_G, true});
    v.push_back({kBack, kBackLo, kBackHi, -1, page_ == Page::Main ? "Quit" : "Exit", SDL_SCANCODE_UNKNOWN, true});
  }
  return v;
}

int FrontEnd::itemAt(glm::vec2 p) const {
  for (const Item& it : items())
    if (inside(p, it.lo, it.hi)) return it.id;
  return -1;
}

// ---------------------------------------------------------------- acting

void FrontEnd::notAvailable() {
  note_ = "Not in this version yet";
  noteTime_ = 2.5f;
}

void FrontEnd::back() {
  switch (page_) {
    case Page::Main: open(Page::Exit); break;
    case Page::Quick:
    case Page::Flight: open(Page::Main); break;
    case Page::World: open(quickWorld_ ? Page::Quick : Page::Flight); break;
    case Page::TopTen: topEntry_ = -1, open(topFrom_); break;
    case Page::Campaign:
      if (speak_) speak_("");
      worldForCampaign_ = true, quickWorld_ = 0;
      open(Page::World);
      break;
    case Page::Missions: open(list_ == List::Training ? Page::Flight : Page::World); break;
    case Page::Exit: open(exitFrom_); break;
    case Page::Log: open(Page::Main); break;
    case Page::Network: open(Page::Flight); break;
    case Page::Recognition: open(recFrom_); break;
    case Page::Animation: open(animFrom_); break;
    case Page::Prefs:
      joys_.close();
      learnStep_ = -1;
      open(prefsFrom_);
      break;
    case Page::Award:
      // The next picture, or on to the menus.
      if (!awards_.empty()) awards_.erase(awards_.begin());
      if (!awards_.empty() && loadPicture(awards_.front(), award_, -1)) break;
      awards_.clear();
      open(afterAwards_);
      break;
  }
}

bool FrontEnd::loadPicture(const std::string& name, render::Texture& t, int transparent) {
  gamedata::Picture pic;
  std::string error;
  const std::string path = gamedata::findOriginalFile(dir_, "MENUSCRN/" + name);
  if (path.empty() || !gamedata::loadPbm(path, pic, transparent, error)) return false;
  t.upload(pic);
  return true;
}

void FrontEnd::showAwards(const sim::LogOutcome& o) {
  awards_.clear();
  if (o.promotedTo > 0) awards_.push_back(kPromotionPictures[o.promotedTo]);
  for (int i = 0; i < 3; ++i)
    if (o.newMedals & (1u << i)) awards_.push_back(kMedalPictures[i]);
  if (awards_.empty() || !loadPicture(awards_.front(), award_, -1)) {
    awards_.clear();
    return;
  }
  afterAwards_ = page_ == Page::Award ? afterAwards_ : page_;
  open(Page::Award);
}

void FrontEnd::saveLog() {
  if (log_ && !logPath_.empty() && !log_->save(logPath_)) std::fprintf(stderr, "Cannot write the pilot's log %s\n", logPath_.c_str());
}

FrontResult FrontEnd::activate(int id) {
  goTo_ = false;
  for (const Item& it : stoneButtons())
    if (it.id == id && !it.available) {
      notAvailable();
      return FrontResult::None;
    }
  auto quick = [&](bool invincible, const char* zone) {
    // Straight up into a single mission of the war chosen, picked at random.
    const std::vector<std::string> names = gamedata::listMissions(dir_, listPrefix(zone));
    if (names.empty()) return FrontResult::None;
    choice_ = Choice{};
    choice_.mission = names[SDL_GetTicks() % names.size()];
    choice_.zone = zone;
    choice_.plan = false;
    choice_.arcade = true;
    choice_.noCrash = invincible;
    choice_.infiniteWeapons = true;  // (manual p. 5)
    choice_.gunnerHints = gunnerHints_;
    choice_.ukraineInterceptors = ukraineInterceptors_;
    return FrontResult::Fly;
  };
  auto war = [&](const char* zone) {
    if (quickWorld_) return quick(quickWorld_ == 2, zone);
    if (!worldForCampaign_) {
      openList(List::Single, zone);
      return FrontResult::None;
    }
    openCampaign(zone);
    return FrontResult::None;
  };
  switch (id) {
    case kQuickstart: open(Page::Quick); break;
    case kFlight: open(Page::Flight); break;
    // Quickstart: the war first (manual p. 5), then up at once.
    case kArcade:
    case kInvincible: quickWorld_ = id == kArcade ? 1 : 2, worldForCampaign_ = false, open(Page::World); break;
    case kTopTen: topFrom_ = page_, topEntry_ = -1, open(Page::TopTen); break;
    case kTraining: openList(List::Training, "saratov"); break;
    case kSingle: quickWorld_ = 0, worldForCampaign_ = false, open(Page::World); break;
    case kCampaign: quickWorld_ = 0, worldForCampaign_ = true, open(Page::World); break;
    case kKazak: return war("kazak");
    case kKorea: return war("korea");
    case kAfghan: return war("afghan");
    case kUkraine: return war("ukraine");
    case kYes: return FrontResult::Quit;
    case kNo: back(); break;
    case kGoTo: goTo_ = true; break;
    case kBack: back(); break;
    case kGoQuit: open(Page::Exit); break;
    case kNetwork:
    case kTwoPlayer:
      netField_ = 0;
      open(Page::Network);
      break;
    case kNetName:
    case kNetGame:
    case kNetAddress:
      // Typed in on its bar; Enter takes it (an address joins at once), Esc leaves it.
      netField_ = id == kNetName ? 1 : id == kNetGame ? 2 : 3;
      typed_ = netField_ == 1 ? net_.name : netField_ == 2 ? net_.game : "";
      break;
    case kNetHost:
      if (hosting_) return FrontResult::StopHost;
      if (net_.name.empty() || net_.game.empty()) {
        note("Your name and the game's name first");
        break;
      }
      return FrontResult::Host;
    case kNetSeat: net_.seat = NetSeat((int(net_.seat) + 1) % 4); break;
    default:
      if (id >= kPref0 && id <= kPrefLast) prefFocus_ = id - kPref0, stepPref(id - kPref0, 1);
      if (id == kPrefJoyDevice) prefFocus_ = kPrefJoystick, stepJoystick(1);
      if (id == kPrefJoyLearn) prefFocus_ = kPrefJoystick, startLearning();
      break;
    case kNetJoin: {
      const std::vector<const net::Advert*> rows = netRows();
      if (netRow_ < 0 || netRow_ >= int(rows.size())) {
        note("Choose a game on the left");
        break;
      }
      const net::Advert& a = *rows[size_t(netRow_)];
      if (net_.name.empty()) {
        note("Your name first");
        break;
      }
      if (net_.seat == NetSeat::Pilot || net_.seat == NetSeat::Bowman) {
        // A Hind of his own (and the bowman): the game's host takes him.
        if (!a.host) {
          note(net_.seat == NetSeat::Bowman ? "On foot: choose the game's host" : "A Hind of your own: choose the game's host");
          break;
        }
        if (net_.seat == NetSeat::Pilot && int(a.wingmen.size()) >= 3) {
          note("No Hind left in this game");
          break;
        }
      } else if (!a.crew[int(net_.seat) - 1].empty()) {
        note(std::string(netSeatName(net_.seat)) + " is taken");
        break;
      }
      // The game's host takes the wingmen, each pilot his own crew.
      net_.address = a.address + ":" + std::to_string(a.port);
      net_.joinGame = a.game;
      return FrontResult::Join;
    }
    case kLog:
    case kGoLogs:
      if (!log_) {
        notAvailable();
        break;
      }
      log_->refresh(int64_t(std::time(nullptr)));
      typing_ = false;
      open(Page::Log);
      break;
    case kLogCreate:
    case kLogRename:
      // The name typed in its line of the list; Enter takes it, Esc leaves it.
      if (!log_) break;
      typing_ = true, renaming_ = id == kLogRename;
      typed_ = renaming_ ? log_->pilot().name : "";
      break;
    case kLogDelete:
      if (log_ && log_->remove(log_->current)) saveLog();
      break;
    case kGoRecognition:
      if (page_ != Page::Recognition) recFrom_ = page_ == Page::Award ? Page::Main : page_;
      open(Page::Recognition);
      sayUnit();
      break;
    case kRecLast:
    case kRecNext: {
      const int n = kRecLists[recList_].count;
      recItem_ = (recItem_ + (id == kRecNext ? 1 : n - 1)) % n;
      sayUnit();
      break;
    }
    case kRecAir:
    case kRecVehicles:
    case kRecShips:
      recList_ = id - kRecAir, recItem_ = 0;
      sayUnit();
      break;
    case kPrefs:
    case kGoPrefs:
      if (!cfg_) {
        notAvailable();
        break;
      }
      if (page_ != Page::Prefs) prefsFrom_ = page_ == Page::Award ? Page::Main : page_;
      open(Page::Prefs);
      break;
    case kHelp: notAvailable(); break;
    case kGoAnimations: playAnimation(); break;
    case kInfinite: infiniteWeapons_ = !infiniteWeapons_; break;
    case kNoCrash: noCrash_ = !noCrash_; break;
    case kWsoAllies: wsoAllies_ = !wsoAllies_; break;
    case kGunnerHints: gunnerHints_ = !gunnerHints_; break;
    case kUkraineStores:
      ukraineInterceptors_ = !ukraineInterceptors_;
      savePrefs([&](platform::Config& c) { c.set("weapons", "quick_ukraine", ukraineInterceptors_ ? "interceptor" : "r60"); });
      break;
    case kCommit:
      if (selected_ < 0 || selected_ >= int(entries_.size())) break;
      choice_ = Choice{};
      choice_.mission = entries_[size_t(selected_)].name;
      choice_.zone = zone_;
      choice_.infiniteWeapons = infiniteWeapons_;
      choice_.noCrash = noCrash_;
      choice_.wsoAllies = wsoAllies_;
      choice_.gunnerHints = gunnerHints_;
      return FrontResult::Fly;
  }
  return FrontResult::None;
}

void FrontEnd::sayUnit() {
  // The WSO names it, as the original's library does.
  const int v = kRecLists[recList_].units[recItem_].voice;
  if (say_ && v >= 0) say_(v);
}

void FrontEnd::moveFocus(int step) {
  const std::vector<Item> stones = stoneButtons();
  keyboard_ = true;
  if (stones.empty()) return;
  const int n = int(stones.size());
  int at = -1;
  for (int i = 0; i < n; ++i)
    if (stones[size_t(i)].id == focus_) at = i;
  at = at < 0 ? (step > 0 ? 0 : n - 1) : (at + step + n) % n;
  focus_ = stones[size_t(at)].id;
}

void FrontEnd::playAnimation() {
  // One of the CD's, picked as the original's; over the page it was started from.
  std::vector<uint8_t> data;
  std::string error;
  const std::string name = gamedata::randomAnimation(unsigned(SDL_GetTicks() * 2654435761u >> 8));
  if (!cdRead_ || !cdRead_(name, data)) {
    note_ = "The animations are on the original game's CD";
    noteTime_ = 3.0f;
    return;
  }
  if (!anim_.open(std::move(data), error) || !animationFrame()) {
    std::fprintf(stderr, "%s: %s\n", name.c_str(), error.empty() ? "broken animation" : error.c_str());
    return;
  }
  animTime_ = 0.0f;
  if (page_ != Page::Animation) animFrom_ = page_;
  open(Page::Animation);
}

bool FrontEnd::animationFrame() {
  if (!anim_.next()) return false;
  anim_.rgb(animRgb_);
  animTex_.uploadRgb(anim_.width(), anim_.height(), animRgb_);
  return true;
}

FrontResult FrontEnd::update(float dt) {
  noteTime_ = std::max(0.0f, noteTime_ - dt);
  if (page_ == Page::Animation) {
    // 12.5 frames a second, then back where it was started.
    for (animTime_ += std::min(dt, 0.5f); animTime_ >= 1.0f / gamedata::Anim::kFps; animTime_ -= 1.0f / gamedata::Anim::kFps)
      if (!animationFrame()) {
        open(animFrom_);
        break;
      }
  }
  if (pending_ >= 0 && (pendingTime_ -= dt) <= 0.0f) {
    const int id = pending_;
    pending_ = -1;
    return activate(id);
  }
  return FrontResult::None;
}

FrontResult FrontEnd::handleEvent(const SDL_Event& e) {
  // An animation: a key or a click ends it.
  if (page_ == Page::Animation) {
    if ((e.type == SDL_KEYDOWN && !e.key.repeat) || (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)) back();
    return FrontResult::None;
  }
  // A promotion or a medal shown: a click or a key goes on.
  if (page_ == Page::Award) {
    if ((e.type == SDL_KEYDOWN && !e.key.repeat) || (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)) back();
    return FrontResult::None;
  }
  // A name or an address being typed on the network page.
  if (page_ == Page::Network && netField_ != 0) {
    if (e.type == SDL_TEXTINPUT && typed_.size() < 24) {
      for (const char* c = e.text.text; *c; ++c)
        if (*c >= ' ' && *c != '|' && *c != ',' && uint8_t(*c) < 128) typed_ += *c;
      return FrontResult::None;
    }
    if (e.type == SDL_KEYDOWN) {
      const SDL_Scancode k = e.key.keysym.scancode;
      if (k == SDL_SCANCODE_BACKSPACE && !typed_.empty()) typed_.pop_back();
      if (k == SDL_SCANCODE_ESCAPE) netField_ = 0;
      if ((k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) && !typed_.empty()) {
        const int field = netField_;
        netField_ = 0;
        if (field == 1) net_.name = typed_;
        if (field == 2) net_.game = typed_;
        if (field == 3) {
          if (net_.name.empty()) {
            note("Your name first");
            return FrontResult::None;
          }
          net_.address = typed_;
          net_.joinGame.clear();
          return FrontResult::Join;
        }
      }
      return FrontResult::None;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN) netField_ = 0;
  }
  // A name for the Top Ten being typed: Enter puts it in, Esc leaves it out.
  if (page_ == Page::TopTen && topEntry_ >= 0) {
    if (e.type == SDL_TEXTINPUT && typed_.size() < 20) {
      for (const char* c = e.text.text; *c; ++c)
        if (*c >= ' ' && *c != '|' && uint8_t(*c) < 128) typed_ += *c;
      return FrontResult::None;
    }
    if (e.type == SDL_KEYDOWN) {
      const SDL_Scancode k = e.key.keysym.scancode;
      if (k == SDL_SCANCODE_BACKSPACE && !typed_.empty()) typed_.pop_back();
      if (k == SDL_SCANCODE_ESCAPE) topEntry_ = -1;
      if ((k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) && !typed_.empty() && topTen_) {
        topTen_->insert(typed_, topScore_, topCrashed_);
        if (!topTenPath_.empty() && !topTen_->save(topTenPath_)) std::fprintf(stderr, "Cannot write %s\n", topTenPath_.c_str());
        topEntry_ = -1;
      }
      return FrontResult::None;
    }
  }
  // A pilot's name being typed.
  if (typing_ && log_) {
    if (e.type == SDL_TEXTINPUT && typed_.size() < 20) {
      for (const char* c = e.text.text; *c; ++c)
        if (*c >= ' ' && *c != '|' && uint8_t(*c) < 128) typed_ += *c;
      return FrontResult::None;
    }
    if (e.type == SDL_KEYDOWN) {
      const SDL_Scancode k = e.key.keysym.scancode;
      if (k == SDL_SCANCODE_BACKSPACE && !typed_.empty()) typed_.pop_back();
      if (k == SDL_SCANCODE_ESCAPE) typing_ = false;
      if ((k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) && !typed_.empty()) {
        if (renaming_) log_->pilot().name = typed_;
        else log_->create(typed_);
        typing_ = false;
        saveLog();
      }
      return FrontResult::None;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN) typing_ = false;
  }
  if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
    const glm::vec2 window = e.type == SDL_MOUSEMOTION ? glm::vec2(e.motion.x, e.motion.y) : glm::vec2(e.button.x, e.button.y);
    mouse_ = canvas_.toScreen(window * mouseScale_);
    if (page_ == Page::Prefs && cfg_) {
      // The setting under the mouse is the one told of; the right button steps it back.
      for (int i = 0; i < kPrefCount; ++i)
        if (inside(mouse_, kPrefBarLo[i], kPrefBarLo[i] + glm::vec2(kPrefBarW, kPrefBarH))) {
          if (e.type == SDL_MOUSEMOTION) prefFocus_ = i;
          if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) prefFocus_ = i, stepPref(i, -1);
        }
      if (learnStep_ < 0 && inside(mouse_, kJoyPanelLo, kJoyPanelHi)) {
        if (e.type == SDL_MOUSEMOTION) prefFocus_ = kPrefJoystick;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT && mouse_.y < kJoyNameBottom) stepJoystick(-1);
      }
    }
    if (e.type == SDL_MOUSEMOTION || e.button.button != SDL_BUTTON_LEFT) return FrontResult::None;
    keyboard_ = false;
    if (e.type == SDL_MOUSEBUTTONDOWN) {
      pressed_ = itemAt(mouse_);
      if (pressed_ < 0 && goTo_) goTo_ = false;
      // A pilot of the log: the one flying from now on.
      if (pressed_ < 0 && page_ == Page::Log && log_ && inside(mouse_, kPilotsLo, kPilotsHi)) {
        const int row = int(std::floor((mouse_.y - kPilotsLo.y - 8.0f) / kPilotRow));
        if (row >= 0 && row < int(log_->pilots.size())) log_->current = row, saveLog();
      }
      // A game on the network page: chosen; twice: joined.
      if (pressed_ < 0 && page_ == Page::Network && inside(mouse_, kGamesLo, kGamesHi)) {
        const int row = int(std::floor((mouse_.y - kGameRowTop) / kGameRowH));
        if (row >= 0 && row < kGameRows && row < int(netRows().size())) {
          const bool again = netRow_ == row && e.button.clicks >= 2;
          netRow_ = row;
          if (again) return activate(kNetJoin);
        }
      }
      // A line of the mission list: chosen; twice: committed.
      if (pressed_ < 0 && page_ == Page::Missions && inside(mouse_, kListLo, kListHi)) {
        const int row = int(std::floor((mouse_.y - kRowTop) / kRowH));
        if (row >= 0 && row < kRows && top_ + row < int(entries_.size())) {
          const bool again = selected_ == top_ + row && e.button.clicks >= 2;
          if (selected_ != top_ + row) textLine_ = 0;
          selected_ = top_ + row;
          if (again) return activate(kCommit);
        }
      }
      return FrontResult::None;
    }
    const int was = pressed_;
    pressed_ = -1;
    // The campaign's page: a click anywhere but on its buttons goes on.
    if (was < 0 && page_ == Page::Campaign && !goTo_ && itemAt(mouse_) < 0) return campaignNext();
    return was >= 0 && itemAt(mouse_) == was ? activate(was) : FrontResult::None;
  }
  if (e.type == SDL_MOUSEWHEEL) {
    if (page_ != Page::Missions) return FrontResult::None;
    if (inside(mouse_, kInfoLo, kInfoHi)) textLine_ = std::clamp(textLine_ - e.wheel.y, 0, std::max(0, textLines_ - 1));
    else top_ = std::clamp(top_ - e.wheel.y, 0, std::max(0, int(entries_.size()) - kRows));
    return FrontResult::None;
  }
  if (e.type != SDL_KEYDOWN ||
      (e.key.repeat && e.key.keysym.scancode != SDL_SCANCODE_UP && e.key.keysym.scancode != SDL_SCANCODE_DOWN))
    return FrontResult::None;
  if (pending_ >= 0) return FrontResult::None;
  const SDL_Scancode sc = e.key.keysym.scancode;
  // Pressed from the keyboard: it shows pressed for a moment first.
  auto press = [&](int id) {
    pending_ = id;
    pendingTime_ = id < kStoneButtons ? 0.15f : 0.0f;
    return FrontResult::None;
  };
  if (page_ == Page::Prefs && learnStep_ >= 0) {
    // Learning the axes: Space or Delete leaves the control without one, Esc stops (what has
    // been learnt is kept only when it is through).
    if (sc == SDL_SCANCODE_ESCAPE) learnStep_ = -1, note("The joystick as it was");
    if (sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
      if (learnStep_ < platform::joy::kControls) learnMap_.axes[learnStep_] = {};
      else learnMap_.hat = {"", -1, false};
      ++learnStep_;
      learner_.start(joys_);
      learn();
    }
    return FrontResult::None;
  }
  if (sc == SDL_SCANCODE_ESCAPE) {
    if (goTo_) goTo_ = false;
    else back();
    return FrontResult::None;
  }
  if (goTo_) return FrontResult::None;
  const bool enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE;
  if (page_ == Page::Exit && enter) return press(kYes);
  if (page_ == Page::Campaign && enter) return campaignNext();
  if (page_ == Page::Log && log_ && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
    const int n = int(log_->pilots.size());
    log_->current = std::clamp(log_->current + (sc == SDL_SCANCODE_DOWN ? 1 : -1), 0, n - 1);
    saveLog();
    return FrontResult::None;
  }
  if (page_ == Page::Network && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
    const int n = std::min(kGameRows, int(netRows().size()));
    if (n > 0) netRow_ = std::clamp(netRow_ + (sc == SDL_SCANCODE_DOWN ? 1 : -1), 0, n - 1);
    return FrontResult::None;
  }
  // The hidden seat: on foot, with a bow.
  if (page_ == Page::Network && sc == SDL_SCANCODE_R && (e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_SHIFT)) {
    net_.seat = NetSeat::Bowman;
    return FrontResult::None;
  }
  if (page_ == Page::Network && enter) return press(kNetJoin);
  if (page_ == Page::Prefs && cfg_) {
    // Up and down through the settings (the left column, then the right), left and right their values.
    if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN) {
      prefFocus_ = (prefFocus_ + (sc == SDL_SCANCODE_DOWN ? 1 : kPrefCount)) % (kPrefCount + 1);
      return FrontResult::None;
    }
    // The joystick's panel: left and right the device, Enter learns its axes.
    if (prefFocus_ == kPrefJoystick && enter) {
      startLearning();
      return FrontResult::None;
    }
    if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT || enter) {
      if (prefFocus_ == kPrefJoystick) stepJoystick(sc == SDL_SCANCODE_LEFT ? -1 : 1);
      else stepPref(prefFocus_, sc == SDL_SCANCODE_LEFT ? -1 : 1);
      return FrontResult::None;
    }
  }
  if (page_ == Page::Recognition && (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT))
    return press(sc == SDL_SCANCODE_RIGHT ? kRecNext : kRecLast);
  if (page_ == Page::Missions) {
    const int n = int(entries_.size());
    int s = selected_;
    if (sc == SDL_SCANCODE_UP) s = s < 0 ? n - 1 : std::max(0, s - 1);
    if (sc == SDL_SCANCODE_DOWN) s = s < 0 ? 0 : std::min(n - 1, s + 1);
    if (sc == SDL_SCANCODE_HOME) s = 0;
    if (sc == SDL_SCANCODE_END) s = n - 1;
    if (s != selected_ && n > 0) {
      selected_ = s;
      textLine_ = 0;
      top_ = std::clamp(top_, std::max(0, s - kRows + 1), s);
    }
    if (sc == SDL_SCANCODE_PAGEUP) textLine_ = std::max(0, textLine_ - 8);
    if (sc == SDL_SCANCODE_PAGEDOWN) textLine_ = std::min(std::max(0, textLines_ - 1), textLine_ + 8);
    if (enter) {
      if (selected_ < 0 && n > 0) selected_ = 0;
      else return press(kCommit);
    }
  } else {
    if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_LEFT) moveFocus(-1);
    if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_TAB) moveFocus(1);
    if (enter) {
      if (focus_ < 0) moveFocus(1);
      else return press(focus_);
    }
  }
  for (const Item& it : items())
    if (it.key != SDL_SCANCODE_UNKNOWN && it.key == sc) return press(it.id);
  return FrontResult::None;
}

// ---------------------------------------------------------------- drawing

glm::vec2 FrontEnd::windowPoint(glm::vec2 screen) const { return canvas_.toWindow(screen) / mouseScale_; }

void FrontEnd::panel(glm::vec2 lo, glm::vec2 hi) {
  // A pane of glass over the picture: a little darker, lit on the top and left edges, with a
  // shadow to the right and below.
  canvas_.fill(lo, hi, glm::vec4(0, 0, 0, 0.07f));
  canvas_.fill({hi.x, lo.y + 6}, {hi.x + 5, hi.y + 6}, glm::vec4(0.02f, 0.02f, 0.02f, 0.72f));
  canvas_.fill({lo.x + 6, hi.y}, {hi.x, hi.y + 6}, glm::vec4(0.02f, 0.02f, 0.02f, 0.72f));
  canvas_.fill(lo, {hi.x, lo.y + 1}, glm::vec4(1, 1, 1, 0.28f));
  canvas_.fill(lo, {lo.x + 1, hi.y}, glm::vec4(1, 1, 1, 0.22f));
}

void FrontEnd::redButton(const Item& it, bool lit) {
  // As the planning screens' buttons (Planner::bevel): raised, the top and left edges light with
  // a white corner; pressed (held, or a toggle that is on) the other way round, the label a pixel
  // down and right. The face stays the same brick red.
  const bool down = lit || (it.id == pressed_ && inside(mouse_, it.lo, it.hi)) || it.id == pending_;
  const glm::vec4 gui2{97 / 255.0f, 32 / 255.0f, 32 / 255.0f, 1.0f}, gui3{113 / 255.0f, 48 / 255.0f, 48 / 255.0f, 1.0f},
                  gui4{113 / 255.0f, 56 / 255.0f, 56 / 255.0f, 1.0f};
  const glm::vec2 lo = it.lo, hi = it.hi, last = hi - glm::vec2(1.0f);
  canvas_.fill(lo, hi, kRed);
  const glm::vec4 top = down ? gui3 : kRedLight, bottom = down ? kRedLight : gui3;
  canvas_.fill(lo, {hi.x, lo.y + 1}, top);
  canvas_.fill(lo, {lo.x + 1, hi.y}, top);
  canvas_.fill({lo.x, last.y}, hi, bottom);
  canvas_.fill({last.x, lo.y}, hi, bottom);
  canvas_.fill(lo, lo + glm::vec2(1.0f), down ? gui2 : kWhite);
  canvas_.fill({last.x, lo.y}, {hi.x, lo.y + 1}, gui4);
  canvas_.fill({lo.x, last.y}, {lo.x + 1, hi.y}, gui4);
  canvas_.fill(last, hi, down ? kWhite : gui2);
  const float d = down ? 1.0f : 0.0f;
  const float y = std::floor((it.lo.y + it.hi.y) * 0.5f) - 6.0f + d;
  canvas_.text(it.label, {std::floor((it.lo.x + it.hi.x) * 0.5f) + d, y}, it.available ? kWhite : kDimText, 0);
}

void FrontEnd::drawMissions() {
  canvas_.image(briefing_, {0, 0}, {640, 480});
  panel(kListLo, kListHi);
  for (int r = 0; r < kRows && top_ + r < int(entries_.size()); ++r) {
    const int i = top_ + r;
    const float y = kRowTop + r * kRowH;
    if (i == selected_) canvas_.fill({23, y}, {318, y + 16}, glm::vec4(kRed.r, kRed.g, kRed.b, 0.85f));
    canvas_.clip({kListLo.x + 4, kListLo.y}, {kListHi.x - 22, kListHi.y});
    canvas_.text(entries_[size_t(i)].mission.title, {27, y + 2}, kWhite);
    canvas_.noClip();
    // At the row's end, as in the original: x when its last logged flight failed, else a dot.
    const bool failed = log_ && !log_->pilots.empty() &&
                        std::find(log_->pilot().failedMissions.begin(), log_->pilot().failedMissions.end(), entries_[size_t(i)].name) != log_->pilot().failedMissions.end();
    canvas_.text(failed ? "x" : ".", {312, y + 2}, kWhite, 1);
  }
  if (top_ > 0) canvas_.text("...", {312, 28}, kWhite, 1);
  if (top_ + kRows < int(entries_.size())) canvas_.text("...", {312, 434}, kWhite, 1);

  if (selected_ >= 0 && selected_ < int(entries_.size())) {
    const gamedata::Mission& m = entries_[size_t(selected_)].mission;
    panel(kInfoLo, kInfoHi);
    std::vector<std::string> lines;
    const float width = kTextHi.x - kTextLo.x;
    for (const auto& f : m.facts)
      for (const std::string& l : canvas_.wrap(f.first + ": " + f.second, width)) lines.push_back(l);
    for (const std::string& p : m.paragraphs)
      for (const std::string& l : canvas_.wrap(p, width)) lines.push_back(l);
    textLines_ = int(lines.size());
    const int shown = int((kTextHi.y - kTextLo.y) / kInfoLine);
    textLine_ = std::clamp(textLine_, 0, std::max(0, textLines_ - shown));
    float y = kTextLo.y;
    for (int i = textLine_; i < textLines_ && i < textLine_ + shown; ++i, y += kInfoLine)
      canvas_.text(lines[size_t(i)], {kTextLo.x, y}, kWhite);
    if (textLine_ + shown < textLines_) canvas_.text("PgDn ...", {kTextLo.x, kCommitLo.y + 2}, kWhite);
  }
}

void FrontEnd::drawLog() {
  canvas_.image(log_bg_, {0, 0}, {640, 480});
  if (!log_) return;
  const int64_t now = int64_t(std::time(nullptr));
  // The pilots: the one flying in red, his rank and status.
  canvas_.text("Pilot's Log", {kPilotsLo.x + 8, kPilotsLo.y + 6}, kWhite);
  for (size_t i = 0; i < log_->pilots.size(); ++i) {
    const sim::Pilot& p = log_->pilots[i];
    const float y = kPilotsLo.y + 28.0f + float(i) * kPilotRow;
    if (int(i) == log_->current) canvas_.fill({kPilotsLo.x + 4, y - 2}, {kPilotsHi.x - 4, y + 15}, glm::vec4(kRed.r, kRed.g, kRed.b, 0.85f));
    const bool edit = typing_ && ((renaming_ && int(i) == log_->current));
    const std::string name = edit ? typed_ + ((SDL_GetTicks() / 400) % 2 ? "_" : "") : p.name;
    const std::string rank = p.immortal ? "Imm" : sim::rankName(sim::rankFor(p.missions));
    canvas_.text(rank.substr(0, 4) + ". " + name, {kPilotsLo.x + 10, y}, kWhite);
    const char* status = p.immortal ? "Immortal" : sim::statusName(p.status);
    canvas_.text(p.status == sim::PilotStatus::Active || p.immortal ? "" : status, {kPilotsHi.x - 10, y}, kDimText, 1);
  }
  if (typing_ && !renaming_) {
    const float y = kPilotsLo.y + 28.0f + float(log_->pilots.size()) * kPilotRow;
    canvas_.fill({kPilotsLo.x + 4, y - 2}, {kPilotsHi.x - 4, y + 15}, glm::vec4(kRed.r, kRed.g, kRed.b, 0.85f));
    canvas_.text("New pilot: " + typed_ + ((SDL_GetTicks() / 400) % 2 ? "_" : ""), {kPilotsLo.x + 10, y}, kWhite);
  }
  const sim::Pilot& p = log_->pilot();
  // His rank and his medals.
  canvas_.text(p.immortal ? "Comrade Immortal" : std::string(sim::rankName(sim::rankFor(p.missions))) + " " + p.name,
               {kRankLo.x + 8, kRankLo.y + 6}, kWhite);
  float mx = kRankLo.x + 10;
  for (int i = 0; i < 3; ++i)
    if ((p.medals & (1u << i)) && medals_[i].loaded()) {
      canvas_.image(medals_[i], {mx, kRankLo.y + 30});
      mx += float(medals_[i].width) + 10.0f;
    }
  if (p.medals == 0) canvas_.text("No decorations yet", {kRankLo.x + 8, kRankLo.y + 30}, kDimText);
  // The details, as the original's lines.
  auto hms = [](double s) {
    char b[32];
    const long t = long(s);
    std::snprintf(b, sizeof b, "%02ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    return std::string(b);
  };
  std::string status = p.immortal ? "Immortal" : sim::statusName(p.status);
  if (!p.immortal && p.until > now) {
    const long h = long((p.until - now + 3599) / 3600);
    status += " (" + std::to_string(h) + " h)";
  }
  auto campaign = [&](const char* key) {
    const bool won = std::find(p.campaignsWon.begin(), p.campaignsWon.end(), key) != p.campaignsWon.end();
    const bool flown = std::find(p.campaignsFlown.begin(), p.campaignsFlown.end(), key) != p.campaignsFlown.end();
    return std::string(won ? "Won" : flown ? "Active" : "Inactive");
  };
  const std::pair<std::string, std::string> lines[] = {
      {"Status:", status},
      {"Total time:", hms(p.totalSeconds)},
      {"Combat time:", hms(p.combatSeconds)},
      {"Total missions:", std::to_string(p.missions)},
      {"Total combat missions:", std::to_string(p.combatMissions)},
      {"Outstanding missions:", std::to_string(p.outstanding)},
      {"Successful missions:", std::to_string(p.successful)},
      {"Failed missions:", std::to_string(p.failed)},
      {"Kazak Campaign:", campaign("kazak")},
      {"Korea Campaign:", campaign("korea")},
      {"Afghan Campaign:", campaign("afghan")},
      {"Ukraine Campaign:", campaign("ukraine")},
  };
  float y = kDetailLo.y + 8;
  for (const auto& l : lines) {
    canvas_.text(l.first, {kDetailLo.x + 8, y}, kWhite);
    canvas_.text(l.second, {kDetailHi.x - 8, y}, kWhite, 1);
    y += 14.5f;
  }
}

std::vector<const net::Advert*> FrontEnd::netRows() const {
  // The hosts by their games' names, each followed by his wingmen.
  std::vector<const net::Advert*> hosts, rows;
  for (const net::Advert& a : games_)
    if (a.host) hosts.push_back(&a);
  std::sort(hosts.begin(), hosts.end(), [](const net::Advert* a, const net::Advert* b) { return a->game < b->game; });
  for (const net::Advert* h : hosts) {
    rows.push_back(h);
    for (const net::Advert& a : games_)
      if (!a.host && a.game == h->game && (a.hostAddress == h->address || a.hostAddress.rfind("127.", 0) == 0)) rows.push_back(&a);
  }
  return rows;
}

std::string FrontEnd::prefValue(int i) const {
  const Pref& p = kPrefList[i];
  const std::string v = cfg_ ? cfg_->getString(p.section, p.key, "") : "";
  // The option it is (numbers compared as numbers: "1" is "1.0").
  for (const auto& [value, shown] : p.options) {
    if (v == value) return shown;
    char* end = nullptr;
    const double a = std::strtod(v.c_str(), &end);
    if (!v.empty() && *end == 0 && std::fabs(a - std::atof(value)) < 1e-4) return shown;
  }
  return v;
}

void FrontEnd::stepPref(int i, int dir) {
  if (!cfg_) return;
  const Pref& p = kPrefList[i];
  const std::string shown = prefValue(i);
  int at = -1;
  for (size_t k = 0; k < p.options.size(); ++k)
    if (shown == p.options[k].second) at = int(k);
  const int n = int(p.options.size());
  const int next = at < 0 ? 0 : (at + dir + n) % n;
  const std::string value = p.options[size_t(next)].first;
  savePrefs([&](platform::Config& c) { c.set(p.section, p.key, value); });
  if (std::string(p.key) == "gunner_hints") gunnerHints_ = value == "1";
  if (changed_) changed_(p.section, p.key);
}

void FrontEnd::savePrefs(const std::function<void(platform::Config&)>& change) {
  if (!cfg_) return;
  change(*cfg_);
  // Saved at once, to what the file has (not this run's command line options).
  platform::Config saved;
  saved.load(cfgPath_);
  saved.applyDefaults();
  change(saved);
  if (!cfgPath_.empty() && !saved.save(cfgPath_)) std::fprintf(stderr, "Cannot write %s\n", cfgPath_.c_str());
}

void FrontEnd::stepJoystick(int dir) {
  // Off, then each device attached.
  joys_.refresh();
  const platform::joy::Map map = platform::joy::load(*cfg_);
  const int n = joys_.count();
  const int at = cfg_->getBool("joystick", "enabled", true) ? joys_.main(map) + 1 : 0;
  const int next = ((at + dir) % (n + 1) + n + 1) % (n + 1);
  savePrefs([&](platform::Config& c) {
    c.set("joystick", "enabled", next > 0 ? "1" : "0");
    if (next > 0) c.set("joystick", "device", std::to_string(next - 1)), c.set("joystick", "device_name", joys_.name(next - 1));
  });
  if (changed_) changed_("joystick", "device");
}

void FrontEnd::startLearning() {
  joys_.refresh();
  if (joys_.count() == 0) {
    note("No joystick attached");
    return;
  }
  learnMap_ = platform::joy::load(*cfg_);
  learnTaken_.clear();
  learnStep_ = 0;
  learner_.start(joys_);
}

void FrontEnd::learn() {
  namespace joy = platform::joy;
  if (learnStep_ < 0) return;
  if (learnStep_ < joy::kControls) {
    int d = -1, a = -1;
    float delta = 0.0f;
    if (!learner_.moved(joys_, d, a, delta, learnTaken_)) return;
    // The way it moved is the control's +.
    const joy::Control c = joy::Control(learnStep_);
    learnMap_.axes[c] = {joys_.name(d), a, delta * joy::sign(c) < 0.0f};
    learnTaken_.push_back({d, a});
    ++learnStep_;
    learner_.start(joys_);
    return;
  }
  if (learnStep_ == joy::kControls) {
    int d = -1, h = -1;
    if (!learner_.hatPressed(joys_, d, h)) return;
    learnMap_.hat = {joys_.name(d), h, false};
    ++learnStep_;
  }
  // Through: the stick's device is the main one (its buttons fire), and so it is kept.
  const joy::Binding& stick = learnMap_.axes[joy::CyclicX].axis >= 0 ? learnMap_.axes[joy::CyclicX] : learnMap_.axes[joy::CyclicY];
  if (stick.axis >= 0 && joys_.index(stick.device) >= 0) learnMap_.deviceName = stick.device, learnMap_.device = joys_.index(stick.device);
  learnMap_.mapped = true;
  savePrefs([&](platform::Config& c) {
    joy::save(c, learnMap_);
    c.set("joystick", "enabled", "1");
  });
  learnStep_ = -1;
  note("Joystick set");
  if (changed_) changed_("joystick", "mapped");
}

void FrontEnd::drawJoystickGlass() {
  namespace joy = platform::joy;
  const glm::vec2 lo = kPrefGlassLo + glm::vec2(10.0f, 8.0f);
  const float right = kPrefGlassHi.x - 10.0f;
  const joy::Map map = learnStep_ >= 0 ? learnMap_ : joy::load(*cfg_);
  canvas_.text("Joystick", lo, kInk);
  if (learnStep_ >= 0) {
    canvas_.text(std::to_string(learnStep_ + 1) + "/" + std::to_string(joy::kControls + 1) + "  " + kLearnAsk[learnStep_], lo + glm::vec2(0, 24), kInk);
    canvas_.text("Space: none   Esc: as it was", lo + glm::vec2(0, 42), kInkDim);
  } else {
    canvas_.text(joys_.count() == 0 ? "Nothing attached" : "Click the panel or Enter: move each", lo + glm::vec2(0, 24), kInkDim);
    canvas_.text("control when asked. Left, right: device", lo + glm::vec2(0, 42), kInkDim);
  }
  // The devices by letter, then each control: its axis, which way, where the axis is now.
  float y = lo.y + 66.0f;
  for (int d = 0; d < joys_.count() && d < 4; ++d, y += 15.0f) {
    canvas_.clip(kPrefGlassLo, kPrefGlassHi - glm::vec2(8.0f, 0.0f));
    canvas_.text(std::string(1, char('A' + d)) + "  " + joys_.name(d) + (d == joys_.main(map) ? "  (buttons)" : ""), {lo.x, y}, kInkDim);
    canvas_.noClip();
  }
  y += 4.0f;
  for (int c = 0; c <= joy::kControls; ++c, y += 15.0f) {
    const joy::Binding& b = c < joy::kControls ? map.axes[c] : map.hat;
    const int d = joys_.of(map, b);
    const bool asked = c == learnStep_;
    canvas_.text(c < joy::kControls ? joy::label(joy::Control(c)) : "Looking hat", {lo.x, y}, asked ? kInk : kInkDim);
    std::string what = b.axis < 0 ? "-" : d < 0 ? "not attached" : std::string(1, char('A' + d)) + (c < joy::kControls ? " axis " : " hat ") + std::to_string(b.axis + 1);
    if (asked) what = "?";
    if (b.axis >= 0 && b.invert && !asked) what += " rev.";
    canvas_.text(what, {right - 60.0f, y}, asked ? kInk : kInkDim, 1);
    // Where it is: a mark on a short scale.
    if (c < joy::kControls && b.axis >= 0 && d >= 0 && !asked) {
      const float v = std::clamp(joys_.value(map, joy::Control(c)), -1.0f, 1.0f), x0 = right - 50.0f, x1 = right;
      canvas_.fill({x0, y + 6}, {x1, y + 7}, kInkDim);
      const float x = (x0 + x1) * 0.5f + v * (x1 - x0) * 0.5f;
      canvas_.fill({x - 1.5f, y + 3}, {x + 1.5f, y + 10}, kInk);
    }
  }
}

void FrontEnd::drawPrefs() {
  namespace joy = platform::joy;
  canvas_.image(prefs_, {0, 0}, {640, 480});
  if (!cfg_) return;
  joys_.refresh();
  learn();
  // The glass: what the setting chosen does (the joystick: its devices and axes).
  if (prefFocus_ == kPrefJoystick || learnStep_ >= 0) {
    drawJoystickGlass();
  } else {
    const Pref& f = kPrefList[prefFocus_];
    canvas_.text("Preferences", {kPrefGlassLo.x + 10, kPrefGlassLo.y + 8}, kInk);
    canvas_.text(std::string(f.label) + ": " + prefValue(prefFocus_), {kPrefGlassLo.x + 10, kPrefGlassLo.y + 36}, kInk);
    float y = kPrefGlassLo.y + 60;
    for (const std::string& line : canvas_.wrap(f.help, kPrefGlassHi.x - kPrefGlassLo.x - 20)) {
      canvas_.text(line, {kPrefGlassLo.x + 10, y}, kInkDim);
      y += 16.0f;
    }
    canvas_.text("Saved in hind.ini", {kPrefGlassLo.x + 10, kPrefGlassHi.y - 22}, kInkDim);
  }
  // The bars: the setting and its value, the chosen one pressed in.
  for (int i = 0; i < kPrefCount; ++i) {
    const glm::vec2 lo = kPrefBarLo[i], hi = lo + glm::vec2(kPrefBarW, kPrefBarH);
    const bool down = pressed_ == kPref0 + i && inside(mouse_, lo, hi);
    if (i == prefFocus_ || down) canvas_.fill(lo, hi, glm::vec4(0, 0, 0, i == prefFocus_ ? 0.16f : 0.25f));
    const float d = down ? 1.0f : 0.0f, ty = std::floor((lo.y + hi.y) * 0.5f) - 7 + d;
    canvas_.text(kPrefList[i].label, {lo.x + 8 + d, ty}, kInk);
    canvas_.text(prefValue(i), {hi.x - 8 + d, ty}, kInk, 1);
  }
  // The joystick's panel: the main device's name, the stick in the square, the collective up the
  // left, the pedals along the bottom, the hat's arrows lit and the look axes its centre, each
  // from the device it is on.
  if (prefFocus_ == kPrefJoystick) canvas_.fill(kJoyPanelLo, {kJoyPanelHi.x, kJoyNameBottom}, glm::vec4(0, 0, 0, 0.16f));
  const joy::Map map = learnStep_ >= 0 ? learnMap_ : joy::load(*cfg_);
  const bool on = cfg_->getBool("joystick", "enabled", true);
  const int main = joys_.main(map);
  const glm::vec4 lit{0.95f, 0.85f, 0.55f, 1.0f};
  if (!on || main < 0) {
    canvas_.text(!on && joys_.count() > 0 ? "Joystick off" : "No joystick", {475, 262}, kInk, 0);
    return;
  }
  canvas_.clip(kJoyPanelLo, {kJoyPanelHi.x, kJoyNameBottom});
  std::string name = joys_.name(main);
  if (joys_.count() > 1) name += "  (" + std::to_string(main + 1) + "/" + std::to_string(joys_.count()) + ")";
  canvas_.text(name, {475, 262}, kInk, 0);
  canvas_.noClip();
  auto value = [&](joy::Control c) { return std::clamp(joys_.value(map, c), -1.0f, 1.0f); };
  const glm::vec2 c = glm::vec2(475, 339) + glm::vec2(value(joy::CyclicX), -value(joy::CyclicY)) * 30.0f;
  canvas_.fill(c - glm::vec2(3), c + glm::vec2(3), lit);
  if (map.axes[joy::Collective].axis >= 0) {
    const float cyPos = 368.0f - (1.0f + value(joy::Collective)) * 0.5f * 58.0f;
    canvas_.fill({338, cyPos - 2}, {344, cyPos + 2}, lit);
  }
  if (map.axes[joy::Pedal].axis >= 0) {
    const float px = 385.0f + value(joy::Pedal) * 33.0f;
    canvas_.fill({px - 2, 365}, {px + 2, 371}, lit);
  }
  const Uint8 hat = joys_.hat(map);
  if (hat & SDL_HAT_UP) canvas_.fill({562, 311}, {569, 317}, lit);
  if (hat & SDL_HAT_DOWN) canvas_.fill({562, 361}, {569, 367}, lit);
  if (hat & SDL_HAT_LEFT) canvas_.fill({537, 336}, {543, 343}, lit);
  if (hat & SDL_HAT_RIGHT) canvas_.fill({588, 336}, {594, 343}, lit);
  if (map.axes[joy::LookX].axis >= 0 || map.axes[joy::LookY].axis >= 0) {
    const glm::vec2 l = glm::vec2(565.5f, 339.5f) + glm::vec2(value(joy::LookX), -value(joy::LookY)) * 14.0f;
    canvas_.fill(l - glm::vec2(2), l + glm::vec2(2), lit);
  }
}

bool FrontEnd::enterTopTen(int score, bool crashed) {
  topCrashed_ = crashed;
  if (!topTen_ || topTen_->place(score) < 0) return false;
  topScore_ = score;
  topEntry_ = topTen_->place(score);
  // The pilot flying, to keep or to change.
  typed_ = log_ && !log_->pilots.empty() && !log_->pilot().immortal ? log_->pilot().name : "";
  // After the promotion and the medals, if there are any.
  if (page_ == Page::Award) {
    if (afterAwards_ != Page::TopTen) topFrom_ = afterAwards_;
    afterAwards_ = Page::TopTen;
    return true;
  }
  if (page_ != Page::TopTen) topFrom_ = page_;
  open(Page::TopTen);
  return true;
}

void FrontEnd::drawTopTen() {
  canvas_.image(topTenBg_, {0, 0}, {640, 480});
  // The table on the steel band: the place, the name and the score; the line being entered red.
  std::vector<sim::TopTenEntry> rows = topTen_ ? topTen_->entries : std::vector<sim::TopTenEntry>{};
  if (topEntry_ >= 0) {
    rows.insert(rows.begin() + std::min(topEntry_, int(rows.size())), {"", topScore_, topCrashed_});
    if (rows.size() > size_t(sim::TopTen::kSize)) rows.resize(sim::TopTen::kSize);
  }
  constexpr float kTop = 116.0f, kRow = 23.0f, kLeft = 70.0f, kRight = 570.0f;
  for (int i = 0; i < sim::TopTen::kSize; ++i) {
    const float y = kTop + float(i) * kRow;
    const bool entering = i == topEntry_;
    if (entering) canvas_.fill({kLeft - 12, y - 3}, {kRight + 12, y + 17}, glm::vec4(kRed.r, kRed.g, kRed.b, 0.9f));
    const glm::vec4 ink = entering ? kWhite : kInk;
    canvas_.text(std::to_string(i + 1) + ".", {kLeft + 18, y}, ink, 1);
    if (i >= int(rows.size())) {
      canvas_.text("-", {kLeft + 34, y}, kInkDim);
      continue;
    }
    const std::string name = entering ? typed_ + ((SDL_GetTicks() / 400) % 2 ? "_" : "") : rows[size_t(i)].name;
    canvas_.text(name, {kLeft + 34, y}, ink);
    canvas_.text(std::to_string(rows[size_t(i)].score), {kRight, y}, ink, 1);
    if (rows[size_t(i)].crashed) canvas_.text("x", {kRight - 64, y}, ink, 1);
  }
  if (topEntry_ >= 0) canvas_.text("Type your name and press Enter", {320, 378}, kInk, 0);
  else if (std::any_of(rows.begin(), rows.end(), [](const sim::TopTenEntry& e) { return e.crashed; }))
    canvas_.text("x: the flight ended in a crash", {320, 378}, kInk, 0);
}

// The campaign's page: the picture above, the text on the steel below it.
const glm::vec2 kCampTextLo{26, 318}, kCampTextHi{580, 470};
constexpr float kCampLine = 16.0f;
constexpr int kCampLines = 6;

void FrontEnd::openCampaign(const std::string& zone) {
  if (!cfg_) return;
  campZone_ = zone;
  camp_ = gamedata::loadCampaign(*cfg_, zone, dir_);
  campPages_ = gamedata::campaignPages(camp_, dir_, zone);
  wrapCampaign();
  campPage_ = campScreen_ = 0;
  // The war's picture (Ukraine's: ours, on Afghanistan's frame).
  const std::string pic = zone == "kazak" ? "C_KAZAK.LBM" : zone == "korea" ? "C_KOREA.LBM" : "C_AFGHAN.LBM";
  loadPicture(pic, campaignBg_, -1);
  if (zone == "ukraine") ukrainePicture(campaignBg_);
  if (page_ == Page::Award) afterAwards_ = Page::Campaign;
  else open(Page::Campaign);
  if (!campPages_.empty() && speak_ && page_ == Page::Campaign) speak_(campPages_[0].speech);
}

void FrontEnd::wrapCampaign() {
  // (Again each frame: the font's width goes by the window's size.)
  campScreens_.clear();
  for (const gamedata::CampaignPage& p : campPages_) {
    std::vector<std::vector<std::string>> screens(1);
    std::stringstream in(p.text);
    for (std::string l; std::getline(in, l, '\n');) {
      // A paragraph: after an empty line, unless it starts a screen.
      if (!screens.back().empty()) screens.back().push_back("");
      for (const std::string& w : canvas_.wrap(l, kCampTextHi.x - kCampTextLo.x)) {
        if (int(screens.back().size()) >= kCampLines) screens.emplace_back();
        if (!(w.empty() && screens.back().empty())) screens.back().push_back(w);
      }
    }
    if (screens.back().empty() && screens.size() > 1) screens.pop_back();
    while (!screens.back().empty() && screens.back().back().empty()) screens.back().pop_back();
    campScreens_.push_back(screens);
  }
}

FrontResult FrontEnd::campaignNext() {
  // The next screen of the text, the next page (read out), then the mission.
  if (campPage_ < int(campScreens_.size())) {
    const int screens = int(campScreens_[size_t(campPage_)].size());
    if (++campScreen_ < screens) return FrontResult::None;
    campScreen_ = 0;
    if (++campPage_ < int(campPages_.size())) {
      if (speak_) speak_(campPages_[size_t(campPage_)].speech);
      return FrontResult::None;
    }
  }
  if (speak_) speak_("");
  using gamedata::CampaignShow;
  if (camp_.show == CampaignShow::Won || camp_.show == CampaignShow::Failure) {
    // Over: the next one from the start.
    note(camp_.show == CampaignShow::Won ? "Campaign won" : "Campaign lost");
    gamedata::restartCampaign(camp_);
    savePrefs([&](platform::Config& c) { gamedata::saveCampaign(c, campZone_, camp_); });
    worldForCampaign_ = true, quickWorld_ = 0;
    open(Page::World);
    return FrontResult::None;
  }
  // The level's mission (another after a failure), told only once.
  if (camp_.mission.empty()) camp_.mission = gamedata::pickCampaignMission(camp_, dir_, campZone_, SDL_GetTicks() ^ unsigned(std::time(nullptr)));
  if (camp_.mission.empty()) {
    note("No mission for this level");
    return FrontResult::None;
  }
  camp_.show = CampaignShow::Situation;
  savePrefs([&](platform::Config& c) { gamedata::saveCampaign(c, campZone_, camp_); });
  choice_ = Choice{};
  choice_.zone = campZone_;
  choice_.gunnerHints = gunnerHints_;
  return FrontResult::Fly;
}

void FrontEnd::ukrainePicture(render::Texture& t) {
  gamedata::Picture frame;
  std::string error;
  if (!ukrainePic_ || !gamedata::loadPbm(gamedata::findOriginalFile(dir_, "MENUSCRN/C_AFGHAN.LBM"), frame, -1, error)) return;
  // A binary PPM, as wide as the frame or a whole number of times wider (each block of pixels
  // averaged), laid over its picture from the top of the window down.
  const std::string path = ukrainePic_();
  if (path.empty()) return;
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w = 0, h = 0, max = 0;
  if (!(in >> magic >> w >> h >> max) || magic != "P6" || max != 255 || w < frame.width || w % frame.width) return;
  in.get();
  std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3);
  if (!in.read(reinterpret_cast<char*>(rgb.data()), std::streamsize(rgb.size()))) return;
  const int k = w / frame.width;
  constexpr int kTop = 37;  // the picture's first row in the frame
  for (int y = 0; y < h / k && kTop + y < frame.height; ++y)
    for (int x = 0; x < frame.width; ++x) {
      uint8_t* d = &frame.rgba[(size_t(kTop + y) * size_t(frame.width) + size_t(x)) * 4];
      for (int c = 0; c < 3; ++c) {
        int sum = 0;
        for (int sy = 0; sy < k; ++sy)
          for (int sx = 0; sx < k; ++sx) sum += rgb[(size_t(y * k + sy) * size_t(w) + size_t(x * k + sx)) * 3 + size_t(c)];
        d[c] = uint8_t(sum / (k * k));
      }
      d[3] = 255;
    }
  t.upload(frame);
}

void FrontEnd::drawCampaign() {
  canvas_.image(campaignBg_, {0, 0}, {640, 480});
  wrapCampaign();
  if (campPage_ >= int(campPages_.size())) return;
  const gamedata::CampaignPage& p = campPages_[size_t(campPage_)];
  const auto& screens = campScreens_[size_t(campPage_)];
  static const std::vector<std::string> none;
  const std::vector<std::string>& lines = campScreen_ < int(screens.size()) ? screens[size_t(campScreen_)] : none;
  canvas_.text(p.heading, kCampTextLo, kWhite);
  std::string lives;
  for (int i = 0; i < camp_.lives; ++i) lives += "* ";
  canvas_.text("Lives " + lives, {kCampTextHi.x, kCampTextLo.y}, kDimText, 1);
  float y = kCampTextLo.y + 22.0f;
  for (const std::string& l : lines) {
    canvas_.text(l, {kCampTextLo.x, y}, glm::vec4(0.86f, 0.86f, 0.88f, 1.0f));
    y += kCampLine;
  }
  canvas_.text("Click or Enter: continue", {kCampTextLo.x, 452}, kDimText);
}

void FrontEnd::drawNetwork() {
  canvas_.image(network_, {0, 0}, {640, 480});
  // The games heard: the game, its pilot, his crew.
  canvas_.text("Games on the network", {kGamesLo.x + 10, kGamesLo.y + 8}, kWhite);
  const std::vector<const net::Advert*> rows = netRows();
  if (netRow_ >= int(rows.size())) netRow_ = int(rows.size()) - 1;
  if (rows.empty()) canvas_.text("None heard yet", {kGamesLo.x + 10, kGameRowTop + 4}, kDimText);
  canvas_.clip(kGamesLo, kGamesHi - glm::vec2(4.0f, 0.0f));
  for (int i = 0; i < int(rows.size()) && i < kGameRows; ++i) {
    const net::Advert& a = *rows[size_t(i)];
    const float y = kGameRowTop + float(i) * kGameRowH;
    if (i == netRow_) canvas_.fill({kGamesLo.x + 4, y - 2}, {kGamesHi.x - 4, y + kGameRowH - 4}, glm::vec4(kRed.r, kRed.g, kRed.b, 0.85f));
    const float x = kGamesLo.x + (a.host ? 10.0f : 50.0f);
    if (a.host && server_.loaded()) canvas_.image(server_, {x, y + 2});
    const float tx = x + (a.host ? 26.0f : 0.0f);
    canvas_.text(a.host ? a.game : "Wingman", {tx, y}, kWhite);
    std::string pilots = "Pilot " + a.pilot;
    if (a.host && !a.wingmen.empty()) pilots += ", " + std::to_string(a.wingmen.size()) + (a.wingmen.size() == 1 ? " wingman" : " wingmen");
    canvas_.text(pilots, {tx, y + 15}, kWhite);
    auto who = [](const std::string& n) { return n.empty() ? std::string("free") : n; };
    canvas_.text("Gun " + who(a.crew[0]) + "  L " + who(a.crew[1]) + "  R " + who(a.crew[2]), {tx, y + 30}, glm::vec4(0.78f, 0.78f, 0.80f, 1.0f));
  }
  canvas_.noClip();
  // The bars: what each holds engraved on it, the one being typed in with its cursor.
  const std::string cursor = (SDL_GetTicks() / 400) % 2 ? "_" : "";
  for (const Item& it : items()) {
    if (it.brush != -2) continue;
    const bool down = (it.id == pressed_ && inside(mouse_, it.lo, it.hi)) || it.id == pending_;
    const int field = it.id == kNetName ? 1 : it.id == kNetGame ? 2 : it.id == kNetAddress ? 3 : 0;
    std::string text = it.label;
    if (it.id == kNetName) text = "Your name: " + net_.name;
    if (it.id == kNetGame) text = "Game: " + net_.game;
    if (it.id == kNetSeat) text = std::string("Seat: ") + netSeatName(net_.seat);
    if (it.id == kNetAddress) text = "By address";
    if (field != 0 && field == netField_) text = (field == 3 ? "" : it.label + ": ") + typed_ + cursor;
    if (down || (field != 0 && field == netField_)) canvas_.fill(it.lo, it.hi, glm::vec4(0, 0, 0, 0.18f));
    const float d = down ? 1.0f : 0.0f;
    canvas_.clip(it.lo, it.hi);
    canvas_.text(text, {it.lo.x + 8 + d, std::floor((it.lo.y + it.hi.y) * 0.5f) - 7 + d}, it.available ? kInk : kInkDim);
    canvas_.noClip();
    if (keyboard_ && it.id == focus_) canvas_.frame(it.lo, it.hi, glm::vec4(0.95f, 0.85f, 0.55f, 0.75f));
  }
}

void FrontEnd::draw(int w, int h) {
  canvas_.begin(w, h);
  switch (page_) {
    case Page::Main: canvas_.image(main_, {0, 0}, {640, 480}); break;
    case Page::Quick:
      canvas_.image(main_, {0, 0}, {640, 480});
      canvas_.image(quickOptions_, kQuickOptionsAt);
      break;
    case Page::Flight: canvas_.image(flight_, {0, 0}, {640, 480}); break;
    case Page::World:
      canvas_.image(world_, {0, 0}, {640, 480});
      if (ukraine_.loaded()) canvas_.image(ukraine_, {415, 216});
      break;
    case Page::Exit: canvas_.image(exit_, {0, 0}, {640, 480}); break;
    case Page::Missions: drawMissions(); break;
    case Page::Log: drawLog(); break;
    case Page::Award: canvas_.image(award_, {0, 0}, {640, 480}); break;
    case Page::Animation: {
      // Twice its size across the screen, on black.
      canvas_.fill({0, 0}, {640, 480}, glm::vec4(0, 0, 0, 1));
      const glm::vec2 size(anim_.width() * 2.0f, anim_.height() * 2.0f);
      canvas_.image(animTex_, (glm::vec2(640, 480) - size) * 0.5f, size);
      break;
    }
    case Page::Network: drawNetwork(); break;
    case Page::Prefs: drawPrefs(); break;
    case Page::TopTen: drawTopTen(); break;
    case Page::Campaign: drawCampaign(); break;
    case Page::Recognition: {
      canvas_.image(recognition_, {0, 0}, {640, 480});
      // The unit's name and who has it under its model, the list and its place on the right.
      const RecList& l = kRecLists[recList_];
      const RecUnit& u = l.units[recItem_];
      canvas_.fill({kRecModelLo.x, 426}, {kRecModelHi.x, 470}, glm::vec4(0, 0, 0, 0.55f));
      canvas_.text(u.name, {(kRecModelLo.x + kRecModelHi.x) * 0.5f, 431}, kWhite, 0);
      canvas_.text(u.who, {(kRecModelLo.x + kRecModelHi.x) * 0.5f, 450}, glm::vec4(0.78f, 0.78f, 0.80f, 1.0f), 0);
      canvas_.text(std::string(l.title) + "  " + std::to_string(recItem_ + 1) + " / " + std::to_string(l.count), {521, 290}, kWhite, 0);
      break;
    }
  }
  // A stone button held down shows its pressed picture; the one the arrows are on is framed.
  for (const Item& it : stoneButtons()) {
    const bool down = it.id == pending_ || (it.id == pressed_ && inside(mouse_, it.lo, it.hi));
    if (down) canvas_.image(brushes_[size_t(it.brush)], it.lo);
    if (keyboard_ && it.id == focus_)
      canvas_.frame(it.lo + glm::vec2(2), it.hi - glm::vec2(2), glm::vec4(0.95f, 0.85f, 0.55f, 0.75f));
  }
  for (const Item& it : items()) {
    if (it.brush >= 0 || it.brush == -2) continue;
    redButton(it, (it.id == kInfinite && infiniteWeapons_) || (it.id == kNoCrash && noCrash_) || (it.id == kWsoAllies && wsoAllies_) ||
              (it.id == kGunnerHints && gunnerHints_));
  }
  if (goTo_) {
    // The menu's lines on one red plate, the one under the mouse lit.
    const glm::vec2 hi = kGoToMenuLo + glm::vec2(kGoToMenuW, 6 * kGoToLine);
    canvas_.fill(kGoToMenuLo, hi, kRed);
    canvas_.frame(kGoToMenuLo, hi, kRedDark);
    for (const Item& it : items()) {
      if (it.id == kBack) continue;
      if (inside(mouse_, it.lo, it.hi)) canvas_.fill(it.lo + glm::vec2(1), it.hi - glm::vec2(1), kRedLight);
      canvas_.text(it.label, {it.lo.x + 5, it.lo.y + 3}, it.available ? kWhite : kDimText);
      if (it.id == kGoQuit) canvas_.fill({it.lo.x + 2, it.lo.y}, {it.hi.x - 2, it.lo.y + 1}, kRedDark);
    }
  }
  if (noteTime_ <= 0.0f && hosting_ && page_ != Page::Award && page_ != Page::Log) {
    // The game hosted, and who is in it.
    const float tw = canvas_.textWidth(hostStatus_);
    const glm::vec2 lo{14, 446}, hi{tw + 34, 470};
    canvas_.fill(lo, hi, kRedDark);
    canvas_.frame(lo, hi, kRed);
    canvas_.text(hostStatus_, {lo.x + 10, lo.y + 6}, kWhite);
  }
  if (noteTime_ > 0.0f) {
    const float tw = canvas_.textWidth(note_);
    const glm::vec2 lo{14, 446}, hi{tw + 34, 470};
    canvas_.fill(lo, hi, kRed);
    canvas_.frame(lo, hi, kRedLight);
    canvas_.text(note_, {lo.x + 10, lo.y + 6}, kWhite);
  }
  canvas_.end();
  if (page_ == Page::Recognition && !goTo_) {
    // The model over the picture's left part.
    if (!recDataTried_) {
      recDataTried_ = true;
      if (!recData_.open(dir_)) std::fprintf(stderr, "Recognition: %s\n", recData_.error().c_str());
    }
    const glm::vec2 lo = canvas_.toWindow(kRecModelLo), hi = canvas_.toWindow(kRecModelHi);
    viewer_.draw(recData_, kRecLists[recList_].units[recItem_].object, {lo.x, lo.y, hi.x, hi.y}, h, float(SDL_GetTicks()) / 1000.0f);
    glViewport(0, 0, w, h);
  }
}

}  // namespace menu
