#pragma once
#include <string>
#include <vector>

#include "gamedata/cd.hpp"

// Where the original Hind's data come from, for the first start (menu/setup.hpp): the game
// installed (the directory with HIND.EXE and 3DDATA), its CD (mounted, or its files copied) or
// the CD's ISO image. The CD has everything the game needs (HIND.EXE in FILESD), so it is enough
// on its own: its data are copied out once, beside hind.ini.
namespace gamedata {

enum class SourceKind { None, Installed, Cd, Iso };

struct Source {
  SourceKind kind = SourceKind::None;
  std::string path;
};

// What is at `path` (a directory or a file, "~" for the home directory).
SourceKind classifySource(const std::string& path);

// The likely places: the usual installations, CDs in the drives (mounted media on Linux, the
// drive letters on Windows), ISO images with "hind" in their names in the home directory, its
// downloads, desktop and DOSBox directories.
std::vector<Source> findSources();

// The game's data copied from the CD (a directory or an .iso) into `dest`, a few files at a time
// so that the screen can show how far it got; into "<dest>.part" first, renamed when complete.
class Extractor {
public:
  bool start(const std::string& cdPath, const std::string& dest, std::string& error);
  // Copies files for about `seconds`; false once it is done or has failed (see error()).
  bool step(float seconds);
  float progress() const { return jobs_.empty() ? 0.0f : float(next_) / float(jobs_.size()); }
  bool done() const { return finished_ && error_.empty(); }
  const std::string& error() const { return error_; }

private:
  struct Job {
    std::string from, to;  // path on the CD, path under dest
  };
  Cd cd_;
  std::vector<Job> jobs_;
  size_t next_ = 0;
  std::string dest_, part_, error_;
  bool finished_ = false;
};

}  // namespace gamedata
