#!/usr/bin/env python3
# Android build of the host (the counterpart of dm/host/Makefile): the same host and graphics sources, the Darwin compatibility layer (dm/android/compat)
# and the Android front end / graphics backend (dm/android/*.c), built with the NDK's clang for arm64.
#   build.py --game <title ID> [--ndk <NDK dir>] [--out <dir>] [--jobs N] [--exe] [--lib] [--only-compile] [--release]
# --release: no debug information, the library stripped, DM_RELEASE defined (the app ignores DM_CTL), outputs in dm/android/out/<title ID>-release.
# Outputs in <out> (default dm/android/out/<title ID>): dmhost (an executable for adb shell runs: headless, no window) and libdmhost.so (the app's NativeActivity).
# Objects are rebuilt when a source or a header it includes changed (clang -MMD dependency files).
import argparse, concurrent.futures as cf, os, re, subprocess, sys
from pathlib import Path

DM = Path(__file__).resolve().parent.parent          # dm/
ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True)
ap.add_argument('--ndk', default=os.environ.get('ANDROID_NDK_HOME') or str(DM.parent.parent / 'tools/android-ndk-r27c'))
ap.add_argument('--out')
ap.add_argument('--api', default='31')
ap.add_argument('--jobs', type=int, default=os.cpu_count() or 8)
ap.add_argument('--exe', action='store_true'); ap.add_argument('--lib', action='store_true')
ap.add_argument('--only-compile', action='store_true', help='compile every source and report errors, no link')
ap.add_argument('--keep-going', action='store_true')
ap.add_argument('-O', default='2')
ap.add_argument('--gfx', default='vk', choices=('vk', 'null'), help='graphics backend: Vulkan or the null device')
ap.add_argument('--release', action='store_true', help='a build to keep installed: no debug information, stripped, no DM_CTL')
ap.add_argument('--adrenotools', action='store_true', help='also libadrenotools.so and its hooks (third_party/libadrenotools), which the APK carries with a custom Vulkan driver')
a = ap.parse_args()
if not (a.exe or a.lib): a.exe = a.lib = True

host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64' if sys.platform.startswith('linux') else 'darwin-x86_64'
bin_ = Path(a.ndk) / 'toolchains/llvm/prebuilt' / host / 'bin'
CC = str(bin_ / ('clang.exe' if os.name == 'nt' else 'clang'))
TARGET = f'--target=aarch64-linux-android{a.api}'
out = Path(a.out) if a.out else DM / 'android/out' / (a.game + ('-release' if a.release else ''))
DBG = [] if a.release else ['-g']
obj = out / 'obj'; gen = out / 'gen'; obj.mkdir(parents=True, exist_ok=True); (gen / a.game).mkdir(parents=True, exist_ok=True)

# the game profile header, as the Makefile makes it
subprocess.run([sys.executable, str(DM / 'games/game.py'), 'header', a.game, str(gen / a.game / 'game_profile.h')], check=True)
# and its Android keys (android.tsv): the on-screen gamepad's layout
subprocess.run([sys.executable, str(DM / 'android/tools/android_profile.py'), 'header', a.game, str(gen / a.game / 'android_profile.h')], check=True)

# the backend's own small shaders (dm/android/shaders): compiled with the NDK's glslc, embedded as gen/<name>_<stage>.spv.h
GLSLC = Path(a.ndk) / 'shader-tools' / host / ('glslc.exe' if os.name == 'nt' else 'glslc')
for sh in sorted((DM / 'android/shaders').glob('*.*')):
    hdr = gen / a.game / f'{sh.stem}_{sh.suffix[1:]}.spv.h'
    if hdr.exists() and hdr.stat().st_mtime >= sh.stat().st_mtime: continue
    spv = gen / f'{sh.name}.spv'
    subprocess.run([str(GLSLC), '--target-env=vulkan1.1', '-O', str(sh), '-o', str(spv)], check=True)
    words = spv.read_bytes(); n = len(words) // 4
    import struct as _st
    body = ', '.join(f'{w:#010x}' for w in _st.unpack(f'<{n}I', words))
    hdr.write_text(f'// generated from dm/android/shaders/{sh.name} by build.py (glslc)\nstatic const uint32_t {sh.stem}_{sh.suffix[1:]}_spv[] = {{{body}}};\n')

