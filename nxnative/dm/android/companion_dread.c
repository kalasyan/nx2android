// Metroid Dread 2.1.0 on the companion page (kalasyan fork): the area's map with Samus on it, live.
// Where Samus is: porter/dread_companion.py wraps the game's system/scripts/scenario.lc with a script that keeps a Lua table
//   NX2A = {1357.25, -8642.75, seq, area, x, y, life, max life, missiles, max missiles, 1}
// up to date every frame (Game.AddSF). Dread's Lua numbers are floats in 16-byte TValues (value, then the type tag, 3 = number), so the table's array
// part is 11 TValues in a row that this file finds by scanning the game's memory (main's data and bss, where the engine keeps its heaps, and the guest
// heap) for the two magic numbers, then reads in place. An old copy of the table can stay in memory after an area loads a new one: of the copies found,
// the one whose seq moves is used. Without a live copy the scan is repeated, less often each time (the title menu has none).
// The maps: dread_companion.py turns each area's minimap data (maps/levels/c10_samus/<area>/<area>.bmmap: the navigation mesh, doors, items) into
// companion/<area>.nx2m in the data folder. Drawn once per area into a cached picture fitted to the screen; each pass copies it and draws Samus.
#include "dm.h"
#include "companion.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DREAD_MAIN "646761f643afebb379edd5e6a5151af2"   // main of Metroid Dread 2.1.0
void svc_heap_range(uint8_t **base, size_t *size);       // hle_svc.c

int dm_dread_active(void) {
    static int on = -1;
    if (on < 0 && g_nmod > 0 && dm_main_build()[0]) on = !strcmp(dm_main_build(), DREAD_MAIN);   // the modules are loaded by the host thread, after the page starts
    return on > 0;
}

// ---- the table ----
typedef struct { union { float n; uint64_t raw; } v; int32_t tt, pad; } tvalue;   // Lua 5.1 TValue with float numbers: 16 bytes
enum { NX_M1, NX_M2, NX_SEQ, NX_AREA, NX_X, NX_Y, NX_LIFE, NX_LIFEMAX, NX_MIS, NX_MISMAX, NX_VER, NX_N };
#define NX_MAGIC1 1357.25f
#define NX_MAGIC2 -8642.75f
#define LUA_TNUMBER 3
static const char *AREA_ID[] = {"", "s010_cave", "s020_magma", "s030_baselab", "s040_aqua", "s050_forest", "s060_quarantine", "s070_basesanc", "s080_shipyard", "s090_skybase"};
static const char *AREA_NAME[] = {"", "Artaria", "Cataris", "Dairon", "Burenia", "Ghavoran", "Elun", "Ferenia", "Hanubia", "Itorash"};
#define NAREA (int)(sizeof AREA_ID / sizeof *AREA_ID)

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int is_table(const volatile tvalue *t) {
    for (int i = 0; i < NX_N; i++) if (t[i].tt != LUA_TNUMBER) return 0;
    return t[NX_M1].v.n == NX_MAGIC1 && t[NX_M2].v.n == NX_MAGIC2 && t[NX_VER].v.n == 1;
}
#define MAXCAND 32
static const volatile tvalue *g_cand[MAXCAND]; static float g_cand_seq[MAXCAND]; static int g_ncand;
static const volatile tvalue *g_tab;   // the live copy
static int64_t g_cand_t;   // when the candidates were found
static void scan_range(const uint8_t *b, size_t n) {
    uint32_t m1 = fbits(NX_MAGIC1), m2 = fbits(NX_MAGIC2);
    if (n < NX_N * sizeof(tvalue)) return;
    const uint8_t *end = b + n - NX_N * sizeof(tvalue);
    for (const uint8_t *p = (const uint8_t *)(((uintptr_t)b + 7) & ~(uintptr_t)7); p <= end && g_ncand < MAXCAND; p += 8) {
        if (*(const uint32_t *)p != m1 || *(const uint32_t *)(p + 16) != m2) continue;
        if (is_table((const volatile tvalue *)p)) { g_cand[g_ncand] = (const volatile tvalue *)p; g_cand_seq[g_ncand] = ((const tvalue *)p)[NX_SEQ].v.n; g_ncand++; }
    }
}
static void scan_readable(const uint8_t *b, size_t n) {   // only the readable mappings of [b, b + n): a guard page would end the game
    FILE *f = fopen("/proc/self/maps", "r"); char line[512];
    if (!f) return;
    uintptr_t lo = (uintptr_t)b, hi = lo + n;
    while (fgets(line, sizeof line, f)) {
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3 || perm[0] != 'r' || e <= lo || s >= hi) continue;
        uintptr_t a = s > lo ? s : lo, z = e < hi ? e : hi;
        scan_range((const uint8_t *)a, z - a);
    }
    fclose(f);
}
static void scan(void) {
    int64_t t0 = (int64_t)now_ns();
    g_ncand = 0;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main") && g_mod[i].data_off < g_mod[i].size) scan_readable(g_mod[i].base + g_mod[i].data_off, g_mod[i].size - g_mod[i].data_off);
    uint8_t *hb; size_t hn; svc_heap_range(&hb, &hn); if (hb) scan_readable(hb, hn);
    g_cand_t = (int64_t)now_ns();
    static int logs, last = -1; if (logs++ < 6 || g_ncand != last) LOG("[companion] Dread: %d copies of the NX2A table found (%.0f ms)%s", g_ncand, (g_cand_t - t0) / 1e6, g_ncand ? ", waiting for one to move" : ""); last = g_ncand;
}

