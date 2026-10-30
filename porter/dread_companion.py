# Metroid Dread's companion page (kalasyan fork): what the page on a second screen needs from the game's files, as files to copy over the data pack.
#   companion/<area>.nx2m           each area's map, from its minimap data (maps/levels/c10_samus/<area>/<area>.bmmap): the navigation mesh's
#                                   triangles, the doors and the items, in the game's world units (nxnative/dm/android/companion_dread.c draws them)
#   RomFS/packs/system/system.pkg   the game's scripts with system/scripts/scenario.lc wrapped: the wrapper runs the original (kept as
#                                   nx2a_scenario_original.lc) and keeps a Lua table with Samus's area, position, energy and missiles up to date every
#                                   frame, which the host finds in memory (companion_dread.c)
#   RomFS/system/files.toc          the asset size table, with the wrapped script's and the new asset's sizes
# Standard library only. Formats as Mercury Engine Data Structures (github.com/randovania/mercury-engine-data-structures) reads them: assets are
# named by CRC-64 (ECMA-182, reflected, initial all ones, no final xor) of their path; a .pkg is u32 header size, u32 data size, u32 count, then
# (u64 asset, u32 start, u32 end) entries, the data from the next multiple of 128, each file padded to 8 bytes; files.toc is u32 count + (u64 asset,
# u32 size); a .bmmap is u64 CRC("CMinimapData"), a 4-byte version, u64 CRC("Root") and the root object, where an object is u32 field count + per field
# (u64 CRC(field name), value), a vector u32 count + items, a dictionary u32 count + (key, value), a string NUL-terminated.
#   python -m porter dread-companion "<game folder>" [-o <out>]
import struct, sys
from pathlib import Path

_POLY = 0xC96C5795D7870F42
_T = []
for _i in range(256):
    _c = _i
    for _ in range(8): _c = (_c >> 1) ^ _POLY if _c & 1 else _c >> 1
    _T.append(_c)


def crc64(s):
    c = 0xFFFFFFFFFFFFFFFF
    for b in (s.encode() if isinstance(s, str) else s): c = _T[(c ^ b) & 255] ^ (c >> 8)
    return c


AREAS = ['s010_cave', 's020_magma', 's030_baselab', 's040_aqua', 's050_forest', 's060_quarantine', 's070_basesanc', 's080_shipyard', 's090_skybase']
SCENARIO = 'system/scripts/scenario.lc'
ORIGINAL = 'system/scripts/nx2a_scenario_original.lc'

# The wrapper. The table's array part is 11 numbers in a row in memory; it is only assigned to after it is made, so it stays where it is.
# Areas are numbered as AREAS (1 = s010_cave); seq counts frames so the host can tell the live table from an old one. The ticks start when the script
# loads and again when an area finishes loading (one chain at a time: a tick of an older chain ends it).
WRAPPER = '''Game.ImportLibrary("system/scripts/nx2a_scenario_original.lua")
-- nx2android companion page (kalasyan fork): the player's state for the port's second screen
NX2A = NX2A or {1357.25, -8642.75, 0, 0, 0, 0, 0, 0, 0, 0, 1}
NX2A_AREA = {%s}
NX2A_CHAIN = NX2A_CHAIN or 0
function NX2A_Tick(chain)
  if chain ~= NX2A_CHAIN then return end
  pcall(function()
    local p = Game.GetPlayer()
    if p ~= nil then
      local v = p.vPos
      NX2A[4] = NX2A_AREA[Game.GetScenarioID()] or 0
      NX2A[5] = v[0]
      NX2A[6] = v[1]
      if p.LIFE ~= nil then NX2A[7] = p.LIFE.fCurrentLife; NX2A[8] = p.LIFE.fMaxLife end
      NX2A[9] = Game.GetItemAmount(Game.GetPlayerName(), "ITEM_WEAPON_MISSILE_CURRENT")
      NX2A[10] = Game.GetItemAmount(Game.GetPlayerName(), "ITEM_WEAPON_MISSILE_MAX")
    end
  end)
  NX2A[3] = (NX2A[3] + 1) %% 1000000
  Game.AddSF(0, "NX2A_Tick", "i", chain)
end
function NX2A_Start()   -- a new chain of ticks; the one before stops at its next tick
  NX2A_CHAIN = NX2A_CHAIN + 1
  pcall(Game.AddSF, 0, "NX2A_Tick", "i", NX2A_CHAIN)
end
local nx2a_onload = Scenario.OnLoadScenarioFinished
function Scenario.OnLoadScenarioFinished()
  if nx2a_onload ~= nil then nx2a_onload() end
  NX2A_Start()
end
NX2A_Start()
''' % ', '.join(f'{a} = {i + 1}' for i, a in enumerate(AREAS))


