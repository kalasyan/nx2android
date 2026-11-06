// Metroid Dread 2.1.0 on the companion page (kalasyan fork): the area's map with Samus on it, live.
// Where Samus is: porter/dread_companion.py wraps the game's system/scripts/scenario.lc with a script that keeps a Lua table
//   NX2A = {1357.25, -8642.75, seq, area, x, y, life, max life, missiles, max missiles, 1}
// up to date every frame (Game.AddSF). Dread's Lua numbers are floats in 16-byte TValues (value, then the type tag, 3 = number), so the table's array
// part is 11 TValues in a row that this file finds by scanning the game's memory (main's data and bss, where the engine keeps its heaps, and the guest
// heap) for the two magic numbers, then reads in place. An old copy of the table can stay in memory after an area loads a new one: of the copies found,
// the one whose seq moves is used. Without a live copy the scan is repeated, less often each time (the title menu has none).
// The maps: dread_companion.py turns each area's minimap data (maps/levels/c10_samus/<area>/<area>.bmmap: the navigation mesh, doors, items) into
// companion/<area>.nx2m in the data folder, and the game's map icons (textures/system/minimap/icons/icons.bctex) into companion/icons.nx2i.
// Drawn as eden-duo-companions' Dread map looks: a cached picture of the rooms for the view (the whole area, or zoomed in by touch), coloured each pass
// by what is explored (the rest is fog), the game's icons over it, and Samus.
#include "dm.h"
#include "companion.h"
#include <math.h>
#include <pthread.h>
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
static void grid_tick(void);   // the game's own explored map, below
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
    grid_tick();
    const volatile tvalue *t = g_tab;
    float x = t[NX_X].v.n, y = t[NX_Y].v.n;
    if (x != g_st.x || y != g_st.y) g_st.moved = now;
    g_st.x = x; g_st.y = y; g_st.area = (int)t[NX_AREA].v.n;
    g_st.life = (int)t[NX_LIFE].v.n; g_st.lifemax = (int)t[NX_LIFEMAX].v.n; g_st.mis = (int)t[NX_MIS].v.n; g_st.mismax = (int)t[NX_MISMAX].v.n;
}

// ---- the maps ----
// companion/<area>.nx2m (porter/dread_companion.py): "NX2M", version 2, bounds; the navigation mesh's triangles; the water, heat, freeze and EMMI rooms'
// triangles; the icons (doors as two halves of their box, blockages over their box, items, stations... at a point), as numbers in companion/icons.nx2i
enum { L_WATER, L_HEAT, L_FREEZE, L_EMMY, NLAYER };
enum { F_BOX = 1, F_FLIPX = 2, F_FLIPY = 4, F_DOOR = 8, F_ITEM = 16 };
typedef struct { uint16_t icon, flags; float x, y, b[4]; } micon;
typedef struct { float minx, miny, maxx, maxy; uint32_t ntri, nicon, nl[NLAYER]; float *tri, *lt[NLAYER]; micon *ic; } nx2m;   // tri: x0 y0 x1 y1 x2 y2 each
static nx2m g_map; static int g_map_area = -1;   // -1 = none tried; the area whose map is loaded (or failed: ntri = 0)
static int read_f(FILE *f, float **out, uint32_t *n, int per) {
    if (fread(n, 4, 1, f) != 1 || *n > 4000000) return 0;
    *out = malloc((size_t)*n * per * 4 + 4); return *out && fread(*out, 4, (size_t)*n * per, f) == (size_t)*n * per;
}
static void map_load(int area) {
    free(g_map.tri); free(g_map.ic); for (int i = 0; i < NLAYER; i++) free(g_map.lt[i]);
    memset(&g_map, 0, sizeof g_map); g_map_area = area;
    const char *root = getenv("DM_ROOT"); char path[700]; char hdr[8];
    snprintf(path, sizeof path, "%s/companion/%s.nx2m", root ? root : ".", AREA_ID[area]);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("[RED] [companion] Dread: no map %s (run porter dread-companion)", path); return; }
    int ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "NX2M\2\0\0\0", 8) && fread(&g_map.minx, 4, 4, f) == 4 && read_f(f, &g_map.tri, &g_map.ntri, 6);
    for (int i = 0; ok && i < NLAYER; i++) ok = read_f(f, &g_map.lt[i], &g_map.nl[i], 6);
    ok = ok && fread(&g_map.nicon, 4, 1, f) == 1 && g_map.nicon < 100000 && (g_map.ic = malloc(sizeof *g_map.ic * g_map.nicon + 1)) && fread(g_map.ic, sizeof *g_map.ic, g_map.nicon, f) == g_map.nicon;
    fclose(f);
    if (!ok || g_map.maxx <= g_map.minx || g_map.maxy <= g_map.miny) { LOG("[RED] [companion] Dread: bad map %s (an older one? run porter dread-companion again)", path); g_map.ntri = 0; return; }
    LOG("[companion] Dread: map %s: %u triangles, %u icons, x %.0f..%.0f y %.0f..%.0f", AREA_ID[area], g_map.ntri, g_map.nicon, g_map.minx, g_map.maxx, g_map.miny, g_map.maxy);
}