def srcs(*globs): return [p for g in globs for p in sorted(DM.glob(g))]
HOST = [p for p in srcs('host/*.c') if p.name not in ('hle_movie_avc.c',)]   # VideoToolbox: dm/android/movie_avc.c instead
GFX = srcs('gfx/*.c', 'gfx/nvn/*.c', 'gfx/tex/*.c')
PORT = [p for p in srcs('android/compat/*.c', 'android/*.c') if not (p.name == 'gfx_null.c' and a.gfx != 'null') and not (p.name.startswith('gfx_vk') and a.gfx != 'vk')]   # one graphics backend
ASM = srcs('host/entry.S', 'gfx/nvn/nvn_entry.S', 'gfx/nvn/nvn_thunks.S')
VPX = srcs('third_party/libvpx/**/*.c')
ZSTD = srcs('third_party/zstd/zstddeclib.c')   # zstd's decompressor: the shader bundle's SPIR-V (gfx_vk_shader.c bundle_spv)
# Castlevania: Belmont's Curse only: igawa6's Eden Duo companion module and its host for the second screen page (dm/android/belmont, C++, kalasyan fork)
BELMONT_GAME = (DM / 'android/belmont/title_id.txt').read_text().strip()   # the one game whose companion lives in belmont/ (kalasyan fork)
BELMONT = srcs('android/belmont/module/*.cpp', 'android/belmont/third_party/*.cpp', 'android/belmont/third_party/lz4.c', 'android/belmont/belmont_host.cpp') if a.game.upper() == BELMONT_GAME else []
if BELMONT:   # the package's layout, embedded: gen/<game>/belmont_manifest.c
    mf = DM / 'android/belmont/manifest.json'; mc = gen / a.game / 'belmont_manifest.c'
    if not mc.exists() or mc.stat().st_mtime < mf.stat().st_mtime:
        data = mf.read_bytes()
        body = ',\n'.join(','.join(str(b) for b in data[i:i + 64]) for i in range(0, len(data), 64))
        mc.write_text(f'// generated from dm/android/belmont/manifest.json by build.py\nconst unsigned char belmont_manifest_json[] = {{\n{body},0}};\nconst unsigned belmont_manifest_json_len = {len(data)};\n')
    BELMONT.append(mc)

# The games with an Eden Duo package (duo/titles.tsv, kalasyan fork): Eden Duo's dual-screen runtime and duo_host.cpp in libdmhost.so, and the title's
# module as libdsmod<title ID>.so beside it (the APK carries both: porter's pipeline.py)
DU = DM / 'android/duo'
DUO_PKG = {l.split('\t')[0].upper(): l.split('\t')[1].strip() for l in (DU / 'titles.tsv').read_text().splitlines() if l.strip() and not l.startswith('#')}.get(a.game.upper())
DUO = ([p for p in srcs('android/duo/eden/core/mods/*.cpp') if p.name not in ('mod_console.cpp', 'mod_re_tools.cpp')]   # the dev tools are off (EDEN_DSMOD_BUILD_DEV_TOOLS=0)
       + srcs('android/duo/eden/common/*.cpp', 'android/duo/eden/common/fs/*.cpp') + [DU / f'eden/core/file_sys/vfs/{n}.cpp' for n in ('vfs', 'vfs_vector', 'vfs_offset', 'vfs_real')]
       + [DU / 'third_party/astc.cpp', DU / 'third_party/bc_decoder/bc_decoder.cpp', DU / 'duo_host.cpp']) if DUO_PKG else []
DUO_MOD = (srcs('android/duo/module/*.cpp') + [DU / f'third_party/zstd/{n}.c' for n in ('common/entropy_common', 'common/error_private', 'common/fse_decompress', 'common/xxhash',
           'common/zstd_common', 'decompress/huf_decompress', 'decompress/zstd_ddict', 'decompress/zstd_decompress', 'decompress/zstd_decompress_block')]) if DUO_PKG else []

INC = ['-include', str(DM / 'android/compat/include/darwin_compat.h'), f'-I{DM / "android/compat/include"}', f'-I{DM / "host"}', f'-I{DM / "gfx/nvn"}', f'-I{DM / "gfx/tex"}', f'-I{DM / "gfx/nvn/include"}',
       f'-I{DM / "third_party/vulkan-headers/include"}', f'-I{gen / a.game}', f'-I{DM / "third_party/libvpx"}', f'-I{DM / "third_party/stb_vorbis"}', f'-I{DM / "third_party/zstd"}', f'-I{DM / "android"}']