# ---- pkg and toc ----
def pkg_read(data):
    n, = struct.unpack_from('<I', data, 8)
    return [(a, data[s:e]) for a, s, e in struct.iter_unpack('<QII', data[12:12 + 16 * n])]


def pkg_build(files):   # as MEDS writes a Dread pkg
    head = 12 + 16 * len(files); head += -head % 128 if files else 0
    body, entries = bytearray(), []
    for a, d in files:
        s = head + len(body); body += d; entries.append((a, s, head + len(body))); body += b'\0' * (-len(d) % 8)
    out = struct.pack('<III', head - 4, len(body), len(files)) + b''.join(struct.pack('<QII', *e) for e in entries)
    return out + b'\0' * (head - len(out)) + bytes(body)


def toc_read(data):
    n, = struct.unpack_from('<I', data, 0)
    return dict(struct.iter_unpack('<QI', data[4:4 + 12 * n]))


def toc_build(toc):
    toc[crc64('system/files.toc')] = 4 + 12 * len(toc)
    return struct.pack('<I', len(toc)) + b''.join(struct.pack('<QI', a, s) for a, s in toc.items())


# ---- bmmap: the types of CMinimapData (dread_types.json of MEDS), as (kind, ...) ----
_TYPES = {
    'CMinimapData': ('struct', {'uDataVersion': 'u32', 'gridDef': 'SGridDef', 'aNavmeshGeos': ('vec', 'SGeoData'), 'mapOccluderGeos': ('dict', 'str', ('dict', 'u64', 'SGeoData')),
                                'oEmmyRoomGeo': 'SGeoData', 'mapEmmyRoomGeos': ('dict', 'str', 'SGeoData'), 'mapHeatRoomGeos': ('dict', 'str', 'SGeoData'),
                                'mapFreezeRoomGeos': ('dict', 'str', 'SGeoData'), 'mapNoFreezeRoomGeos': ('dict', 'str', 'SGeoData'), 'mapVignetteGeos': ('dict', 'str', 'SGeoData'),
                                'mapHintGeos': ('dict', 'str', 'SGeoData'), 'mapWaterPoolGeos': ('dict', 'str', 'SGeoData'), 'mapMagnetSurfaces': ('dict', 'str', 'SMagnetSurfaceData'),
                                'aMovingMagnetSurfaces': ('vec', 'SMovingMagnetSurfaceData'), 'mapTransportSigns': ('dict', 'str', 'STransportSignData'),
                                'mapDoors': ('dict', 'str', 'SDoorData'), 'mapBlockages': ('dict', 'str', 'SEntityData'), 'mapUsables': ('dict', 'str', 'SEntityData'),
                                'mapItems': ('dict', 'str', 'SEntityData'), 'mapProps': ('dict', 'str', 'SEntityData'), 'mapCentralUnits': ('dict', 'str', 'SEntityData'),
                                'mapBosses': ('dict', 'str', 'SEntityData'), 'mapBossBattleLabels': ('dict', 'str', 'str'), 'mapAbilityLabels': ('dict', 'str', 'str'),
                                'mapVignetteOccludedIcons': ('dict', 'str', ('vec', 'str'))}),
    'SGridDef': ('struct', {'sScenarioId': 'str', 'vGridMin': 'v2', 'vGridMax': 'v2', 'aGridCellBlocks': ('vec', 'u32')}),
    'SGeoData': ('struct', {'aVertex': ('vec', 'v3'), 'aIndex': ('vec', 'u16')}),
    'SMagnetSurfaceData': ('struct', {'oPolyLine': 'CPolygon2D'}),
    'CPolygon2D': ('struct', {'bClosed': 'bool', 'oSegmentData': ('vec', 'SSegmentData'), 'bOutwardsNormal': 'bool'}),
    'SSegmentData': ('struct', {'vPos': 'v3'}),
    'SMovingMagnetSurfaceData': ('struct', {'sMoveEntityID': 'str', 'iEntityType': 'i32', 'oPathLine': 'SLogicSubPath', 'bInverseNormal': 'bool', 'fSurfaceLenght': 'f32',
                                            'vSurfaceStart': 'v2', 'vSurfaceEnd': 'v2'}),
    'SLogicSubPath': ('struct', {'tNodes': ('vec', 'SLogicPathNode')}),
    'SLogicPathNode': ('struct', {'vPos': 'v3', 'fSwarmRadius': 'f32', 'fDiversionChance': 'f32'}),
    'STransportSignData': ('struct', {'vPathStart': 'v2', 'vPathEnd': 'v2', 'sDestAreaId': 'str', 'fZOffset': 'f32'}),
    'SDoorData': ('struct', {'vPos': 'v2', 'oBoxL': 'CAABox2D', 'oBoxR': 'CAABox2D', 'sLeftIconId': 'str', 'sRightIconId': 'str', 'aRoomIds': ('vec', 'str'), 'sRoomId': 'str'}),
    'SEntityData': ('struct', {'vPos': 'v2', 'oBox': 'CAABox2D', 'sIconId': 'str', 'bFlipX': 'bool', 'bFlipY': 'bool'}),
    'CAABox2D': ('struct', {'Min': 'v2', 'Max': 'v2'}),
}
_PRIM = {'u32': '<I', 'i32': '<i', 'f32': '<f', 'u16': '<H', 'u64': '<Q', 'bool': '<?', 'v2': '<2f', 'v3': '<3f'}
_FIELDS = {name: {crc64(f): (f, t) for f, t in d[1].items()} for name, d in _TYPES.items()}


