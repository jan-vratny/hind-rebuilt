#include "gamedata/install.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gamedata/original.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace gamedata {

namespace {

// What the game reads from the original, as the installer lays it out (the CD has it in the
// same directories, HIND.EXE in FILESD).
const char* const kDataDirs[] = {"3DDATA", "COCKPITS", "MENUSCRN", "MISSDATA", "FONTS", "SOUND", "UIDATA",
                                 "AFGHAN.WZ", "KAZAK.WZ", "KOREA.WZ", "SARATOV.WZ"};

std::string home() {
  const char* h = std::getenv("HOME");
#ifdef _WIN32
  if (!h || !*h) h = std::getenv("USERPROFILE");
#endif
  return h ? h : "";
}

std::string expand(const std::string& path) {
  if (!path.empty() && path[0] == '~') return home() + path.substr(1);
  return path;
}

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A CD (a directory or an image) with the game's data on it.
bool isGameCd(const Cd& cd) {
  std::vector<Cd::Item> items;
  if (!cd.listDir("FILESD", items) ||
      std::none_of(items.begin(), items.end(), [](const Cd::Item& i) { return !i.dir && i.name == "HIND.EXE"; }))
    return false;
  return cd.listDir("3DDATA", items) && !items.empty();
}

}  // namespace

SourceKind classifySource(const std::string& given) {
  const std::string path = expand(given);
  std::error_code ec;
  if (path.empty() || !fs::exists(path, ec)) return SourceKind::None;
  if (fs::is_directory(path, ec) && !findOriginalFile(path, "3DDATA").empty() && !findOriginalFile(path, "HIND.EXE").empty())
    return SourceKind::Installed;
  Cd cd;
  if (!cd.open(path, "") || !isGameCd(cd)) return SourceKind::None;
  return fs::is_directory(path, ec) ? SourceKind::Cd : SourceKind::Iso;
}

std::vector<Source> findSources() {
  std::vector<std::string> paths;
  const std::string h = home();
  // The installations where findOriginalDir looks.
  if (const std::string d = findOriginalDir(""); !d.empty()) paths.push_back(d);
  // Where the stores put it (GOG, Steam, the Heroic and Lutris launchers on Linux): the folder
  // itself, or one a level down.
  {
    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const char* r : {"C:/GOG Games", "C:/Program Files (x86)/GOG Galaxy/Games", "C:/Program Files/GOG Galaxy/Games",
                          "C:/Program Files (x86)/Steam/steamapps/common", "C:/Program Files/Steam/steamapps/common"})
      roots.push_back(r);
#endif
    if (!h.empty())
      for (const char* r : {"GOG Games", "Games", "Games/Heroic", "Games/gog", ".steam/steam/steamapps/common",
                            ".local/share/Steam/steamapps/common", ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common"})
        roots.push_back(fs::path(h) / r);
    std::error_code ec;
    for (const fs::path& r : roots) {
      if (!fs::is_directory(r, ec)) continue;
      for (const auto& e : fs::directory_iterator(r, ec)) {
        if (!e.is_directory(ec) || lower(e.path().filename().string()).find("hind") == std::string::npos) continue;
        paths.push_back(e.path().string());
        for (const auto& sub : fs::directory_iterator(e.path(), ec))
          if (sub.is_directory(ec)) paths.push_back(sub.path().string());
      }
    }
  }
  // CDs in the drives.
#ifdef _WIN32
  // No "insert a disk" box for an empty drive.
  const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
  const DWORD drives = GetLogicalDrives();
  for (int i = 2; i < 26; ++i) {
    if (!(drives & (1u << i))) continue;
    const std::string root = std::string(1, char('A' + i)) + ":\\";
    if (GetDriveTypeA(root.c_str()) == DRIVE_CDROM) paths.push_back(root);
  }
  SetErrorMode(oldMode);
#else
  std::vector<fs::path> mounts = {"/cdrom", "/media/cdrom"};
  if (const char* user = std::getenv("USER"))
    for (const char* base : {"/media/", "/run/media/"}) mounts.push_back(fs::path(base) / user);
  mounts.push_back("/media");
  mounts.push_back("/mnt");
  std::error_code ec;
  for (const fs::path& m : mounts) {
    if (!fs::is_directory(m, ec)) continue;
    paths.push_back(m.string());
    for (const auto& e : fs::directory_iterator(m, ec))
      if (e.is_directory(ec)) paths.push_back(e.path().string());
  }
#endif
  // ISO images with "hind" in the name, where downloads and DOSBox's CDs lie.
  if (!h.empty()) {
    std::error_code ec;
    std::vector<const char*> where = {"", "Downloads", "Desktop", "Documents", "dosbox", "dosbox/CDs", "dosbox/cds",
                                      "dosbox/cd", "ISO", "iso", "Games", "games"};
#ifndef _WIN32
    // Their Czech names on a Czech desktop (Windows keeps the English ones underneath).
    where.insert(where.end(), {"Stažené", "Plocha", "Dokumenty"});
#endif
    for (const char* d : where) {
      const fs::path dir = fs::path(h) / d;
      if (!fs::is_directory(dir, ec)) continue;
      for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = lower(e.path().filename().string());
        if (e.is_regular_file(ec) && n.size() > 4 && n.substr(n.size() - 4) == ".iso" && n.find("hind") != std::string::npos)
          paths.push_back(e.path().string());
      }
    }
  }
  std::vector<Source> out;
  for (const std::string& p : paths) {
    const SourceKind k = classifySource(p);
    if (k == SourceKind::None) continue;
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(p, ec);
    if (std::none_of(out.begin(), out.end(), [&](const Source& s) { return fs::weakly_canonical(s.path, ec) == canon; }))
      out.push_back({k, p});
  }
  return out;
}

