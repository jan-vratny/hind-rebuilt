#include "gamedata/sound.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

#include "gamedata/original.hpp"

namespace gamedata {

bool SoundBank::load(const std::string& gameDir, std::string& error) {
  blocks_.clear();
  const std::string path = findOriginalFile(gameDir, "SOUND/MAXIMAL.BLK");
  std::ifstream f(path, std::ios::binary);
  if (path.empty() || !f) {
    error = "No SOUND/MAXIMAL.BLK in " + gameDir;
    return false;
  }
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t o) {
    uint32_t v;
    std::memcpy(&v, d.data() + o, 4);
    return v;
  };
  for (size_t o = 4; o + 8 <= d.size();) {
    const uint32_t id = u32(o), len = u32(o + 4);
    o += 8;
    if (id > 4096 || len > d.size() - o) {
      error = path + ": damaged at offset " + std::to_string(o - 8);
      blocks_.clear();
      return false;
    }
    if (id >= blocks_.size()) blocks_.resize(id + 1);
    std::vector<float>& s = blocks_[id];
    s.resize(len);
    for (uint32_t i = 0; i < len; ++i) s[i] = (float(d[o + i]) - 128.0f) / 128.0f;
    o += len;
  }
  if (blocks_.empty()) error = path + ": no sounds";
  return !blocks_.empty();
}

namespace {

// Object names (a trailing * matches the beginning) with the WSO's words for them, in the order
// he has them; found by comparing the names with what he says.
struct TypeLine {
  const char* object;
  int line;
};
const TypeLine kTypeLines[] = {
    {"Mi_24*", 177}, {"Mi_17_Hip_H", 178}, {"MiG_27_Flogger", 179}, {"MiG_29_Fulcrum", 180},
    {"Su_25_Frogfoot", 181}, {"Antonov_An_12BP_Cub_A", 182}, {"T62_MBT*", 183}, {"T80_MBT", 184},
    {"BMP2_APC", 185}, {"BRDM_2_APC", 186}, {"BRDM_2_Sagger", 187}, {"MT_LBU_Command_APC", 188},
    {"MT_LBus_Jamming_Vehicle", 189}, {"Long_Track_Mobile_EWR", 190}, {"HMMWV_Utility_Vehicle", 191},
    {"D_30_Gun_Howitzer", 192}, {"D30_Gun_Howitzer_Dugin", 192}, {"SA8_ROMB_Mobile_SAM", 193},
    {"SA_8_Gecko_SAM", 193}, {"SA_9_Gaskin_SP_SAM", 194}, {"SA13_Strela_Mobile_SAM", 195},
    {"ZSU23_Shilka_AAA", 196}, {"maz543", 197}, {"Silkworm_Launcher", 198},
    {"Romeo_Class_Sub", 199},  // "Romeo class frigate", as he says
    {"Missile_Boat_OSA1", 200}, {"AH64D_Apache_Longbow", 201}, {"UH_60_Blackhawk", 202},
    {"AH_1W_SuperCobra", 203}, {"F_14A_Tomcat", 204}, {"F15", 205}, {"F16*", 206}, {"F_16C_High_Detail", 206},
    {"F18_Hornet", 207}, {"A10_Thunderbolt", 208}, {"C130_Hercules", 209}, {"M1A1_Abrams_MBT", 210},
    {"Challenger_1_MBT", 211}, {"Leopard_1_MBT", 212}, {"M113APC_basic", 213}, {"Warrior_APC", 214},
    {"MLRS_AFV", 215}, {"Leop1_Biber_Bridgelayer", 216}, {"Leopard_1_AEV_ARV", 217},
    {"Giraffe_Mobile_Radar", 218}, {"UAZ_469_Russian_Jeep", 219}, {"M109_SPH", 220},
    {"Patriot_Launcher", 221}, {"Tracked_Rapier_SPSAM", 222}, {"M163_Vulcan", 223},
    {"GDF_D03_SPAAG", 224}, {"Oerlikon_35mm_Twin_Mount", 224}, {"LHA_Tarawa", 225},
    {"Frigate_OHP_Class", 226}, {"MEKO_200_Class_FFG", 227}, {"FPB_57_Patrol_Boat", 228},
    {"Type_209_SSK", 229}, {"Generic_UAV", 230}, {"Military_Supply_Barge", 231}, {"Mine_Sweeper", 232},
    {"Mine_Layer", 233}, {"Coastal_Gun_Emplacement", 234}, {"HAS_*", 235}, {"Dual_HAS_*", 235},
    {"Hangar_*", 236}, {"bunker", 237}, {"Bunker_*", 237}, {"Barrack*", 238}, {"Hard_Bulk_Fuel_Store", 239},
    {"Munitions_Store*", 240}, {"Control_Tower_*", 241}, {"Water_Tower_Military", 242}, {"EWR_Model_1", 243},
    {"ewrd", 243}, {"commcn", 244}, {"Fort", 245}, {"Tent_*", 246}, {"Fuel_Bladder*", 247},
    {"Supply_Pallets*", 248}, {"Truck_Military_A", 249}, {"Fuel_Bowser_Small", 250},
    {"Oil_Tanker_250000dwt", 251}, {"Cargo_Ship_7500dwt", 252}, {"Generic_Tug", 253},
    {"Civilian_Supply_Barge", 254}, {"Lifeboat_36ft", 255}, {"Car", 256}, {"Truck_Civ*", 257},
    {"Generic_Diesel_Loco", 258}, {"Rail_Tank_Txpt_Generic", 259}, {"Rail_Tanker_Generic", 260},
    {"Rail_Wagon_Generic", 261}, {"Power_Stn_Turbine_Hall", 262}, {"pstnn", 262}, {"Cooling_Tower", 263},
    {"Smokestack_Tall", 264}, {"stacksd", 264}, {"Stack_120_NS", 264}, {"POL_Tank_Spherical_1", 265},
    {"gentanks", 266}, {"cyltanks", 266}, {"Oil_Pumping_Station", 268}, {"Oil_Well_Head", 269},
    {"Airfield_Radar_Tower", 270}, {"Airport_Terminal_Bldg", 271}, {"Rail_Station_Large_1", 273},
    {"Bridge_*", 274}, {"Korean_Government_Bldg", 275}, {"Soviet_Regional_Govt_Bldg", 275},
    {"Industrial_Building*", 276}, {"PortaKabin_24", 278}, {"Tower_Block*", 280}, {"Soviet_Housing*", 282},
    {"Arabic_Housing", 282}, {"Afghan_House_*", 282}, {"hotel", 283}, {"tvmast", 284}, {"Afghan_Mosque_*", 285},
    {"Mosque_*", 285}, {"Floodlight_Tower_A", 286}, {"Russian_Soldier_*", 288}, {"Mujaheddin_*", 289},
    {"US_Soldier_*", 290}, {"US_Infantryman", 290}, {"DShK_HMG*", 291}, {"M2_Heavy_Machine_Gun", 292},
    {"M2_HMG_Tripod", 292}, {"Red_Flag", 293}, {"Blue_Flag", 294},
};

bool matches(const std::string& name, const char* pattern) {
  size_t i = 0;
  for (; pattern[i] && pattern[i] != '*'; ++i)
    if (i >= name.size() || std::tolower(name[i]) != std::tolower(pattern[i])) return false;
  return pattern[i] == '*' || i == name.size();
}

}  // namespace