// the game's map icons: companion/icons.nx2i ("NX2I", version 1, count, size; each its ink rect, 4 u16, and size x size RGBA)
enum { I_SAMUS = 0, I_ETANK = 5, I_MTANK = 7 };
static struct { int n, sz, tried; uint16_t *ink; uint32_t *px; } g_ico;
static void icons_load(void) {
    g_ico.tried = 1; const char *root = getenv("DM_ROOT"); char path[700]; uint32_t h[4];
    snprintf(path, sizeof path, "%s/companion/icons.nx2i", root ? root : ".");
    FILE *f = fopen(path, "rb"); if (!f) { LOG("[companion] Dread: no %s: plain shapes for the icons", path); return; }
    if (fread(h, 4, 4, f) == 4 && h[0] == 0x4932584e /* NX2I */ && h[1] == 1 && h[2] && h[2] < 1000 && h[3] && h[3] <= 256) {
        size_t per = (size_t)h[3] * h[3]; g_ico.ink = malloc(8 * h[2]); g_ico.px = malloc(4 * per * h[2]);
        int ok = g_ico.ink && g_ico.px;
        for (uint32_t i = 0; ok && i < h[2]; i++) ok = fread(g_ico.ink + 4 * i, 2, 4, f) == 4 && fread(g_ico.px + per * i, 4, per, f) == per;
        if (ok) { g_ico.n = (int)h[2]; g_ico.sz = (int)h[3]; LOG("[companion] Dread: %d map icons", g_ico.n); }
        else { free(g_ico.ink); free(g_ico.px); g_ico.ink = NULL; g_ico.px = NULL; }
    }
    if (!g_ico.n) LOG("[RED] [companion] Dread: bad %s", path);
    fclose(f);
}
// icon i (its ink rect for a box, the whole cell otherwise) over the page in [x0, x1) x [y0, y1), rows from top on; alpha * a / 256
static void icon_blit(page *p, int i, int box, int flags, float x0, float y0, float x1, float y1, int top, int a) {
    if (i < 0 || i >= g_ico.n || x1 - x0 < 1 || y1 - y0 < 1) return;
    const uint16_t *k = g_ico.ink + 4 * i; int S = g_ico.sz;
    float u0 = box ? k[0] : 0, v0 = box ? k[1] : 0, uw = box ? k[2] - k[0] : S, vh = box ? k[3] - k[1] : S; if (uw < 1) uw = 1; if (vh < 1) vh = 1;
    const uint32_t *src = g_ico.px + (size_t)S * S * i;
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < 0) ix0 = 0; if (iy0 < top) iy0 = top; if (ix1 > p->w) ix1 = p->w; if (iy1 > p->h) iy1 = p->h;
    for (int y = iy0; y < iy1; y++) {
        float fv = (y + 0.5f - y0) / (y1 - y0); if (fv < 0 || fv >= 1) continue; if (flags & F_FLIPY) fv = 1 - fv;
        int v = (int)(v0 + fv * vh); if (v >= S) v = S - 1;
        uint32_t *d = p->px + (size_t)y * p->stride;
        for (int x = ix0; x < ix1; x++) {
            float fu = (x + 0.5f - x0) / (x1 - x0); if (fu < 0 || fu >= 1) continue; if (flags & F_FLIPX) fu = 1 - fu;
            int u = (int)(u0 + fu * uw); if (u >= S) u = S - 1;
            uint32_t c = src[(size_t)v * S + u]; unsigned al = (c >> 24) * (unsigned)a >> 8; if (!al) continue;
            uint32_t o = d[x], r = 0xff000000u;
            for (int sh = 0; sh < 24; sh += 8) r |= ((((c >> sh) & 255) * al + ((o >> sh) & 255) * (255 - al)) / 255) << sh;
            d[x] = r;
        }
    }
}

// ---- what Samus has seen: a grid over the area, revealed around her as she goes ----
// The game keeps its own map's explored rooms in the save, which its scripts cannot read: the page reveals the map itself, REVEAL world units around
// Samus (DM_COMPANION_REVEAL in env.txt; about a screen of the game), in cells of CELL units. Kept per area in companion/seen_<area>.bin of the data folder.
#define CELL 100.0f
static struct { int area, gw, gh, dirty; uint8_t *bit; int64_t saved; } g_seen = {.area = -1};
static void seen_path(char *b, size_t n, int area) { const char *root = getenv("DM_ROOT"); snprintf(b, n, "%s/companion/seen_%s.bin", root ? root : ".", AREA_ID[area]); }
static void seen_save(void) {
    if (g_seen.area <= 0 || !g_seen.bit || !g_seen.dirty) return;
    char p[700], t[710]; seen_path(p, sizeof p, g_seen.area); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) { LOG("[RED] [companion] Dread: cannot write %s", t); g_seen.dirty = 0; return; }
    uint32_t h[3] = {0x53324e4e /* NN2S */, (uint32_t)g_seen.gw, (uint32_t)g_seen.gh}; size_t n = ((size_t)g_seen.gw * g_seen.gh + 7) / 8;
    int ok = fwrite(h, 4, 3, f) == 3 && fwrite(g_seen.bit, 1, n, f) == n; ok = !fclose(f) && ok;
    if (ok) rename(t, p); else remove(t);
    g_seen.dirty = 0; g_seen.saved = (int64_t)now_ns();
}
static void seen_load(int area) {   // the map of the area is loaded (g_map)
    seen_save(); free(g_seen.bit); memset(&g_seen, 0, sizeof g_seen); g_seen.area = area;
    g_seen.gw = (int)((g_map.maxx - g_map.minx) / CELL) + 2; g_seen.gh = (int)((g_map.maxy - g_map.miny) / CELL) + 2;
    size_t n = ((size_t)g_seen.gw * g_seen.gh + 7) / 8; g_seen.bit = calloc(1, n); if (!g_seen.bit) return;
    char p[700]; seen_path(p, sizeof p, area); FILE *f = fopen(p, "rb"); uint32_t h[3];
    if (f && fread(h, 4, 3, f) == 3 && h[0] == 0x53324e4e && h[1] == (uint32_t)g_seen.gw && h[2] == (uint32_t)g_seen.gh && fread(g_seen.bit, 1, n, f) == n) LOG("[companion] Dread: %s's explored map loaded", AREA_ID[area]);
    if (f) fclose(f);
}
static void seen_reveal(float x, float y) {
    static float reveal; if (!reveal) { const char *e = getenv("DM_COMPANION_REVEAL"); reveal = e && atof(e) > 0 ? (float)atof(e) : 1200; }
    int r = (int)(reveal / CELL), cx = (int)((x - g_map.minx) / CELL), cy = (int)((y - g_map.miny) / CELL);
    for (int j = cy - r; j <= cy + r; j++) for (int i = cx - r; i <= cx + r; i++) {
        if (i < 0 || j < 0 || i >= g_seen.gw || j >= g_seen.gh || (i - cx) * (i - cx) + (j - cy) * (j - cy) > r * r) continue;
        size_t k = (size_t)j * g_seen.gw + i; if (!(g_seen.bit[k >> 3] >> (k & 7) & 1)) { g_seen.bit[k >> 3] |= (uint8_t)(1 << (k & 7)); g_seen.dirty = 1; }
    }
}