class _Reader:
    def __init__(self, data, pos=0): self.d, self.p = data, pos

    def take(self, fmt):
        v = struct.unpack_from(fmt, self.d, self.p); self.p += struct.calcsize(fmt)
        return v if len(v) > 1 else v[0]

    def value(self, t):
        if isinstance(t, tuple):
            n = self.take('<I')
            if t[0] == 'vec': return [self.value(t[1]) for _ in range(n)]
            return {self.value(t[1]): self.value(t[2]) for _ in range(n)}
        if t == 'str':
            e = self.d.index(b'\0', self.p); s = self.d[self.p:e].decode('utf-8', 'replace'); self.p = e + 1
            return s
        if t in _PRIM: return self.take(_PRIM[t])
        out, fields = {}, _FIELDS[t]
        for _ in range(self.take('<I')):
            h = self.take('<Q')
            if h not in fields: raise ValueError(f'{t}: unknown field {h:#018x} at {self.p - 8:#x}')
            f, ft = fields[h]; out[f] = self.value(ft)
        return out


def bmmap_read(data):
    r = _Reader(data)
    if r.take('<Q') != crc64('CMinimapData'): raise ValueError('not a CMinimapData file')
    r.take('<I')   # version (1.0.2)
    if r.take('<Q') != crc64('Root'): raise ValueError('no Root')
    root = r.value('CMinimapData')
    if r.p != len(data): raise ValueError(f'{len(data) - r.p} bytes left over')
    return root


