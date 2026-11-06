// SPDX-License-Identifier: GPL-3.0-or-later
// The second screen's companion for the games that have an Eden Duo package (kalasyan fork): igawa6's dual-screen runtime (eden/, Eden Duo's
// src/core/mods) and the title's native module (module/, eden-duo-companions), run inside the port. This file is what Eden's emulator was
// around them:
//   - the services the runtime took from Core::System (shim/): a 60 Hz tick thread for its CoreTiming event, the game's process and memory
//     (this process's: the game's code runs natively, its pointers are ours), the clock, the language, the folders (duo_platform.h);
//   - the second screen: the runtime draws its page into VideoCore::DSMod::AuxRouting (PublishUi), and companion_android.c's thread copies the
//     newest page into the bottom screen's window (dm_duo_draw); the page's touches go back to it (dm_duo_touch -> SetTouch);
//   - the game's controller: the companion's own presses (the package's button actions: equipping a tool, the NookPhone's apps) and its
//     controller focus reach the port's HID (hle_hid.c dm_duo_pad / dm_duo_pad_filter); its guest writes go through the runtime's write
//     extension into this process's memory (core/memory.h, process_vm_writev).
// The package (manifest.json, its font list and text) is in the APK's assets (duo/<package>/dualscreen/...), copied once into the app's
// internal storage; the module is the APK's lib<dsmod_title>.so, built from module/ (build.py).
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <android/asset_manager.h>

#include "common/device_power_state.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/mods/mod_runtime.h"
#include "duo_platform.h"
#include "hid_core/resources/npad/dsmod_pad_gate.h"
#include "video_core/dsmod/aux_routing.h"

extern "C" {
#include "companion.h"
// companion_android.c / front_android.c / gfx_vk_present.c / hle_svc.c / hle_hid.c / haptics_android.c (C, dm.h)
int dm_companion_main_module(uint8_t **base, size_t *size, const char **build);
void svc_heap_range(uint8_t **base, size_t *size);
uint64_t dm_frames_shown(void);
AAssetManager *dm_front_assets(void);
void dm_hid_player1(uint64_t *buttons, int32_t *axes);
void dm_haptics_android_tap(void);
}

#ifndef DUO_TITLE
#error "build.py passes DUO_TITLE, the title ID whose companion this is"
#endif
#ifndef DUO_PACKAGE
#error "build.py passes DUO_PACKAGE, the package's folder name (duo/package/<name>), as a bare word"
#endif
#define DUO_STR2(x) #x
#define DUO_STR(x) DUO_STR2(x)

namespace {
using Clock = std::chrono::steady_clock;
const Clock::time_point g_t0 = Clock::now();

void logf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    char b[2048];
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    fprintf(stderr, "[dm] [companion] duo: %s\n", b);
}

// ---- guest memory: this process's mappings, as belmont_host.cpp reads them ----
struct Maps {
    struct Range { u64 s, e; bool w; };
    std::mutex mu;
    std::vector<Range> r;
    Clock::time_point at{};
    void load() {
        std::vector<Range> n;
        if (FILE* f = fopen("/proc/self/maps", "r")) {
            char line[512];
            while (fgets(line, sizeof line, f)) {
                unsigned long s, e;
                char perm[8];
                if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3 || perm[0] != 'r') continue;
                const bool w = perm[1] == 'w';
                if (!n.empty() && n.back().e == s && n.back().w == w) n.back().e = e;
                else n.push_back({s, e, w});
            }
            fclose(f);
        }
        r.swap(n);
        at = Clock::now();
    }
    bool has(u64 a, u64 size, bool write) {
        if (!size || a + size < a) return false;
        std::lock_guard l{mu};
        for (int pass = 0; pass < 2; pass++) {
            auto it = std::upper_bound(r.begin(), r.end(), a, [](u64 v, const Range& x) { return v < x.s; });
            // a range may span several entries of different permissions: walk them
            if (it != r.begin()) {
                auto i = it - 1;
                u64 at_ = a;
                const u64 end = a + size;
                while (i != r.end() && at_ >= i->s && at_ < i->e && (!write || i->w)) {
                    at_ = i->e;
                    if (at_ >= end) return true;
                    ++i;
                    if (i == r.end() || i->s != at_) break;
                }
            }
            if (pass || Clock::now() - at < std::chrono::milliseconds(250)) return false; // new mappings: look again at most 4 times a second
            load();
        }
        return false;
    }
} g_maps;
std::atomic<int> g_vm{1}; // process_vm_readv / writev usable

