#!/usr/bin/env bash
# Sestavení hry pro 64bitová Windows křížovým překladem z Linuxu (MinGW-w64).
# Potřebné balíčky (jednou, ručně):
#   sudo apt install mingw-w64 cmake libglm-dev zip
# SDL2 pro Windows si skript stáhne do third_party/ (mimo git), GLM vezme ze systému.
# Výsledek: build-win/hind.exe, a balíček dist/hind-win.zip (hind.exe, SDL2.dll, data/missions, data/models …)
# k poslání na počítač s Windows. Testy se nespouští (běží jen ve Windows).
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

SDL_VERSION=2.32.10
SDL_DIR=third_party/SDL2-$SDL_VERSION
GLM_DIR=third_party/glm-include

if ! command -v x86_64-w64-mingw32-g++-posix >/dev/null; then
  echo "Chybí MinGW-w64: sudo apt install mingw-w64" >&2
  exit 1
fi

# SDL2 pro Windows (oficiální vývojářský balík pro MinGW)
if [[ ! -d $SDL_DIR ]]; then
  echo "==> Stahuji SDL2 $SDL_VERSION pro Windows"
  mkdir -p third_party/dl
  curl -fL -o third_party/dl/SDL2-devel-$SDL_VERSION-mingw.tar.gz \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
  tar -xzf third_party/dl/SDL2-devel-$SDL_VERSION-mingw.tar.gz -C third_party
fi

# GLM (jen hlavičky): kopie systémové, aby se do překladu pro Windows nepletly linuxové hlavičky
if [[ ! -f $GLM_DIR/glm/glm.hpp ]]; then
  if [[ ! -f /usr/include/glm/glm.hpp ]]; then
    echo "Chybí GLM: sudo apt install libglm-dev" >&2
    exit 1
  fi
  mkdir -p $GLM_DIR
  cp -r /usr/include/glm $GLM_DIR/
fi

if [[ ! -f build-win/CMakeCache.txt ]]; then
  cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
    -DSDL2_DIR="$PWD/$SDL_DIR/x86_64-w64-mingw32/lib/cmake/SDL2" \
    -DGLM_INCLUDE_DIR="$PWD/$GLM_DIR"
fi
cmake --build build-win -j "$(nproc)" --target hind

# Balíček pro Windows
rm -rf dist/hind-win
mkdir -p dist/hind-win/data
cp build-win/hind.exe build-win/SDL2.dll dist/hind-win/
cp LICENSE CREDITS.md CREDITS.en.md dist/hind-win/
cp -r data/missions data/sounds data/cockpit data/models dist/hind-win/data/
if [ -d data/speech ]; then cp -r data/speech dist/hind-win/data/; fi
if command -v zip >/dev/null; then
  (cd dist && rm -f hind-win.zip && zip -qr hind-win.zip hind-win)
  echo "Hotovo: dist/hind-win.zip"
else
  echo "Hotovo: dist/hind-win/ (zip není nainstalovaný, balíček nezabalen)"
fi