// ---- the state, read each pass ----
static struct { int area, life, lifemax, mis, mismax; float x, y; int64_t moved; } g_st;
static int64_t g_next_scan, g_backoff = 2000000000;   // the next scan; the wait after a scan that found nothing live, doubling to 16 s
void dm_dread_tick(void) {
    if (!dm_dread_active()) return;
    int64_t now = (int64_t)now_ns();
    if (g_tab && !is_table(g_tab)) { LOG("[companion] Dread: the NX2A table went (an area loading?)"); g_tab = NULL; g_ncand = 0; g_next_scan = now + 1000000000; g_backoff = 2000000000; }
    if (!g_tab && g_ncand) {   // a copy whose seq moves is the live one
        for (int i = 0; i < g_ncand; i++) {
            if (!is_table(g_cand[i])) { g_ncand--; g_cand[i] = g_cand[g_ncand]; g_cand_seq[i] = g_cand_seq[g_ncand]; i--; continue; }
            if (g_cand[i][NX_SEQ].v.n != g_cand_seq[i]) { g_tab = g_cand[i]; LOG("[companion] Dread: following the NX2A table at %p", (void *)g_tab); break; }
        }
        // none moves (the copy the game made at boot, before any area): look again, the area's own copy may be elsewhere
        if (!g_tab && g_ncand && now - g_cand_t > 3000000000) { g_ncand = 0; g_next_scan = now + g_backoff; if (g_backoff < 16000000000) g_backoff *= 2; }
    }
    if (!g_tab && !g_ncand && now >= g_next_scan) {
        scan();
        if (!g_ncand) { g_next_scan = now + g_backoff; if (g_backoff < 16000000000) g_backoff *= 2; }
    }
    if (!g_tab) return;
    const volatile tvalue *t = g_tab;
    float x = t[NX_X].v.n, y = t[NX_Y].v.n;
    if (x != g_st.x || y != g_st.y) g_st.moved = now;
    g_st.x = x; g_st.y = y; g_st.area = (int)t[NX_AREA].v.n;
    g_st.life = (int)t[NX_LIFE].v.n; g_st.lifemax = (int)t[NX_LIFEMAX].v.n; g_st.mis = (int)t[NX_MIS].v.n; g_st.mismax = (int)t[NX_MISMAX].v.n;
}

// ---- the maps ----
typedef struct { float minx, miny, maxx, maxy; uint32_t ntri, ndoor, nitem; float *tri, *door, *item; } nx2m;   // tri: x0 y0 x1 y1 x2 y2 each; door, item: x y each
static nx2m g_map; static int g_map_area = -1;   // -1 = none tried; the area whose map is loaded (or failed: ntri = 0)
static int read_f(FILE *f, float **out, uint32_t *n, int per) {
    if (fread(n, 4, 1, f) != 1 || *n > 4000000) return 0;
    *out = malloc((size_t)*n * per * 4 + 4); return *out && fread(*out, 4, (size_t)*n * per, f) == (size_t)*n * per;
}
static void map_load(int area) {
    free(g_map.tri); free(g_map.door); free(g_map.item); memset(&g_map, 0, sizeof g_map); g_map_area = area;
    const char *root = getenv("DM_ROOT"); char path[700]; char hdr[8];
    snprintf(path, sizeof path, "%s/companion/%s.nx2m", root ? root : ".", AREA_ID[area]);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("[RED] [companion] Dread: no map %s (run porter dread-companion)", path); return; }
    int ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "NX2M\1\0\0\0", 8) && fread(&g_map.minx, 4, 4, f) == 4
          && read_f(f, &g_map.tri, &g_map.ntri, 6) && read_f(f, &g_map.door, &g_map.ndoor, 2) && read_f(f, &g_map.item, &g_map.nitem, 2);
    fclose(f);
    if (!ok || g_map.maxx <= g_map.minx || g_map.maxy <= g_map.miny) { LOG("[RED] [companion] Dread: bad map %s", path); g_map.ntri = 0; return; }
    LOG("[companion] Dread: map %s: %u triangles, %u doors, %u items, x %.0f..%.0f y %.0f..%.0f", AREA_ID[area], g_map.ntri, g_map.ndoor, g_map.nitem, g_map.minx, g_map.maxx, g_map.miny, g_map.maxy);
}