std::string wsoTypeName(int line) {
  // What the WSO says, as the game's long names have it ("Government Building").
  static const char* names[] = {
    "Mi-24 Hind",
    "Mi-17",
    "MiG-27",
    "MiG-29",
    "Su-25",
    "An-12",
    "T-62 Tank",
    "T-80 Tank",
    "BMP-2 APC",
    "BRDM-2 APC",
    "BRDM-2 Sagger APC",
    "MT-LB Command Vehicle",
    "MT-LB 2 Jamming Vehicle",
    "Mobile Early Warning Radar",
    "Utility Vehicle",
    "D-30 Howitzer",
    "SA-8 SAM",
    "SA-9 SAM",
    "SA-13 SAM",
    "ZSU-23 AAA",
    "SCUD Launcher",
    "Silkworm Launcher",
    "Romeo Class Submarine",
    "Osa 1 Missile Boat",
    "AH-64D Apache",
    "UH-60 Black Hawk",
    "AH-1W Super Cobra",
    "F-14",
    "F-15",
    "F-16",
    "F-18",
    "A-10",
    "C-130 Hercules",
    "M1A1 Tank",
    "Challenger Tank",
    "Leopard Tank",
    "M113 APC",
    "Warrior APC",
    "MLRS",
    "Leopard Bridge Layer",
    "Leopard Engineering Vehicle",
    "Giraffe Mobile Radar",
    "UAZ Jeep",
    "M109 Howitzer",
    "Patriot SAM Launcher",
    "Rapier SAM",
    "M163 AAA",
    "GDF AAA",
    "LHA Tarawa",
    "OHP Frigate",
    "MEKO 200 Frigate",
    "FPB-57 Patrol Boat",
    "Type 209 Submarine",
    "Drone Aircraft",
    "Military Supply Barge",
    "Minesweeper",
    "Minelayer",
    "Coastal Gun",
    "Hardened Aircraft Shelter",
    "Hangar",
    "Bunker",
    "Barracks",
    "Hardened Field Store",
    "Munition Store",
    "Control Tower",
    "Water Tower",
    "Early Warning Radar",
    "Communication Centre",
    "Fort",
    "Tent",
    "Field Fuel Bladder",
    "Supply Pallets",
    "Military Truck",
    "Field Bowser",
    "Oil Tanker",
    "Cargo Ship",
    "Tug",
    "Civilian Supply Barge",
    "Lifeboat",
    "Car",
    "Civilian Truck",
    "Diesel Locomotive",
    "Tank Transporter",
    "Rail Tanker",
    "Rail Wagon",
    "Power Station",
    "Cooling Tower",
    "Chimney",
    "POL Tank",
    "Storage Tanks",
    "Oil Storage Tanks",
    "Oil Pumping Station",
    "Oil Well",
    "Airfield Radar",
    "Terminal Building",
    "Runway",
    "Railway Station",
    "Bridge",
    "Government Building",
    "Industrial Building",
    "Warehouse",
    "Portakabin",
    "Petrol Station",
    "Office Block",
    "Officers",
    "Housing",
    "Hotel",
    "TV Mast",
    "Mosque",
    "Floodlight Tower",
    "Kazakh Rebel Soldier",
    "Russian Soldier",
    "Mujaheddin Soldier",
    "American Soldier",
    "DShK Machine Gun",
    "M2 Machine Gun",
    "Red Flag",
    "Blue Flag"};
  const int i = line - snd::WsoTypeFirst;
  return i >= 0 && i < int(sizeof names / sizeof *names) ? names[i] : "";
}

int wsoTypeLine(const std::string& object, bool enemy) {
  for (const TypeLine& t : kTypeLines)
    if (matches(object, t.object)) return t.line == 288 && enemy ? 287 : t.line;  // Kazakh rebel
  return -1;
}

const std::vector<float>& SoundBank::get(int id) const {
  static const std::vector<float> none;
  return id >= 0 && id < int(blocks_.size()) ? blocks_[size_t(id)] : none;
}

}  // namespace gamedata
