#include "gamedata/mission.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "gamedata/original.hpp"
#include "gamedata/zone.hpp"

namespace gamedata {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The file's lines without line ends (the files mix DOS and Unix ones).
bool readLines(const std::string& path, std::vector<std::string>& lines) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return true;
}

std::vector<int> csvInts(const std::string& s) {
  std::vector<int> v;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) v.push_back(int(std::strtol(item.c_str(), nullptr, 10)));
  return v;
}

MissionWaypoint waypoint(const std::vector<int>& f) {
  MissionWaypoint w;
  w.type = f[0];
  w.pos = glm::vec2(float(f[1]), float(f[2])) * kZoneUnit;
  w.alt = float(f[3]) * kZoneUnit;
  w.time = f[4] < 0 ? -1.0f : float(f[4]) * 0.01f;
  w.speed = float(f[5]) * kFeet / 512.0f;
  w.wait = f.size() > 17 ? float(f[17]) * 0.01f : 0.0f;
  return w;
}

void parseBriefing(const std::vector<std::string>& lines, Mission& m) {
  std::string section, text;
  auto flush = [&]() {
    std::string t = trim(text);
    text.clear();
    if (t.empty()) return;
    if (section == "Title") {
      m.title = t;
      return;
    }
    if (section == "Outstanding" || section == "Success" || section == "Failure") {
      (section == "Outstanding" ? m.outstandingText : section == "Success" ? m.successText : m.failureText) = t;
      return;
    }
    // "Key:value" lines are facts, anything else is prose.
    size_t colon = t.find(':');
    if (colon != std::string::npos && colon < 24 && t.find('\n') == std::string::npos &&
        t.find(". ") == std::string::npos) {
      m.facts.emplace_back(trim(t.substr(0, colon)), trim(t.substr(colon + 1)));
      return;
    }
    m.paragraphs.push_back(t);
  };
  for (const std::string& l : lines) {
    if (!l.empty() && l[0] == ':') {
      flush();
      section = l.substr(1);
      continue;
    }
    if (!text.empty()) text += ' ';
    text += trim(l);
  }
  flush();
}

void parseForces(const std::vector<std::string>& lines, Mission& m) {
  MissionTask* task = nullptr;
  MissionDrone* drone = nullptr;
  std::string key;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    if (l.empty()) continue;
    if (l[0] == ':') {
      key = l.substr(1);
      if (key == "Task") {
        m.tasks.emplace_back();
        task = &m.tasks.back();
        drone = nullptr;
      } else if (key == "User Task" && task) {
        task->user = true;
      } else if (key == "Drone" && task) {
        task->drones.emplace_back();
        drone = &task->drones.back();
      } else if (key == "Kill Success" && drone) {
        drone->kill = Kill::Success;
      } else if (key == "Follow Player" && drone) {
        drone->follow = true;
      } else if (key == "Kill Outstanding" && drone) {
        drone->kill = Kill::Outstanding;
      } else if (key == "Kill Failure" && drone) {
        drone->kill = Kill::Failure;
      } else if (key == "Destroy") {
        // Points, to the end of the file.
        for (size_t k = i + 1; k < lines.size() && !lines[k].empty() && lines[k][0] != ':'; ++k) {
          std::vector<int> f = csvInts(lines[k]);
          if (f.size() >= 2) m.destroyed.push_back(glm::vec2(float(f[0]), float(f[1])) * kZoneUnit);
        }
        task = nullptr;
        drone = nullptr;
      } else if (key.rfind("Gameplay", 0) == 0 || key == "Battle Cell") {
        task = nullptr;
        drone = nullptr;
      }
      continue;
    }
    if (key == "Gameplay Sides") {
      // "x,y side"
      int x = 0, y = 0, side = 0;
      if (std::sscanf(l.c_str(), "%d,%d %d", &x, &y, &side) == 3 && x >= 0 && y >= 0 && x < Mission::kSideCells &&
          y < Mission::kSideCells) {
        m.cellSides.resize(size_t(Mission::kSideCells * Mission::kSideCells), 0);
        m.cellSides[size_t(x * Mission::kSideCells + y)] = uint8_t(side);
      }
      continue;
    }
    if (key == "Gameplay AAA" || key == "Gameplay SAM" || key == "Gameplay Tanks" || key == "Gameplay APC") {
      std::vector<int> f = csvInts(l);
      if (f.size() < 2) continue;
      const glm::vec2 p = glm::vec2(float(f[0]), float(f[1])) * kZoneUnit;
      (key == "Gameplay AAA" ? m.gameplayAaa : key == "Gameplay SAM" ? m.gameplaySam : key == "Gameplay Tanks" ? m.gameplayTanks : m.gameplayApc).push_back(p);
      continue;
    }
    if (key == "Drone Name" && drone) {
      drone->name = trim(l);
      continue;
    }
    const int value = std::atoi(l.c_str());
    if (key == "Side" && task) task->side = value;
    else if (key == "Task Type" && task) task->type = value;
    else if (key == "Drone Type" && drone) drone->object = value + 1;
    else if (key == "Formation Type" && drone) drone->formation = value;
    else if (key == "Drone unarmed" && drone) drone->unarmed = value != 0;
    else if (key == "Drone cargo" && drone) drone->cargo = value;
    else if (key == "Drone onboard" && drone) drone->onboard = value;
    else if (key == "Start Waypoint" && drone) drone->startWaypoint = value;
    else if (key == "Waypoint" && drone) {
      std::vector<int> f = csvInts(l);
      if (f.size() >= 8) drone->route.push_back(waypoint(f));
    }
    else if (key == "Autorotation") m.autorotation = value;
    else if (key == "Time") {
      size_t colon = l.find(':');
      m.startHour = float(value) + (colon == std::string::npos ? 0.0f : float(std::atoi(l.c_str() + colon + 1)) / 60.0f);
    }
    else if (key == "Weather Condition") m.weather = value;
    else if (key == "Wind Speed") m.windSpeed = float(value);
    else if (key == "Wind Direction") m.windDirection = float(value);
    else if (key == "Success Count") m.successCount = value;
    else if (key == "Outstanding Count") m.outstandingCount = value;
    else if (key == "Failure Count") m.failureCount = value;
    else if (key == "Humans loaded count") m.humansLoaded = value;
    else if (key == "Weapon") {
      std::vector<int> f = csvInts(l);
      if (f.size() >= 2) m.weapons.emplace_back(f[0], f[1]);
    }
    else if (key == "Humans deployed count") m.humansDeployed = value;
  }
}

}  // namespace