// ---- the view: the whole area (zoom 1), or closer, following Samus or where a finger moved it ----
// Double tap: closer on Samus (x3), or the whole area again. Two fingers: zoom. One finger: move the map (it stops following Samus until a double tap).
static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;
static float g_zoom = 1, g_vcx, g_vcy; static int g_follow = 1;   // the view (g_vmu): world point at the middle of the map's part of the page
static float g_k0, g_scx, g_scy;   // the last drawn page: zoom-1 scale, the screen middle of the map's part (g_vmu)
static struct { int64_t down_t, tap_t; float dx, dy, tx, ty, lx, ly, d0, z0, wx, wy; int moved, pinch; } g_g;
static float fdist(const float *x, const float *y) { return sqrtf((x[1] - x[0]) * (x[1] - x[0]) + (y[1] - y[0]) * (y[1] - y[0])); }
void dm_dread_touch(int act, int idx, int n, const float *x, const float *y) {
    (void)idx; if (!dm_dread_active()) return;
    int64_t now = (int64_t)now_ns(); float slop = 24;
    pthread_mutex_lock(&g_vmu);
    float k = g_k0 * g_zoom;
    if (act == 0) {   // ACTION_DOWN
        g_g.down_t = now; g_g.dx = g_g.lx = x[0]; g_g.dy = g_g.ly = y[0]; g_g.moved = g_g.pinch = 0;
        if (now - g_g.tap_t < 350000000 && fabsf(x[0] - g_g.tx) < 3 * slop && fabsf(y[0] - g_g.ty) < 3 * slop) {   // a double tap
            if (g_zoom > 1.05f) { g_zoom = 1; g_follow = 1; } else { g_zoom = 3; g_follow = 1; }
            g_g.tap_t = 0; g_g.moved = 1;
        }
    } else if (act == 5 && n >= 2 && k > 0) {   // ACTION_POINTER_DOWN: a pinch starts; the world point under the fingers' middle stays under it
        g_g.pinch = 1; g_g.moved = 1; g_g.d0 = fmaxf(fdist(x, y), 1); g_g.z0 = g_zoom;
        float mx = (x[0] + x[1]) / 2, my = (y[0] + y[1]) / 2; g_g.wx = g_vcx + (mx - g_scx) / k; g_g.wy = g_vcy - (my - g_scy) / k;
    } else if (act == 2 && k > 0) {   // ACTION_MOVE
        if (g_g.pinch && n >= 2) {
            g_zoom = fminf(fmaxf(g_g.z0 * fdist(x, y) / g_g.d0, 1), 16); float k1 = g_k0 * g_zoom, mx = (x[0] + x[1]) / 2, my = (y[0] + y[1]) / 2;
            g_vcx = g_g.wx - (mx - g_scx) / k1; g_vcy = g_g.wy + (my - g_scy) / k1; g_follow = 0;
        } else if (!g_g.pinch && n == 1) {
            if (fabsf(x[0] - g_g.dx) > slop || fabsf(y[0] - g_g.dy) > slop) g_g.moved = 1;
            if (g_g.moved && g_zoom > 1.05f) { g_vcx -= (x[0] - g_g.lx) / k; g_vcy += (y[0] - g_g.ly) / k; g_follow = 0; }
            g_g.lx = x[0]; g_g.ly = y[0];
        }
    } else if (act == 1) {   // ACTION_UP: a short touch that did not move is a tap
        if (!g_g.moved && now - g_g.down_t < 300000000) { g_g.tap_t = now; g_g.tx = x[0]; g_g.ty = y[0]; }
    }
    if (g_zoom <= 1.05f && act != 2) { g_zoom = 1; g_follow = 1; }
    pthread_mutex_unlock(&g_vmu);
}