// ---- the companion's pad (hle_hid.c reads it every sample) ----
std::atomic<u64> g_pad_buttons{0};
std::atomic<u64> g_pad_axes{0}; // 4 x s16: lx ly rx ry
u64 pack(int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    return (u64)(u16)lx | (u64)(u16)ly << 16 | (u64)(u16)rx << 32 | (u64)(u16)ry << 48;
}

// ---- the runtime ----
std::once_flag g_once;
std::atomic<int> g_state{0}; // 0 not started, 1 running, -1 off (no package / no module / an error)
std::unique_ptr<Core::Mods::ModRuntime> g_rt;
VideoCore::DSMod::AuxRouting g_aux;
std::string g_root, g_state_dir, g_build;
u64 g_main_base = 0, g_main_size = 0;

std::string env(const char* k, const char* d = "") {
    const char* v = getenv(k);
    return v && *v ? v : d;
}

bool mkdirs(const std::string& p) {
    std::error_code e;
    std::filesystem::create_directories(p, e);
    return std::filesystem::is_directory(p, e);
}

// the package out of the APK's assets: assets/duo/<package>/... (pipeline.py lists every file in assets/duo/index.txt) into <state>/packages
bool unpack_package(const std::string& dst) {
    AAssetManager* am = dm_front_assets();
    if (!am) return false;
    AAsset* idx = AAssetManager_open(am, "duo/index.txt", AASSET_MODE_BUFFER);
    if (!idx) { logf("[RED] no duo/index.txt in the APK's assets: the companion's package is missing"); return false; }
    std::string list(static_cast<const char*>(AAsset_getBuffer(idx)), static_cast<size_t>(AAsset_getLength(idx)));
    AAsset_close(idx);
    // the index's first line is the package's version (its files' hash): a package already unpacked at that version is kept
    const auto nl = list.find('\n');
    const std::string version = list.substr(0, nl), stamp = dst + "/.version";
    if (FILE* f = fopen(stamp.c_str(), "r")) {
        char b[128] = {};
        const size_t n = fread(b, 1, sizeof b - 1, f);
        fclose(f);
        if (std::string(b, n) == version) return true;
    }
    std::error_code e;
    std::filesystem::remove_all(dst, e);
    size_t files = 0;
    for (size_t at = nl == std::string::npos ? list.size() : nl + 1; at < list.size();) {
        size_t end = list.find('\n', at);
        if (end == std::string::npos) end = list.size();
        const std::string rel = list.substr(at, end - at);
        at = end + 1;
        if (rel.empty() || rel.find("..") != std::string::npos) continue;
        AAsset* a = AAssetManager_open(am, ("duo/" + rel).c_str(), AASSET_MODE_BUFFER);
        if (!a) { logf("[RED] asset duo/%s is missing", rel.c_str()); return false; }
        const std::string out = dst + "/" + rel;
        mkdirs(out.substr(0, out.rfind('/')));
        FILE* f = fopen(out.c_str(), "wb");
        const size_t n = static_cast<size_t>(AAsset_getLength(a));
        const bool ok = f && fwrite(AAsset_getBuffer(a), 1, n, f) == n;
        if (f) fclose(f);
        AAsset_close(a);
        if (!ok) { logf("[RED] cannot write %s", out.c_str()); return false; }
        files++;
    }
    if (FILE* f = fopen(stamp.c_str(), "w")) { fwrite(version.data(), 1, version.size(), f); fclose(f); }
    logf("package unpacked: %zu files into %s", files, dst.c_str());
    return true;
}