int Mission::sideAt(glm::vec2 pos) const {
  if (cellSides.empty()) return 0;
  const float cell = 16384.0f * kFeet;
  const int x = int(std::floor(pos.x / cell)), y = int(std::floor(pos.y / cell));
  if (x < 0 || y < 0 || x >= kSideCells || y >= kSideCells) return 0;
  return cellSides[size_t(x * kSideCells + y)];
}

const MissionDrone* Mission::player() const {
  for (const MissionTask& t : tasks)
    if (t.user && !t.drones.empty() && t.drones[0].route.size() >= 2) return &t.drones[0];
  return nullptr;
}

std::string Mission::fact(const std::string& key) const {
  for (const auto& f : facts)
    if (upper(f.first) == upper(key)) return f.second;
  return {};
}

namespace {
std::vector<std::string>& ownDirs() {
  static std::vector<std::string> dirs;
  return dirs;
}
}  // namespace

void addMissionDir(const std::string& dir) {
  if (std::find(ownDirs().begin(), ownDirs().end(), dir) == ownDirs().end()) ownDirs().push_back(dir);
}

bool loadMission(const std::string& gameDir, const std::string& name, Mission& out, std::string& error) {
  out = Mission();
  out.name = upper(name);
  // The remake's own missions first, then the original's.
  std::string own;
  for (const std::string& d : ownDirs())
    if (fs::exists(d + "/" + out.name + ".DTA")) own = d;
  auto file = [&](const char* ext) {
    return own.empty() ? findOriginalFile(gameDir, "MISSDATA/" + out.name + ext) : own + "/" + out.name + ext;
  };
  std::vector<std::string> mis, txt, dta;
  if (!readLines(file(".DTA"), dta)) {
    error = "no mission " + out.name + " in " + gameDir + "/MISSDATA";
    return false;
  }
  if (readLines(file(".MIS"), mis))
    for (size_t i = 0; i + 1 < mis.size(); ++i) {
      if (mis[i] == ":War Zone") out.zone = std::atoi(mis[i + 1].c_str());
      if (mis[i] == ":Campaign Level") out.level = std::atoi(mis[i + 1].c_str());
    }
  if (readLines(file(".TXT"), txt)) parseBriefing(txt, out);
  if (out.title.empty()) out.title = out.name;
  parseForces(dta, out);
  if (!out.player()) {
    error = out.name + ": no route for the player's helicopter";
    return false;
  }
  return true;
}

void followPlayerRoute(Mission& m) {
  const MissionDrone* player = m.player();
  if (!player || player->route.size() < 2) return;
  std::vector<MissionWaypoint> middle(player->route.begin() + 1, player->route.end() - 1);
  if (middle.empty()) return;
  for (MissionTask& t : m.tasks)
    for (MissionDrone& d : t.drones) {
      if (!d.follow || &d == player || d.route.size() < 3) continue;
      const MissionWaypoint first = d.route.front(), last = d.route.back(), own = d.route[1];
      std::vector<MissionWaypoint> r{first};
      for (size_t k = 0; k + 2 < d.route.size(); ++k) {
        MissionWaypoint w = middle[k % middle.size()];
        w.alt = own.alt, w.speed = own.speed, w.wait = 0.0f;
        r.push_back(w);
      }
      MissionWaypoint end = last;
      r.push_back(end);
      // The times along the new legs (the first point keeps its own).
      for (size_t k = 1; k < r.size(); ++k)
        if (r[k].speed > 0.0f && r[k - 1].time >= 0.0f)
          r[k].time = r[k - 1].time + glm::distance(r[k - 1].pos, r[k].pos) / r[k].speed;
      d.route = std::move(r);
    }
}

