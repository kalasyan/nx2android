#!/usr/bin/env python3
# Builds belmont_shot (belmont_shot.cpp), an arm64 Android executable for an adb shell run (kalasyan fork, a test tool).
#   build_shot.py [--ndk <NDK dir>] [--out <dir>]   (default NDK: the main checkout's tools/android-ndk-r27c, as build.py)
import argparse, concurrent.futures as cf, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--ndk', default=os.environ.get('ANDROID_NDK_HOME') or str(HERE.parents[3] / 'tools/android-ndk-r27c'))
ap.add_argument('--out', default=str(HERE / 'out'))
a = ap.parse_args()
host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64' if sys.platform.startswith('linux') else 'darwin-x86_64'
bin_ = Path(a.ndk) / 'toolchains/llvm/prebuilt' / host / 'bin'
exe = '.exe' if os.name == 'nt' else ''
CC, CXX = str(bin_ / f'clang{exe}'), str(bin_ / f'clang++{exe}')
out = Path(a.out); (out / 'obj').mkdir(parents=True, exist_ok=True)
flags = ['--target=aarch64-linux-android31', '-O2', '-fPIC', '-fvisibility=hidden', '-D_GNU_SOURCE', '-w'] + [f'-I{p}' for p in (HERE / 'eden', HERE / 'third_party', HERE / 'module', HERE, HERE.parent)]
data = (HERE / 'manifest.json').read_bytes()
(out / 'belmont_manifest.c').write_text('const unsigned char belmont_manifest_json[] = {' + ','.join(map(str, data)) + ',0};\nconst unsigned belmont_manifest_json_len = %d;\n' % len(data))
srcs = sorted((HERE / 'module').glob('*.cpp')) + sorted((HERE / 'third_party').glob('*.cpp')) + [HERE / 'belmont_host.cpp', HERE / 'belmont_shot.cpp', HERE / 'third_party/lz4.c', out / 'belmont_manifest.c']

def one(s):
    o = out / 'obj' / (s.stem + '.o')
    if o.exists() and o.stat().st_mtime > s.stat().st_mtime and s.name not in ('belmont_host.cpp', 'belmont_shot.cpp'): return o, 0, ''
    cmd = [CXX if s.suffix == '.cpp' else CC] + flags + (['-std=c++20'] if s.suffix == '.cpp' else []) + ['-c', str(s), '-o', str(o)]
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace'); return o, r.returncode, r.stderr

with cf.ThreadPoolExecutor(os.cpu_count() or 4) as ex: res = list(ex.map(one, srcs))
bad = [e for _, rc, e in res if rc]
if bad: print('\n'.join(bad), file=sys.stderr); sys.exit(1)
r = subprocess.run([CXX, '--target=aarch64-linux-android31', '-fuse-ld=lld', '-static-libstdc++', '-pie', '-o', str(out / 'belmont_shot')] + [str(o) for o, _, _ in res] + ['-lm'], capture_output=True, text=True, errors='replace')
if r.returncode: print(r.stderr, file=sys.stderr); sys.exit(1)
print(out / 'belmont_shot')
