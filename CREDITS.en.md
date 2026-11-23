# Credits

[Česky](CREDITS.md)

## 3D models

| File | Author | Source | License | Use in the game |
|---|---|---|---|---|
| Mi-24 (STL with landing gear) | [Pfelpensson](https://www.thingiverse.com/Pfelpensson/designs) | [Thingiverse: thing:4597365](https://www.thingiverse.com/thing:4597365) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | the game's icon and logo (`data/hind.svg`, `data/hind.png`, `data/hind.ico`, `src/render/icon.inc`): the model simplified, coloured in the Ukrainian missions' livery and drawn half as a wireframe by `tools/make_icon.py` and `tools/half_wire.py`. The game itself does not use the model. |
| M60 (Low Poly) | [Jawa](https://sketchfab.com/HELLBRAD) | [Sketchfab: m60-low-poly](https://sketchfab.com/3d-models/m60-low-poly-42b4f0588ef24e62b7b2fe9b9fc413b5) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | the M60 machine gun in the hands of a man on foot in the network game (`data/models/m60.mesh`): one of its two ammunition boxes left out, the model simplified and its colours lightened by `tools/glb2mesh.py` |
| M72 LAW Rocket Launcher | [Rectus](https://sketchfab.com/rectus) | [Sketchfab: m72-law-rocket-launcher](https://sketchfab.com/3d-models/m72-law-rocket-launcher-b4367ffcf3084d4b9eff8c497bf5c7fd) | [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) | the M72 LAW anti-tank weapon in the hands of a man on foot in the network game (`data/models/m72.mesh`) and its rocket in flight (`data/models/m72rocket.mesh`, the loose rocket of the same model): of the weapon the clear front sight and the rear cover left out, the model simplified; both painted by the model's texture by `tools/glb2mesh.py`; for non-commercial use only |

The icon and the logo are derivative works of the Mi-24 model, `data/models/m60.mesh` of the M60 model, `data/models/m72.mesh` and `data/models/m72rocket.mesh` of the M72 LAW model; they fall under their licenses.

## Pictures

| File | Author | Source | License | Use in the game |
|---|---|---|---|---|
| `GShG_762.JPG` | [Atirador](https://commons.wikimedia.org/wiki/User:Atirador) | [Wikimedia Commons: File:GShG_762.JPG](https://commons.wikimedia.org/wiki/File:GShG_762.JPG) | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) | the GShG-7.62 machine gun in the Pay window: cut out, scaled down, brightened and given a shadow by `tools/gshg2sprite.py`, into `src/menu/gshg_sprite.inc` |

The modified picture in `src/menu/gshg_sprite.inc` is a derivative work of the photograph and falls under its license.

## Sounds

| File | Author | Source | License | Use in the game |
|---|---|---|---|---|
| “Squeak Rusty Metal Water Tap” | [freesound_community](https://pixabay.com/users/freesound_community-46691455/) | [Pixabay](https://pixabay.com/sound-effects/) | [Pixabay Content License](https://pixabay.com/service/license-summary/) | a creak while screwing (`data/sounds/squeak.wav`): its first creak cut out, converted to 22050 Hz mono |

## Libraries and data

| What | Author | Source | License | Use in the game |
|---|---|---|---|---|
| SDL2 | Sam Lantinga and contributors | [libsdl.org](https://www.libsdl.org/) | [zlib](https://www.libsdl.org/license.php) | window, OpenGL, keyboard, mouse, joystick and sound; on Windows shipped as `SDL2.dll` |
| GLM (OpenGL Mathematics) | G-Truc Creation | [github.com/g-truc/glm](https://github.com/g-truc/glm) | [MIT](https://github.com/g-truc/glm/blob/master/copying.txt) | vector and matrix maths (compiled into the game) |
| Yale Bright Star Catalogue, 5th ed. | D. Hoffleit, W. H. Warren Jr. | [tdc-www.harvard.edu/catalogs/bsc5.html](http://tdc-www.harvard.edu/catalogs/bsc5.html) | public domain | the night sky's stars (`src/render/stars.inc`, tool `tools/stars.py`) |
