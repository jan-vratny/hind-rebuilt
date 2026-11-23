#include "platform/filedialog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

namespace platform {

#ifdef _WIN32

bool canBrowse() { return true; }

std::string browse(bool folder, const std::string& title) {
  // The shell's file dialog (Vista on), picking a folder or an .iso.
  std::string chosen;
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  IFileOpenDialog* dlg = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg)))) {
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | DWORD(FOS_FORCEFILESYSTEM) | (folder ? DWORD(FOS_PICKFOLDERS) : DWORD(0)));
    std::wstring wtitle(title.begin(), title.end());
    dlg->SetTitle(wtitle.c_str());
    if (!folder) {
      const COMDLG_FILTERSPEC types[] = {{L"CD image (*.iso)", L"*.iso"}, {L"All files", L"*.*"}};
      dlg->SetFileTypes(2, types);
    }
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->Show(nullptr)) && SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        chosen.resize(size_t(std::max(n - 1, 0)));
        WideCharToMultiByte(CP_UTF8, 0, path, -1, chosen.data(), n, nullptr, nullptr);
        CoTaskMemFree(path);
      }
      item->Release();
    }
    dlg->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return chosen;
}

#else

namespace {

bool has(const char* program) {
  const std::string cmd = std::string("command -v ") + program + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

std::string quote(const std::string& s) {
  std::string q = "'";
  for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return q + "'";
}

}  // namespace

bool canBrowse() { return has("zenity") || has("kdialog") || has("qarma") || has("yad"); }

std::string browse(bool folder, const std::string& title) {
  std::string cmd;
  // zenity (GNOME, Xfce, Cinnamon, MATE), qarma (its Qt twin), kdialog (KDE), yad.
  const char* zenityLike = has("zenity") ? "zenity" : has("qarma") ? "qarma" : nullptr;
  if (zenityLike)
    cmd = std::string(zenityLike) + " --file-selection --title=" + quote(title) +
          (folder ? std::string(" --directory") : std::string(" --file-filter='CD image (*.iso) | *.iso *.ISO' --file-filter='All files | *'"));
  else if (has("kdialog"))
    cmd = std::string("kdialog --title ") + quote(title) +
          (folder ? std::string(" --getexistingdirectory ~") : std::string(" --getopenfilename ~ 'CD image (*.iso *.ISO)'"));
  else if (has("yad"))
    cmd = "yad --file --title=" + quote(title) + (folder ? std::string(" --directory") : std::string(" --file-filter='CD image | *.iso *.ISO'"));
  else
    return "";
  std::string out;
  if (FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r")) {
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

#endif

}  // namespace platform