CFLAGS = [TARGET, f'-O{a.O}'] + DBG + ['-fPIC', '-fno-omit-frame-pointer', '-ffixed-x18', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
          '-Wno-format', '-Wno-deprecated-declarations', '-Wno-gnu-folding-constant', '-Werror=implicit-function-declaration', '-Werror=return-type',
          '-Werror=incompatible-pointer-types', '-Werror=int-conversion', '-D_GNU_SOURCE', '-DDM_ANDROID=1', '-fno-strict-aliasing', '-fvisibility=hidden'] + (['-DDM_RELEASE=1'] if a.release else [])
VPX_FLAGS = [TARGET, '-O2'] + DBG + ['-fPIC', f'-I{DM / "third_party/libvpx"}', f'-I{DM / "third_party/libvpx/config"}', '-w']
ZSTD_FLAGS = [TARGET, '-O2'] + DBG + ['-fPIC', '-fvisibility=hidden', '-DZSTDLIB_VISIBLE=', '-DZSTDERRORLIB_VISIBLE=', '-w']   # its API stays inside libdmhost.so

CXX = str(bin_ / ('clang++.exe' if os.name == 'nt' else 'clang++'))
BL = DM / 'android/belmont'
DUO_FLAGS = [TARGET, '-O2'] + DBG + ['-fPIC', '-ffixed-x18', '-fvisibility=hidden', '-D_GNU_SOURCE', '-DNDEBUG', '-w', '-fexperimental-library', '-DFMT_HEADER_ONLY',
             '-DEDEN_DSMOD_BUILD_DEV_TOOLS=0', f'-DDUO_TITLE=0x{a.game.upper()}ULL', f'-DDUO_PACKAGE={DUO_PKG}', f'-I{DU / "shim"}', f'-I{DU / "eden"}', f'-I{DU / "third_party"}',
             f'-I{DU / "third_party/stb"}', f'-I{DU / "third_party/bc_decoder"}', f'-I{DM / "android"}']
DUO_MOD_FLAGS = [TARGET, '-O2'] + DBG + ['-fPIC', '-ffixed-x18', '-fvisibility=hidden', '-DNDEBUG', '-w', '-DEDEN_ACNH_DIAGNOSTICS=0', '-DZSTD_DISABLE_ASM=1', '-DZSTD_MULTITHREAD=0',
                 '-DXXH_NAMESPACE=DUO_', f'-I{DU / "eden"}', f'-I{DU / "module"}', f'-I{DU / "third_party/stb"}', f'-I{DU / "third_party/bc_decoder"}', f'-I{DU / "third_party/zstd"}']
DUO_NO_FMA = ('acnh_draw.cpp', 'acnh_lyt.cpp', 'acnh_ui_bg.cpp', 'acnh_ui_pages.cpp')   # the module's art recipes: no fused multiply-add (its CMakeLists.txt)
BELMONT_FLAGS = [TARGET, '-O2'] + DBG + ['-fPIC', '-ffixed-x18', '-fvisibility=hidden', '-D_GNU_SOURCE', '-DNDEBUG', '-w', f'-I{BL / "eden"}', f'-I{BL / "third_party"}', f'-I{BL / "module"}', f'-I{BL}', f'-I{DM / "android"}']

import hashlib, shutil
_stamp = obj / 'flags.stamp'; _flags = hashlib.sha256(repr((CFLAGS, INC, VPX_FLAGS, ZSTD_FLAGS, BELMONT_FLAGS, DUO_FLAGS, DUO_MOD_FLAGS, CC)).encode()).hexdigest()
if not _stamp.exists() or _stamp.read_text() != _flags:   # compiler flags changed: every object is rebuilt
    shutil.rmtree(obj, ignore_errors=True); obj.mkdir(parents=True, exist_ok=True); _stamp.write_text(_flags)

def dep_stale(o, d, s):
    if not o.exists() or o.stat().st_mtime < s.stat().st_mtime: return True
    if not d.exists(): return True
    t = o.stat().st_mtime
    txt = d.read_text(errors='replace').replace('\\\n', ' ')
    for f in re.split(r'(?<!\\)\s+', txt.split(':', 1)[1] if ':' in txt else ''):
        f = f.replace('\\ ', ' ').strip()
        if f and Path(f).exists() and Path(f).stat().st_mtime > t: return True
    return False

