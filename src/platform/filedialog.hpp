#pragma once
#include <string>

// The system's own dialog for choosing a folder or a file: Windows' (the shell's), on Linux
// zenity's, qarma's, kdialog's or yad's, whichever is installed. It blocks until closed.
namespace platform {

// Whether there is a dialog to show.
bool canBrowse();
// The folder chosen (`folder`) or the file (offering .iso images), "" when cancelled.
std::string browse(bool folder, const std::string& title);

}  // namespace platform
