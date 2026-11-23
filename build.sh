#!/usr/bin/env bash
# Kompletní sestavení hry: nakonfiguruje, přeloží a spustí testy letového modelu.
# Spouští se bez parametrů. Potřebné balíčky:
#   sudo apt install g++ cmake libsdl2-dev libglm-dev
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

# Starý build z jiného cmake (např. jiné cesty) by konfiguraci rozbil -> smazat
cache=build/CMakeCache.txt
if [[ -f $cache ]] && ! grep -qx "CMAKE_COMMAND:INTERNAL=$(command -v cmake)" "$cache"; then
  echo "==> Mažu starý adresář build/ (byl vytvořen jiným cmake)"
  rm -rf build
fi

# Konfigurace jen poprvé; při změně CMakeLists.txt ji build zopakuje sám
if [[ ! -f $cache ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure

# Náhledy README a CREDITS, jak je ukáže GitHub (README.html …, mimo git), když je cmark-gfm
if command -v cmark-gfm >/dev/null; then
  python3 tools/readme_html.py >/dev/null
fi

echo
echo "Hotovo. Spuštění: ./build/hind   (celá obrazovka: ./build/hind --fullscreen)"