// the picture of the map for the view, cached while the view stays: a class per pixel (the look of eden-duo-companions' Dread map: the game's
// rooms dark blue with grey edges, walked ones lighter, the water, heat, cold and EMMI rooms tinted), coloured each pass by what is explored
enum { K_NONE, K_ROOM, K_EDGE, K_WATER, K_HEAT, K_FREEZE, K_EMMY, NK };
static uint8_t *g_pic; static int g_pic_w, g_pic_h, g_pic_area = -1, g_top;
static float g_k, g_cx, g_cy, g_pk, g_pcx, g_pcy;   // screen = (g_scx + (x - g_cx) * g_k, g_scy - (y - g_cy) * g_k); g_p*: the picture's
static int *g_colc, *g_rowc; static int g_colc_n, g_rowc_n;   // each screen column's and row's grid cell, for the fog
#define RGB(r, g, b) (0xff000000u | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))
#define C_BG    RGB(0x0b, 0x0f, 0x14)
#define C_LINE  RGB(0x8e, 0x8e, 0x8e)
#define C_DOOR  RGB(0xe8, 0xe0, 0x30)
#define C_ITEM  RGB(0xff, 0xc8, 0x40)
static uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0xff000000u;
    for (int sh = 0; sh < 24; sh += 8) r |= (uint32_t)lrintf(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t) << sh;
    return r;
}
static uint32_t g_pal[2][NK];   // [walked][class]: walked, or only seen (as the game's map shows a room a map station revealed)
static void pal_init(void) {
    static const uint32_t layer[] = {RGB(0x0a, 0x5f, 0x65), RGB(0xa6, 0x0c, 0x09), RGB(0x0a, 0x12, 0x9f), RGB(0x8e, 0x14, 0x14)};
    uint32_t room = mix(C_BG, RGB(0x00, 0x13, 0x30), 0.7f), walked = mix(room, RGB(0x16, 0x4c, 0x3c), 112 / 255.0f);
    g_pal[0][K_NONE] = g_pal[1][K_NONE] = C_BG;
    g_pal[0][K_ROOM] = room; g_pal[1][K_ROOM] = walked;
    g_pal[0][K_EDGE] = mix(C_BG, C_LINE, 0.55f); g_pal[1][K_EDGE] = C_LINE;
    for (int i = 0; i < NLAYER; i++) { g_pal[0][K_WATER + i] = mix(room, layer[i], 0.35f); g_pal[1][K_WATER + i] = mix(walked, layer[i], 0.55f); }
}
static void sxy(float x, float y, float *sx, float *sy) { *sx = g_scx + (x - g_cx) * g_k; *sy = g_scy - (y - g_cy) * g_k; }
static void tri_fill(uint8_t *px, int w, int h, int y0c, float x0, float y0, float x1, float y1, float x2, float y2, uint8_t c, int only_room) {
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0); if (area == 0) return;
    if (area < 0) { float tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))), bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))), by0 = (int)floorf(fminf(y0, fminf(y1, y2))), by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (bx0 < 0) bx0 = 0; if (by0 < y0c) by0 = y0c; if (bx1 > w) bx1 = w; if (by1 > h) by1 = h;
    for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) {
        float px_ = x + 0.5f, py = y + 0.5f;
        if ((x1 - x0) * (py - y0) - (y1 - y0) * (px_ - x0) >= 0 && (x2 - x1) * (py - y1) - (y2 - y1) * (px_ - x1) >= 0 && (x0 - x2) * (py - y2) - (y0 - y2) * (px_ - x2) >= 0) {
            uint8_t *d = px + (size_t)y * w + x; if (!only_room || *d) *d = c;
        }
    }
}
static void tris_fill(const float *t, uint32_t n, int w, int h, int top, uint8_t c, int only_room) {
    for (uint32_t i = 0; i < n; i++, t += 6) {
        float a[6]; sxy(t[0], t[1], &a[0], &a[1]); sxy(t[2], t[3], &a[2], &a[3]); sxy(t[4], t[5], &a[4], &a[5]);
        if (fmaxf(a[0], fmaxf(a[2], a[4])) < 0 || fminf(a[0], fminf(a[2], a[4])) > w || fmaxf(a[1], fmaxf(a[3], a[5])) < top || fminf(a[1], fminf(a[3], a[5])) > h) continue;
        tri_fill(g_pic, w, h, top, a[0], a[1], a[2], a[3], a[4], a[5], c, only_room);
    }
}
static void pic_make(int w, int h, int top, int area) {
    if (g_pic_w != w || g_pic_h != h || !g_pic) { free(g_pic); g_pic = malloc((size_t)w * h); }
    g_pic_w = w; g_pic_h = h; g_pic_area = area; g_top = top; g_pk = g_k; g_pcx = g_cx; g_pcy = g_cy;
    if (!g_pic) return;
    memset(g_pic, K_NONE, (size_t)w * h);
    tris_fill(g_map.tri, g_map.ntri, w, h, top, K_ROOM, 0);
    static const uint8_t order[] = {L_EMMY, L_WATER, L_HEAT, L_FREEZE};   // the later on top
    for (int i = 0; i < NLAYER; i++) tris_fill(g_map.lt[order[i]], g_map.nl[order[i]], w, h, top, (uint8_t)(K_WATER + order[i]), 1);
    for (int y = top; y < h; y++) for (int x = 0; x < w; x++) {   // the rooms' edges: a room pixel beside an empty one
        uint8_t *c = g_pic + (size_t)y * w + x; if (*c == K_NONE || *c == K_EDGE) continue;
        if (x == 0 || y == top || x == w - 1 || y == h - 1) continue;
        if (!c[-1] || !c[1] || !c[-w] || !c[w]) *c = K_EDGE;
    }
}
static void disc(page *p, float cx, float cy, float r, uint32_t c) {
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) for (int x = (int)(cx - r); x <= (int)(cx + r); x++)
        if ((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy) <= r * r && x >= 0 && y >= 0 && x < p->w && y < p->h) p->px[(size_t)y * p->stride + x] = c;
}
static int seen_at(float wx, float wy) {   // the page's own fog: Samus has been near
    int c = (int)((wx - g_map.minx) / CELL), r = (int)((wy - g_map.miny) / CELL);
    if (!g_seen.bit || c < 0 || r < 0 || c >= g_seen.gw || r >= g_seen.gh) return 0;
    size_t k = (size_t)r * g_seen.gw + c; return g_seen.bit[k >> 3] >> (k & 7) & 1;
}
// zoom 1 frames what is explored (with a margin, at least MINVIEW units across), not the whole area: an area is up to 60000 units wide, and early on
// the explored part of it would be a strip a few pixels high. Taken again every 2 s, from the game's grid or the page's own fog.
#define MINVIEW 9000.0f
static float g_ex[4]; static int g_exarea; static int64_t g_ext;   // the explored part's bounds, of g_exarea, at g_ext
static int grid_state(float wx, float wy);
static void explored(int game, int area) {
    int64_t now = (int64_t)now_ns(); if (g_exarea == area && now - g_ext < 2000000000) return;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (float y = g_map.miny; y <= g_map.maxy; y += CELL) for (float x = g_map.minx; x <= g_map.maxx; x += CELL) {
        if (game ? grid_state(x, y) <= 1 : !seen_at(x, y)) continue;
        x0 = fminf(x0, x); y0 = fminf(y0, y); x1 = fmaxf(x1, x + CELL); y1 = fmaxf(y1, y + CELL);
    }
    float sx = fmaxf(fminf(g_st.x, g_map.maxx), g_map.minx), sy = fmaxf(fminf(g_st.y, g_map.maxy), g_map.miny);   // Samus is always in it
    x0 = fminf(x0, sx); y0 = fminf(y0, sy); x1 = fmaxf(x1, sx); y1 = fmaxf(y1, sy);
    float m = 600, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = fmaxf((x1 - x0) / 2 + m, MINVIEW / 2), hh = fmaxf((y1 - y0) / 2 + m, MINVIEW / 4);
    pthread_mutex_lock(&g_vmu); g_ex[0] = cx - hw; g_ex[1] = cy - hh; g_ex[2] = cx + hw; g_ex[3] = cy + hh; g_exarea = area; g_ext = now; pthread_mutex_unlock(&g_vmu);
}
static void view_set(int w, int h, int top) {   // g_k, g_cx, g_cy for this pass, from the view the touches left
    pthread_mutex_lock(&g_vmu);
    float m = h / 40.0f, aw = w - 2 * m, ah = h - top - 2 * m, mw = g_ex[2] - g_ex[0], mh = g_ex[3] - g_ex[1];
    g_k0 = fminf(aw / mw, ah / mh); g_scx = w / 2.0f; g_scy = top + (h - top) / 2.0f;
    float midx = (g_ex[0] + g_ex[2]) / 2, midy = (g_ex[1] + g_ex[3]) / 2;
    if (g_zoom <= 1.05f) { g_vcx = midx; g_vcy = midy; }
    else if (g_follow) {   // keep Samus in the middle third: the picture is made again only when she leaves it
        float k = g_k0 * g_zoom, hx = (w / 2.0f) / k / 3, hy = ((h - top) / 2.0f) / k / 3;
        if (fabsf(g_st.x - g_vcx) > hx || fabsf(g_st.y - g_vcy) > hy) { g_vcx = g_st.x; g_vcy = g_st.y; }
    }
    float k = g_k0 * g_zoom, hw = (w / 2.0f) / k, hh = ((h - top) / 2.0f) / k;   // no further than the area's edge
    if (g_zoom > 1.05f) { g_vcx = fminf(fmaxf(g_vcx, g_map.minx + hw * 0.5f), g_map.maxx - hw * 0.5f); g_vcy = fminf(fmaxf(g_vcy, g_map.miny + hh * 0.5f), g_map.maxy - hh * 0.5f); }
    g_k = k; g_cx = g_vcx; g_cy = g_vcy;
    pthread_mutex_unlock(&g_vmu);
}
// ---- the game's own explored map ----
// Dread keeps what the player has explored in its minimap grid (CMinimapGrid): 650x300 cells of 0x48 bytes, 100 world units square, inline at
// grid+0x118; each cell holds its world x, y (floats at +0, +4) and its state (+8: 0 unknown, 1 a fringe the game hides, 2..5 seen, 6 walked).
// The grid's current scenario name: *(*(grid+0x110)+0x10). The layout comes from igawa6's eden-duo-companions (GPL-3.0, Metroid Dread module).
// The array is found by its shape in the guest heap (a slice per pass), kept while it keeps the shape, and drawn instead of the page's own fog;
// the page's fog (above) is the fallback while it is not found.
#define GCOLS 650
#define GROWS 300
#define GSTRIDE 0x48
#define GROWB ((size_t)GCOLS * GSTRIDE)
#define GBYTES ((size_t)GROWS * GROWB)
static const uint8_t *g_cells; static float g_gx0, g_gy0, g_gdy; static int g_garea;   // cell 0, its world x/y, +-100 per row, the area it is of
static uint8_t *g_hb; static size_t g_hn;   // the guest heap
static struct { uintptr_t lo, hi; } g_rng[256]; static int g_nrng, g_ri; static uintptr_t g_rcur; static int64_t g_gnext;
static int in_heap(const void *p, size_t n) { uintptr_t a = (uintptr_t)p; return g_hb && a >= (uintptr_t)g_hb && a + n <= (uintptr_t)g_hb + g_hn; }
static int in_rng(const void *p, size_t n) { uintptr_t a = (uintptr_t)p; for (int i = 0; i < g_nrng; i++) if (a >= g_rng[i].lo && a + n <= g_rng[i].hi) return 1; return 0; }
static int cell_at(const uint8_t *c, float *x, float *y) {
    if (c[8] > 6 || c[0x38] > 1) return 0;
    memcpy(x, c, 4); memcpy(y, c + 4, 4); return isfinite(*x) && isfinite(*y) && fabsf(*x) <= 1e6f && fabsf(*y) <= 1e6f;
}
static int near1(float a, float b) { return fabsf(a - b) < 0.5f; }
static int grid_area(const uint8_t *cells) {   // the area the grid is of, from its scenario name (0: unreadable)
    const uint8_t *grid = cells - 0x118; uint64_t sid, nm;
    memcpy(&sid, grid + 0x110, 8); if (!in_heap((void *)(uintptr_t)sid, 0x18)) return 0;
    memcpy(&nm, (const uint8_t *)(uintptr_t)sid + 0x10, 8);
    const char *n = (const char *)(uintptr_t)nm; if (!in_heap(n, 16) && !in_rng(n, 16)) return 0;
    for (int a = 1; a < NAREA; a++) if (!strncmp(n, AREA_ID[a], strlen(AREA_ID[a])) && !n[strlen(AREA_ID[a])]) return a;
    return 0;
}
static int grid_check(const uint8_t *b) {   // b is cell 0: the corners keep the grid's shape
    float x0, y0, x1, y1, xe, ye, xr, yr;
    if (!in_rng(b - 0x118, GBYTES + 0x118)) return 0;
    if (!cell_at(b, &x0, &y0) || !cell_at(b + GSTRIDE, &x1, &y1) || !cell_at(b + (GCOLS - 1) * GSTRIDE, &xe, &ye) || !cell_at(b + GROWB, &xr, &yr)) return 0;
    return near1(x1, x0 + 100) && near1(y1, y0) && near1(xe, x0 + 100 * (GCOLS - 1)) && near1(ye, y0) && near1(xr, x0) && near1(fabsf(yr - y0), 100);
}
static void grid_ranges(void) {   // the readable mappings of the guest heap
    g_nrng = 0; FILE *f = fopen("/proc/self/maps", "r"); char line[512]; if (!f) return;
    uintptr_t lo = (uintptr_t)g_hb, hi = lo + g_hn;
    while (fgets(line, sizeof line, f) && g_nrng < 256) {
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3 || perm[0] != 'r' || e <= lo || s >= hi) continue;
        if (g_nrng && g_rng[g_nrng - 1].hi == (s > lo ? s : lo)) g_rng[g_nrng - 1].hi = e < hi ? e : hi;   // adjacent mappings as one
        else { g_rng[g_nrng].lo = s > lo ? s : lo; g_rng[g_nrng].hi = e < hi ? e : hi; g_nrng++; }
    }
    fclose(f); g_ri = 0; g_rcur = g_nrng ? g_rng[0].lo : 0;
}
static const uint8_t *grid_walk(const uint8_t *p) {   // p and the cell after it are a row's cells: back to cell 0, checked
    float x, y, bx, by; int run = 1; const uint8_t *c = p;
    if (!cell_at(c, &x, &y)) return NULL;
    for (; run < 128; run++, c += GSTRIDE) { float nx, ny; if (!in_rng(c + GSTRIDE, 0x40) || !cell_at(c + GSTRIDE, &nx, &ny) || !near1(nx, x + 100) || !near1(ny, y)) break; x = nx; y = ny; }
    if (run < 128) return NULL;   // 128 cells 100 apart are not chance
    c = p; cell_at(c, &x, &y);
    for (int i = 0; i < GCOLS && in_rng(c - GSTRIDE, 0x40) && cell_at(c - GSTRIDE, &bx, &by) && near1(bx, x - 100) && near1(by, y); i++) { c -= GSTRIDE; x = bx; }
    cell_at(c, &x, &y);
    for (int i = 0; i < GROWS && in_rng(c - GROWB, 0x40) && cell_at(c - GROWB, &bx, &by) && near1(bx, x) && near1(fabsf(by - y), 100); i++) { c -= GROWB; y = by; }
    return grid_check(c) ? c : NULL;
}
static void grid_tick(void) {   // the companion thread, each pass
    int64_t now = (int64_t)now_ns();
    if (!g_hb) { svc_heap_range(&g_hb, &g_hn); if (!g_hb) return; }
    if (g_cells) {
        if (grid_check(g_cells)) {
            int a = grid_area(g_cells); static int said;
            if (!a && !said++) LOG("[companion] Dread: the minimap grid's area name is not readable: taken as Samus's area");
            g_garea = a ? a : g_st.area; return;
        }
        LOG("[companion] Dread: the game's minimap grid went"); g_cells = NULL; g_garea = 0; g_nrng = 0;
    }
    if (now < g_gnext) return;
    if (!g_nrng) { grid_ranges(); if (!g_nrng) { g_gnext = now + 2000000000; return; } }
    size_t budget = 48u << 20;   // bytes looked at per pass
    while (budget && g_ri < g_nrng) {
        uintptr_t hi = g_rng[g_ri].hi; if (g_rcur < g_rng[g_ri].lo) g_rcur = (g_rng[g_ri].lo + 7) & ~(uintptr_t)7;
        uintptr_t end = hi - 0x48 - 0x40 < g_rcur + budget ? hi - 0x48 - 0x40 : g_rcur + budget;
        for (uintptr_t a = g_rcur; a < end; a += 8) {
            const uint8_t *c = (const uint8_t *)a; float x0, y0, x1, y1;
            if (c[8] > 6 || c[0x38] > 1 || c[8 + GSTRIDE] > 6) continue;
            memcpy(&x0, c, 4); memcpy(&x1, c + GSTRIDE, 4); if (!(x1 - x0 > 99.5f && x1 - x0 < 100.5f)) continue;
            memcpy(&y0, c + 4, 4); memcpy(&y1, c + GSTRIDE + 4, 4); if (!near1(y0, y1)) continue;
            const uint8_t *b = grid_walk(c); if (!b) continue;
            float bx, by, rx, ry; cell_at(b, &bx, &by); cell_at(b + GROWB, &rx, &ry);
            g_cells = b; g_gx0 = bx; g_gy0 = by; g_gdy = ry - by; g_garea = grid_area(b); if (!g_garea) g_garea = g_st.area;
            LOG("[companion] Dread: the game's minimap grid at %p (cell 0 at %.0f, %.0f, rows %+.0f), area %s", (void *)b, bx, by, g_gdy, g_garea ? AREA_ID[g_garea] : "?");
            return;
        }
        budget -= end > g_rcur ? end - g_rcur : 0; g_rcur = end;
        if (end >= hi - 0x48 - 0x40 || end < g_rcur) { g_ri++; g_rcur = g_ri < g_nrng ? g_rng[g_ri].lo : 0; }
    }
    if (g_ri >= g_nrng) { g_nrng = 0; g_gnext = now + 3000000000; }   // a whole sweep found none: again in 3 s, from a fresh map of the heap
}
static int grid_state(float wx, float wy) {   // the game's state of the cell at a world point (-1: outside the grid)
    int c = (int)floorf((wx - g_gx0) / 100), r = (int)floorf((wy - g_gy0) / g_gdy);
    if (c < 0 || r < 0 || c >= GCOLS || r >= GROWS) return -1;
    return g_cells[(size_t)r * GROWB + (size_t)c * GSTRIDE + 8];
}

