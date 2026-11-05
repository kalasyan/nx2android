// Castlevania: Belmont's Curse 1.1.0 on the companion page (kalasyan fork): igawa6's Eden Duo companion for the game, run inside the port.
//
// module/ is that companion's native module, unchanged (eden-duo-companions, GPL-3.0-or-later, README.md): it reads the running game's map, fog,
// landmarks, objective and equipment, and decodes the game's own art and font from its RomFS. In Eden Duo the emulator hosts it; here this file
// does what the emulator did:
//   - the host table (EdenDsmodHostApi): guest memory is this process's memory (the game's code runs natively, its pointers are ours), the RomFS
//     is the data pack's RomFS/ folder, ASTC is decoded on the CPU (third_party/astc.cpp, Eden's decoder);
//   - the page: the package's layout (manifest.json, the companion's own) drawn on the CPU into the companion page: its widgets (images, labels,
//     rects, buttons), the values they are bound to (the module's outputs, the package's derived values and flags), and its map widget (the area's
//     picture the module renders, with the player's rose and the landmark, save point, elevator and fast travel icons, pan and zoom by touch).
// Read only: no guest writes and no guest helper, so pins and equipment changes (which need them) stay off; the map, the objective and the
// equipment overview work.
// The companion thread (companion_android.c) calls dm_belmont_tick and dm_belmont_draw; touches come from the main thread (dm_belmont_touch).
#include "belmont_json.h"
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
extern "C" {
#include "companion.h"
}
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

extern "C" {
// companion_android.c (dm.h): main's image and build, the guest heap, the clock
int dm_companion_main_module(uint8_t **base, size_t *size, const char **build);
void svc_heap_range(uint8_t **base, size_t *size);
uint64_t now_ns(void);
const EdenDsmodModuleApi *eden_dsmod_get_module(uint32_t, uint64_t);
const EdenDsmodModuleExtensions *eden_dsmod_get_extensions(uint32_t, uint64_t);
const EdenDsmodFontExtensions *eden_dsmod_get_font_extensions(uint32_t, uint64_t);
const EdenDsmodModuleDataExtensions *eden_dsmod_get_data_extensions(uint32_t, uint64_t);
}
namespace Tegra::Texture::ASTC { void Decompress(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth, uint32_t block_width, uint32_t block_height, std::span<uint8_t> output); }
extern const unsigned char belmont_manifest_json[];   // belmont_manifest.c (build.py makes it from manifest.json)
extern const unsigned belmont_manifest_json_len;

namespace {
void logf(const char *fmt, ...) {
    va_list a; va_start(a, fmt); char b[1024]; vsnprintf(b, sizeof b, fmt, a); va_end(a);
    fprintf(stderr, "[dm] [companion] Belmont: %s\n", b);
}
constexpr const char *BUILD = "0f1169aa7d0fa4529c861f96adec306e";   // main of Castlevania: Belmont's Curse 1.1.0 (the module's build)
constexpr uint64_t TITLE = 0x0100BA4026E94000ull;
constexpr int CW = 1240, CH = 1080;   // the package's canvas

// ---- pictures ----
struct Img { int w = 0, h = 0; std::vector<uint8_t> rgba; };   // straight alpha
using ImgP = std::shared_ptr<const Img>;

// ---- guest memory: this process's readable mappings ----
struct Maps {
    std::mutex mu; std::vector<std::pair<uint64_t, uint64_t>> r; uint64_t at = 0;
    void load() {
        std::vector<std::pair<uint64_t, uint64_t>> n;
        FILE *f = fopen("/proc/self/maps", "r"); char line[512];
        if (f) {
            while (fgets(line, sizeof line, f)) {
                unsigned long s, e; char perm[8];
                if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3 || perm[0] != 'r') continue;
                if (!n.empty() && n.back().second == s) n.back().second = e; else n.emplace_back(s, e);
            }
            fclose(f);
        }
        r.swap(n); at = now_ns();
    }
    bool has(uint64_t a, uint64_t size) {
        if (!size || a + size < a) return false;
        std::lock_guard l{mu};
        for (int pass = 0; pass < 2; pass++) {
            auto it = std::upper_bound(r.begin(), r.end(), std::make_pair(a, ~0ull));
            if (it != r.begin() && a >= (it - 1)->first && a + size <= (it - 1)->second) return true;
            if (pass || now_ns() - at < 250000000ull) return false;   // a mapping made since: look again at most 4 times a second
            load();
        }
        return false;
    }
} g_maps;

// ---- the module's outputs ----
struct Out { int kind = 0; int64_t i = 0; double f = 0; std::string s; };   // kind: 1 integer, 2 float, 3 text
struct Host {
    EdenDsmodHostApi api{}, worker{};   // worker: the copy load_image/load_data get (no publishing)
    EdenDsmodHostExtensions ext{};
    std::string root;
    std::unordered_map<std::string, Out> out;   // the companion thread only
};
Host g_host;