def nx2m(root):   # companion_dread.c's map: "NX2M", version 1, bounds, triangles (x y × 3), doors (x y), items (x y)
    tris = []
    for g in root.get('aNavmeshGeos', []):
        v, ix = g.get('aVertex', []), g.get('aIndex', [])
        for i in range(0, len(ix) - 2, 3): tris.append((v[ix[i]][:2], v[ix[i + 1]][:2], v[ix[i + 2]][:2]))
    if not tris: raise ValueError('no navigation mesh')
    xs = [p[0] for t in tris for p in t]; ys = [p[1] for t in tris for p in t]
    doors = [d['vPos'] for d in root.get('mapDoors', {}).values() if 'vPos' in d]
    items = [d['vPos'] for d in root.get('mapItems', {}).values() if 'vPos' in d]
    out = bytearray(b'NX2M' + struct.pack('<I4f', 1, min(xs), min(ys), max(xs), max(ys)))
    out += struct.pack('<I', len(tris)) + b''.join(struct.pack('<6f', *t[0], *t[1], *t[2]) for t in tris)
    for pts in (doors, items): out += struct.pack('<I', len(pts)) + b''.join(struct.pack('<2f', *p) for p in pts)
    return bytes(out), len(tris), len(doors), len(items)


def find_romfs(folder):
    folder = Path(folder)
    if (folder / 'system' / 'files.toc').is_file(): return folder
    found = sorted(folder.glob('010093801237C000_build/extract/v*/RomFS'), key=lambda p: int(p.parent.name[1:]) if p.parent.name[1:].isdigit() else 0)
    if not found: raise SystemExit(f'no Metroid Dread RomFS in {folder} (port the game first, or give the RomFS folder)')
    return found[-1]


def make(folder, out=None, log=print):
    romfs = find_romfs(folder); out = Path(out) if out else Path(folder) / 'MetroidDread-companion'
    log(f'RomFS: {romfs}')
    toc = toc_read((romfs / 'system' / 'files.toc').read_bytes())
    pkg_path = romfs / 'packs' / 'system' / 'system.pkg'; files = pkg_read(pkg_path.read_bytes()); by_id = dict(files)
    (out / 'companion').mkdir(parents=True, exist_ok=True)
    for area in AREAS:
        a = crc64(f'maps/levels/c10_samus/{area}/{area}.bmmap')
        if a not in by_id: log(f'  {area}: no minimap data in system.pkg'); continue
        data, nt, nd, ni = nx2m(bmmap_read(by_id[a]))
        (out / 'companion' / f'{area}.nx2m').write_bytes(data); log(f'  {area}: {nt} triangles, {nd} doors, {ni} items')
    sc, orig = crc64(SCENARIO), crc64(ORIGINAL)
    if orig in by_id: raise SystemExit(f'{pkg_path} is already wrapped: give the original RomFS')
    if sc not in by_id: raise SystemExit(f'{SCENARIO} is not in {pkg_path}')
    wrapper = WRAPPER.encode('ascii')
    files = [(a, wrapper if a == sc else d) for a, d in files] + [(orig, by_id[sc])]
    (out / 'RomFS' / 'packs' / 'system').mkdir(parents=True, exist_ok=True); (out / 'RomFS' / 'system').mkdir(parents=True, exist_ok=True)
    (out / 'RomFS' / 'packs' / 'system' / 'system.pkg').write_bytes(pkg_build(files))
    toc[sc] = len(wrapper); toc[orig] = len(by_id[sc])
    (out / 'RomFS' / 'system' / 'files.toc').write_bytes(toc_build(toc))
    log(f'{SCENARIO} wrapped ({len(by_id[sc])} bytes kept as {ORIGINAL})\nCopy the contents of {out} over the game\'s data folder on the device '
        '(companion/ and RomFS/ beside mods/), with DM_COMPANION=1 in its env.txt')
    return out


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(prog='porter dread-companion'); ap.add_argument('folder'); ap.add_argument('-o', '--out')
    a = ap.parse_args(argv); make(a.folder, a.out); return 0


if __name__ == '__main__': sys.exit(main())