// the header: the area's name over a line, energy and missiles under it with the game's tank icons
static int head_h(int s) { int s2 = s * 4 / 7 < 2 ? 2 : s * 4 / 7, m = 3 * s; return m + 7 * s + m + 7 * s2 + 4 + m; }
static void head_draw(page *p, int s, int area) {
    int s2 = s * 4 / 7 < 2 ? 2 : s * 4 / 7, m = 3 * s, h = head_h(s), cx = p->w / 2;
    fill(p, 0, 0, p->w, h, C_BG);
    char up[32]; const char *n = AREA_NAME[area]; size_t i = 0; for (; n[i] && i < sizeof up - 1; i++) up[i] = (char)(n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]); up[i] = 0;
    int tw = (int)strlen(up) * 6 * s - s; text(p, cx - tw / 2, m, s, 0xffffffff, up);
    int ly = m + 7 * s + m / 2, half = tw / 2 + 6 * s, t = s / 3 < 1 ? 1 : s / 3;   // the line, its ends bent up
    fill(p, cx - half, ly, cx + half, ly + t, C_LINE);
    fill(p, cx - half - 4 * s, ly - 2 * s, cx - half, ly - 2 * s + t, C_LINE); fill(p, cx - half - t, ly - 2 * s, cx - half, ly + t, C_LINE);
    fill(p, cx + half, ly - 2 * s, cx + half + 4 * s, ly - 2 * s + t, C_LINE); fill(p, cx + half, ly - 2 * s, cx + half + t, ly + t, C_LINE);
    char e[24], mi[24]; snprintf(e, sizeof e, "%d/%d", g_st.life, g_st.lifemax); snprintf(mi, sizeof mi, "%d/%d", g_st.mis, g_st.mismax);
    int ic = 7 * s2 + 6 * s2, gap = 4 * s, ew = ic + s2 + (int)strlen(e) * 6 * s2, mw = ic + s2 + (int)strlen(mi) * 6 * s2;
    int x = cx - (ew + 3 * gap + mw) / 2, y = ly + m / 2 + 4, iy = y + 7 * s2 / 2;
    if (g_ico.n > I_MTANK) icon_blit(p, I_ETANK, 0, 0, x, iy - ic / 2, x + ic, iy + ic / 2, 0, 256); else fill(p, x + ic / 4, y, x + ic * 3 / 4, y + 7 * s2, RGB(0xd2, 0x3c, 0xbe));
    text(p, x + ic + s2, y, s2, RGB(0xe0, 0xe0, 0xe0), e); x += ew + 3 * gap;
    if (g_ico.n > I_MTANK) icon_blit(p, I_MTANK, 0, 0, x, iy - ic / 2, x + ic, iy + ic / 2, 0, 256); else fill(p, x + ic / 4, y, x + ic * 3 / 4, y + 7 * s2, RGB(0xe0, 0x30, 0x30));
    text(p, x + ic + s2, y, s2, RGB(0xe0, 0xe0, 0xe0), mi);
    fill(p, 0, h - 1, p->w, h, mix(C_BG, C_LINE, 0.35f));
}

