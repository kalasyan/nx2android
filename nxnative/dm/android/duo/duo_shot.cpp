// SPDX-License-Identifier: GPL-3.0-or-later
// A test of duo/ without the game (kalasyan fork): the runtime and the title's module started as the port starts them (duo_host.cpp), with a stand-in
// for the game's main module, then the companion page drawn into a PNG. The module reads no game (its memory reads fail), so it shows the page it
// shows before a game is found; what this checks is the rest: the package is parsed, the module loads and starts, its fonts and art come from the
// RomFS given, the runtime ticks and draws, and taps reach it.
//   duo_shot <state folder> <RomFS folder or -> <module .so> <out.png> [seconds] [x,y taps...]
// <state folder>/packages/<package>/dualscreen/ must hold the package (duo/package/<package>/dualscreen). Builds on a desktop (build_shot.py) or for the device.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include "companion.h"
int dm_duo_active(void);
void dm_duo_tick(void);
int dm_duo_draw(page* p);
void dm_duo_touch(int action, int index, int n, const float* x, const float* y);
}
#include "stb_image_write.h"   // its implementation is in eden/common/stb.cpp

namespace {
std::vector<uint8_t> g_main(1 << 20);
const char* g_build = "ff1d1c05670db6021c85b624a710b963";   // Animal Crossing: New Horizons 3.0.3's main (nxnative/dm/games/01006F8002326000/game.tsv)
}
extern "C" {
int dm_companion_main_module(uint8_t** base, size_t* size, const char** build) { *base = g_main.data(); *size = g_main.size(); *build = getenv("DUO_SHOT_BUILD") ? getenv("DUO_SHOT_BUILD") : g_build; return 1; }
void svc_heap_range(uint8_t** base, size_t* size) { *base = nullptr; *size = 0; }
uint64_t dm_frames_shown(void) { return 1; }   // the game is up: the companion starts at once
void* dm_front_assets(void) { return nullptr; }
void dm_hid_player1(uint64_t* b, int32_t* a) { *b = 0; for (int i = 0; i < 4; i++) a[i] = 0; }
void dm_haptics_android_tap(void) {}
void fill(page*, int, int, int, int, uint32_t) {}
void text(page*, int, int, int, uint32_t, const char*) {}
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "duo_shot <state folder> <RomFS folder or -> <module .so> <out.png> [seconds] [x,y ...]\n"); return 2; }
    const std::string state = argv[1];
    setenv("DM_ROOT", state.c_str(), 1);
    setenv("DM_SAVE", (state + "/save").c_str(), 1);   // duo_host.cpp keeps its state beside the save: <state>/duo
    if (strcmp(argv[2], "-")) setenv("DM_ROMFS", argv[2], 1);
    setenv("DM_DUO_MODULE", argv[3], 1);
    const double secs = argc > 5 ? atof(argv[5]) : 5;
    const auto t0 = std::chrono::steady_clock::now();
    int tap = 6;
    std::vector<uint32_t> px(1240 * 1080);
    page p{px.data(), 1240, 1080, 1240};
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
        dm_duo_tick();
        if (!dm_duo_active()) { fprintf(stderr, "the companion is off (see the log above)\n"); return 1; }
        dm_duo_draw(&p);
        // the taps, one a second after the first two seconds
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (tap < argc && el > 2 + (tap - 6)) {
            float x = 0, y = 0;
            if (sscanf(argv[tap], "%f,%f", &x, &y) == 2) {
                dm_duo_touch(0, 0, 1, &x, &y); std::this_thread::sleep_for(std::chrono::milliseconds(60));
                dm_duo_touch(1, 0, 1, &x, &y);
                fprintf(stderr, "tap %g,%g\n", x, y);
            }
            tap++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (!dm_duo_draw(&p)) { fprintf(stderr, "no page drawn\n"); return 1; }
    if (!stbi_write_png(argv[4], 1240, 1080, 4, px.data(), 1240 * 4)) { fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
    fprintf(stderr, "wrote %s\n", argv[4]);
    return 0;
}
