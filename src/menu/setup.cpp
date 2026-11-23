#include "menu/setup.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "gamedata/install.hpp"
#include "platform/filedialog.hpp"
#include "render/font.hpp"
#include "render/gl.hpp"
#include "render/shaders.hpp"

namespace fs = std::filesystem;

namespace menu {

namespace {

// The HUD's 5 x 7 lettering with lower case and a few more signs, for paths that are told
// apart by their case; two rows more below the line for the descenders.
constexpr int kRows = render::kGlyphH + 2;
using Rows = std::array<std::uint8_t, kRows>;
struct Extra {
  char ch;
  const char* rows[kRows];
};
const Extra kExtra[] = {
    {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####", ".....", "....."}},
    {'b', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "####.", ".....", "....."}},
    {'c', {".....", ".....", ".###.", "#....", "#....", "#...#", ".###.", ".....", "....."}},
    {'d', {"....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####", ".....", "....."}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###.", ".....", "....."}},
    {'f', {"..##.", ".#..#", ".#...", "###..", ".#...", ".#...", ".#...", ".....", "....."}},
    {'g', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
    {'h', {"#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#", ".....", "....."}},
    {'i', {"..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###.", ".....", "....."}},
    {'j', {"...#.", ".....", "..##.", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'k', {"#....", "#....", "#..#.", "#.#..", "##...", "#.#..", "#..#.", ".....", "....."}},
    {'l', {".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###.", ".....", "....."}},
    {'m', {".....", ".....", "##.#.", "#.#.#", "#.#.#", "#...#", "#...#", ".....", "....."}},
    {'n', {".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#", ".....", "....."}},
    {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###.", ".....", "....."}},
    {'p', {".....", ".....", "####.", "#...#", "#...#", "#...#", "####.", "#....", "#...."}},
    {'q', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", "....#"}},
    {'r', {".....", ".....", "#.##.", "##..#", "#....", "#....", "#....", ".....", "....."}},
    {'s', {".....", ".....", ".###.", "#....", ".###.", "....#", "####.", ".....", "....."}},
    {'t', {".#...", ".#...", "###..", ".#...", ".#...", ".#..#", "..##.", ".....", "....."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#", ".....", "....."}},
    {'v', {".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#..", ".....", "....."}},
    {'w', {".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#.", ".....", "....."}},
    {'x', {".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", ".....", "....."}},
    {'y', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
    {'z', {".....", ".....", "#####", "...#.", "..#..", ".#...", "#####", ".....", "....."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####", ".....", "....."}},
    {'~', {".....", ".....", ".#...", "#.#.#", "...#.", ".....", ".....", ".....", "....."}},
    {'\\', {".....", "#....", ".#...", "..#..", "...#.", "....#", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#..", ".....", "....."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#..", ".....", "....."}},
    {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'"', {".#.#.", ".#.#.", ".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", ".....", ".....", "....."}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###.", ".....", "....."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###.", ".....", "....."}},
    {'*', {".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", ".....", ".....", "....."}},
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#...", ".....", "....."}},
    {',', {".....", ".....", ".....", ".....", ".....", ".##..", ".##..", "..#..", ".#..."}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#", ".....", "....."}},
};

Rows glyph(char ch) {
  Rows g{};
  for (const Extra& e : kExtra)
    if (e.ch == ch) {
      for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < render::kGlyphW; ++c)
          if (e.rows[r][c] == '#') g[size_t(r)] |= std::uint8_t(1u << (render::kGlyphW - 1 - c));
      return g;
    }
  const render::GlyphRows& hud = render::glyphRows(ch);
  std::copy(hud.begin(), hud.end(), g.begin());
  return g;
}

// UTF-8 to what can be drawn: one sign a character, '?' for what the lettering has not.
std::string drawable(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) out += char(c);
    else if ((c & 0xC0) != 0x80) out += '?';
  }
  return out;
}

const glm::vec4 kNight{0.035f, 0.06f, 0.10f, 1.0f}, kCream{0.91f, 0.87f, 0.75f, 1.0f}, kAmber{0.95f, 0.66f, 0.2f, 1.0f},
    kDim{0.55f, 0.6f, 0.66f, 1.0f}, kRed{1.0f, 0.45f, 0.38f, 1.0f}, kGreen{0.55f, 0.92f, 0.55f, 1.0f},
    kField{0.08f, 0.12f, 0.18f, 1.0f};

// Flat rectangles and lettering in the window's pixels, y down.
class Painter {
public:
  ~Painter() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
  }
  bool init() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
    return prog_.build("setup", render::shaders::kHudVS, render::shaders::kHudFS);
  }
  void begin(int w, int h) { w_ = w, h_ = h, verts_.clear(); }
  void rect(float x0, float y0, float x1, float y1, glm::vec4 c) {
    const glm::vec2 a{x0, float(h_) - y0}, b{x1, float(h_) - y0}, d{x1, float(h_) - y1}, e{x0, float(h_) - y1};
    for (glm::vec2 p : {a, b, d, a, d, e}) verts_.push_back({p, c});
  }
  // Lettering of `px` pixels a dot, from its top left corner; the width it took.
  float text(const std::string& utf8, float x, float y, float px, glm::vec4 c) {
    const std::string s = drawable(utf8);
    for (size_t i = 0; i < s.size(); ++i) {
      const Rows g = glyph(s[i]);
      for (int r = 0; r < kRows; ++r)
        for (int col = 0; col < render::kGlyphW; ++col)
          if (g[size_t(r)] & (1u << (render::kGlyphW - 1 - col))) {
            const float gx = x + (float(i) * (render::kGlyphW + 1) + float(col)) * px, gy = y + float(r) * px;
            rect(gx, gy, gx + px, gy + px, c);
          }
    }
    return float(s.size()) * (render::kGlyphW + 1) * px;
  }
  static float width(const std::string& utf8, float px) { return float(drawable(utf8).size()) * (render::kGlyphW + 1) * px; }
  void end() {
    glViewport(0, 0, w_, h_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(kNight.r, kNight.g, kNight.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    prog_.use();
    glUniform2f(prog_.loc("uScreen"), float(w_), float(h_));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(V)), verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
  }

private:
  struct V {
    glm::vec2 p;
    glm::vec4 c;
  };
  render::Program prog_;
  GLuint vao_ = 0, vbo_ = 0;
  std::vector<V> verts_;
  int w_ = 0, h_ = 0;
};

// The paragraph cut into lines of at most `n` signs.
std::vector<std::string> wrap(const std::string& text, size_t n) {
  std::vector<std::string> lines;
  std::string line;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(' ', start);
    if (end == std::string::npos) end = text.size();
    const std::string word = text.substr(start, end - start);
    if (!line.empty() && drawable(line).size() + 1 + drawable(word).size() > n) lines.push_back(line), line.clear();
    line += (line.empty() ? "" : " ") + word;
    start = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// A long path cut from its start to fit `n` signs.
std::string fit(const std::string& s, size_t n) {
  const std::string d = drawable(s);
  return d.size() <= n || n < 4 ? d : "..." + d.substr(d.size() - (n - 3));
}

// Tab: the path completed as far as the directory's entries agree; `matches` those that fit.
std::string complete(const std::string& typed, std::vector<std::string>& matches) {
  matches.clear();
  std::string path = typed;
  if (!path.empty() && path[0] == '~')
    if (const char* h = std::getenv("HOME")) path = h + path.substr(1);
  const size_t slash = path.find_last_of("/\\");
  const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash + 1);
  const std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
  std::error_code ec;
  std::vector<std::pair<std::string, bool>> found;
  for (const auto& e : fs::directory_iterator(dir.empty() ? "/" : dir, ec)) {
    const std::string n = e.path().filename().string();
    if (n.compare(0, stem.size(), stem) == 0 && (stem.size() || n[0] != '.')) found.push_back({n, e.is_directory(ec)});
  }
  if (found.empty()) return typed;
  std::string common = found[0].first;
  for (const auto& f : found) {
    size_t k = 0;
    while (k < common.size() && k < f.first.size() && common[k] == f.first[k]) ++k;
    common.resize(k);
    matches.push_back(f.first + (f.second ? "/" : ""));
  }
  std::sort(matches.begin(), matches.end());
  std::string out = (slash == std::string::npos ? "" : typed.substr(0, typed.find_last_of("/\\") + 1)) + common;
  if (found.size() == 1 && found[0].second) out += '/';
  if (found.size() == 1) matches.clear();
  return out;
}

const char* kindName(gamedata::SourceKind k) {
  switch (k) {
    case gamedata::SourceKind::Installed: return "INSTALLED";
    case gamedata::SourceKind::Cd: return "CD";
    case gamedata::SourceKind::Iso: return "CD IMAGE";
    default: return "";
  }
}

}  // namespace

bool runSetup(SDL_Window* window, const std::string& copyTo, const std::string& why, SetupResult& out,
              const std::string& screenshot) {
  Painter painter;
  if (!painter.init()) return false;
  std::vector<gamedata::Source> sources = gamedata::findSources();
  // The rows: the sources found, then the path typed (the last row).
  int selected = 0;
  std::string typed, status;
  glm::vec4 statusColour = kDim;
  std::vector<std::string> matches;
  // Tab again and again goes round the matches as bash's menu-complete (Shift+Tab back): the
  // directory they are in and the one filled in (-1 none yet).
  std::string cycleDir;
  int cycle = -1;
  gamedata::Extractor extractor;
  bool copying = false;
  std::string copyingFrom;
  const bool browsing = platform::canBrowse();
  // Where the rows lie on the screen (for the mouse), from the last frame.
  std::vector<std::pair<float, float>> rowsY;
  float px = 2.0f;

  auto use = [&](const std::string& path) {
    matches.clear();
    if (path.empty()) {
      status = "Type the path of the game, its CD or the CD's image first.", statusColour = kRed;
      return false;
    }
    switch (gamedata::classifySource(path)) {
      case gamedata::SourceKind::Installed:
        out = {path, ""};
        return true;
      case gamedata::SourceKind::Cd:
      case gamedata::SourceKind::Iso: {
        std::string error;
        if (!extractor.start(path, copyTo, error)) {
          status = error, statusColour = kRed;
          return false;
        }
        copying = true, copyingFrom = path;
        status = "Copying the game's data from the CD...", statusColour = kCream;
        return false;
      }
      default:
        status = "No Hind there: " + path, statusColour = kRed;
        return false;
    }
  };
  auto browse = [&](bool folder) {
    const std::string chosen = platform::browse(folder, folder ? "The Hind game or its CD" : "The Hind CD's image");
    if (chosen.empty()) return false;
    typed = chosen;
    selected = int(sources.size());
    return use(chosen);
  };

  SDL_StartTextInput();
  bool done = false, quit = false;
  Uint32 lastClick = 0;
  int lastClickRow = -1;
  while (!done && !quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) quit = true;
      if (copying) continue;  // nothing to do but wait (or close the window)
      const int rows = int(sources.size()) + 1;
      if (e.type == SDL_KEYDOWN) {
        const SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_ESCAPE) quit = true;
        else if (k == SDLK_UP) selected = std::max(0, selected - 1);
        else if (k == SDLK_DOWN) selected = std::min(rows - 1, selected + 1);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
          done = use(selected < int(sources.size()) ? sources[size_t(selected)].path : typed);
        else if (k == SDLK_TAB) {
          selected = rows - 1;
          status.clear();
          const int n = int(matches.size());
          const bool going = n > 1 && (cycle < 0 ? typed.rfind(cycleDir, 0) == 0 : typed == cycleDir + matches[size_t(cycle)]);
          if (going) {
            const bool back = (e.key.keysym.mod & KMOD_SHIFT) != 0;
            cycle = cycle < 0 ? (back ? n - 1 : 0) : (cycle + (back ? n - 1 : 1)) % n;
            typed = cycleDir + matches[size_t(cycle)];
          } else {
            typed = complete(typed, matches);
            cycleDir = typed.substr(0, typed.find_last_of("/\\") + 1);
            cycle = -1;
          }
        } else if (k == SDLK_BACKSPACE && !typed.empty()) {
          selected = rows - 1;
          matches.clear(), cycle = -1;
          do typed.pop_back();
          while (!typed.empty() && (static_cast<unsigned char>(typed.back()) & 0xC0) == 0x80);
        } else if ((k == SDLK_F2 || k == SDLK_F3) && browsing)
          done = browse(k == SDLK_F2);
        else if (k == SDLK_v && (e.key.keysym.mod & KMOD_CTRL) && SDL_HasClipboardText()) {
          char* clip = SDL_GetClipboardText();
          typed += clip;
          SDL_free(clip);
          matches.clear(), cycle = -1;
          selected = rows - 1;
        }
      } else if (e.type == SDL_TEXTINPUT) {
        typed += e.text.text;
        selected = rows - 1;
        matches.clear(), cycle = -1;
      } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int ww, wh, dw, dh;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        const float y = float(e.button.y) * float(dh) / float(std::max(wh, 1));
        for (size_t r = 0; r < rowsY.size(); ++r)
          if (y >= rowsY[r].first && y < rowsY[r].second) {
            const bool again = int(r) == lastClickRow && e.button.timestamp - lastClick < 400;
            selected = int(r);
            if (again) done = use(selected < int(sources.size()) ? sources[size_t(selected)].path : typed);
            lastClick = e.button.timestamp, lastClickRow = int(r);
          }
      }
    }
    if (copying) {
      if (!extractor.step(0.03f)) {
        copying = false;
        if (extractor.done()) {
          out = {copyTo, copyingFrom};
          done = true;
        } else {
          status = extractor.error(), statusColour = kRed;
        }
      }
    }

    // The screen.
    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    // Some 90 signs across, room for the paragraphs, the rows and the keys down the screen.
    px = std::max(1.0f, std::floor(std::min(float(w) / 560.0f, float(h) / 340.0f)));
    const float cw = (render::kGlyphW + 1) * px, line = 11.0f * px, margin = std::max(16.0f, 4.0f * cw);
    const size_t columns = size_t(std::max(20.0f, (float(w) - 2.0f * margin) / cw));
    painter.begin(w, h);
    float y = margin;
    painter.text("HIND REBUILT", margin, y, px * 3.0f, kCream);
    y += 7.0f * px * 3.0f + line;
    if (!why.empty()) {
      for (const std::string& l : wrap(why, columns)) painter.text(l, margin, y, px, kAmber), y += line;
      y += line * 0.5f;
    }
    const std::string intro =
        "The game is played with the data of the original Hind (Digital Integration, 1996), which are not part of it, "
        "and without them it cannot run. Show it where they are: the game's CD, an image of the CD (.iso), or the game "
        "installed (the folder with HIND.EXE, e.g. in DOSBox). From a CD the data are copied out once (some 65 MB); the "
        "CD is then needed only for the briefings' speech, the music and the animations.";
    for (const std::string& l : wrap(intro, columns)) painter.text(l, margin, y, px, kDim), y += line;
    y += line;
    rowsY.clear();
    painter.text(sources.empty() ? "Nothing found in the usual places." : "Found:", margin, y, px, kCream);
    y += line * 1.3f;
    const int rows = int(sources.size()) + 1;
    for (int r = 0; r < rows; ++r) {
      const bool sel = r == selected;
      const float top = y - 2.0f * px, bottom = y + 10.0f * px;
      rowsY.push_back({top, bottom + 2.0f * px});
      if (r < int(sources.size())) {
        if (sel) painter.rect(margin - cw, top, float(w) - margin + cw, bottom, kField);
        const gamedata::Source& s = sources[size_t(r)];
        painter.text(sel ? ">" : " ", margin - cw, y, px, kAmber);
        painter.text(kindName(s.kind), margin, y, px, sel ? kAmber : kDim);
        painter.text(fit(s.path, columns - 11), margin + 11.0f * cw, y, px, sel ? kCream : kDim);
      } else {
        y += line * 0.5f;
        const float t = y - 2.0f * px, b = y + 10.0f * px;
        rowsY.back() = {t, b + 2.0f * px};
        painter.text(sel ? ">" : " ", margin - cw, y, px, kAmber);
        painter.text("Path:", margin, y, px, sel ? kAmber : kDim);
        painter.rect(margin + 6.0f * cw, t, float(w) - margin + cw, b, kField);
        const std::string shown = fit(typed, columns - 8);
        const float tw = painter.text(shown, margin + 6.5f * cw, y, px, kCream);
        if (sel && (SDL_GetTicks() / 500) % 2 == 0) painter.rect(margin + 6.5f * cw + tw, y, margin + 6.5f * cw + tw + px, y + 7.0f * px, kAmber);
      }
      y += line * 1.2f;
    }
    if (!matches.empty()) {
      // The matches across the lines, the one Tab has filled in lit.
      float x = margin;
      for (size_t i = 0; i < matches.size(); ++i) {
        const float mw = Painter::width(matches[i], px);
        if (x > margin && x + mw > float(w) - margin) x = margin, y += line;
        painter.text(matches[i], x, y, px, int(i) == cycle ? kAmber : kDim);
        x += mw + 2.0f * cw;
      }
      y += line;
    }
    y += line * 0.5f;
    if (copying) {
      const float x1 = float(w) - margin;
      painter.rect(margin, y, x1, y + 8.0f * px, kField);
      painter.rect(margin, y, margin + (x1 - margin) * extractor.progress(), y + 8.0f * px, kAmber);
      y += line * 1.4f;
    }
    if (!status.empty())
      for (const std::string& l : wrap(status, columns)) painter.text(l, margin, y, px, statusColour), y += line;
    const std::string keys = std::string("Up/Down choose   Enter use   Tab completes the path   ") +
                             (browsing ? "F2 browse folders   F3 browse .iso   " : "") + "Esc quit";
    // At the foot of the screen, or below the rest when it reaches that far.
    const std::vector<std::string> keyLines = wrap(keys, columns);
    float ky = std::max(y + line, float(h) - margin * 0.5f - float(keyLines.size()) * line);
    for (const std::string& l : keyLines) painter.text(l, margin, ky, px, kDim), ky += line;
    painter.end();
    if (!screenshot.empty()) {
      std::vector<unsigned char> rgb(size_t(w) * size_t(h) * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
      if (FILE* f = std::fopen(screenshot.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int row = h - 1; row >= 0; --row) std::fwrite(&rgb[size_t(row) * size_t(w) * 3], 1, size_t(w) * 3, f);
        std::fclose(f);
      }
      break;
    }
    SDL_GL_SwapWindow(window);
    SDL_Delay(copying ? 0 : 10);
  }
  SDL_StopTextInput();
  return done && !quit;
}

}  // namespace menu