// the icons where the map is explored: doors and blockages over their box, the rest a square that grows a little with the zoom
static void icons_draw(page *p, int top, int game) {
    float base = fminf(fmaxf(160 * g_k, p->h / 45.0f), p->h / 12.0f), mn = fmaxf(3, p->h / 160.0f);
    for (uint32_t i = 0; i < g_map.nicon; i++) {
        const micon *c = &g_map.ic[i];
        if (game ? grid_state(c->x, c->y) <= 1 : !seen_at(c->x, c->y)) continue;
        float x0, y0, x1, y1;
        if (c->flags & F_BOX) {
            sxy(c->b[0], c->b[3], &x0, &y0); sxy(c->b[2], c->b[1], &x1, &y1);
            if (x1 - x0 < mn) { float m = (x0 + x1) / 2; x0 = m - mn / 2; x1 = m + mn / 2; }
            if (y1 - y0 < mn) { float m = (y0 + y1) / 2; y0 = m - mn / 2; y1 = m + mn / 2; }
        } else { float cx, cy; sxy(c->x, c->y, &cx, &cy); x0 = cx - base / 2; y0 = cy - base / 2; x1 = x0 + base; y1 = y0 + base; }
        if (x1 < 0 || x0 > p->w || y1 < top || y0 > p->h) continue;
        if (g_ico.n) icon_blit(p, c->icon, c->flags & F_BOX, c->flags, x0, y0, x1, y1, top, 256);
        else if (c->flags & (F_DOOR | F_ITEM)) { int b = (int)fmaxf(2, (x1 - x0) / 4); page q = *p; fill(&q, (int)x0 + b, (int)fmaxf(y0 + b, top), (int)x1 - b, (int)y1 - b, c->flags & F_DOOR ? C_DOOR : C_ITEM); }
    }
}

