# duo/ — Eden Duo's dual-screen runtime inside the port

**License: GPL-3.0-or-later** (`LICENSE.GPL-3.0`), not MIT like the rest of the repository. A `libdmhost.so` / APK built with this folder is GPL-3.0 as a whole. `build.py` compiles it only for the titles in `titles.tsv`.

The companion pages of [eden-duo](https://github.com/igawa6/eden-duo) (Eden's `src/core/mods` runtime, commit 35186847d) run unchanged on the AYN Thor's bottom screen, with the title's module from [eden-duo-companions](https://github.com/igawa6/eden-duo-companions) (commit dc34c46). The game reads the module's pages, taps and button presses exactly as under Eden.

- `eden/` — Eden's sources. Edits are marked `nx2android:` (the `__ANDROID__` paths of `common/fs`, ROMFS and package lookup through `DuoHost::*`, the module loaded from the APK's lib folder without staging or hashing, the language from `DM_LANG`). `mod_console.cpp` and `mod_re_tools.cpp` are left out (`EDEN_DSMOD_BUILD_DEV_TOOLS=0`).
- `shim/` — the Eden emulator APIs the runtime uses (System, CoreTiming, Memory, KProcess, HID, Settings, logging), over the port's own memory (guest address == host address) and HID.
- `third_party/` — nlohmann json 3.11.3, fmt 11.0.2, ankerl unordered_dense 4.4.0, zstd 1.5.6, stb, bc_decoder, ASTC.
- `module/` — the title's module (Animal Crossing: New Horizons, `01006F8002326000`), built as `libdsmod<ID>.so`.
- `package/<package>/dualscreen/` — the manifest and data files; porter puts them in the APK under `assets/duo/` and `duo_host.cpp` unpacks them to `<state>/duo/packages` on first start.
- `duo_host.cpp` — the glue: starts the runtime when the game's main module is known, ticks it at 60 Hz, draws its page into the companion (`companion_android.c`), passes taps, and merges its button presses into player 1 (`hle_hid.c`).
- `duo_shot.cpp` — a test without the game: runs the runtime and module and writes the page to a PNG.

Animal Crossing needs update 3.0.3 (v2228224, main build `ff1d1c05670db6021c85b624a710b963`); with another build the module shows its version page. Enable with `DM_COMPANION=1` in the game's `env.txt`.
