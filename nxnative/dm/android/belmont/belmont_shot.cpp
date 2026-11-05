// belmont_shot: the Castlevania companion page drawn without the game, from a data pack's RomFS, into a BMP (kalasyan fork, a test tool).
//   belmont_shot <data folder (holds RomFS/)> <out.bmp> [seconds to wait for the art, default 40]
// Built by build_shot.sh for an adb shell run on the device. The module's game readers need the running game, so the outputs the page is bound to
// are set here (a world with a made-up area name): what this checks is the RomFS reading, the area table, the font and the game's art in the layout.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <vector>
extern "C" {
#include "companion.h"
int belmont_shot_mode = 1;
void belmont_shot_set(const char *k, int kind, double v, const char *t);
const char *belmont_shot_areas(void);
int dm_belmont_active(void); void dm_belmont_tick(void); int dm_belmont_draw(page *p);
uint64_t now_ns(void) { return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void svc_heap_range(uint8_t **b, size_t *n) { *b = nullptr; *n = 0; }
static uint8_t *g_fake_main;
int dm_companion_main_module(uint8_t **base, size_t *size, const char **build) {
    if (!g_fake_main) g_fake_main = (uint8_t *)mmap(nullptr, 1 << 20, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *base = g_fake_main; *size = 1 << 20; *build = "0F1169AA7D0FA4529C861F96ADEC306E"; return 1;
}
#include "font5x7.h"
void fill(page *p, int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > p->w) x1 = p->w; if (y1 > p->h) y1 = p->h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) p->px[(size_t)y * p->stride + x] = c;
}
void text(page *p, int x, int y, int s, uint32_t c, const char *t) {
    for (; *t; t++, x += 6 * s) { unsigned ch = (unsigned char)*t; if (ch < 32 || ch > 126) ch = '?';
        for (int col = 0; col < 5; col++) for (int row = 0; row < 7; row++) if (FONT5X7[ch - 32][col] >> row & 1) fill(p, x + col * s, y + row * s, x + (col + 1) * s, y + (row + 1) * s, c); }
}
}
static void bmp(const char *path, const page &p) {
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return; }
    uint32_t n = (uint32_t)(p.w * p.h * 4);
    uint8_t h[54] = {'B', 'M'}; auto u32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
    u32(2, 54 + n); u32(10, 54); u32(14, 40); u32(18, (uint32_t)p.w); u32(22, (uint32_t)-p.h); h[26] = 1; h[28] = 32; u32(34, n);
    fwrite(h, 1, 54, f);
    std::vector<uint8_t> row((size_t)p.w * 4);
    for (int y = 0; y < p.h; y++) { for (int x = 0; x < p.w; x++) { uint32_t c = p.px[(size_t)y * p.stride + x]; row[x * 4] = c >> 16 & 0xff; row[x * 4 + 1] = c >> 8 & 0xff; row[x * 4 + 2] = c & 0xff; row[x * 4 + 3] = 255; } fwrite(row.data(), 1, row.size(), f); }
    fclose(f);
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: belmont_shot <data folder> <out.bmp> [seconds]\n"); return 2; }
    setenv("DM_ROOT", argv[1], 1);
    int secs = argc > 3 ? atoi(argv[3]) : 40;
    if (!dm_belmont_active()) { fprintf(stderr, "not active\n"); return 1; }
    dm_belmont_tick();
    std::vector<uint32_t> px(1240 * 1080); page p{px.data(), 1240, 1080, 1240};
    std::string area;
    for (int i = 0; i < secs * 5; i++) {
        if (area.empty()) { const char *a = belmont_shot_areas(); if (*a) { area.assign(a, strcspn(a, " ")); fprintf(stderr, "areas: %s\n", a); } }
        belmont_shot_set("belmont.build_supported", 1, 1, nullptr); belmont_shot_set("belmont.native_assets_ready", 1, 1, nullptr);
        belmont_shot_set("belmont.ui_world", 1, 1, nullptr); belmont_shot_set("belmont.map_ready", 1, 1, nullptr); belmont_shot_set("belmont.marker_ready", 1, 1, nullptr);
        belmont_shot_set("belmont.biome_name", 3, 0, "Master's Quarters"); belmont_shot_set("belmont.area_name", 3, 0, "Sanctuary of Solitude");
        belmont_shot_set("belmont.map_area", 3, 0, area.c_str());
        dm_belmont_draw(&p);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    auto t0 = now_ns(); dm_belmont_draw(&p); fprintf(stderr, "one page: %.1f ms\n", (now_ns() - t0) / 1e6);
    bmp(argv[2], p); fprintf(stderr, "wrote %s\n", argv[2]);
    return 0;
}