size_t romfs(void *, const char *path, uint64_t off, void *dst, size_t n) {
    std::string p = path ? path : "", file;
    if (p.rfind("base:", 0) == 0) p = p.substr(5);
    if (p.find("..") != std::string::npos) return 0;
    if (p.rfind("aoc:", 0) == 0) {   // the DLC: aoc/<index>/<path> in the data pack
        p = p.substr(4);
        if (DIR *d = opendir((g_host.root + "/aoc").c_str())) {
            for (dirent *e; (e = readdir(d));) { std::string c = g_host.root + "/aoc/" + e->d_name + "/" + p; struct stat st; if (e->d_name[0] != '.' && !stat(c.c_str(), &st)) { file = c; break; } }
            closedir(d);
        }
        if (file.empty()) return 0;
    } else if (p.find(':') != std::string::npos) return 0;   // a source this host does not have
    else file = g_host.root + "/RomFS/" + (p[0] == '/' ? p.substr(1) : p);
    FILE *f = fopen(file.c_str(), "rb"); if (!f) return 0;
    size_t got = 0;
    if (!dst) { struct stat st; if (!fstat(fileno(f), &st)) got = (size_t)st.st_size; }
    else if (!fseeko(f, (off_t)off, SEEK_SET)) got = fread(dst, 1, n, f);
    fclose(f); return got;
}
EdenDsmodBool mapped(void *, uint64_t a, uint64_t n) { return g_maps.has(a, n) ? EDEN_DSMOD_TRUE : EDEN_DSMOD_FALSE; }
EdenDsmodBool read_mem(void *, uint64_t a, void *dst, size_t n) {
    if (!g_maps.has(a, n)) return EDEN_DSMOD_FALSE;
    static int vm = 1;   // process_vm_readv fails instead of faulting when a mapping went since the look at the maps
    if (vm) {
        iovec l{dst, n}, r{(void *)(uintptr_t)a, n};
        ssize_t got = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
        if (got == (ssize_t)n) return EDEN_DSMOD_TRUE;
        if (got < 0 && (errno == ENOSYS || errno == EPERM)) { vm = 0; logf("process_vm_readv unavailable (%s): plain reads", strerror(errno)); }
        else return EDEN_DSMOD_FALSE;
    }
    memcpy(dst, (const void *)(uintptr_t)a, n); return EDEN_DSMOD_TRUE;
}
const uint8_t *read_ptr(void *, uint64_t a, size_t n) { return g_maps.has(a, n) ? (const uint8_t *)(uintptr_t)a : nullptr; }
uint64_t tick(void *) { return now_ns() / 1000 * 192 / 10; }   // the Switch's 19.2 MHz counter
uint64_t heap_lo(void *) { uint8_t *b; size_t n; svc_heap_range(&b, &n); return (uint64_t)(uintptr_t)b; }
uint64_t heap_hi(void *) { uint8_t *b; size_t n; svc_heap_range(&b, &n); return (uint64_t)(uintptr_t)b + n; }
void mlog(void *, uint32_t lvl, const char *m) { if (lvl >= EDEN_DSMOD_LOG_INFO) logf("module: %s", m ? m : ""); }
void nop(void *) {}
void pub_i(void *, const char *k, int64_t v) { auto &o = g_host.out[k]; o.kind = 1; o.i = v; }
void pub_f(void *, const char *k, double v) { auto &o = g_host.out[k]; o.kind = 2; o.f = v; }
void pub_t(void *, const char *k, const char *v) { auto &o = g_host.out[k]; o.kind = 3; o.s = v ? v : ""; }
void pub_a(void *, const char *k, uint64_t v) { auto &o = g_host.out[k]; o.kind = 1; o.i = (int64_t)v; }
void pub_map(void *, const EdenDsmodMapFrame *) {}
void w_i(void *, const char *, int64_t) {} void w_f(void *, const char *, double) {} void w_t(void *, const char *, const char *) {} void w_a(void *, const char *, uint64_t) {}
int64_t get_i(void *, const char *k, int64_t fb) { return k && !strcmp(k, "__aux_present") ? 1 : fb; }
double get_f(void *, const char *, double fb) { return fb; }
const char *get_t(void *, const char *) { return nullptr; }
EdenDsmodBool astc(void *, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, const uint8_t *in, size_t in_n, uint8_t *o, size_t o_n) {
    if (!in || !o || !w || !h || w > 4096 || h > 4096 || bw < 4 || bh < 4 || bw > 12 || bh > 12) return EDEN_DSMOD_FALSE;
    if (in_n != (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * 16 || o_n != (size_t)w * h * 4) return EDEN_DSMOD_FALSE;
    Tegra::Texture::ASTC::Decompress({in, in_n}, w, h, 1, bw, bh, {o, o_n});
    return EDEN_DSMOD_TRUE;
}

// ---- the module ----
const EdenDsmodModuleApi *g_api; const EdenDsmodModuleExtensions *g_ext; const EdenDsmodFontExtensions *g_font; const EdenDsmodModuleDataExtensions *g_data;
void *g_inst;
std::atomic<int> g_state{0};   // 0 not started, 1 running, -1 refused (another build, no module)

// ---- the loader thread: the area table, the font, pictures ----
struct Glyph { uint16_t x, y, w, h; int16_t bx, by; uint16_t adv; };
struct Font { uint32_t line = 0, first = 0; std::vector<Glyph> g; ImgP atlas; };
std::mutex g_lmu; std::condition_variable g_lcv;
std::deque<std::string> g_want;
std::atomic<uint64_t> g_loaded_gen{0};   // bumped when a picture or the font lands
struct Pic { ImgP img; int64_t asked = 0, used = 0; int fails = 0; bool queued = false; };
std::unordered_map<std::string, Pic> g_imgs;    // pictures by key (g_lmu)
std::shared_ptr<const Font> g_fontp;            // (g_lmu)
std::shared_ptr<const bjson::Value> g_areas;    // the module's area table (g_lmu)

ImgP image(const std::string &key) {   // the picture now, or null and asked for (again after a failure, less often each time)
    if (key.empty()) return nullptr;
    int64_t now = now_ns();
    std::lock_guard l{g_lmu};
    Pic &p = g_imgs[key]; p.used = now;
    if (p.img) return p.img;
    if (!p.queued && now - p.asked >= (int64_t)std::min(8, 1 << std::min(p.fails, 3)) * 250000000) { p.queued = true; p.asked = now; g_want.push_back(key); g_lcv.notify_one(); }
    return nullptr;
}
void evict() {   // the module's per-render pictures (map frames, objective, landmark names, equipment) once unused for 10 s; the game's own art stays
    int64_t now = now_ns(); std::lock_guard l{g_lmu};
    for (auto it = g_imgs.begin(); it != g_imgs.end();) {
        const std::string &k = it->first;
        bool generated = k.rfind("module:belmont:", 0) == 0 && k.rfind("module:belmont:map-icon:", 0) && k.rfind("module:belmont:pin-icon:", 0) && k != "module:belmont:rose-marker";
        if (generated && !it->second.queued && now - it->second.used > 10000000000ll) it = g_imgs.erase(it); else ++it;
    }
}

void loader() {
    // the area table: the module builds it from the RomFS (a few seconds)
    for (int tries = 0; g_data && tries < 600; tries++) {
        std::string got;
        auto sink = +[](void *r, const uint8_t *b, size_t n) { static_cast<std::string *>(r)->assign((const char *)b, n); };
        if (g_data->load_data(g_inst, &g_host.worker, "module:belmont:areas", &got, sink) && !got.empty()) {
            auto v = std::make_shared<bjson::Value>();
            if (bjson::Parser::parse(got, *v)) { std::lock_guard l{g_lmu}; g_areas = v; logf("area table: %zu areas", v->o.size()); }
            else logf("the area table is not JSON");
            break;
        }
        if (!tries) logf("waiting for the area table");
        usleep(500000);
    }
    // the game's font: the package's request (fonts/latin51sc.bfont) decoded by the module, then its atlas
    for (int tries = 0; g_font && tries < 120; tries++) {
        static const char req[] = "BELMONT_FONT_V1 LatinFont 51sc\n";
        auto f = std::make_shared<Font>();
        auto sink = +[](void *r, uint32_t line, uint32_t first, const EdenDsmodFontGlyph *gl, uint32_t n) {
            auto &f = *static_cast<Font *>(r); f.line = line; f.first = first; f.g.resize(n);
            for (uint32_t i = 0; i < n; i++) f.g[i] = {gl[i].x, gl[i].y, gl[i].w, gl[i].h, gl[i].bearing_x, gl[i].bearing_y, gl[i].advance};
        };
        if (g_font->decode_font(g_inst, (const uint8_t *)req, sizeof req - 1, f.get(), sink) && f->line) {
            auto img = std::make_shared<Img>();
            auto isink = +[](void *r, uint32_t w, uint32_t h, const uint8_t *px, size_t n) { auto &i = *static_cast<Img *>(r); if (px && n == (size_t)w * h * 4) { i.w = w; i.h = h; i.rgba.assign(px, px + n); } };
            if (g_ext->load_image(g_inst, &g_host.worker, "module:belmont-font:LatinFont:51sc", img.get(), isink) && img->w) {
                f->atlas = img; { std::lock_guard l{g_lmu}; g_fontp = f; } g_loaded_gen++;
                logf("font: %zu glyphs from U+%04X, line %u, atlas %dx%d", f->g.size(), f->first, f->line, img->w, img->h); break;
            }
        }
        usleep(1000000);
    }
    for (;;) {
        std::string key;
        { std::unique_lock l{g_lmu}; g_lcv.wait(l, [] { return !g_want.empty(); }); key = std::move(g_want.front()); g_want.pop_front(); }
        auto img = std::make_shared<Img>();
        auto sink = +[](void *r, uint32_t w, uint32_t h, const uint8_t *px, size_t n) { auto &i = *static_cast<Img *>(r); if (px && w && h && n == (size_t)w * h * 4) { i.w = w; i.h = h; i.rgba.assign(px, px + n); } };
        int64_t t0 = now_ns();
        bool ok = false;
        try { ok = g_ext->load_image(g_inst, &g_host.worker, key.c_str(), img.get(), sink) && img->w; } catch (...) {}
        std::lock_guard l{g_lmu};
        Pic &p = g_imgs[key]; p.queued = false;
        if (ok) { p.img = img; g_loaded_gen++; }
        else { p.fails++; static int said; if (p.fails == 3 && said++ < 40) logf("no picture for %s", key.c_str()); }
        if (ok && now_ns() - t0 > 200000000) logf("picture %s: %dx%d in %.0f ms", key.c_str(), img->w, img->h, (now_ns() - t0) / 1e6);
    }
}

// ---- the package's layout ----
bjson::Value g_mf;   // manifest.json
std::map<std::string, bool> g_flags;
struct Expr;
using ExprP = std::unique_ptr<Expr>;
struct Expr { enum K { Num, Name, Not, And, Or, Eq, Ne, Cond } k; double n = 0; std::string name; ExprP a, b, c; };
struct Derived { std::string name; ExprP e; };
std::vector<Derived> g_derived;
std::unordered_map<std::string, double> g_dval;
int g_view_custom;   // the user moved or zoomed the map

ExprP parse_expr(const char *&p);
void sp(const char *&p) { while (*p == ' ') p++; }
ExprP parse_atom(const char *&p) {
    sp(p);
    if (*p == '!') { p++; auto e = std::make_unique<Expr>(); e->k = Expr::Not; e->a = parse_atom(p); return e; }
    if (*p == '(') { p++; auto e = parse_expr(p); sp(p); if (*p == ')') p++; return e; }
    auto e = std::make_unique<Expr>();
    if (*p == '\'' || *p == '"') { char q = *p++; e->k = Expr::Name; while (*p && *p != q) e->name += *p++; if (*p) p++; return e; }
    if ((*p >= '0' && *p <= '9') || *p == '-') { e->k = Expr::Num; char *end; e->n = strtod(p, &end); p = end; return e; }
    e->k = Expr::Name; while (*p && (isalnum((unsigned char)*p) || strchr("_.@:$", *p))) e->name += *p++;
    if (e->name == "true") { e->k = Expr::Num; e->n = 1; } else if (e->name == "false") { e->k = Expr::Num; e->n = 0; }
    return e;
}
ExprP parse_cmp(const char *&p) {
    auto a = parse_atom(p); sp(p);
    if ((p[0] == '=' || p[0] == '!') && p[1] == '=') { auto e = std::make_unique<Expr>(); e->k = p[0] == '=' ? Expr::Eq : Expr::Ne; p += 2; e->a = std::move(a); e->b = parse_atom(p); return e; }
    return a;
}
ExprP parse_and(const char *&p) { auto a = parse_cmp(p); for (sp(p); p[0] == '&' && p[1] == '&'; sp(p)) { p += 2; auto e = std::make_unique<Expr>(); e->k = Expr::And; e->a = std::move(a); e->b = parse_cmp(p); a = std::move(e); } return a; }
ExprP parse_or(const char *&p) { auto a = parse_and(p); for (sp(p); p[0] == '|' && p[1] == '|'; sp(p)) { p += 2; auto e = std::make_unique<Expr>(); e->k = Expr::Or; e->a = std::move(a); e->b = parse_and(p); a = std::move(e); } return a; }
ExprP parse_expr(const char *&p) {
    auto a = parse_or(p); sp(p);
    if (*p != '?') return a;
    p++; auto e = std::make_unique<Expr>(); e->k = Expr::Cond; e->a = std::move(a); e->b = parse_expr(p); sp(p); if (*p == ':') p++; e->c = parse_expr(p); return e;
}

double val(const std::string &name);
double eval(const Expr &e) {
    switch (e.k) {
    case Expr::Num: return e.n;
    case Expr::Name: return val(e.name);
    case Expr::Not: return eval(*e.a) == 0;
    case Expr::And: return eval(*e.a) != 0 && eval(*e.b) != 0;
    case Expr::Or: return eval(*e.a) != 0 || eval(*e.b) != 0;
    case Expr::Eq: return eval(*e.a) == eval(*e.b);
    case Expr::Ne: return eval(*e.a) != eval(*e.b);
    case Expr::Cond: return eval(*e.a) != 0 ? eval(*e.b) : eval(*e.c);
    }
    return 0;
}
double val(const std::string &n) {
    if (n.empty()) return 0;
    if (n[0] == '!') return val(n.substr(1)) == 0;
    if (n.rfind("@flag:", 0) == 0) { auto it = g_flags.find(n.substr(6)); return it != g_flags.end() && it->second; }
    if (n == "module_ready") return g_state.load() == 1;
    if (n == "view_custom:belmont_map") return g_view_custom;
    if (auto it = g_dval.find(n); it != g_dval.end()) return it->second;
    if (auto it = g_host.out.find(n); it != g_host.out.end()) return it->second.kind == 1 ? (double)it->second.i : it->second.kind == 2 ? it->second.f : !it->second.s.empty();
    return 0;
}
std::string text(const std::string &n) {
    if (auto it = g_host.out.find(n); it != g_host.out.end()) {
        if (it->second.kind == 3) return it->second.s;
        if (it->second.kind == 1) return std::to_string(it->second.i);
        char b[32]; snprintf(b, sizeof b, "%g", it->second.f); return b;
    }
    return "";
}
void derive_ordered() {   // the package's derived values, in their order (later ones use earlier ones)
    g_dval.clear(); size_t ei = 0;
    for (const auto &d : g_mf.get("derived")->a) {
        std::string name = d.str("name"); double v = 0;
        if (d.get("expr")) { v = ei < g_derived.size() ? eval(*g_derived[ei].e) : 0; ei++; }
        else if (const auto *all = d.get("all_nonzero")) { v = 1; for (const auto &x : all->a) if (val(x.s) == 0) v = 0; }
        else if (const auto *any = d.get("any_nonzero")) { for (const auto &x : any->a) if (val(x.s) != 0) v = 1; }
        else if (d.get("cmp")) {
            double a = val(d.str("a")); const auto *b = d.get("b"); double bv = b && b->kind == bjson::Value::Num ? b->n : b && b->kind == bjson::Value::Str ? val(b->s) : b && b->kind == bjson::Value::Bool ? b->b : 0;
            std::string c = d.str("cmp"); v = c == "eq" ? a == bv : c == "ne" ? a != bv : 0;
        }
        g_dval[name] = v;
    }
}
bool shown(const bjson::Value &w) {
    std::string nb = w.str("need_bind");
    if (nb.find("pin_") != std::string::npos) return false;   // pins: they need the guest helper (and the native-menu fallback presses the game's buttons)
    if (!nb.empty() && val(nb) == 0) return false;
    std::string hb = w.str("hide_bind");
    if (!hb.empty() && val(hb) == w.num("hide_eq", 1)) return false;
    return true;
}
uint32_t color(const std::string &s, uint32_t d = 0xffffffffu) {   // "#AARRGGBB" -> 0xAABBGGRR
    if (s.size() != 9 || s[0] != '#') return d;
    uint32_t v = (uint32_t)strtoul(s.c_str() + 1, nullptr, 16);
    return (v & 0xff00ff00u) | (v >> 16 & 0xff) | (v & 0xff) << 16;
}

// ---- drawing on the canvas (0xAABBGGRR, opaque) ----
// A canvas is the page (opaque) or a layer (premultiplied alpha, kept between frames and laid over the page).
struct Canvas { int w, h; uint32_t *px; int stride; bool layer = false; };
inline void blend_layer(uint32_t &d, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    uint32_t ia = 255 - a;
    uint32_t pr = (r * a + 127) / 255 + ((d & 0xff) * ia + 127) / 255, pg = (g * a + 127) / 255 + ((d >> 8 & 0xff) * ia + 127) / 255;
    uint32_t pb = (b * a + 127) / 255 + ((d >> 16 & 0xff) * ia + 127) / 255, pa = a + ((d >> 24) * ia + 127) / 255;
    d = std::min(255u, pa) << 24 | std::min(255u, pb) << 16 | std::min(255u, pg) << 8 | std::min(255u, pr);
}
bool g_layer;   // drawing into a layer (the drawing functions below run on the companion thread only)
inline void blend(uint32_t &d, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {   // a: 0..255
    if (!a) return;
    if (g_layer) { blend_layer(d, r, g, b, a); return; }
    if (a == 255) { d = 0xff000000u | b << 16 | g << 8 | r; return; }
    uint32_t ia = 255 - a, dr = d & 0xff, dg = d >> 8 & 0xff, db = d >> 16 & 0xff;
    d = 0xff000000u | ((b * a + db * ia + 127) / 255) << 16 | ((g * a + dg * ia + 127) / 255) << 8 | ((r * a + dr * ia + 127) / 255);
}
void fill_rect(Canvas &c, int x, int y, int w, int h, uint32_t col) {
    uint32_t a = col >> 24; if (!a) return;
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(c.w, x + w), y1 = std::min(c.h, y + h);
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) blend(c.px[(size_t)j * c.stride + i], col & 0xff, col >> 8 & 0xff, col >> 16 & 0xff, a);
}
// The picture's part [sx0, sx1) x [sy0, sy1) (source pixels, may be fractional) drawn into [dx, dx + dw) x [dy, dy + dh), tinted (multiplied).
// A box filter when it shrinks (each screen pixel averages the source pixels under it), bilinear when it grows.
void draw_img(Canvas &c, const Img &im, double sx0, double sy0, double sx1, double sy1, double dx, double dy, double dw, double dh, uint32_t tint = 0xffffffffu, bool fx = false, bool fy = false) {
    if (dw <= 0 || dh <= 0 || im.w <= 0 || im.h <= 0 || sx1 <= sx0 || sy1 <= sy0) return;
    int x0 = std::max(0, (int)floor(dx)), y0 = std::max(0, (int)floor(dy)), x1 = std::min(c.w, (int)ceil(dx + dw)), y1 = std::min(c.h, (int)ceil(dy + dh));
    if (x0 >= x1 || y0 >= y1) return;
    double kx = (sx1 - sx0) / dw, ky = (sy1 - sy0) / dh;
    uint32_t tr = tint & 0xff, tg = tint >> 8 & 0xff, tb = tint >> 16 & 0xff, ta = tint >> 24;
    const uint8_t *s = im.rgba.data();
    for (int j = y0; j < y1; j++) {
        double v0 = (j - dy) * ky, v1 = (j + 1 - dy) * ky;
        if (fy) { double t = (sy1 - sy0) - v1; v1 = (sy1 - sy0) - v0; v0 = t; }
        v0 += sy0; v1 += sy0;
        uint32_t *d = c.px + (size_t)j * c.stride;
        for (int i = x0; i < x1; i++) {
            double u0 = (i - dx) * kx, u1 = (i + 1 - dx) * kx;
            if (fx) { double t = (sx1 - sx0) - u1; u1 = (sx1 - sx0) - u0; u0 = t; }
            u0 += sx0; u1 += sx0;
            double r = 0, g = 0, b = 0, a = 0, wsum = 0;
            if (kx > 1.2 || ky > 1.2) {   // shrinking: average (premultiplied) over the footprint, whole pixels weighted by their overlap
                int a0 = std::max(0, (int)floor(u0)), a1 = std::min(im.w, (int)ceil(u1)), b0 = std::max(0, (int)floor(v0)), b1 = std::min(im.h, (int)ceil(v1));
                int stepx = std::max(1, (a1 - a0) / 8), stepy = std::max(1, (b1 - b0) / 8);   // at most 8x8 samples per screen pixel
                for (int y = b0; y < b1; y += stepy) for (int x = a0; x < a1; x += stepx) {
                    const uint8_t *q = s + ((size_t)y * im.w + x) * 4; double qa = q[3];
                    r += q[0] * qa; g += q[1] * qa; b += q[2] * qa; a += qa; wsum += 1;
                }
            } else {   // bilinear at the footprint's middle
                double u = (u0 + u1) / 2 - 0.5, v = (v0 + v1) / 2 - 0.5;
                int xa = (int)floor(u), ya = (int)floor(v); double fu = u - xa, fv = v - ya;
                for (int t = 0; t < 4; t++) {
                    int x = std::clamp(xa + (t & 1), 0, im.w - 1), y = std::clamp(ya + (t >> 1), 0, im.h - 1);
                    if (u0 >= im.w || v0 >= im.h || u1 <= 0 || v1 <= 0) continue;
                    double wt = ((t & 1) ? fu : 1 - fu) * ((t >> 1) ? fv : 1 - fv);
                    const uint8_t *q = s + ((size_t)y * im.w + x) * 4; double qa = q[3] * wt;
                    r += q[0] * qa; g += q[1] * qa; b += q[2] * qa; a += qa; wsum += wt;
                }
            }
            if (wsum <= 0 || a <= 0) continue;
            double av = a / wsum;   // 0..255
            uint32_t oa = (uint32_t)std::lround(av * ta / 255);
            if (!oa) continue;
            // coverage at the rect's fractional edges
            double cov = 1;
            if (i < dx) cov *= 1 - (dx - i); if (i + 1 > dx + dw) cov *= 1 - (i + 1 - (dx + dw));
            if (j < dy) cov *= 1 - (dy - j); if (j + 1 > dy + dh) cov *= 1 - (j + 1 - (dy + dh));
            if (cov < 1) oa = (uint32_t)std::lround(oa * std::max(0.0, cov));
            blend(d[i], (uint32_t)std::lround(r / a) * tr / 255, (uint32_t)std::lround(g / a) * tg / 255, (uint32_t)std::lround(b / a) * tb / 255, std::min(255u, oa));
        }
    }
}

// the game's font: line height = text_scale * 5 px, baseline at the top + that
std::shared_ptr<const Font> font() { std::lock_guard l{g_lmu}; return g_fontp; }
const Glyph *glyph(const Font &f, uint32_t cp) { return cp >= f.first && cp - f.first < f.g.size() ? &f.g[cp - f.first] : nullptr; }
uint32_t next_cp(const std::string &s, size_t &i) {
    unsigned char c = s[i++]; if (c < 0x80) return c;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1; uint32_t cp = c & (0x3f >> n);
    for (int k = 0; k < n && i < s.size(); k++) cp = cp << 6 | (s[i++] & 0x3f);
    return cp;
}
uint32_t fallback(uint32_t cp) {   // a letter the font lacks: its plain form
    static const struct { uint32_t c; char r; } t[] = {{0xe1, 'a'}, {0xe9, 'e'}, {0xed, 'i'}, {0xf3, 'o'}, {0xfa, 'u'}, {0xfc, 'u'}, {0xf1, 'n'}, {0xc1, 'A'}, {0xc9, 'E'}, {0xcd, 'I'}, {0xd3, 'O'}, {0xda, 'U'}, {0xd1, 'N'}, {0x2019, '\''}, {0x2014, '-'}};
    for (auto &e : t) if (e.c == cp) return (uint32_t)e.r;
    return '?';
}
double measure(const Font &f, const std::string &s, int scale) {
    double want = scale * 5.0, k = want / f.line, pen = 0;
    for (size_t i = 0; i < s.size();) { uint32_t cp = next_cp(s, i); const Glyph *g = glyph(f, cp); if (!g) g = glyph(f, fallback(cp)); pen += g ? g->adv * k : want * 0.5; }
    return pen;
}
void draw_text(Canvas &c, double x, double y, const std::string &s, int scale, uint32_t col, int align, uint32_t outline = 0, int outline_px = 0) {
    auto f = font(); if (!f || !f->atlas) return;
    double want = scale * 5.0, k = want / f->line, base = y + want;
    if (align == 1) x -= measure(*f, s, scale) / 2; else if (align == 2) x -= measure(*f, s, scale);
    for (int pass = outline_px > 0 && (outline >> 24) ? 0 : 1; pass < 2; pass++) {
        double pen = x; uint32_t cc = pass ? col : outline;
        for (size_t i = 0; i < s.size();) {
            uint32_t cp = next_cp(s, i); const Glyph *g = glyph(*f, cp); if (!g) g = glyph(*f, fallback(cp));
            if (!g) { pen += want * 0.5; continue; }
            if (g->w && g->h) {
                double gx = pen + g->bx * k, gy = base - g->by * k, gw = g->w * k, gh = g->h * k;
                // the atlas keeps coverage in alpha: the glyph is a mask the colour is painted through (as a tint)
                if (pass) draw_img(c, *f->atlas, g->x, g->y, g->x + g->w, g->y + g->h, gx, gy, gw, gh, cc);
                else for (int a = 0; a < 16; a++) draw_img(c, *f->atlas, g->x, g->y, g->x + g->w, g->y + g->h, gx + outline_px * cos(a * 0.3927), gy + outline_px * sin(a * 0.3927), gw, gh, cc);
            }
            pen += g->adv * k;
        }
    }
}
}   // namespace

