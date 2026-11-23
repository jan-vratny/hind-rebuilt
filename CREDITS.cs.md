# Credits

[English](CREDITS.md)

## 3D models

| Soubor | Autor | Zdroj | Licence | Použití ve hře |
|---|---|---|---|---|
| Mi-24 (STL s podvozkem) | [Pfelpensson](https://www.thingiverse.com/Pfelpensson/designs) | [Thingiverse: thing:4597365](https://www.thingiverse.com/thing:4597365) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | ikona a logo hry (`data/hind.svg`, `data/hind.png`, `data/hind.ico`, `src/render/icon.inc`): model zjednodušený, obarvený v barvách ukrajinských misí a napůl vykreslený jako drátěný model nástroji `tools/make_icon.py` a `tools/half_wire.py`. Ve hře samotné se model nepoužívá. |
| M60 (Low Poly) | [Jawa](https://sketchfab.com/HELLBRAD) | [Sketchfab: m60-low-poly](https://sketchfab.com/3d-models/m60-low-poly-42b4f0588ef24e62b7b2fe9b9fc413b5) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | kulomet M60 v rukou pěšáka v síťové hře (`data/models/m60.mesh`): jedna ze dvou schránek na náboje vynechaná, model zjednodušený a barvy zesvětlené nástrojem `tools/glb2mesh.py` |
| M72 LAW Rocket Launcher | [Rectus](https://sketchfab.com/rectus) | [Sketchfab: m72-law-rocket-launcher](https://sketchfab.com/3d-models/m72-law-rocket-launcher-b4367ffcf3084d4b9eff8c497bf5c7fd) | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) | protitanková zbraň M72 LAW v rukou pěšáka v síťové hře (`data/models/m72.mesh`) a její raketa v letu (`data/models/m72rocket.mesh`, volná raketa z téhož modelu): u zbraně průhledné hledí a zadní víčko vynechané, model zjednodušený; obojí obarvené podle textury modelu nástrojem `tools/glb2mesh.py`; jen k nekomerčnímu užití |

Ikona a logo jsou odvozená díla modelu Mi-24, `data/models/m60.mesh` modelu M60, `data/models/m72.mesh` a `data/models/m72rocket.mesh` modelu M72 LAW; platí pro ně uvedené licence.

## Obrázky

| Soubor | Autor | Zdroj | Licence | Použití ve hře |
|---|---|---|---|---|
| `GShG_762.JPG` | [Atirador](https://commons.wikimedia.org/wiki/User:Atirador) | [Wikimedia Commons: File:GShG_762.JPG](https://commons.wikimedia.org/wiki/File:GShG_762.JPG) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) | kulomet GŠG-7.62 v okně Pay: vyříznutý, zmenšený, zesvětlený a se stínem převedený nástrojem `tools/gshg2sprite.py` do `src/menu/gshg_sprite.inc` |

Upravený obrázek v `src/menu/gshg_sprite.inc` je odvozené dílo fotografie a platí pro něj uvedená licence.

## Zvuky

| Soubor | Autor | Zdroj | Licence | Použití ve hře |
|---|---|---|---|---|
| „Squeak Rusty Metal Water Tap“ | [freesound_community](https://pixabay.com/users/freesound_community-46691455/) | [Pixabay](https://pixabay.com/sound-effects/) | [Pixabay Content License](https://pixabay.com/service/license-summary/) | vrznutí při šroubování (`data/sounds/squeak.wav`): vyříznuté první vrznutí, převedené na 22050 Hz mono |

## Knihovny a data

| Co | Autor | Zdroj | Licence | Použití ve hře |
|---|---|---|---|---|
| SDL2 | Sam Lantinga a přispěvatelé | [libsdl.org](https://www.libsdl.org/) | [zlib](https://www.libsdl.org/license.php) | okno, OpenGL, klávesnice, myš, joystick a zvuk; ve Windows přiložené jako `SDL2.dll` |
| GLM (OpenGL Mathematics) | G-Truc Creation | [github.com/g-truc/glm](https://github.com/g-truc/glm) | [MIT](https://github.com/g-truc/glm/blob/master/copying.txt) | vektorová a maticová matematika (přeložená do hry) |
| Yale Bright Star Catalogue, 5. vyd. | D. Hoffleit, W. H. Warren Jr. | [tdc-www.harvard.edu/catalogs/bsc5.html](http://tdc-www.harvard.edu/catalogs/bsc5.html) | volné dílo | hvězdy noční oblohy (`src/render/stars.inc`, nástroj `tools/stars.py`) |