def compile_one(s):
    rel = s.relative_to(DM) if s.is_relative_to(DM) else s.relative_to(out); o = obj / (str(rel).replace(os.sep, '_').replace('/', '_') + '.o'); d = o.with_suffix('.d')
    src = s
    if s.suffix == '.S':   # Mach-O assembly → ELF
        src = gen / (s.stem + '.elf.S')
        if not src.exists() or src.stat().st_mtime < s.stat().st_mtime:
            subprocess.run([sys.executable, str(DM / 'android/tools/darwin2elf.py'), str(s), str(src)], check=True)
    if not dep_stale(o, d, src): return s, o, 0, ''
    if s in DUO or s in DUO_MOD:   # C++20, or plain C (zstd)
        cpp = s.suffix == '.cpp'
        fl = (DUO_FLAGS if s in DUO else DUO_MOD_FLAGS) + (['-std=c++20'] if cpp else []) + (['-ffp-contract=off'] if s.name in DUO_NO_FMA else [])
        r = subprocess.run([CXX if cpp else CC] + fl + ['-MMD', '-MF', str(d), '-c', str(src), '-o', str(o)], capture_output=True, text=True, errors='replace')
        return s, o, r.returncode, r.stderr
    if s in BELMONT:   # C++20 (the module), or plain C (lz4, the manifest)
        cc = CXX if s.suffix == '.cpp' else CC
        r = subprocess.run([cc] + BELMONT_FLAGS + (['-std=c++20'] if s.suffix == '.cpp' else []) + ['-MMD', '-MF', str(d), '-c', str(src), '-o', str(o)], capture_output=True, text=True, errors='replace')
        return s, o, r.returncode, r.stderr
    flags = VPX_FLAGS if 'third_party/libvpx' in str(rel).replace(os.sep, '/') else ZSTD_FLAGS if 'third_party/zstd' in str(rel).replace(os.sep, '/') else [TARGET] + DBG if s.suffix == '.S' else CFLAGS + INC
    r = subprocess.run([CC] + flags + ['-MMD', '-MF', str(d), '-c', str(src), '-o', str(o)], capture_output=True, text=True, errors='replace')
    return s, o, r.returncode, r.stderr

def build(files):
    objs, failed = [], []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for s, o, rc, err in ex.map(compile_one, files):
            objs.append(o)
            if rc: failed.append(s); print(f'--- {s.relative_to(DM)}\n{err.strip()}\n', file=sys.stderr)
            elif err.strip() and '--verbose' in sys.argv: print(err, file=sys.stderr)
    return objs, failed

all_files = HOST + GFX + PORT + ASM
objs, failed = build(all_files)
vobjs, vfailed = build(VPX + ZSTD + BELMONT + DUO)
mobjs, mfailed = build(DUO_MOD)
failed += vfailed + mfailed
print(f'compiled {len(all_files) + len(VPX) + len(ZSTD) + len(BELMONT) + len(DUO) + len(DUO_MOD)} sources, {len(failed)} failed' + (': ' + ', '.join(str(f.relative_to(DM)) for f in failed) if failed else ''))
if failed and not a.keep_going: sys.exit(1)
if a.only_compile: sys.exit(1 if failed else 0)

LIBS = ['-landroid', '-llog', '-lvulkan', '-laaudio', '-lnativewindow', '-lmediandk', '-lz', '-lm', '-ldl']   # -lz: the gamepad theme's PNGs (overlay_theme.c)
LDFLAGS = [TARGET, '-fuse-ld=lld', '-Wl,--error-limit=0', '-Wl,-z,max-page-size=16384', '-Wl,--build-id', '-Wl,-z,nostart-stop-gc']
LD = CC
if BELMONT: LD = CXX; LDFLAGS += ['-static-libstdc++']   # the C++ runtime inside libdmhost.so (the APK carries no libc++_shared.so)
if DUO: LD = CXX; LDFLAGS += ['-static-libstdc++', '-fexperimental-library']   # std::jthread (Eden's threads)
if a.exe:
    r = subprocess.run([LD] + LDFLAGS + ['-pie', '-o', str(out / 'dmhost')] + [str(o) for o in objs + vobjs] + LIBS, capture_output=True, text=True, errors='replace')
    print(r.stderr.strip() if r.returncode else f'linked {out / "dmhost"}', file=sys.stderr if r.returncode else sys.stdout)
    if r.returncode: sys.exit(1)
