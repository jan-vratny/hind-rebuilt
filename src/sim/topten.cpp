#include "sim/topten.hpp"

#include <cstdio>
#include <algorithm>
#include <fstream>

namespace sim {

bool TopTen::load(const std::string& path) {
  entries.clear();
  std::ifstream in(path);
  if (!in) return true;  // none yet
  // A line each: "score|name", with "|x" after it when the flight ended in a crash.
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line[0] == '#') continue;
    const size_t bar = line.find('|');
    if (bar == std::string::npos) continue;
    std::string name = line.substr(bar + 1);
    const bool crashed = name.size() >= 2 && name.compare(name.size() - 2, 2, "|x") == 0;
    if (crashed) name.resize(name.size() - 2);
    entries.push_back({name, std::atoi(line.c_str()), crashed});
  }
  std::stable_sort(entries.begin(), entries.end(), [](const TopTenEntry& a, const TopTenEntry& b) { return a.score > b.score; });
  if (entries.size() > size_t(kSize)) entries.resize(kSize);
  return true;
}

bool TopTen::save(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return false;
  out << "# HIND REBUILT Top Ten (Quickstart Arcade): score|name, |x: ended in a crash\n";
  for (const TopTenEntry& e : entries) out << e.score << "|" << e.name << (e.crashed ? "|x" : "") << "\n";
  return bool(out);
}

int TopTen::place(int score) const {
  if (score <= 0) return -1;
  for (size_t i = 0; i < entries.size(); ++i)
    if (score > entries[i].score) return int(i);
  return entries.size() < size_t(kSize) ? int(entries.size()) : -1;
}

int TopTen::insert(const std::string& name, int score, bool crashed) {
  const int at = place(score);
  if (at < 0) return -1;
  entries.insert(entries.begin() + at, {name, score, crashed});
  if (entries.size() > size_t(kSize)) entries.resize(kSize);
  return at;
}

int arcadePoints(const Vehicle& v) {
  const VehicleType& t = vehicleType(v.kind);
  if (v.scenery) return v.side == Side::Enemy ? 25 : -25;
  int p = 0;
  switch (t.cls) {
    case UnitClass::Soldier: p = 10; break;
    case UnitClass::Structure: p = 50; break;
    case UnitClass::Helicopter: p = 300; break;
    case UnitClass::Aircraft: p = 400; break;
    case UnitClass::Drone: p = isDecoy(v.kind) ? 0 : isCruiseMissile(v.kind) ? 500 : 150; break;
    case UnitClass::Vehicle: p = t.armour == Armour::Heavy ? 150 : t.armour == Armour::Light ? 75 : 40; break;
  }
  // What shoots at helicopters is worth more.
  if (t.arms == Arms::Aaa || t.arms == Arms::Manpads) p += 75;
  if (t.arms == Arms::Sam) p += 125;
  return v.side == Side::Enemy ? p : -2 * p;
}

MissionResult arcadeResult(bool crashed, int score) {
  if (crashed) return MissionResult::Failed;
  return score >= kArcadeOutstandingScore ? MissionResult::Outstanding : MissionResult::Success;
}

std::vector<std::string> arcadeDebrief(bool crashed, const std::string& lost, bool scored, int score, int bonus, double airborne,
                                       const std::map<std::string, ArcadeKill>& kills) {
  std::vector<std::string> out;
  if (scored) {
    out.push_back("Score: " + std::to_string(score) + (bonus > 0 ? " (" + std::to_string(bonus) + " of it for coming through)" : ""));
  }
  const int seconds = int(airborne);
  char time[32];
  std::snprintf(time, sizeof time, "Time in the air: %d:%02d", seconds / 60, seconds % 60);
  out.push_back(time);
  if (crashed) out.push_back(lost.empty() ? "Hind destroyed." : lost);
  else out.push_back("Still flying when it ended.");
  out.push_back("");
  std::vector<std::pair<std::string, ArcadeKill>> list(kills.begin(), kills.end());
  std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second.points > b.second.points; });
  int total = 0;
  for (const auto& [name, k] : list) total += k.count;
  out.push_back("Destroyed by you: " + std::to_string(total));
  for (const auto& [name, k] : list)
    out.push_back("  " + std::to_string(k.count) + " x " + name + (scored ? "  (" + std::to_string(k.points) + ")" : ""));
  return out;
}

}  // namespace sim