namespace {
// ---- the map widget ----
struct MapView { double cx = 0, cy = 0, ppw = 0; };   // world point at the widget's middle, screen pixels per world unit
struct MapState {
    MapView custom; double home_sig[5] = {};
    std::string area;
    std::mutex mu;   // touch (main thread) and drawing
    // gesture
    int down = 0, pinch = 0, moved = 0; double lx = 0, ly = 0, dx0 = 0, dy0 = 0, d0 = 0, ppw0 = 0, wx = 0, wy = 0; int64_t down_t = 0, tap_t = 0; double tx = 0, ty = 0;
    MapView last;    // the view last drawn
    double rx = 0, ry = 0, rw = 0, rh = 0;   // the widget's rect last drawn
    std::deque<std::pair<double, double>> taps;   // taps for buttons (canvas coordinates)
    int reset = 0;   // a double tap asked for the home view
    double k = 1, ox = 0, oy = 0;   // page = canvas * k + (ox, oy)
} g_mv;
struct Mips { std::string key; std::vector<ImgP> lv; };
Mips g_mips;
const Img &mip_for(const ImgP &base, const std::string &key, double shrink) {   // a level no smaller than the screen needs
    if (g_mips.key != key) { g_mips.key = key; g_mips.lv.assign(1, base); }
    int want = 0; while (shrink >= 2.0 && want < 6) { shrink /= 2; want++; }
    while ((int)g_mips.lv.size() <= want) {
        const Img &s = *g_mips.lv.back(); if (s.w < 4 || s.h < 4) break;
        auto d = std::make_shared<Img>(); d->w = s.w / 2; d->h = s.h / 2; d->rgba.resize((size_t)d->w * d->h * 4);
        for (int y = 0; y < d->h; y++) for (int x = 0; x < d->w; x++) {
            uint32_t acc[4] = {}, aa = 0;
            for (int t = 0; t < 4; t++) { const uint8_t *q = &s.rgba[((size_t)(2 * y + (t >> 1)) * s.w + 2 * x + (t & 1)) * 4]; for (int ch = 0; ch < 3; ch++) acc[ch] += q[ch] * q[3]; aa += q[3]; }
            uint8_t *o = &d->rgba[((size_t)y * d->w + x) * 4];
            for (int ch = 0; ch < 3; ch++) o[ch] = aa ? (uint8_t)(acc[ch] / aa) : 0; o[3] = (uint8_t)(aa / 4);
        }
        g_mips.lv.push_back(d);
    }
    return *g_mips.lv[std::min<size_t>(want, g_mips.lv.size() - 1)];
}

void draw_map(Canvas &c, const bjson::Value &w) {
    auto r = w.nums("rect"); if (r.size() < 4) return;
    double rx = r[0], ry = r[1], rw = r[2], rh = r[3];
    fill_rect(c, (int)rx, (int)ry, (int)rw, (int)rh, color(w.str("bg"), 0xff16191du));
    std::string area = text(w.str("room_bind")), key = text(w.str("image_bind"));
    std::shared_ptr<const bjson::Value> areas; { std::lock_guard l{g_lmu}; areas = g_areas; }
    const bjson::Value *a = areas ? areas->get(area) : nullptr;
    if (!a) return;
    auto mn = a->nums("min"), mx = a->nums("max"); if (mn.size() < 2 || mx.size() < 2 || mx[0] <= mn[0] || mx[1] <= mn[1]) return;
    // the home view: the package's view rect fitted into the widget
    double vx0 = val(w.str("view_rect_x0_bind")), vy0 = val(w.str("view_rect_y0_bind")), vx1 = val(w.str("view_rect_x1_bind")), vy1 = val(w.str("view_rect_y1_bind"));
    MapView home;
    if (vx1 > vx0 && vy1 > vy0) { home.ppw = std::min(rw / (vx1 - vx0), rh / (vy1 - vy0)); home.cx = (vx0 + vx1) / 2; home.cy = (vy0 + vy1) / 2; }
    else { home.ppw = std::min(rw / (mx[0] - mn[0]), rh / (mx[1] - mn[1])); home.cx = (mn[0] + mx[0]) / 2; home.cy = (mn[1] + mx[1]) / 2; }
    MapView v;
    {
        std::lock_guard l{g_mv.mu};
        if (g_mv.area != area) { g_mv.area = area; g_view_custom = 0; }
        if (g_mv.reset) { g_mv.reset = 0; g_view_custom = 0; }
        v = g_view_custom ? g_mv.custom : home;
        double minz = w.num("min_zoom", 0.25), maxz = w.num("max_zoom", 6);
        v.ppw = std::clamp(v.ppw, home.ppw * minz, home.ppw * maxz);
        // clamp_view: the view's middle stays inside the area
        v.cx = std::clamp(v.cx, mn[0], mx[0]); v.cy = std::clamp(v.cy, mn[1], mx[1]);
        if (g_view_custom) g_mv.custom = v;
        g_mv.last = v; g_mv.rx = rx; g_mv.ry = ry; g_mv.rw = rw; g_mv.rh = rh;
    }
    auto sx = [&](double x) { return rx + rw / 2 + (x - v.cx) * v.ppw; };
    auto sy = [&](double y) { return ry + rh / 2 - (y - v.cy) * v.ppw; };
    Canvas clip{(int)std::min<double>(c.w, rx + rw), (int)std::min<double>(c.h, ry + rh), c.px, c.stride};
    // the area's picture (the module's render: rooms, fog, decoration); row 0 is the area's top (max y)
    if (ImgP im = image(key)) {
        double dw = (mx[0] - mn[0]) * v.ppw, dh = (mx[1] - mn[1]) * v.ppw;
        const Img &lv = mip_for(im, key, im->w / std::max(1.0, dw));
        draw_img(clip, lv, 0, 0, lv.w, lv.h, sx(mn[0]), sy(mx[1]), dw, dh);
    }
    // icons: landmarks, save points, elevators, fast travel (the area table's dynamic markers)
    if (const auto *dm = a->get("dynamic_markers")) for (const auto &m : dm->a) {
        if (m.str("group") == "companion_pins") continue;   // pins need the guest helper
        if (val(m.str("show_bind")) == 0) continue;
        ImgP ic = image(text(m.str("icon_src_bind"))); if (!ic) continue;
        double x = val(m.str("x")), y = val(m.str("y"));
        auto an = m.nums("anchor"); double ax = an.size() > 1 ? an[0] : 0.5, ay = an.size() > 1 ? an[1] : 0.5;
        double size = m.get("size_world") ? m.num("size_world") * v.ppw : m.num("size", 48);
        double iw = size, ih = size;
        if (ic->w > ic->h) ih = size * ic->h / ic->w; else iw = size * ic->w / ic->h;
        draw_img(clip, *ic, 0, 0, ic->w, ic->h, sx(x) - iw * ax, sy(y) - ih * ay, iw, ih);
    }
    // the player: the rose
    if (ImgP rose = image(w.str("marker_src"))) {
        double k = w.num("marker_scale", 1), x = val(w.str("marker_x_bind")) * k, y = val(w.str("marker_y_bind")) * k;
        auto ms = w.nums("marker_size"); auto an = w.nums("marker_anchor");
        double mw = ms.size() > 1 ? ms[0] : 96, mh = ms.size() > 1 ? ms[1] : 96;
        draw_img(clip, *rose, 0, 0, rose->w, rose->h, sx(x) - mw * (an.size() > 1 ? an[0] : 0.5), sy(y) - mh * (an.size() > 1 ? an[1] : 0.5), mw, mh);
    }
}

// ---- the page ----
std::vector<uint32_t> g_canvas;
void run_action(const std::string &name, int64_t payload = 0);
std::string label_text(const bjson::Value &w);
// What a run of widgets shows now, as text: the layer is drawn again only when it changes.
std::string layer_sig(size_t from, size_t to) {
    const auto &ws = g_mf.get("pages")->a[0].get("widgets")->a; std::string sig; sig.reserve(1024);
    bool f = (bool)font(); sig += f ? 'F' : 'f';
    for (size_t i = from; i < to && i < ws.size(); i++) {
        const auto &w = ws[i];
        if (w.get("repeat") || w.get("scroll") || !shown(w)) continue;
        std::string t = w.str("type"); sig += std::to_string(i); sig += ':';
        if (t == "image") { std::string key = w.get("src_bind") ? text(w.str("src_bind")) : w.str("src"); sig += key; sig += image(key) ? '+' : '-'; }
        else if (t == "label" || t == "value") sig += label_text(w);
        sig += ';';
    }
    return sig;
}
const char *es(const std::string &t) {   // the package's own words in Spanish (the game's names come from its English tables)
    static const std::pair<const char *, const char *> tr[] = {{"Area", "Zona"}, {"Sub-Area", "Subzona"}, {"Reset", "Centrar"}, {"Arcanas", "Arcanos"}, {"Armoury", "Armer\xc3\xad" "a"}, {"Relics", "Reliquias"},
        {"ATK", "ATQ"}, {"HP", "PV"}, {"MP", "PM"}, {"Equipment", "Equipo"}, {"Map", "Mapa"}, {"Lvl ", "Nv. "}, {"Companion unavailable", "Companion no disponible"},
        {"Check the game update and companion package", "Hace falta la version 1.1.0 del juego"}};
    for (const auto &[en, s] : tr) if (t == en) return s;
    return nullptr;
}
std::string label_text(const bjson::Value &w) {
    std::string t = w.str("type");
    if (t == "value") { std::string pre = w.str("text"); const char *s = es(pre); return (s ? s : pre) + text(w.str("bind")); }
    if (w.get("bind_text")) return text(w.str("bind_text"));
    std::string s = w.str("text"); const char *e = es(s); return e ? e : s;
}
void draw_widgets(Canvas &c, size_t from, size_t to) {
    const auto &all = g_mf.get("pages")->a[0].get("widgets")->a;
    for (size_t wi = from; wi < to && wi < all.size(); wi++) {
        const auto &w = all[wi];
        if (w.get("repeat") || w.get("scroll")) continue;   // the equipment picker's grid: needs the guest helper
        if (!shown(w)) continue;
        std::string t = w.str("type"); auto r = w.nums("rect"); if (r.size() < 4) continue;
        if (t == "map") continue;   // drawn on its own, between the layers
        if (t == "rect") { fill_rect(c, (int)r[0], (int)r[1], (int)r[2], (int)r[3], color(w.str("bg"), 0)); continue; }
        if (t == "image") {
            std::string key = w.get("src_bind") ? text(w.str("src_bind")) : w.str("src");
            ImgP im = image(key); if (!im) continue;
            auto s = w.nums("src_rect"); double s0 = 0, t0 = 0, s1 = 1, t1 = 1;
            if (s.size() >= 4) { s0 = s[0]; t0 = s[1]; s1 = s[2]; t1 = s[3]; }
            uint32_t tint = color(w.str("tint"), 0xffffffffu); bool fx = w.flag("flip_x"), fy = w.flag("flip_y");
            if (w.str("fill") == "slice") {   // nine-slice: corners kept, edges and middle stretched (insets in source pixels, the same on screen)
                auto sl = w.nums("slice"); if (sl.size() < 4) continue;
                double sw = im->w * (s1 - s0), sh = im->h * (t1 - t0), bx = im->w * s0, by = im->h * t0;
                double xs[4] = {0, sl[0], sw - sl[2], sw}, ys[4] = {0, sl[1], sh - sl[3], sh}, xd[4] = {r[0], r[0] + sl[0], r[0] + r[2] - sl[2], r[0] + r[2]}, yd[4] = {r[1], r[1] + sl[1], r[1] + r[3] - sl[3], r[1] + r[3]};
                for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) draw_img(c, *im, bx + xs[i], by + ys[j], bx + xs[i + 1], by + ys[j + 1], xd[i], yd[j], xd[i + 1] - xd[i], yd[j + 1] - yd[j], tint);
                continue;
            }
            draw_img(c, *im, im->w * s0, im->h * t0, im->w * s1, im->h * t1, r[0], r[1], r[2], r[3], tint, fx, fy);
            continue;
        }
        if (t == "label" || t == "value") {
            std::string s = label_text(w);
            if (s.empty()) continue;
            std::string al = w.str("align"); int align = al == "center" ? 1 : al == "right" ? 2 : 0;
            draw_text(c, r[0], r[1], s, (int)w.num("text_scale", 5), color(w.str("color")), align, color(w.str("text_outline"), 0), (int)w.num("text_outline_px", 0));
            continue;
        }
    }
}
void taps() {   // buttons under the taps, the top one
    std::deque<std::pair<double, double>> q; { std::lock_guard l{g_mv.mu}; q.swap(g_mv.taps); }
    const auto *ws = g_mf.get("pages")->a[0].get("widgets");
    for (auto [x, y] : q) {
        const bjson::Value *hit = nullptr;
        for (const auto &w : ws->a) {
            if (w.str("type") != "button" || w.get("repeat") || !shown(w)) continue;
            auto r = w.nums("rect"); if (r.size() < 4) continue;
            if (x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3]) hit = &w;
        }
        if (hit) run_action(hit->str("on_tap"));
    }
}
void run_action(const std::string &name, int64_t payload) {
    const bjson::Value *a = g_mf.get("actions") ? g_mf.get("actions")->get(name) : nullptr;
    if (!a) return;
    std::string eb = a->str("enabled_bind"); if (!eb.empty() && val(eb) == 0) return;
    std::string k = a->str("kind");
    if (k == "flag") g_flags[a->str("flag")] = a->flag("value");
    else if (k == "view_reset") { std::lock_guard l{g_mv.mu}; g_view_custom = 0; }
    else if (k == "module" && g_ext && g_ext->on_action && a->str("action") == "equipment_visibility") {   // the only module action without guest writes or button presses
        const auto *arg = a->get("argument"); int64_t v = arg && arg->kind == bjson::Value::Num ? (int64_t)arg->n : arg && arg->kind == bjson::Value::Str && arg->s == "$payload" ? payload : 0;
        g_ext->on_action(g_inst, a->str("action").c_str(), v);
    }
}
void enforce() {   // the package's standing actions while its gate holds (the tier acknowledgements, the equipment reader on and off)
    const auto *gate = g_mf.get("enforce_gate"); const auto *list = g_mf.get("enforce");
    if (!list || (gate && val(gate->str("point")) == 0)) return;
    for (const auto &e : list->a) {
        if (const auto *fl = e.get("flag")) { (void)fl; if (g_flags[e.str("flag")] != e.flag("value")) continue; }
        run_action(e.str("action"));
    }
}