bool Extractor::start(const std::string& cdPath, const std::string& dest, std::string& error) {
  *this = Extractor();
  if (!cd_.open(expand(cdPath), "") || !isGameCd(cd_)) {
    error = "no Hind CD at " + cdPath;
    return false;
  }
  dest_ = dest;
  part_ = dest + ".part";
  std::error_code ec;
  fs::remove_all(part_, ec);
  // Every file under the data directories, walked on the CD.
  std::vector<std::string> dirs(std::begin(kDataDirs), std::end(kDataDirs));
  for (size_t i = 0; i < dirs.size(); ++i) {
    std::vector<Cd::Item> items;
    if (!cd_.listDir(dirs[i], items)) {
      error = dirs[i] + " is not on the CD";
      return false;
    }
    for (const Cd::Item& it : items) {
      const std::string p = dirs[i] + "/" + it.name;
      if (it.dir) dirs.push_back(p);
      else jobs_.push_back({p, p});
    }
  }
  jobs_.push_back({"FILESD/HIND.EXE", "HIND.EXE"});
  return true;
}

bool Extractor::step(float seconds) {
  if (finished_) return false;
  const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(long(seconds * 1e6f));
  std::error_code ec;
  // At least one file a step, however short.
  for (bool first = true; next_ < jobs_.size() && (first || std::chrono::steady_clock::now() < until); first = false) {
    const Job& j = jobs_[next_];
    std::vector<uint8_t> data;
    const fs::path to = fs::path(part_) / j.to;
    fs::create_directories(to.parent_path(), ec);
    std::ofstream out(to, std::ios::binary);
    if (!cd_.read(j.from, data) || !out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()))) {
      error_ = "could not copy " + j.from + " to " + to.string();
      finished_ = true;
      return false;
    }
    ++next_;
  }
  if (next_ < jobs_.size()) return true;
  fs::remove_all(dest_, ec);
  fs::rename(part_, dest_, ec);
  if (ec) error_ = "could not rename " + part_ + " to " + dest_ + ": " + ec.message();
  finished_ = true;
  return false;
}

}  // namespace gamedata