// before a save is loaded: the game's logo over Artaria's illustration, darkened (companion/title.nx2t: "NX2T", version 1, the illustration's w, h
// and RGBA, the logo's w, h and RGBA), composed once for the page's size
static struct { int tried, bw, bh, lw, lh, pw, ph; uint32_t *bg, *logo, *pic; } g_ti;
static uint32_t *rgba_read(FILE *f, int *w, int *h) {
    uint32_t d[2]; if (fread(d, 4, 2, f) != 2 || !d[0] || !d[1] || d[0] > 4096 || d[1] > 4096) return NULL;
    uint32_t *px = malloc((size_t)d[0] * d[1] * 4); if (!px) return NULL;
    if (fread(px, 4, (size_t)d[0] * d[1], f) != (size_t)d[0] * d[1]) { free(px); return NULL; }
    *w = (int)d[0]; *h = (int)d[1]; return px;
}
static void title_load(void) {
    g_ti.tried = 1; const char *root = getenv("DM_ROOT"); char path[700]; uint32_t hd[2];
    snprintf(path, sizeof path, "%s/companion/title.nx2t", root ? root : ".");
    FILE *f = fopen(path, "rb"); if (!f) { LOG("[companion] Dread: no %s: text before a save is loaded", path); return; }
    if (fread(hd, 4, 2, f) == 2 && hd[0] == 0x5432584e /* NX2T */ && hd[1] == 1 && (g_ti.bg = rgba_read(f, &g_ti.bw, &g_ti.bh)) && (g_ti.logo = rgba_read(f, &g_ti.lw, &g_ti.lh)))
        LOG("[companion] Dread: title page %dx%d, logo %dx%d", g_ti.bw, g_ti.bh, g_ti.lw, g_ti.lh);
    else { LOG("[RED] [companion] Dread: bad %s", path); free(g_ti.bg); g_ti.bg = NULL; }
    fclose(f);
}
static int title_draw(page *p) {
    if (!g_ti.tried) title_load();
    if (!g_ti.bg || !g_ti.logo) return 0;
    if (!g_ti.pic || g_ti.pw != p->w || g_ti.ph != p->h) {
        free(g_ti.pic); g_ti.pic = malloc((size_t)p->w * p->h * 4); g_ti.pw = p->w; g_ti.ph = p->h; if (!g_ti.pic) return 0;
        float k = fmaxf((float)p->w / g_ti.bw, (float)p->h / g_ti.bh), ox = (g_ti.bw - p->w / k) / 2, oy = (g_ti.bh - p->h / k) / 2;   // the illustration covers the page
        for (int y = 0; y < p->h; y++) {
            float dk = 0.42f * (1 - 0.6f * fabsf((float)y / p->h - 0.4f));   // darker at the top and the bottom
            int sy = (int)(oy + y / k); if (sy < 0) sy = 0; if (sy >= g_ti.bh) sy = g_ti.bh - 1;
            for (int x = 0; x < p->w; x++) {
                int sx = (int)(ox + x / k); if (sx < 0) sx = 0; if (sx >= g_ti.bw) sx = g_ti.bw - 1;
                uint32_t c = g_ti.bg[(size_t)sy * g_ti.bw + sx], r = 0xff000000u;
                for (int sh = 0; sh < 24; sh += 8) r |= (uint32_t)(((c >> sh) & 255) * dk) << sh;
                g_ti.pic[(size_t)y * p->w + x] = r;
            }
        }
        float lw = p->w * 0.72f, lh = lw * g_ti.lh / g_ti.lw, lx = (p->w - lw) / 2, ly = p->h * 0.36f - lh / 2;   // the logo
        for (int y = (int)ly; y < (int)(ly + lh); y++) {
            if (y < 0 || y >= p->h) continue;
            int sy = (int)((y - ly) / lh * g_ti.lh); if (sy >= g_ti.lh) sy = g_ti.lh - 1;
            for (int x = (int)lx; x < (int)(lx + lw); x++) {
                if (x < 0 || x >= p->w) continue;
                int sx = (int)((x - lx) / lw * g_ti.lw); if (sx >= g_ti.lw) sx = g_ti.lw - 1;
                uint32_t c = g_ti.logo[(size_t)sy * g_ti.lw + sx], *d = &g_ti.pic[(size_t)y * p->w + x], o = *d, r = 0xff000000u; unsigned a = c >> 24;
                for (int sh = 0; sh < 24; sh += 8) r |= ((((c >> sh) & 255) * a + ((o >> sh) & 255) * (255 - a)) / 255) << sh;
                *d = r;
            }
        }
    }
    for (int y = 0; y < p->h; y++) memcpy(p->px + (size_t)y * p->stride, g_ti.pic + (size_t)y * p->w, (size_t)p->w * 4);
    return 1;
}