bool start() {
    uint8_t *base; size_t size; const char *build;
    if (!dm_companion_main_module(&base, &size, &build)) return false;
    if (strcasecmp(build, BUILD)) { g_state = -1; return false; }
    g_api = eden_dsmod_get_module(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
    g_ext = eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
    g_font = eden_dsmod_get_font_extensions(EDEN_DSMOD_FONT_EXT_VERSION, EDEN_DSMOD_FONT_EXT_HASH);
    g_data = eden_dsmod_get_data_extensions(EDEN_DSMOD_DATA_EXT_VERSION, EDEN_DSMOD_DATA_EXT_HASH);
    if (!g_api || !g_ext) { logf("no module"); g_state = -1; return false; }
    if (!bjson::Parser::parse(std::string_view((const char *)belmont_manifest_json, belmont_manifest_json_len), g_mf) || !g_mf.get("pages") || !g_mf.get("derived")) { logf("the manifest does not parse"); g_state = -1; return false; }
    for (const auto &d : g_mf.get("derived")->a) if (const auto *e = d.get("expr")) { const char *p = e->s.c_str(); g_derived.push_back({d.str("name"), parse_expr(p)}); }
    if (const auto *fl = g_mf.get("flags")) for (const auto &[k, v] : fl->o) g_flags[k] = v.kind == bjson::Value::Bool && v.b;
    const char *root = getenv("DM_ROOT"); g_host.root = root ? root : ".";
    auto &h = g_host.api;
    h.abi_version = EDEN_DSMOD_MODULE_ABI_VERSION; h.struct_size = sizeof h; h.abi_hash = EDEN_DSMOD_MODULE_ABI_HASH;
    h.capabilities = EDEN_DSMOD_CAP_ROMFS_READ | EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_SOURCE_PREFIXES | EDEN_DSMOD_CAP_SOURCE_BASE;
    h.userdata = &g_host; h.title_id = TITLE;
    for (int i = 0; i < 16; i++) { unsigned b; sscanf(build + 2 * i, "%2x", &b); h.build_id[i] = (uint8_t)b; }
    h.main_base = (uint64_t)(uintptr_t)base; h.main_size = size;
    h.get_tick = tick; h.get_heap_begin = heap_lo; h.get_heap_end = heap_hi; h.is_mapped = mapped; h.read_memory = read_mem; h.get_read_pointer = read_ptr;
    h.write_memory = nullptr; h.log = mlog; h.begin_output = nop; h.publish_i64 = pub_i; h.publish_f64 = pub_f; h.publish_text = pub_t; h.publish_address = pub_a;
    h.publish_map = pub_map; h.end_output = nop; h.read_romfs = romfs; h.get_i64 = get_i; h.get_f64 = get_f; h.get_text = get_t;
    g_host.worker = h; g_host.worker.publish_i64 = w_i; g_host.worker.publish_f64 = w_f; g_host.worker.publish_text = w_t; g_host.worker.publish_address = w_a;
    char hex[65]; for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02X", h.build_id[i]);
    if (g_api->supports_build && !g_api->supports_build(hex)) { logf("the module does not take build %s", hex); g_state = -1; return false; }
    g_maps.load();
    g_inst = g_api->create(&h, "{}");
    if (!g_inst) { logf("the module refused the host"); g_state = -1; return false; }
    auto &x = g_host.ext; x.version = EDEN_DSMOD_EXT_VERSION; x.struct_size = sizeof x; x.abi_hash = EDEN_DSMOD_EXT_HASH; x.userdata = &g_host; x.decode_astc = astc;
    g_ext->configure(g_inst, &x);
    g_state = 1;
    std::thread(loader).detach();
    logf("module started: main at %p (%zu bytes), data in %s", base, size, g_host.root.c_str());
    return true;
}
}   // namespace

