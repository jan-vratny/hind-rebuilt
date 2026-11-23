#pragma once
#include <string>

#include <SDL.h>

// The first start, when the original Hind's data are not found: a screen of our own (nothing of
// the original's to draw it with yet) that asks where they are. It offers what it finds (the
// game installed, its CD in a drive, an ISO image of the CD) and takes a path typed (Tab
// completes it) or chosen in the system's dialog. A CD or its image has everything: its data are
// copied out once (some 65 MB) into `copyTo`; the CD itself stays for the briefings' speech, the
// music and the animations. Without the data the game cannot be played: Esc quits.
namespace menu {

struct SetupResult {
  std::string originalDir;  // the installation, or where the CD's data were copied
  std::string cd;           // the CD or its image ("" when an installation was chosen)
};

// Runs until a source is chosen (true) or the player quits (false). `why`: shown above, e.g.
// that a configured directory has no game in it any more.
// `screenshot` (development): only draws the screen once into that PPM file, then false.
bool runSetup(SDL_Window* window, const std::string& copyTo, const std::string& why, SetupResult& out,
              const std::string& screenshot = "");

}  // namespace menu