int dm_dread_draw(page *p) {
    if (!dm_dread_active()) return 0;
    static int pal; if (!pal) { pal_init(); pal = 1; }
    if (!g_ico.tried) icons_load();
    int s = p->w / (6 * 28 + 6); if (s > p->h / (11 * 16)) s = p->h / (11 * 16); if (s < 2) s = 2;   // as companion_android.c's page
    int m = 3 * s, top = head_h(s);
    int area = g_tab && g_st.area > 0 && g_st.area < NAREA ? g_st.area : 0;
    if (!area) {
        if (title_draw(p)) return 1;
        fill(p, 0, 0, p->w, p->h, C_BG); text(p, m, m, s, 0xffffffff, "METROID DREAD"); fill(p, 0, 2 * m + 7 * s - 1, p->w, 2 * m + 7 * s, mix(C_BG, C_LINE, 0.35f));
        text(p, m, 2 * m + 7 * s + 2 * m, s, 0xffb0b0b0, g_ncand ? "Buscando a Samus..." : "El mapa sale al entrar en una zona");
        return 1;
    }
    if (area != g_map_area) { map_load(area); if (g_map.ntri) seen_load(area); }
    if (g_map.ntri) {
        if (g_seen.area == area && g_seen.bit && !(g_cells && g_garea == area)) { seen_reveal(g_st.x, g_st.y); if (g_seen.dirty && (int64_t)now_ns() - g_seen.saved > 10000000000LL) seen_save(); }
        explored(g_cells && g_garea == area, area); view_set(p->w, p->h, top);
        if (g_pic_area != area || g_pic_w != p->w || g_pic_h != p->h || g_top != top || g_pk != g_k || g_pcx != g_cx || g_pcy != g_cy) pic_make(p->w, p->h, top, area);
    }
    int game = g_map.ntri && g_pic && g_cells && g_garea == area;   // the game's own explored map; else the page's own fog
    if (g_map.ntri && g_pic && (game || g_seen.bit)) {
        if (g_colc_n != p->w) { free(g_colc); g_colc = malloc(sizeof *g_colc * p->w); g_colc_n = g_colc ? p->w : 0; }
        if (g_rowc_n != p->h) { free(g_rowc); g_rowc = malloc(sizeof *g_rowc * p->h); g_rowc_n = g_rowc ? p->h : 0; }
        if (g_colc && g_rowc) {
            float ox = game ? g_gx0 : g_map.minx, oy = game ? g_gy0 : g_map.miny, dy = game ? g_gdy : CELL; int nc = game ? GCOLS : g_seen.gw, nr = game ? GROWS : g_seen.gh;
            for (int x = 0; x < p->w; x++) { int c = (int)floorf((g_cx + (x + 0.5f - g_scx) / g_k - ox) / 100); g_colc[x] = c >= 0 && c < nc ? c : -1; }
            for (int y = 0; y < p->h; y++) { int r = (int)floorf((g_cy - (y + 0.5f - g_scy) / g_k - oy) / dy); g_rowc[y] = y >= top && r >= 0 && r < nr ? r : -1; }
            for (int y = top; y < p->h; y++) {
                uint32_t *d = p->px + (size_t)y * p->stride; const uint8_t *src = g_pic + (size_t)y * p->w; int r = g_rowc[y];
                if (r < 0) { for (int x = 0; x < p->w; x++) d[x] = C_BG; continue; }
                if (game) {   // walked cells as walked, seen ones as seen, the rest not drawn
                    const uint8_t *row = g_cells + (size_t)r * GROWB + 8;
                    for (int x = 0; x < p->w; x++) { int c = g_colc[x]; uint8_t st = c >= 0 ? row[(size_t)c * GSTRIDE] : 0; d[x] = st > 1 ? g_pal[st >= 6][src[x]] : C_BG; }
                } else {
                    size_t base = (size_t)r * g_seen.gw;
                    for (int x = 0; x < p->w; x++) { int c = g_colc[x]; size_t k = base + (size_t)c; d[x] = c >= 0 && (g_seen.bit[k >> 3] >> (k & 7) & 1) ? g_pal[1][src[x]] : C_BG; }
                }
            }
        }
        icons_draw(p, top, game);
    } else fill(p, 0, top, p->w, p->h, C_BG);
    head_draw(p, s, area);
    char b[16];
    if (g_zoom > 1.05f) { snprintf(b, sizeof b, "x%.1f", g_zoom); int s2 = s * 4 / 7 < 2 ? 2 : s * 4 / 7; text(p, p->w - m - (int)strlen(b) * 6 * s2, m, s2, 0xffb0b0b0, b); }
    if (g_map.ntri) {
        float x, y; sxy(g_st.x, g_st.y, &x, &y);
        if (g_ico.n) { float z = fmaxf(fminf(fmaxf(220 * g_k, p->h / 22.0f), p->h / 9.0f), 8); if (y - z / 2 >= top) icon_blit(p, I_SAMUS, 0, 0, x - z / 2, y - z / 2, x + z / 2, y + z / 2, top, 256); }
        else { float r = fmaxf(5, p->h / 90.0f); if (y - r * 1.6f >= top) { disc(p, x, y, r * 1.6f, 0xff000000); disc(p, x, y, r, 0xff20b0ff); } }
    } else text(p, m, top + 2 * m, s, 0xffb0b0b0, "Sin mapa de esta zona");
    return 1;
}