extern "C" int dm_belmont_active(void) {   // the game is Castlevania: Belmont's Curse 1.1.0 (known once the modules are loaded)
    if (g_state.load() == 0) { uint8_t *b; size_t n; const char *build; if (dm_companion_main_module(&b, &n, &build)) return !strcasecmp(build, BUILD); return 0; }
    return g_state.load() == 1;
}
extern "C" int belmont_shot_mode;   // belmont_shot.cpp: no game, so no samples; it sets the module's outputs itself
__attribute__((weak)) int belmont_shot_mode = 0;
extern "C" void belmont_shot_set(const char *k, int kind, double v, const char *t) { auto &o = g_host.out[k]; o.kind = kind; o.i = (int64_t)v; o.f = v; o.s = t ? t : ""; }
extern "C" const char *belmont_shot_areas(void) { static std::string s; std::lock_guard l{g_lmu}; s.clear(); if (g_areas) for (const auto &[k, v] : g_areas->o) { s += k; s += ' '; } return s.c_str(); }
extern "C" void dm_belmont_tick(void) {
    if (g_state.load() == 0) start();
    if (g_state.load() != 1 || belmont_shot_mode) return;
    try { g_api->sample(g_inst, &g_host.api); if (g_api->tick) g_api->tick(g_inst, &g_host.api); }
    catch (...) { static int said; if (!said++) logf("the module threw"); }
    static int64_t last; int64_t now = now_ns();
    if (now - last > 5000000000ll) {   // what the module sees, now and then
        last = now;
        logf("state: world %d map %d native %d title %d loading %d (%s)", (int)val("belmont.ui_world"), (int)val("belmont.map_ready"), (int)val("belmont.native_assets_ready"), (int)val("belmont.ui_title"), (int)val("belmont.ui_loading"), text("belmont.state_reason").c_str());
    }
}
extern "C" int dm_belmont_draw(page *p) {
    if (g_state.load() != 1) return 0;
    derive_ordered(); enforce(); taps(); derive_ordered();
    image(text("belmont.pending_map_key"));   // the next map picture: the module shows it only once the host has taken it
    static int64_t ev; if (now_ns() - ev > 2000000000ll) { ev = now_ns(); evict(); }
    // the page: the widgets before the map (a layer), the map, the widgets after it (a layer); each layer drawn again only when what it shows changed
    struct Layer { std::vector<uint32_t> px; std::string sig; };
    static Layer under, over; static size_t map_at = SIZE_MAX;
    const auto &all = g_mf.get("pages")->a[0].get("widgets")->a;
    if (map_at == SIZE_MAX) { map_at = all.size(); for (size_t i = 0; i < all.size(); i++) if (all[i].str("type") == "map") map_at = i; }
    for (int li = 0; li < 2; li++) {
        Layer &L = li ? over : under; size_t from = li ? map_at + 1 : 0, to = li ? all.size() : map_at;
        std::string sig = layer_sig(from, to);
        if (sig == L.sig && !L.px.empty()) continue;
        L.sig = sig; L.px.assign((size_t)CW * CH, 0);
        Canvas lc{CW, CH, L.px.data(), CW, true}; g_layer = true; draw_widgets(lc, from, to); g_layer = false;
    }
    g_canvas.resize((size_t)CW * CH);
    Canvas c{CW, CH, g_canvas.data(), CW};
    uint32_t bg = color(g_mf.str("background"), 0xff16191du);
    std::fill(g_canvas.begin(), g_canvas.end(), bg);
    auto lay = [&](const Layer &L) {   // premultiplied over opaque
        for (size_t i = 0; i < (size_t)CW * CH; i++) {
            uint32_t s_ = L.px[i], a = s_ >> 24; if (!a) continue;
            if (a == 255) { g_canvas[i] = s_; continue; }
            uint32_t d = g_canvas[i], ia = 255 - a;
            g_canvas[i] = 0xff000000u | std::min(255u, (s_ >> 16 & 0xff) + ((d >> 16 & 0xff) * ia + 127) / 255) << 16 | std::min(255u, (s_ >> 8 & 0xff) + ((d >> 8 & 0xff) * ia + 127) / 255) << 8 | std::min(255u, (s_ & 0xff) + ((d & 0xff) * ia + 127) / 255);
        }
    };
    lay(under);
    if (map_at < all.size() && shown(all[map_at])) draw_map(c, all[map_at]);
    lay(over);
    bool direct = p->w == CW && p->h == CH;
    { std::lock_guard l{g_mv.mu}; g_mv.k = std::min((double)p->w / CW, (double)p->h / CH); g_mv.ox = (p->w - CW * g_mv.k) / 2; g_mv.oy = (p->h - CH * g_mv.k) / 2; }
    if (direct) for (int y = 0; y < CH; y++) memcpy(p->px + (size_t)y * p->stride, g_canvas.data() + (size_t)y * CW, CW * 4);
    if (val("ui.native") == 0) {   // before the game's art is ready: say so in the page's own font, or the 5x7 one
        text(p, 40, 40, 4, 0xffffffff, "CASTLEVANIA");
        text(p, 40, 120, 3, 0xffb0b0b0, val("belmont.build_supported") ? "Cargando el arte del juego..." : "Esperando al juego...");
    }
    if (!direct) {   // scaled to the page, kept in proportion
        double k = std::min((double)p->w / CW, (double)p->h / CH); int ox = (int)((p->w - CW * k) / 2), oy = (int)((p->h - CH * k) / 2);
        fill(p, 0, 0, p->w, p->h, bg);
        for (int y = 0; y < (int)(CH * k); y++) { const uint32_t *s = g_canvas.data() + (size_t)std::min(CH - 1, (int)(y / k)) * CW; uint32_t *d = p->px + (size_t)(y + oy) * p->stride + ox; for (int x = 0; x < (int)(CW * k); x++) d[x] = s[std::min(CW - 1, (int)(x / k))]; }
    }
    return 1;
}
// a touch on the page (main thread): one finger moves the map, two zoom it, a tap presses a button, a double tap on the map goes back to the home view
extern "C" void dm_belmont_touch(int act, int idx, int n, const float *xs, const float *ys) {
    (void)idx; if (g_state.load() != 1) return;
    std::lock_guard l{g_mv.mu};
    int64_t now = now_ns(); float cx[2], cy[2];
    for (int i = 0; i < 2 && i < n; i++) { cx[i] = (float)((xs[i] - g_mv.ox) / g_mv.k); cy[i] = (float)((ys[i] - g_mv.oy) / g_mv.k); }
    xs = cx; ys = cy; double x = xs[0], y = ys[0];
    auto in_map = [&](double px, double py) { return px >= g_mv.rx && py >= g_mv.ry && px < g_mv.rx + g_mv.rw && py < g_mv.ry + g_mv.rh && g_mv.last.ppw > 0; };
    if (act == 0) { g_mv.down = 1; g_mv.moved = 0; g_mv.pinch = 0; g_mv.lx = g_mv.dx0 = x; g_mv.ly = g_mv.dy0 = y; g_mv.down_t = now; }
    else if (act == 5 && n >= 2 && in_map(x, y)) {
        g_mv.pinch = 1; g_mv.moved = 1; g_mv.d0 = std::max(1.0, (double)hypotf(xs[1] - xs[0], ys[1] - ys[0]));
        MapView v = g_view_custom ? g_mv.custom : g_mv.last; g_mv.ppw0 = v.ppw;
        double mx = (xs[0] + xs[1]) / 2, my = (ys[0] + ys[1]) / 2;
        g_mv.wx = v.cx + (mx - g_mv.rx - g_mv.rw / 2) / v.ppw; g_mv.wy = v.cy - (my - g_mv.ry - g_mv.rh / 2) / v.ppw;
        g_mv.custom = v; g_view_custom = 1;
    } else if (act == 2) {
        if (g_mv.pinch && n >= 2) {
            double ppw = g_mv.ppw0 * hypotf(xs[1] - xs[0], ys[1] - ys[0]) / g_mv.d0, mx = (xs[0] + xs[1]) / 2, my = (ys[0] + ys[1]) / 2;
            g_mv.custom.ppw = ppw; g_mv.custom.cx = g_mv.wx - (mx - g_mv.rx - g_mv.rw / 2) / ppw; g_mv.custom.cy = g_mv.wy + (my - g_mv.ry - g_mv.rh / 2) / ppw;
        } else if (!g_mv.pinch && n == 1) {
            if (fabs(x - g_mv.dx0) > 24 || fabs(y - g_mv.dy0) > 24) {
                if (!g_mv.moved && in_map(g_mv.dx0, g_mv.dy0)) { g_mv.custom = g_view_custom ? g_mv.custom : g_mv.last; g_view_custom = 1; }
                g_mv.moved = 1;
            }
            if (g_mv.moved && g_view_custom && in_map(g_mv.dx0, g_mv.dy0) && g_mv.custom.ppw > 0) { g_mv.custom.cx -= (x - g_mv.lx) / g_mv.custom.ppw; g_mv.custom.cy += (y - g_mv.ly) / g_mv.custom.ppw; }
            g_mv.lx = x; g_mv.ly = y;
        }
    } else if (act == 1) {
        g_mv.down = 0;
        if (!g_mv.moved && now - g_mv.down_t < 400000000) {
            if (now - g_mv.tap_t < 350000000 && fabs(x - g_mv.tx) < 60 && fabs(y - g_mv.ty) < 60 && in_map(x, y)) { g_mv.reset = 1; g_mv.tap_t = 0; }
            else { g_mv.tap_t = now; g_mv.tx = x; g_mv.ty = y; g_mv.taps.emplace_back(x, y); }
        }
    }
}