std::vector<std::string> listMissions(const std::string& gameDir, const std::string& prefix) {
  std::vector<std::string> names;
  std::error_code ec;
  std::vector<std::string> dirs = ownDirs();
  if (const std::string dir = findOriginalFile(gameDir, "MISSDATA"); !dir.empty()) dirs.push_back(dir);
  for (const std::string& dir : dirs)
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      std::string f = upper(e.path().filename().string());
      if (f.size() > 4 && f.compare(f.size() - 4, 4, ".DTA") == 0 && f.rfind(upper(prefix), 0) == 0)
        names.push_back(f.substr(0, f.size() - 4));
    }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

namespace {

std::string missionFile(const std::string& gameDir, const std::string& name, const char* ext) {
  const std::string n = upper(name);
  for (const std::string& d : ownDirs())
    if (fs::exists(d + "/" + n + ext)) return d + "/" + n + ext;
  return findOriginalFile(gameDir, "MISSDATA/" + n + ext);
}

}  // namespace

int missionLevel(const std::string& gameDir, const std::string& name) {
  std::vector<std::string> mis;
  if (!readLines(missionFile(gameDir, name, ".MIS"), mis)) return 0;
  for (size_t i = 0; i + 1 < mis.size(); ++i)
    if (mis[i] == ":Campaign Level") return std::atoi(mis[i + 1].c_str());
  return 0;
}

std::vector<std::pair<std::string, std::string>> loadTextSections(const std::string& gameDir, const std::string& name) {
  std::vector<std::pair<std::string, std::string>> out;
  std::vector<std::string> lines;
  if (!readLines(missionFile(gameDir, name, ".TXT"), lines)) return out;
  for (const std::string& l : lines) {
    if (!l.empty() && l[0] == ':') {
      if (l != ":EOF") out.push_back({l.substr(1), ""});
      continue;
    }
    if (out.empty()) continue;
    std::string& t = out.back().second;
    // Lines run on; an empty one is a new paragraph.
    if (l.empty()) t += "\n";
    else t += (t.empty() || t.back() == '\n' ? "" : " ") + l;
  }
  return out;
}

bool loadFormations(const std::string& gameDir, std::vector<Formation>& out, std::string& error) {
  out.clear();
  std::vector<uint8_t> exe;
  {
    std::ifstream f(findOriginalFile(gameDir, "HIND.EXE"), std::ios::binary);
    if (!f) {
      error = "no HIND.EXE in " + gameDir;
      return false;
    }
    exe.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (at + 4 <= exe.size()) std::memcpy(&v, &exe[at], 4);
    return v;
  };
  // The names, one after another: "Diamond of T80's {Allied}[Any],{Enemy}[Kazak]", ...
  const char first[] = "Diamond of T80";
  auto it = std::search(exe.begin(), exe.end(), first, first + sizeof(first) - 1);
  if (it == exe.end()) {
    error = "HIND.EXE: no formation names (another version of the game?)";
    return false;
  }
  std::vector<size_t> names;
  for (size_t at = size_t(it - exe.begin()); names.size() < 400 && at < exe.size();) {
    names.push_back(at);
    while (at < exe.size() && exe[at]) ++at;
    while (at < exe.size() && !exe[at]) ++at;
  }
  // The table of pointers to them: same distances apart as the names (the program is loaded at
  // some offset from the file). Its element 0 is a null name; the formations' table precedes it.
  size_t table = 0;
  uint32_t shift = 0;
  for (size_t p = 0; p + 16 <= exe.size() && !table; ++p) {
    uint32_t s = u32(p) - uint32_t(names[0]);
    bool ok = true;
    for (int k = 1; k < 4 && ok; ++k) ok = u32(p + 4 * size_t(k)) - uint32_t(names[size_t(k)]) == s;
    if (ok) table = p, shift = s;
  }
  if (!table) {
    error = "HIND.EXE: no formation table";
    return false;
  }
  size_t count = 0;
  while (count < names.size() && u32(table + 4 * count) == uint32_t(names[count]) + shift) ++count;
  const size_t records = table - 4 - 4 * count;
  out.resize(count + 1);
  for (size_t k = 0; k < count; ++k) {
    Formation& f = out[k + 1];
    std::string name(reinterpret_cast<const char*>(&exe[names[k]]));
    f.name = trim(name.substr(0, name.find_first_of("\t{")));
    size_t rec = size_t(u32(records + 4 * k) - shift);
    uint32_t n = u32(rec);
    if (rec + 4 + 16 * size_t(n) > exe.size() || n > 64) continue;
    for (uint32_t i = 0; i < n; ++i) {
      size_t at = rec + 4 + 16 * size_t(i);
      FormationMember m;
      m.offset = glm::vec3(float(int32_t(u32(at))), float(int32_t(u32(at + 4))), float(int32_t(u32(at + 8)))) * kZoneUnit;
      m.object = int(u32(at + 12)) + 1;
      f.members.push_back(m);
    }
  }
  return true;
}

}  // namespace gamedata