if a.lib:
    r = subprocess.run([LD] + LDFLAGS + ['-shared', '-Wl,-soname,libdmhost.so', '-o', str(out / 'libdmhost.so')] + [str(o) for o in objs + vobjs] + LIBS, capture_output=True, text=True, errors='replace')
    print(r.stderr.strip() if r.returncode else f'linked {out / "libdmhost.so"}', file=sys.stderr if r.returncode else sys.stdout)
    if r.returncode: sys.exit(1)
    if DUO_MOD:   # the title's companion module, which the runtime loads beside libdmhost.so (duo_host.cpp DuoHost::ModulePath); bc_decoder is the runtime's object
        exp = out / 'dsmod.exports'; exp.write_text('{\n global:\n' + ''.join(f'  eden_dsmod_get_{n};\n' for n in ('module', 'extensions', 'font_extensions', 'data_extensions', 'write_extensions')) + ' local: *;\n};\n')
        mso = out / f'libdsmod{a.game.upper()}.so'
        bc = [o for s, o in zip(DUO, vobjs[len(VPX) + len(ZSTD) + len(BELMONT):]) if s.name == 'bc_decoder.cpp']
        r = subprocess.run([CXX, TARGET, '-fuse-ld=lld', '-shared', '-static-libstdc++', '-Wl,-z,max-page-size=16384', '-Wl,--build-id', '-Wl,--no-undefined', f'-Wl,--version-script={exp}',
                            f'-Wl,-soname,{mso.name}', '-o', str(mso)] + [str(o) for o in mobjs + bc] + ['-llog', '-lm'], capture_output=True, text=True, errors='replace')
        print(r.stderr.strip() if r.returncode else f'linked {mso}', file=sys.stderr if r.returncode else sys.stdout)
        if r.returncode: sys.exit(1)
        if a.release: subprocess.run([str(bin_ / ('llvm-strip.exe' if os.name == 'nt' else 'llvm-strip')), '--strip-unneeded', str(mso)], check=True)
    if a.release:   # the static symbol table goes too (the dynamic one stays: ANativeActivity_onCreate)
        subprocess.run([str(bin_ / ('llvm-strip.exe' if os.name == 'nt' else 'llvm-strip')), '--strip-unneeded', str(out / 'libdmhost.so')], check=True)
        print(f'stripped {out / "libdmhost.so"}: {(out / "libdmhost.so").stat().st_size // 1024} KB')

# --adrenotools: libadrenotools.so (gfx_vk_core.c vk_init loads a custom Vulkan driver through it, DM_VK_DRIVER) and the hooks it loads from the APK's library folder,
# as its CMake files build them: linkernsbypass inside libadrenotools and libhook_impl, the three small hooks over libhook_impl with -z global
if a.adrenotools:
    AT = DM / 'third_party/libadrenotools'; LN = AT / 'lib/linkernsbypass'
    ATF = [TARGET, '-O2'] + DBG + ['-fPIC', '-w', f'-I{AT / "include"}', f'-I{AT}', f'-I{LN}']
    ATL = ['-fuse-ld=lld', '-shared', '-Wl,-z,max-page-size=16384', '-static-libstdc++', '-landroid', '-llog', '-ldl']
    libs = [('libadrenotools.so', [AT / 'src/driver.cpp', AT / 'src/bcenabler.cpp', LN / 'android_linker_ns.cpp', LN / 'elf_soname_patcher.cpp'], []),
            ('libhook_impl.so', [AT / 'src/hook/hook_impl.cpp', LN / 'android_linker_ns.cpp', LN / 'elf_soname_patcher.cpp'], ['-fvisibility=hidden'])]
    libs += [(f'lib{h}.so', [AT / f'src/hook/{h}.c'], ['-Wl,-z,global', f'-L{out}', '-lhook_impl']) for h in ('main_hook', 'file_redirect_hook', 'gsl_alloc_hook')]
    newest = max(p.stat().st_mtime for p in AT.rglob('*') if p.is_file())
    for name, files, extra in libs:
        o = out / name
        if o.exists() and o.stat().st_mtime >= newest: continue
        r = subprocess.run([CXX if any(f.suffix == '.cpp' for f in files) else CC] + ATF + [str(f) for f in files] + ['-o', str(o), f'-Wl,-soname,{name}'] + ATL + extra,
                           capture_output=True, text=True, errors='replace')
        print(r.stderr.strip() if r.returncode else f'linked {o}', file=sys.stderr if r.returncode else sys.stdout)
        if r.returncode: sys.exit(1)