std::array<u8, 0x20> build_bytes(const std::string& hex) {
    std::array<u8, 0x20> b{};
    for (size_t i = 0; i + 1 < hex.size() && i / 2 < b.size(); i += 2) b[i / 2] = static_cast<u8>(std::stoi(hex.substr(i, 2), nullptr, 16));
    return b;
}

void start() {
    uint8_t* base = nullptr;
    size_t size = 0;
    const char* build = nullptr;
    if (!dm_companion_main_module(&base, &size, &build)) return; // the game's modules are not loaded yet: asked again on the next pass
    std::call_once(g_once, [&] {
        try {
            g_main_base = reinterpret_cast<u64>(base);
            g_main_size = size;
            g_build = build;
            g_root = env("DM_ROOT", ".");
            const std::string save = env("DM_SAVE");
            g_state_dir = (save.empty() ? g_root : save.substr(0, save.rfind('/'))) + "/duo";
            mkdirs(g_state_dir);
            Common::FS::SetEdenPath(Common::FS::EdenPath::EdenDir, g_state_dir);
            Common::FS::SetEdenPath(Common::FS::EdenPath::CacheDir, g_state_dir + "/cache");
            if (!unpack_package(g_state_dir + "/packages/" DUO_STR(DUO_PACKAGE))) { g_state = -1; return; }
            auto& sys = Core::System::GetInstance();
            g_aux.present.store(true);
            g_aux.width.store(1240);
            g_aux.height.store(1080);
            g_aux.SetHapticSink([](const VideoCore::DSMod::AuxRouting::HapticEvent&) { dm_haptics_android_tap(); });
            auto manifest = Core::Mods::ModRuntime::Discover(sys, DUO_TITLE, build_bytes(g_build));
            if (!manifest) { logf("[RED] no package for this game in %s", (g_state_dir + "/packages").c_str()); g_state = -1; return; }
            g_rt = std::make_unique<Core::Mods::ModRuntime>(sys, std::move(*manifest));
            g_rt->SetMainMemoryParameters(g_main_base, g_main_size);
            g_rt->Initialize();
            g_state = 1;
            logf("running: title %016llX, main build %s at %p (%zu KB)", (unsigned long long)DUO_TITLE, g_build.c_str(), base, size / 1024);
        } catch (const std::exception& e) {
            logf("[RED] the companion did not start: %s", e.what());
            g_state = -1;
        } catch (...) {
            logf("[RED] the companion did not start");
            g_state = -1;
        }
    });
}

// ---- the tick thread: Eden's CoreTiming looping event ----
struct Ticker {
    std::mutex mu;
    std::shared_ptr<Core::Timing::EventType> event;
    std::jthread thread;
} g_ticker;
} // namespace

// ================= the shims' definitions =================

namespace Common::Log {
bool DuoEnabled(Level level) {
    static const int min = atoi(env("DM_DUO_LOG", "2").c_str()); // 2 = info and above; 1 debug
    return static_cast<int>(level) >= min;
}
void DuoWrite(Level level, const char* cls, const std::string& text) {
    static const char* names[] = {"T", "D", "I", "W", "E", "C"};
    const bool red = level >= Level::Error;
    fprintf(stderr, "[dm] [companion] %s%s %s: %s\n", red ? "[RED] " : "", names[std::min<int>(static_cast<int>(level), 5)], cls, text.c_str());
}
} // namespace Common::Log

