#include "sim/pilot_log.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sim {

namespace {

constexpr int kRankMissions[] = {0, 5, 15, 30, 50};

// Medals for campaigns and single missions won (manual p. 14). The original has three wars;
// with ours four, three of them count as all.
unsigned medalsFor(const Pilot& p) {
  const int c = int(p.campaignsWon.size()), s = p.singlesWon;
  unsigned m = 0;
  if (c >= 2 || (c >= 1 && s >= 10)) m |= kRedStar;
  if (c >= 3 || (c >= 2 && s >= 20)) m |= kLenin;
  if (c >= 3 && s >= 30) m |= kGoldStar;
  return m;
}

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
  return s;
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> v;
  std::stringstream in(s);
  for (std::string x; std::getline(in, x, ',');)
    if (!x.empty()) v.push_back(x);
  return v;
}

}  // namespace

const char* rankName(Rank r) {
  static const char* names[] = {"Sergeant", "Lieutenant", "Captain", "Major", "Colonel"};
  return names[int(r)];
}

const char* statusName(PilotStatus s) {
  static const char* names[] = {"Active", "Injured", "Missing In Action", "Prisoner Of War", "Killed In Action", "Killed In Training"};
  return names[int(s)];
}

Rank rankFor(int missions) {
  int r = 0;
  while (r + 1 < int(std::size(kRankMissions)) && missions >= kRankMissions[r + 1]) ++r;
  return Rank(r);
}

void PilotLog::reset() {
  pilots.clear();
  Pilot immortal;
  immortal.name = "Comrade Immortal";
  immortal.immortal = true;
  pilots.push_back(immortal);
  current = 0;
}

bool PilotLog::load(const std::string& path) {
  reset();
  std::ifstream in(path);
  if (!in) return true;  // no log yet
  // One pilot a line: key=value fields separated by '|'; "current=N" on its own line.
  std::vector<Pilot> read;
  int cur = 0;
  bool ok = true;
  for (std::string line; std::getline(in, line);) {
    if (line.rfind("current=", 0) == 0) {
      cur = std::atoi(line.c_str() + 8);
      continue;
    }
    if (line.rfind("pilot|", 0) != 0) continue;
    Pilot p;
    std::stringstream fields(line.substr(6));
    for (std::string f; std::getline(fields, f, '|');) {
      const size_t eq = f.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = f.substr(0, eq), v = f.substr(eq + 1);
      if (k == "name") p.name = v;
      else if (k == "immortal") p.immortal = v == "1";
      else if (k == "status") p.status = PilotStatus(std::clamp(std::atoi(v.c_str()), 0, int(PilotStatus::Kit)));
      else if (k == "until") p.until = std::atoll(v.c_str());
      else if (k == "total") p.totalSeconds = std::atof(v.c_str());
      else if (k == "combat") p.combatSeconds = std::atof(v.c_str());
      else if (k == "missions") p.missions = std::atoi(v.c_str());
      else if (k == "combatMissions") p.combatMissions = std::atoi(v.c_str());
      else if (k == "outstanding") p.outstanding = std::atoi(v.c_str());
      else if (k == "successful") p.successful = std::atoi(v.c_str());
      else if (k == "failed") p.failed = std::atoi(v.c_str());
      else if (k == "singles") p.singlesWon = std::atoi(v.c_str());
      else if (k == "won") p.campaignsWon = split(v);
      else if (k == "flown") p.campaignsFlown = split(v);
      else if (k == "failedMissions") p.failedMissions = split(v);
      else if (k == "medals") p.medals = unsigned(std::atoi(v.c_str()));
    }
    if (p.name.empty()) ok = false;
    else read.push_back(p);
  }
  if (!read.empty()) {
    pilots = read;
    if (pilots.size() > size_t(kMaxPilots)) pilots.resize(size_t(kMaxPilots));
    // Comrade Immortal is always there, first.
    if (!pilots[0].immortal) {
      reset();
      std::vector<Pilot> rest = read;
      for (Pilot& p : rest)
        if (!p.immortal && pilots.size() < size_t(kMaxPilots)) pilots.push_back(p);
    }
  }
  current = std::clamp(cur, 0, int(pilots.size()) - 1);
  return ok;
}