// the picture of the map, fitted to the area under the bar
static uint32_t *g_pic; static int g_pic_w, g_pic_h, g_pic_area = -1, g_top;
static float g_k, g_ox, g_oy;   // screen = (g_ox + (x - minx) * g_k, g_oy + (maxy - y) * g_k)
#define C_BG    0xff201008u
#define C_ROOM  0xffa05a1eu
#define C_DOOR  0xfff0e0a0u
#define C_ITEM  0xff40c8ffu
#define C_BAR   0xff5a2a1au
static void sxy(float x, float y, float *sx, float *sy) { *sx = g_ox + (x - g_map.minx) * g_k; *sy = g_oy + (g_map.maxy - y) * g_k; }
static void tri_fill(uint32_t *px, int w, int h, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0); if (area == 0) return;
    if (area < 0) { float tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))), bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))), by0 = (int)floorf(fminf(y0, fminf(y1, y2))), by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > w) bx1 = w; if (by1 > h) by1 = h;
    for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) {
        float px_ = x + 0.5f, py = y + 0.5f;
        if ((x1 - x0) * (py - y0) - (y1 - y0) * (px_ - x0) >= 0 && (x2 - x1) * (py - y1) - (y2 - y1) * (px_ - x1) >= 0 && (x0 - x2) * (py - y2) - (y0 - y2) * (px_ - x2) >= 0) px[(size_t)y * w + x] = c;
    }
}
static void pic_make(int w, int h, int top, int area) {
    free(g_pic); g_pic = malloc((size_t)w * h * 4); g_pic_w = w; g_pic_h = h; g_pic_area = area; g_top = top;
    if (!g_pic) return;
    for (size_t i = 0; i < (size_t)w * h; i++) g_pic[i] = C_BG;
    float m = h / 40.0f, aw = w - 2 * m, ah = h - top - 2 * m, mw = g_map.maxx - g_map.minx, mh = g_map.maxy - g_map.miny;
    g_k = fminf(aw / mw, ah / mh); g_ox = m + (aw - mw * g_k) / 2; g_oy = top + m + (ah - mh * g_k) / 2;
    for (uint32_t i = 0; i < g_map.ntri; i++) {
        const float *t = g_map.tri + 6 * i; float a[6];
        sxy(t[0], t[1], &a[0], &a[1]); sxy(t[2], t[3], &a[2], &a[3]); sxy(t[4], t[5], &a[4], &a[5]);
        tri_fill(g_pic, w, h, a[0], a[1], a[2], a[3], a[4], a[5], C_ROOM);
    }
    page p = {g_pic, w, h, w}; int d = (int)fmaxf(2, h / 270.0f);
    for (uint32_t i = 0; i < g_map.ndoor; i++) { float x, y; sxy(g_map.door[2 * i], g_map.door[2 * i + 1], &x, &y); fill(&p, (int)x - d / 2, (int)y - 2 * d, (int)x + d - d / 2, (int)y + 2 * d, C_DOOR); }
    for (uint32_t i = 0; i < g_map.nitem; i++) { float x, y; sxy(g_map.item[2 * i], g_map.item[2 * i + 1], &x, &y); fill(&p, (int)x - d, (int)y - d, (int)x + d, (int)y + d, C_ITEM); }
}
static void disc(page *p, float cx, float cy, float r, uint32_t c) {
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) for (int x = (int)(cx - r); x <= (int)(cx + r); x++)
        if ((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy) <= r * r && x >= 0 && y >= 0 && x < p->w && y < p->h) p->px[(size_t)y * p->stride + x] = c;
}
int dm_dread_draw(page *p) {
    if (!dm_dread_active()) return 0;
    int s = p->w / (6 * 28 + 6); if (s > p->h / (11 * 16)) s = p->h / (11 * 16); if (s < 2) s = 2;   // as companion_android.c's page
    int m = 3 * s, top = 2 * m + 7 * s;
    int area = g_tab && g_st.area > 0 && g_st.area < NAREA ? g_st.area : 0;
    if (!area) {
        fill(p, 0, 0, p->w, p->h, C_BG); fill(p, 0, 0, p->w, top, C_BAR); text(p, m, m, s, 0xffffffff, "METROID DREAD");
        text(p, m, top + 2 * m, s, 0xffb0b0b0, g_ncand ? "Buscando a Samus..." : "El mapa sale al entrar en una zona");
        return 1;
    }
    if (area != g_map_area) map_load(area);
    if (g_map.ntri && (g_pic_area != area || g_pic_w != p->w || g_pic_h != p->h || g_top != top)) pic_make(p->w, p->h, top, area);
    if (g_map.ntri && g_pic) for (int y = 0; y < p->h; y++) memcpy(p->px + (size_t)y * p->stride, g_pic + (size_t)y * p->w, (size_t)p->w * 4);
    else fill(p, 0, 0, p->w, p->h, C_BG);
    fill(p, 0, 0, p->w, top, C_BAR);
    char b[96]; snprintf(b, sizeof b, "%s  E %d/%d  M %d/%d", AREA_NAME[area], g_st.life, g_st.lifemax, g_st.mis, g_st.mismax); text(p, m, m, s, 0xffffffff, b);
    if (g_map.ntri) { float x, y; sxy(g_st.x, g_st.y, &x, &y); float r = fmaxf(5, p->h / 90.0f); disc(p, x, y, r * 1.6f, 0xff000000); disc(p, x, y, r, 0xff20b0ff); }
    else text(p, m, top + 2 * m, s, 0xffb0b0b0, "Sin mapa de esta zona");
    return 1;
}