namespace Core::Memory {
bool DuoMapped(u64 a, u64 n, bool write) { return g_maps.has(a, n, write); }
bool DuoRead(u64 a, void* dst, size_t n) {
    if (!n) return true;
    if (!g_maps.has(a, n, false)) return false;
    if (g_vm.load()) {
        iovec l{dst, n}, r{reinterpret_cast<void*>(static_cast<uintptr_t>(a)), n};
        const ssize_t got = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
        if (got == static_cast<ssize_t>(n)) return true;
        if (got < 0 && (errno == ENOSYS || errno == EPERM)) { g_vm = 0; logf("process_vm_readv unavailable (%s): plain reads", strerror(errno)); }
        else return false;
    }
    std::memcpy(dst, reinterpret_cast<const void*>(static_cast<uintptr_t>(a)), n);
    return true;
}
bool DuoWrite(u64 a, const void* src, size_t n) {
    if (!n) return true;
    if (!g_maps.has(a, n, true)) return false;
    if (g_vm.load()) {
        iovec l{const_cast<void*>(src), n}, r{reinterpret_cast<void*>(static_cast<uintptr_t>(a)), n};
        const ssize_t got = process_vm_writev(getpid(), &l, 1, &r, 1, 0);
        if (got == static_cast<ssize_t>(n)) return true;
        if (got < 0 && (errno == ENOSYS || errno == EPERM)) g_vm = 0;
        else return false;
    }
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(a)), src, n);
    return true;
}
} // namespace Core::Memory

namespace Core::Timing {
std::chrono::nanoseconds CoreTiming::GetGlobalTimeNs() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - g_t0);
}
void CoreTiming::ScheduleLoopingEvent(std::chrono::nanoseconds start, std::chrono::nanoseconds every, const std::shared_ptr<EventType>& e, bool) {
    std::scoped_lock l{g_ticker.mu};
    g_ticker.event = e;
    if (g_ticker.thread.joinable()) return;
    g_ticker.thread = std::jthread([start, every](std::stop_token stop) {
        Common::SetCurrentThreadName("DSModTick");
        auto next = Clock::now() + start;
        while (!stop.stop_requested()) {
            std::this_thread::sleep_until(next);
            next += every;
            if (Clock::now() > next + every * 4) next = Clock::now() + every; // a long stall (the app paused): no burst of catch-up ticks
            std::shared_ptr<EventType> ev;
            {
                std::scoped_lock l{g_ticker.mu};
                ev = g_ticker.event;
            }
            if (!ev) continue;
            try {
                ev->callback(0, std::chrono::nanoseconds{0});
            } catch (const std::exception& x) {
                static int said;
                if (said++ < 3) logf("[RED] a tick threw: %s", x.what());
            }
        }
    });
}
void CoreTiming::UnscheduleEvent(const std::shared_ptr<EventType>& e, bool) {
    std::scoped_lock l{g_ticker.mu};
    if (g_ticker.event == e) g_ticker.event = nullptr;
}
} // namespace Core::Timing

namespace Core {
System& System::GetInstance() {
    static System s;
    return s;
}
PerfTotals PerfStats::GetTotals() const {
    const u64 f = dm_frames_shown();
    return PerfTotals{.game_frames = f, .system_frames = f, .frametime = std::chrono::nanoseconds{16666667}};
}
VideoCore::DSMod::AuxRouting& GPUShim::DSModAux() { return g_aux; }
} // namespace Core

namespace Kernel {
u64 KPageTable::GetHeapRegionStart() const { uint8_t* b = nullptr; size_t n = 0; svc_heap_range(&b, &n); return reinterpret_cast<u64>(b); }
u64 KPageTable::GetHeapRegionSize() const { uint8_t* b = nullptr; size_t n = 0; svc_heap_range(&b, &n); return n; }
u64 KPageTable::GetNormalMemorySize() const { return GetHeapRegionSize(); }
u64 KProcess::GetProgramId() const { return DUO_TITLE; }
} // namespace Kernel

