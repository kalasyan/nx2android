# Castlevania: Belmont's Curse on the second screen (kalasyan fork)

The companion page of the AYN Thor's bottom screen (`companion_android.c`, `DM_COMPANION=1` in the data pack's `env.txt`) for
Castlevania: Belmont's Curse 1.1.0 (`0100BA4026E94000`, main build `0F1169AA7D0FA4529C861F96ADEC306E`): the map of the current area
with the game's own fog, room art, landmarks, save points, elevators and fast travel points, the player's rose, the area and sub-area
names on the game's ribbon, the current objective, and the equipment overview (Arcana, weapon, relics, ATK/DEF/HP/MP), drawn with
the game's own art and font.

It is igawa6's companion for Eden Duo, run inside the port:

| Path | What | From |
|---|---|---|
| `module/` | The companion's native module, unchanged: it reads the running game (IL2CPP objects at fixed offsets of this build) and decodes the game's Unity bundles from the RomFS | [igawa6/eden-duo-companions](https://github.com/igawa6/eden-duo-companions) `native/modules` at `fc0f85e` (2026-11-03) |
| `manifest.json` | The companion package's layout (widgets, bindings, actions) | the same repository, `packages/BelmontsCurse/dualscreen/manifest.json` |
| `eden/core/mods/` | The module ABI headers | [igawa6/eden-duo](https://github.com/igawa6/eden-duo) `src/core/mods` at `3518684` (2026-11-03) |
| `third_party/astc.cpp` | Eden's software ASTC decoder, without boost and Eden's worker threads | eden-duo `src/video_core/textures/astc.cpp` |
| `third_party/bc_decoder.*` | BCn decoder (MPL-2.0) | eden-duo `externals/bc_decoder` |
| `third_party/lz4.*` | LZ4 (BSD-2-Clause) | lz4 v1.10.0 |
| `belmont_host.cpp` | What Eden Duo did around the module: the host table (guest memory is this process's memory, the RomFS is the data pack's `RomFS/`), the page (the manifest's widgets, derived values, flags and actions, its map widget with pan and zoom), touches | this fork |
| `belmont_shot.cpp`, `build_shot.py` | A test tool: the page drawn from a data pack without the game, into a BMP, run with `adb shell` | this fork |

`build.py` builds all of it into `libdmhost.so` for this title only (C++20, `-static-libstdc++`); other games get the weak stubs in
`companion_android.c`.

Read only: the host gives no guest writes and installs no guest helper, so placing pins and changing equipment from the bottom screen
(which call the game's functions through that helper, or press the game's buttons) are hidden. The game's names (areas, objective,
landmarks, equipment) come from its English tables, as in the module; the package's own words are in Spanish.

License: the module, the manifest, the Eden headers and `astc.cpp` are GPL-3.0-or-later (`LICENSE.GPL-3.0`), and so is this folder's
own code. A build of the Castlevania host that contains them is under the GPL-3.0 as a whole. Nothing from the game is in the repository:
the art and the font are decoded from the player's own RomFS at run time.

Test on the device, without the game (the data pack's folder as the game uses it):

```sh
python nxnative/dm/android/belmont/build_shot.py --out /tmp/shot
adb push /tmp/shot/belmont_shot /data/local/tmp/ && adb shell chmod +x /data/local/tmp/belmont_shot
adb shell /data/local/tmp/belmont_shot /storage/emulated/0/Juegos/Castlevania /data/local/tmp/belmont.bmp 40
adb pull /data/local/tmp/belmont.bmp
```
