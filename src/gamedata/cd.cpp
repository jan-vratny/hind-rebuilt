#include "gamedata/cd.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "gamedata/original.hpp"

namespace fs = std::filesystem;

namespace gamedata {

namespace {

constexpr uint32_t kSector = 2048;

std::string upper(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAt(const std::string& path, uint64_t at, uint32_t n, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(std::streamoff(at));
  out.resize(n);
  in.read(reinterpret_cast<char*>(out.data()), std::streamsize(n));
  return in.gcount() == std::streamsize(n);
}

}  // namespace

bool Cd::open(const std::string& configured, const std::string& originalDir) {
  dir_.clear(), iso_.clear();
  std::error_code ec;
  auto tryPath = [&](const fs::path& p) {
    if (p.empty()) return false;
    if (fs::is_directory(p, ec)) {
      if (findOriginalFile(p.string(), "SPEECH").empty()) return false;
      dir_ = p.string();
      return true;
    }
    return fs::is_regular_file(p, ec) && openIso(p.string());
  };
  if (!configured.empty()) return tryPath(configured);
  if (originalDir.empty()) return false;
  if (tryPath(originalDir)) return true;
  // GOG's (and Steam's) DOSBox package: the CD's files in DATA beside the installed game.
  if (const std::string data = findOriginalFile(originalDir, "DATA"); !data.empty() && tryPath(data)) return true;
  // An image of the CD near the installation (as DOSBox users keep them).
  fs::path up = fs::absolute(originalDir, ec);
  for (int level = 0; level < 4 && !up.empty(); ++level, up = up.parent_path()) {
    for (const fs::path& d : {up, up / "CDs", up / "cds", up / "CD", up / "cd", up / "iso", up / "ISO"}) {
      if (!fs::is_directory(d, ec)) continue;
      for (const auto& e : fs::directory_iterator(d, ec)) {
        const std::string n = upper(e.path().filename().string());
        if (n.size() > 4 && n.substr(n.size() - 4) == ".ISO" && n.find("HIND") != std::string::npos && openIso(e.path().string())) return true;
      }
    }
    if (up == up.parent_path()) break;
  }
  return false;
}

bool Cd::openIso(const std::string& path) {
  // The primary volume descriptor at sector 16, its root directory's record at 156.
  std::vector<uint8_t> pvd;
  if (!readAt(path, 16 * kSector, kSector, pvd) || pvd[0] != 1 || std::string(pvd.begin() + 1, pvd.begin() + 6) != "CD001") return false;
  root_.lba = le32(&pvd[156 + 2]);
  root_.size = le32(&pvd[156 + 10]);
  root_.dir = true;
  iso_ = path;
  std::vector<Entry> top;
  const bool speech = list(root_.lba, root_.size, top) &&
                      std::any_of(top.begin(), top.end(), [](const Entry& e) { return e.dir && e.name == "SPEECH"; });
  if (!speech) iso_.clear();
  return speech;
}

bool Cd::list(uint32_t lba, uint32_t size, std::vector<Entry>& out) const {
  std::vector<uint8_t> d;
  if (size > 4u << 20 || !readAt(iso_, uint64_t(lba) * kSector, size, d)) return false;
  for (size_t at = 0; at < d.size();) {
    const uint8_t len = d[at];
    if (len == 0) {
      // Records do not cross sectors: on to the next one.
      at = (at / kSector + 1) * kSector;
      continue;
    }
    if (at + len > d.size() || len < 34) break;
    const uint8_t* r = &d[at];
    const uint8_t nameLen = r[32];
    std::string name(reinterpret_cast<const char*>(r + 33), std::min<size_t>(nameLen, len - 33));
    if (const size_t semi = name.find(';'); semi != std::string::npos) name.resize(semi);
    if (!name.empty() && name.back() == '.') name.pop_back();
    if (nameLen == 1 && (r[33] == 0 || r[33] == 1)) name.clear();  // "." and ".."
    if (!name.empty()) out.push_back({upper(name), le32(r + 2), le32(r + 10), (r[25] & 2) != 0});
    at += len;
  }
  return true;
}

bool Cd::read(const std::string& path, std::vector<uint8_t>& out) const {
  if (!dir_.empty()) {
    const std::string p = findOriginalFile(dir_, path);
    std::ifstream in(p, std::ios::binary);
    if (p.empty() || !in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
  }
  if (iso_.empty()) return false;
  Entry at = root_;
  size_t start = 0;
  const std::string want = upper(path);
  while (start <= want.size()) {
    const size_t slash = want.find('/', start);
    const std::string part = want.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    std::vector<Entry> entries;
    if (!at.dir || !list(at.lba, at.size, entries)) return false;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == part; });
    if (it == entries.end()) return false;
    at = *it;
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return !at.dir && readAt(iso_, uint64_t(at.lba) * kSector, at.size, out);
}

bool Cd::listDir(const std::string& path, std::vector<Item>& out) const {
  out.clear();
  if (!dir_.empty()) {
    const std::string d = path.empty() ? dir_ : findOriginalFile(dir_, path);
    std::error_code ec;
    if (d.empty() || !fs::is_directory(d, ec)) return false;
    for (const auto& e : fs::directory_iterator(d, ec)) out.push_back({upper(e.path().filename().string()), e.is_directory(ec)});
    return true;
  }
  if (iso_.empty()) return false;
  Entry at = root_;
  const std::string want = upper(path);
  for (size_t start = 0; start < want.size();) {
    const size_t slash = std::min(want.find('/', start), want.size());
    const std::string part = want.substr(start, slash - start);
    std::vector<Entry> entries;
    if (!list(at.lba, at.size, entries)) return false;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.dir && e.name == part; });
    if (it == entries.end()) return false;
    at = *it;
    start = slash + 1;
  }
  std::vector<Entry> entries;
  if (!list(at.lba, at.size, entries)) return false;
  for (const Entry& e : entries) out.push_back({e.name, e.dir});
  return true;
}

std::string speechName(const std::string& mission, SpeechPart part) {
  const std::string m = upper(mission);
  if (m.size() != 8) return "";
  // The trainings have only their briefings; the campaigns' missions too (their outcome is the
  // campaign's level text, see campaign).
  if (m.rfind("SARATT", 0) == 0 || m[5] == 'C') return part == SpeechPart::Briefing ? m : "";
  char letter = m[5];
  if (letter == 'S') letter = "SOPF"[int(part)];
  else if (letter == 'V') letter = "MVRG"[int(part)];  // Korea's second set
  else return part == SpeechPart::Briefing ? m : "";
  return m.substr(0, 5) + letter + m.substr(6);
}

}  // namespace gamedata