namespace InputCommon {
namespace {
// VirtualButton -> NpadButton bit
constexpr int NpadBit[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, -1, -1};
s32 Axis(float v) { return static_cast<s32>(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }
}
void VirtualGamepad::SetButtonState(std::size_t player, int id, bool value) {
    if ((player != 0 && player != 8) || id < 0 || id >= static_cast<int>(std::size(NpadBit)) || NpadBit[id] < 0) return;
    const u64 bit = 1ULL << NpadBit[id];
    if (value) g_pad_buttons.fetch_or(bit);
    else g_pad_buttons.fetch_and(~bit);
}
void VirtualGamepad::SetStickPosition(std::size_t player, int axis, float x, float y) {
    if (player != 0 && player != 8) return;
    u64 v = g_pad_axes.load();
    const int shift = axis == 0 ? 0 : 32;
    v &= ~(0xFFFFFFFFULL << shift);
    v |= (pack(Axis(x), Axis(y), 0, 0) & 0xFFFFFFFFULL) << shift;
    g_pad_axes.store(v);
}
void VirtualGamepad::ResetControllers() {
    g_pad_buttons = 0;
    g_pad_axes = 0;
}
} // namespace InputCommon

namespace Core::HID {
NpadButtonState EmulatedController::GetNpadButtons() const {
    u64 b = 0;
    int32_t a[4] = {};
    dm_hid_player1(&b, a);
    NpadButtonState s{};
    s.raw = static_cast<NpadButton>(b);
    return s;
}
AnalogSticks EmulatedController::GetSticks() const {
    u64 b = 0;
    int32_t a[4] = {};
    dm_hid_player1(&b, a);
    AnalogSticks s{};
    s.left.x = a[0]; s.left.y = a[1]; s.right.x = a[2]; s.right.y = a[3];
    return s;
}
} // namespace Core::HID

namespace Settings {
namespace {
struct Lang { const char* code; Language settings; const char* app; const char* name; };
// DM_LANG (hle_oe.c GetDesiredLanguage) -> the settings language and the ApplicationLanguage the game is given
constexpr Lang Langs[] = {
    {"ja", Language::Japanese, "Japanese", "Japanese"},
    {"en-US", Language::EnglishAmerican, "AmericanEnglish", "EnglishAmerican"},
    {"en-GB", Language::EnglishBritish, "BritishEnglish", "EnglishBritish"},
    {"fr-CA", Language::FrenchCanadian, "CanadianFrench", "FrenchCanadian"},
    {"fr", Language::French, "French", "French"},
    {"de", Language::German, "German", "German"},
    {"it", Language::Italian, "Italian", "Italian"},
    {"es-419", Language::SpanishLatin, "LatinAmericanSpanish", "SpanishLatin"},
    {"es", Language::Spanish, "Spanish", "Spanish"},
    {"zh-CN", Language::ChineseSimplified, "SimplifiedChinese", "ChineseSimplified"},
    {"zh-Hans", Language::ChineseSimplified, "SimplifiedChinese", "ChineseSimplified"},
    {"zh-TW", Language::ChineseTraditional, "TraditionalChinese", "ChineseTraditional"},
    {"zh-Hant", Language::ChineseTraditional, "TraditionalChinese", "ChineseTraditional"},
    {"ko", Language::Korean, "Korean", "Korean"},
    {"nl", Language::Dutch, "Dutch", "Dutch"},
    {"pt-BR", Language::PortugueseBrazilian, "BrazilianPortuguese", "PortugueseBrazilian"},
    {"pt", Language::Portuguese, "Portuguese", "Portuguese"},
    {"ru", Language::Russian, "Russian", "Russian"},
};
const Lang& Current() {
    const std::string code = env("DM_LANG", "en-US");
    for (const auto& l : Langs) if (code == l.code) return l;
    for (const auto& l : Langs) { const std::string c = l.code; if (code.compare(0, c.size(), c) == 0 && c.find('-') == std::string::npos) return l; }
    return Langs[1];
}
} // namespace
Language DuoLanguage() { return Current().settings; }
std::string_view CanonicalizeEnum(Language l) {
    for (const auto& x : Langs) if (x.settings == l) return x.name;
    return "EnglishAmerican";
}
} // namespace Settings

namespace Common {
void SetCurrentThreadPriority(ThreadPriority) {}
PowerStatus GetPowerStatus() {
    PowerStatus s;
    if (FILE* f = fopen("/sys/class/power_supply/battery/capacity", "r")) { if (fscanf(f, "%d", &s.percentage) != 1) s.percentage = -1; fclose(f); }
    if (FILE* f = fopen("/sys/class/power_supply/battery/status", "r")) { char b[32] = {}; if (fgets(b, sizeof b, f)) s.charging = !strncmp(b, "Charging", 8) || !strncmp(b, "Full", 4); fclose(f); }
    s.has_battery = s.percentage >= 0;
    return s;
}
} // namespace Common

namespace DuoHost {
namespace {
FileSys::VirtualDir Dir(const std::string& path) {
    static FileSys::RealVfsFilesystem fs;
    struct stat st;
    if (stat(path.c_str(), &st) || !S_ISDIR(st.st_mode)) return nullptr;
    return fs.OpenDirectory(path, FileSys::OpenMode::Read);
}
}
FileSys::VirtualDir GameRomfs() { return Dir(env("DM_ROMFS", (g_root + "/RomFS").c_str())); }
FileSys::VirtualDir AocRomfs() {
    const std::string dir = env("DM_AOC_DIR");
    if (dir.empty()) return nullptr;
    std::vector<std::string> subs;
    if (DIR* d = opendir(dir.c_str())) {
        for (dirent* e; (e = readdir(d));) if (e->d_name[0] >= '0' && e->d_name[0] <= '9') subs.emplace_back(e->d_name);
        closedir(d);
    }
    std::sort(subs.begin(), subs.end(), [](const std::string& a, const std::string& b) { return std::stoll(a) < std::stoll(b); });
    return subs.empty() ? nullptr : Dir(dir + "/" + subs[0]);
}
FileSys::VirtualDir PackageDir() { return Dir(g_state_dir + "/packages"); }
std::filesystem::path StateDir() { return g_state_dir; }
std::string ModulePath(u64 title_id) {
    // beside libdmhost.so: the APK's extracted native libraries
    Dl_info di{};
    std::string dir = ".";
    if (dladdr(reinterpret_cast<void*>(&ModulePath), &di) && di.dli_fname) { dir = di.dli_fname; dir = dir.substr(0, dir.rfind('/')); }
    char name[64];
    snprintf(name, sizeof name, "/libdsmod%016llX.so", static_cast<unsigned long long>(title_id));
    return dir + name;
}
std::string MainBuildId() { return g_build; }
void MainRegion(u64& begin, u64& size) { begin = g_main_base; size = g_main_size; }
std::string GameLanguage() { return Settings::Current().app; }
std::vector<u8> GameIconJpeg() {
    std::vector<u8> b;
    if (FILE* f = fopen((g_root + "/icon.jpg").c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) { b.resize(static_cast<size_t>(n)); if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear(); }
        fclose(f);
    }
    return b;
}
} // namespace DuoHost

// ================= the companion page (companion_android.c) and the port's HID (hle_hid.c) =================

extern "C" int dm_duo_active(void) { return g_state.load() >= 0; } // until it is known to be off: the page is the companion's

extern "C" void dm_duo_tick(void) {
    if (g_state.load() == 0) start();
}

extern "C" int dm_duo_draw(page* p) {
    if (g_state.load() != 1) return 0;
    g_aux.width.store(static_cast<u32>(p->w));
    g_aux.height.store(static_cast<u32>(p->h));
    static std::vector<u32> px;
    static u32 w = 0, h = 0;
    static u64 serial = 0;
    g_aux.TakeUi(px, w, h, serial);
    if (!w || !h || px.size() < static_cast<size_t>(w) * h) return 0;
    // the canvas (ARGB words) into the window (RGBA bytes: 0xAABBGGRR), centred and scaled as Eden fits it (FitCompanion), nearest pixel
    const auto fit = VideoCore::DSMod::FitCompanion(static_cast<u32>(p->w), static_cast<u32>(p->h), w, h, VideoCore::DSMod::SecondScreenOptions::Ratio());
    const u32 bg = g_aux.ui_bg.load();
    const u32 bgp = (bg & 0xFF00FF00u) | ((bg >> 16) & 0xFFu) | ((bg & 0xFFu) << 16);
    for (int y = 0; y < p->h; y++) {
        u32* row = p->px + static_cast<size_t>(y) * p->stride;
        const bool in_y = static_cast<u32>(y) >= fit.y && static_cast<u32>(y) < fit.y + fit.h;
        const u32 sy = in_y ? (static_cast<u32>(y) - fit.y) * h / std::max(1u, fit.h) : 0;
        for (int x = 0; x < p->w; x++) {
            if (!in_y || static_cast<u32>(x) < fit.x || static_cast<u32>(x) >= fit.x + fit.w) { row[x] = bgp; continue; }
            const u32 sx = (static_cast<u32>(x) - fit.x) * w / std::max(1u, fit.w);
            const u32 c = px[static_cast<size_t>(sy) * w + sx];
            row[x] = (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16) | 0xFF000000u;
        }
    }
    return 1;
}

// MotionEvent: ACTION_DOWN 0, UP 1, MOVE 2, CANCEL 3, POINTER_DOWN 5, POINTER_UP 6; up to 2 pointers, by index (companion_android.c)
extern "C" void dm_duo_touch(int action, int index, int n, const float* x, const float* y) {
    if (g_state.load() != 1) return;
    static VideoCore::DSMod::AuxFingerTracker fingers;
    static std::mutex mu;
    std::scoped_lock l{mu};
    const auto clampx = [](float v, int lim) { return static_cast<u32>(std::clamp(v, 0.0f, static_cast<float>(lim - 1))); };
    const int pw = static_cast<int>(g_aux.width.load()), ph = static_cast<int>(g_aux.height.load());
    fingers.ClearStarts();
    for (int i = 0; i < n && i < 2; i++) {
        const bool ending = action == 1 || action == 3 || (action == 6 && i == index);
        if (ending) fingers.Release(0, static_cast<u64>(i));
        else fingers.Update(0, static_cast<u64>(i), clampx(x[i], pw), clampx(y[i], ph));
    }
    if (action == 1 || action == 3) fingers.Clear();
    g_aux.SetTouch(fingers.Points());
}

extern "C" int dm_duo_pad(uint64_t* buttons, int32_t* axes) {
    const u64 b = g_pad_buttons.load(), v = g_pad_axes.load();
    if (!b && !v) return 0;
    *buttons = b;
    for (int i = 0; i < 4; i++) axes[i] = static_cast<int16_t>(v >> (16 * i));
    return 1;
}

extern "C" void dm_duo_pad_filter(uint64_t* buttons, int32_t* axes) {
    Core::HID::AnalogStickState l{axes[0], axes[1]}, r{axes[2], axes[3]};
    u64 b = *buttons;
    if (Core::HID::DSModPadGate::Apply(0, b, l, r)) { axes[0] = l.x; axes[1] = l.y; axes[2] = r.x; axes[3] = r.y; }
    *buttons = b;
}

// common/assert.h: Eden logs a failed soft assert and goes on (its debug builds break into the debugger)
void AssertFailSoftImpl() {}
void AssertFatalImpl() { logf("[RED] a fatal assert in the companion runtime"); abort(); }