bool PilotLog::save(const std::string& path) const {
  std::ofstream out(path);
  if (!out) return false;
  out << "# HIND REBUILT pilot's log\n";
  out << "current=" << current << "\n";
  for (const Pilot& p : pilots) {
    std::string name = p.name;
    std::replace(name.begin(), name.end(), '|', '/');
    out << "pilot|name=" << name << "|immortal=" << (p.immortal ? 1 : 0) << "|status=" << int(p.status) << "|until=" << p.until
        << "|total=" << p.totalSeconds << "|combat=" << p.combatSeconds << "|missions=" << p.missions
        << "|combatMissions=" << p.combatMissions << "|outstanding=" << p.outstanding << "|successful=" << p.successful
        << "|failed=" << p.failed << "|singles=" << p.singlesWon << "|won=" << joined(p.campaignsWon)
        << "|flown=" << joined(p.campaignsFlown) << "|failedMissions=" << joined(p.failedMissions) << "|medals=" << p.medals << "\n";
  }
  return bool(out);
}

bool PilotLog::create(const std::string& name) {
  if (name.empty() || pilots.size() >= size_t(kMaxPilots)) return false;
  Pilot p;
  p.name = name;
  pilots.push_back(p);
  current = int(pilots.size()) - 1;
  return true;
}

bool PilotLog::remove(int index) {
  if (index <= 0 || index >= int(pilots.size()) || pilots[size_t(index)].immortal) return false;
  pilots.erase(pilots.begin() + index);
  if (current >= index) current = std::max(0, current - 1);
  return true;
}

void PilotLog::refresh(int64_t now) {
  for (Pilot& p : pilots)
    if (p.until > 0 && now >= p.until && (p.status == PilotStatus::Injured || p.status == PilotStatus::Missing || p.status == PilotStatus::Pow))
      p.status = PilotStatus::Active, p.until = 0;
}

bool PilotLog::canLog(int64_t now) const {
  const Pilot& p = pilot();
  return p.status == PilotStatus::Active || (p.until > 0 && now >= p.until);
}

LogOutcome PilotLog::record(const FlightRecord& f, int64_t now) {
  refresh(now);
  LogOutcome out;
  Pilot& p = pilot();
  out.status = p.status;
  if (!canLog(now)) return out;
  out.logged = true;
  const Rank before = rankFor(p.missions);
  const unsigned medalsBefore = p.medals;
  p.totalSeconds += f.airborneSeconds;
  if (f.combat) p.combatSeconds += f.airborneSeconds, ++p.combatMissions;
  ++p.missions;
  if (f.result == 2) ++p.outstanding;
  if (f.result >= 1) ++p.successful;
  else ++p.failed;
  // The last logged flight counts for the mission list's mark.
  std::erase(p.failedMissions, f.mission);
  if (f.result == 0 && !f.mission.empty()) p.failedMissions.push_back(f.mission);
  if (f.result >= 1 && !f.campaign && f.combat) ++p.singlesWon;
  if (f.campaign && std::find(p.campaignsFlown.begin(), p.campaignsFlown.end(), f.zone) == p.campaignsFlown.end())
    p.campaignsFlown.push_back(f.zone);
  if (f.campaignWon && std::find(p.campaignsWon.begin(), p.campaignsWon.end(), f.zone) == p.campaignsWon.end())
    p.campaignsWon.push_back(f.zone);
  p.medals |= medalsFor(p);
  out.newMedals = p.medals & ~medalsBefore;
  const Rank after = rankFor(p.missions);
  if (after != before) out.promotedTo = int(after);
  // The crash: Comrade Immortal walks away from anything.
  if (f.crashed && !p.immortal) {
    if (f.died) p.status = f.combat ? PilotStatus::Kia : PilotStatus::Kit, p.until = 0;
    else if (f.enemyTerritory) p.status = PilotStatus::Missing, p.until = now + int64_t(kMissingHours) * 3600;
    else p.status = PilotStatus::Injured, p.until = now + int64_t(kInjuredHours) * 3600;
  }
  out.status = p.status;
  return out;
}

}  // namespace sim
