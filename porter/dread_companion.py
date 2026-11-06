# Metroid Dread's companion page (kalasyan fork): what the page on a second screen needs from the game's files, as files to copy over the data pack.
#   companion/<area>.nx2m           each area's map, from its minimap data (maps/levels/c10_samus/<area>/<area>.bmmap): the navigation mesh's
#                                   triangles, the water/heat/freeze/EMMI rooms, the doors, items, stations... with their map icons, in the game's
#                                   world units (nxnative/dm/android/companion_dread.c draws them)
#   companion/icons.nx2i            the game's map icons (textures/system/minimap/icons/icons.bctex), decoded
#   companion/title.nx2t            the game's logo and Artaria's illustration, for the page before a save is loaded
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
# Areas are numbered as AREAS (1 = s010_cave); seq counts ticks so the host can tell the live table from an old one. The ticks start when the script
# loads and again when an area finishes loading (one chain at a time: a tick of an older chain ends it).
WRAPPER = '''Game.ImportLibrary("system/scripts/nx2a_scenario_original.lua")
-- nx2android companion page (kalasyan fork): the player's state for the port's second screen
-- Ticks go through Game.AddSF as the game's own scripts call it: a Scenario.* function by its full name, a delay and a format of "s" (the game never uses "i")
NX2A = NX2A or {1357.25, -8642.75, 0, 0, 0, 0, 0, 0, 0, 0, 1}
NX2A_AREA = {%s}
NX2A_CHAIN = NX2A_CHAIN or 0
function Scenario.NX2A_Tick(chain)
  if chain ~= tostring(NX2A_CHAIN) then return end
  NX2A[3] = (NX2A[3] + 1) %% 1000000
  pcall(function()
    local id = Scenario.CurrentScenarioID
    if id ~= nil then NX2A[4] = NX2A_AREA[id] or 0 end
    local p = Game.GetPlayer()
    if p ~= nil then
      local v = p.vPos
      NX2A[5] = v[0]
      NX2A[6] = v[1]
    end
  end)
  pcall(function()
    local l = Game.GetPlayer().LIFE
    NX2A[7] = l.fCurrentLife; NX2A[8] = l.fMaxLife
  end)
  pcall(function()
    NX2A[9] = Game.GetItemAmount(Game.GetPlayerName(), "ITEM_WEAPON_MISSILE_CURRENT")
    NX2A[10] = Game.GetItemAmount(Game.GetPlayerName(), "ITEM_WEAPON_MISSILE_MAX")
  end)
  Game.AddSF(0.05, "Scenario.NX2A_Tick", "s", chain)
end
function Scenario.NX2A_Start()   -- a new chain of ticks; the one before stops at its next tick
  NX2A_CHAIN = NX2A_CHAIN + 1
  pcall(Game.AddSF, 0.05, "Scenario.NX2A_Tick", "s", tostring(NX2A_CHAIN))
end
local nx2a_onload = Scenario.OnLoadScenarioFinished
function Scenario.OnLoadScenarioFinished(...)
  if nx2a_onload ~= nil then nx2a_onload(...) end
  Scenario.NX2A_Start()
end
Scenario.NX2A_Start()
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


# ---- the game's map icons: textures/system/minimap/icons/icons.bctex, a 16-column atlas of 128-pixel cells (cell names from eden-duo-companions'
# Metroid Dread package, GPL-3.0; the ink rects are where a door or blockage icon has its pixels, stretched over the thing's box) ----
ICON_ATLAS = 'textures/system/minimap/icons/icons.bctex'
ICONS = {  # name: (row, column[, ink x0, y0, x1, y1 in the cell])
    'Samus': (0, 0), 'Emmy': (0, 2), 'CentralUnit': (0, 3), 'ItemSphere': (0, 4), 'ItemCube': (2, 9), 'ItemEnergyTank': (0, 5),
    'ItemEnergyFragment': (0, 6), 'ItemMissileTank': (0, 7), 'ItemMissileTankPlus': (0, 8), 'ItemPowerbombExpansion': (0, 9),
    'DoorPowerL': (1, 0, 43, 27, 84, 101), 'DoorPowerR': (1, 1, 43, 27, 85, 101), 'DoorPowerLockedL': (2, 0, 43, 27, 85, 114),
    'DoorPowerLockedR': (2, 1, 43, 27, 98, 114), 'DoorFrame': (0, 12, 44, 25, 86, 103), 'DoorFrame4x2': (0, 13, 39, 14, 90, 115),
    'DoorFrameTrap': (5, 9, 14, 51, 114, 77), 'DoorFrameL': (0, 12, 44, 25, 86, 103), 'DoorFrameR': (0, 12, 44, 25, 86, 103),
    'DoorClosedL': (0, 14, 45, 27, 84, 101), 'DoorClosedR': (0, 14, 45, 27, 84, 101), 'DoorOpenedL': (0, 14, 45, 27, 84, 101),
    'DoorOpenedR': (0, 14, 45, 27, 84, 101), 'DoorChargeL': (2, 2, 45, 27, 83, 101), 'DoorChargeR': (2, 3, 45, 27, 83, 101),
    'DoorChargeLockedL': (2, 4, 45, 27, 83, 101), 'DoorChargeLockedR': (2, 5, 44, 27, 83, 101), 'DoorGrappleL': (1, 2, 43, 26, 84, 101),
    'DoorGrappleR': (1, 3, 45, 27, 85, 101), 'DoorWideL': (1, 4, 55, 28, 76, 101), 'DoorWideR': (1, 5, 52, 28, 73, 101),
    'DoorPresenceL': (1, 6, 19, 24, 119, 102), 'DoorPresenceR': (1, 7, 10, 24, 110, 102), 'DoorShutter': (1, 9, 44, 14, 86, 113),
    'DoorEmmyOpen': (1, 13, 42, 14, 86, 113), 'DoorEmmyClosed': (2, 13, 42, 13, 86, 112), 'DoorCentralUnit': (1, 14, 45, 25, 81, 102),
    'DoorThermalOpen': (1, 11, 41, 14, 88, 113), 'DoorThermalClosed': (2, 11, 41, 14, 88, 113), 'DoorThermalTrapOpen': (1, 10, 14, 51, 115, 77),
    'DoorThermalTrapClosed': (2, 10, 14, 51, 114, 77),
    'BlockageMissileL': (3, 6, 9, 5, 75, 123), 'BlockageMissileR': (3, 7, 53, 5, 115, 123), 'BlockageSuperMissileL': (3, 0, 23, 6, 75, 121),
    'BlockageSuperMissileR': (3, 1, 53, 6, 125, 121), 'BlockagePlasmaL': (3, 2, 42, 30, 75, 97), 'BlockagePlasmaR': (3, 3, 53, 30, 74, 97),
    'BlockageWaveL': (3, 4, 54, 31, 75, 98), 'BlockageWaveR': (3, 5, 54, 31, 75, 98), 'BlockageDisabledL': (5, 0, 25, 24, 104, 105),
    'BlockageDisabledR': (5, 0, 25, 24, 104, 105),
    'PropFootstepPlatform': (1, 8), 'PropHyperBeam': (1, 15), 'PropMultiLockon': (1, 12), 'PropEmmyValve': (2, 14), 'PropGrappleBlockL': (3, 8),
    'PropGrappleBlockR': (3, 9), 'PropGrappleBoxL': (3, 10), 'PropGrappleBoxR': (3, 11), 'PropWideBeamBoxL': (3, 12), 'PropWideBeamBoxR': (3, 13),
    'PropMorphTunnelEntry': (3, 14), 'PropMorphTunnelExit': (3, 15), 'PropZipline': (6, 0), 'PropSwingPoint': (2, 15), 'PropWarlotus': (2, 12),
    'PropTurbineActivator': (4, 0),
    'UsableStationEnergy': (4, 13), 'DisabledStationEnergy': (4, 13), 'UsableStationAmmo': (4, 14), 'DisabledStationAmmo': (4, 14),
    'UsableStationTotal': (4, 12), 'DisabledStationTotal': (4, 12), 'UsableWaterValve': (4, 0), 'UsableThermalDevice': (4, 0),
    'UsablePowerGenerator': (4, 0), 'DisabledTeleport': (4, 6), 'UsableTeleportA': (4, 6), 'UsableTeleportE': (4, 7), 'UsableTeleportI': (4, 8),
    'UsableTeleportO': (4, 9), 'UsableTeleportU': (4, 10), 'UsableTeleportX': (4, 11), 'UsableTeleportY': (5, 6), 'UsableTeleportZ': (4, 11),
    'UsableTrain': (4, 1), 'UsableElevator': (4, 2), 'DisabledElevator': (4, 2), 'UsableStationSave': (4, 3), 'DisabledStationSave': (4, 3),
    'UsableStationMap': (4, 4), 'DisabledStationMap': (4, 4), 'UsableAccessPoint': (4, 5), 'DisabledAccessPoint': (4, 5), 'UsableTransport': (2, 8),
    'Gunship': (2, 7), 'Boss': (5, 2), 'BossDefeated': (5, 2),
}
ICON_NAMES = list(ICONS)   # an icon's number in icons.nx2i and the maps; companion_dread.c knows 0 = Samus, 5 = energy tank, 7 = missile tank
ICON_PX = 64               # each icon's size in icons.nx2i


def _bl_offset(x, y, bpp, bh, gobs):   # where texel (x, y) sits in a block-linear (Tegra) image
    gob = (y // (8 * bh)) * 512 * bh * gobs + (x * bpp // 64) * 512 * bh + ((y % (8 * bh)) // 8) * 512
    xb = x * bpp
    return gob + ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((xb % 32) // 16) * 32 + (y % 2) * 16 + (xb % 16)


def _rgb565(c): return ((c >> 11 & 31) * 255 + 15) // 31, ((c >> 5 & 63) * 255 + 31) // 63, ((c & 31) * 255 + 15) // 31


def _bc_colors(b, at, bc1):
    c0, c1, bits = struct.unpack_from('<HHI', b, at); p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or not bc1: pal = [p0 + (255,), p1 + (255,), tuple((2 * a + c) // 3 for a, c in zip(p0, p1)) + (255,), tuple((a + 2 * c) // 3 for a, c in zip(p0, p1)) + (255,)]
    else: pal = [p0 + (255,), p1 + (255,), tuple((a + c) // 2 for a, c in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    return [pal[bits >> 2 * i & 3] for i in range(16)]


def _bc3_alpha(b, at):
    a0, a1 = b[at], b[at + 1]; bits = int.from_bytes(b[at + 2:at + 8], 'little')
    if a0 > a1: pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else: pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[bits >> 3 * i & 7] for i in range(16)]


class Texture:
    """A decoded .bctex ("MTXT" + gzip around an NVN XTX), as Eden's DecodeBctex reads it; pixels decoded on demand, as RGBA tuples."""
    def __init__(self, data):
        import zlib
        if data[:4] != b'MTXT': raise ValueError('not a bctex')
        gz = data.find(b'\x1f\x8b\x08')
        if gz < 0: raise ValueError('bctex: no gzip stream')
        raw = zlib.decompressobj(31).decompress(data[gz:])
        x = raw.find(b'DFvN')
        if x < 0: raise ValueError('bctex: no XTX container')
        at, self.w, self.h, self.fmt, self.bh, pix = x + struct.unpack_from('<I', raw, x + 4)[0], 0, 0, 0, 16, None
        while at + 36 <= len(raw) and raw[at:at + 4] == b'HBvN':
            size, off = struct.unpack_from('<QQ', raw, at + 8); kind, = struct.unpack_from('<I', raw, at + 24); d = at + off
            if d + size > len(raw): break
            if kind == 2 and size >= 0x70:
                self.w, self.h = struct.unpack_from('<II', raw, d + 12); self.fmt, = struct.unpack_from('<I', raw, d + 28)
                self.bh = 1 << struct.unpack_from('<I', raw, d + 40 + 17 * 4)[0]
            elif kind == 3: pix = raw[d:d + size]
            at = d + size
        if not self.w or not pix: raise ValueError('bctex: no texture')
        if self.fmt not in (0x42, 0x44, 0x25, 0x6D): raise ValueError(f'bctex: format {self.fmt:#x} not decoded')
        self.pix, self.cache = pix, {}

    def block(self, bx, by):   # a 4x4 block of a BC texture: 16 RGBA tuples
        k = (bx, by)
        if k not in self.cache:
            bc1 = self.fmt == 0x42; n = 8 if bc1 else 16; wide = (self.w + 3) // 4
            at = _bl_offset(bx, by, n, self.bh, (wide * n + 63) // 64)
            if at + n > len(self.pix): self.cache[k] = [(0, 0, 0, 0)] * 16
            elif bc1: self.cache[k] = _bc_colors(self.pix, at, True)
            else: self.cache[k] = [c[:3] + (a,) for c, a in zip(_bc_colors(self.pix, at + 8, False), _bc3_alpha(self.pix, at))]
        return self.cache[k]

    def get(self, x, y):
        if self.fmt in (0x42, 0x44): return self.block(x // 4, y // 4)[(y % 4) * 4 + x % 4]
        at = _bl_offset(x, y, 4, self.bh, (self.w * 4 + 63) // 64)
        r, g, b, a = self.pix[at:at + 4] if at + 4 <= len(self.pix) else b'\0\0\0\0'
        return (b, g, r, a) if self.fmt == 0x6D else (r, g, b, a)


def icons_nx2i(tex, cell=128, size=ICON_PX):   # "NX2I", version 1, count, size; per icon its ink rect (4 u16, in its own pixels) and size x size RGBA
    f = cell // size; out = bytearray(b'NX2I' + struct.pack('<III', 1, len(ICONS), size))
    for name in ICON_NAMES:
        r, c, *ink = ICONS[name]; ink = ink or [0, 0, cell, cell]
        out += struct.pack('<4H', *(v // f for v in ink))
        for y in range(size):
            for x in range(size):
                s = [0, 0, 0, 0]
                for j in range(f):
                    for i in range(f):
                        p = tex.get(c * cell + x * f + i, r * cell + y * f + j); a = p[3]
                        s[0] += p[0] * a; s[1] += p[1] * a; s[2] += p[2] * a; s[3] += a
                a = s[3]; out += bytes((s[0] // a, s[1] // a, s[2] // a, a // (f * f)) if a else (0, 0, 0, 0))
    return bytes(out)


def find_asset(romfs, path):   # a RomFS file, loose or in one of the packs
    loose = Path(romfs) / path
    if loose.is_file(): return loose.read_bytes()
    want = crc64(path)
    for pkg in sorted((Path(romfs) / 'packs').rglob('*.pkg')):
        with open(pkg, 'rb') as f:
            head = f.read(12)
            if len(head) < 12: continue
            n, = struct.unpack_from('<I', head, 8)
            for a, s, e in struct.iter_unpack('<QII', f.read(16 * n)):
                if a == want: f.seek(s); return f.read(e - s)
    return None


# ---- the page before a save is loaded: the game's logo over an area's illustration ----
# The illustrations (textures/gui/textures/ir_<area>.bctex, 2048x1024) fill the top-left of the texture and leave the rest white; the logo is only in
# the gallery's er_dread.bctex, cropped (with soft edges) so that none of that picture shows. Written as companion/title.nx2t: "NX2T", version 1,
# the background (u32 w, h, RGBA) and the logo (u32 w, h, RGBA).
TITLE_BG = 'textures/gui/textures/ir_cave.bctex'
TITLE_LOGO = 'textures/gui/textures/er_dread.bctex'
LOGO_RECT = (1085, 15, 1575, 220)   # x0 y0 x1 y1 in er_dread, full size
LOGO_WHITE = (1090, 20, 1570, 150)  # where "METROID" is: its white letters are kept
LOGO_RED = (1150, 145, 1485, 215)   # where "DREAD" is: its red letters are kept


def _logo(tex, width=None):
    """The logo's letters (white METROID, red DREAD) with their own colours, as RGBA at er_dread's own size (the page scales it smoothly):
    each word keyed by its colour inside its box, then what is thinner than a letter's stroke (the picture's streaks, which cross and touch the
    letters) taken out: an opening of the alpha over 7x7, red kept only where it is 6 or more pixels tall, and specks under 150 pixels dropped"""
    x0, y0, x1, y1 = LOGO_RECT; w, h = x1 - x0, y1 - y0
    inr = lambda r, x, y: r[0] <= x < r[2] and r[1] <= y < r[3]
    rgb, al, red = [], [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = tex.get(x, y); lo, hi = min(r, g, b), max(r, g, b)
            aw = min(255, max(0, (lo - 150) * 255 // 50)) if inr(LOGO_WHITE, x, y) and hi - lo <= 45 else 0
            ar = min(255, max(0, (r - max(g, b) - 60) * 255 // 90)) if inr(LOGO_RED, x, y) and r > 120 else 0
            rgb.append((r, g, b)); al.append(max(aw, ar) * a // 255); red.append(ar > aw)
    def over(src, pick, n=3):
        rows = [[pick(src[y * w + max(0, x - n):y * w + min(w, x + n + 1)]) for x in range(w)] for y in range(h)]
        return [pick([rows[yy][x] for yy in range(max(0, y - n), min(h, y + n + 1))]) for y in range(h) for x in range(w)]
    op = over(over(al, min), max); al = [min(a, o) for a, o in zip(al, op)]
    for x in range(w):   # red runs under 6 pixels tall (the dashes between DREAD's letters)
        y = 0
        while y < h:
            if not (red[y * w + x] and al[y * w + x] > 40): y += 1; continue
            e = y
            while e < h and red[e * w + x] and al[e * w + x] > 40: e += 1
            if e - y < 6:
                for k in range(y, e): al[k * w + x] = 0
            y = e
    seen = [False] * (w * h)   # specks
    for i in range(w * h):
        if seen[i] or al[i] <= 40: continue
        comp, st = [], [i]; seen[i] = True
        while st:
            j = st.pop(); comp.append(j); jx, jy = j % w, j // w
            for k in (j - 1 if jx else -1, j + 1 if jx < w - 1 else -1, j - w, j + w):
                if 0 <= k < w * h and not seen[k] and al[k] > 40: seen[k] = True; st.append(k)
        if len(comp) < 150:
            for j in comp: al[j] = 0
    out = bytearray()
    for (r, g, b), a in zip(rgb, al): out += bytes((r, g, b, a))
    return w, h, bytes(out)


def _content(tex, step=8):   # the part of an illustration that is not the white padding
    def ink(x, y): r, g, b, a = tex.get(x, y); return a > 16 and min(r, g, b) < 240
    xs = [x for x in range(0, tex.w, step) if any(ink(x, y) for y in range(0, tex.h, step * 4))]
    ys = [y for y in range(0, tex.h, step) if any(ink(x, y) for x in range(0, tex.w, step * 4))]
    if not xs or not ys: return 0, 0, tex.w, tex.h
    x1, y1 = min(tex.w, xs[-1] + step), min(tex.h, ys[-1] + step)
    while x1 > xs[-1] + 1 and not any(ink(x1 - 1, y) for y in range(ys[0], y1, 2)): x1 -= 1
    while y1 > ys[-1] + 1 and not any(ink(x, y1 - 1) for x in range(xs[0], x1, 2)): y1 -= 1
    return xs[0], ys[0], x1, y1


def _crop(tex, rect, width, feather=0):   # rect of tex scaled to width pixels across (box filtered), RGBA bytes; feather: alpha fading at the edges
    x0, y0, x1, y1 = rect; f = (x1 - x0) / width; h = max(1, round((y1 - y0) / f)); out = bytearray()
    n = max(1, int(f)); fe = feather / f if feather else 0
    for j in range(h):
        for i in range(width):
            sx, sy = x0 + i * f, y0 + j * f; acc = [0, 0, 0, 0]
            for b in range(n):
                for a in range(n):
                    p = tex.get(min(x1 - 1, int(sx) + a), min(y1 - 1, int(sy) + b))
                    for c in range(4): acc[c] += p[c]
            px = [v // (n * n) for v in acc]
            if fe:
                d = min(i, j, width - 1 - i, h - 1 - j); px[3] = px[3] * min(1.0, d / fe) if d < fe else px[3]
            out += bytes(int(v) for v in px)
    return width, h, bytes(out)


def title_nx2t(bg, logo, width=1280):
    w, h, px = _crop(bg, _content(bg), width)
    out = bytearray(b'NX2T' + struct.pack('<III', 1, w, h)) + px
    lw, lh, lp = _logo(logo)
    return bytes(out + struct.pack('<II', lw, lh) + lp)


LAYERS = ('mapWaterPoolGeos', 'mapHeatRoomGeos', 'mapFreezeRoomGeos', 'mapEmmyRoomGeos')   # companion_dread.c draws them in this order
F_BOX, F_FLIPX, F_FLIPY, F_DOOR, F_ITEM = 1, 2, 4, 8, 16
_BIG = 1e30


def _tris(geos):
    tris = []
    for g in geos:
        v, ix = g.get('aVertex', []), g.get('aIndex', [])
        for i in range(0, len(ix) - 2, 3):
            if max(ix[i:i + 3]) < len(v): tris.append((v[ix[i]][:2], v[ix[i + 1]][:2], v[ix[i + 2]][:2]))
    return tris


def _box(b):
    if not b or 'Min' not in b or 'Max' not in b: return None
    v = (*b['Min'], *b['Max'])
    return None if any(abs(c) > _BIG or c != c for c in v) or v[2] <= v[0] or v[3] <= v[1] else v


def map_icons(root, unknown=None):   # (icon, flags, x, y, box x0, y0, x1, y1): doors as two halves of their boxes, as eden-duo's dread_mapgen
    out = []
    def known(n):
        if n in ICONS: return True
        if n and unknown is not None: unknown[n] = unknown.get(n, 0) + 1
        return False
    for d in root.get('mapDoors', {}).values():
        if 'vPos' not in d: continue
        bs = [b for b in (_box(d.get('oBoxL')), _box(d.get('oBoxR'))) if b]
        x, y = d['vPos']
        u = (min(b[0] for b in bs), min(b[1] for b in bs), max(b[2] for b in bs), max(b[3] for b in bs)) if bs else (x - 150, y - 150, x + 150, y + 150)
        cx, cy = (u[0] + u[2]) / 2, (u[1] + u[3]) / 2
        l, r = d.get('sLeftIconId', ''), d.get('sRightIconId', ''); both = known(l) & known(r)
        for n, half in ((l, (u[0], u[1], cx, u[3])), (r, (cx, u[1], u[2], u[3]))):
            if n in ICONS: out.append((ICON_NAMES.index(n), F_BOX | F_DOOR, cx, cy, *(half if both else u)))
    for cat, fl in (('mapBlockages', F_BOX), ('mapItems', F_ITEM), ('mapUsables', 0), ('mapCentralUnits', 0), ('mapBosses', 0), ('mapProps', 0)):
        for e in root.get(cat, {}).values():
            n = e.get('sIconId', '')
            if 'vPos' not in e or not known(n): continue
            x, y = e['vPos']; b = _box(e.get('oBox')) if fl & F_BOX else None
            f = (fl if b else fl & ~F_BOX) | (F_FLIPX if e.get('bFlipX') else 0) | (F_FLIPY if e.get('bFlipY') else 0)
            out.append((ICON_NAMES.index(n), f, x, y, *(b or (x, y, x, y))))
    return out


def nx2m(root, unknown=None):
    """companion_dread.c's map: "NX2M", version 2, bounds (4 f32), the navigation mesh's triangles (u32 count, x y x 3 each), the 4 LAYERS' triangles,
    the icons (u32 count; u16 icon, u16 flags, f32 x, y, box x0 y0 x1 y1 each)"""
    tris = _tris(root.get('aNavmeshGeos', []))
    if not tris: raise ValueError('no navigation mesh')
    xs = [p[0] for t in tris for p in t]; ys = [p[1] for t in tris for p in t]
    out = bytearray(b'NX2M' + struct.pack('<I4f', 2, min(xs), min(ys), max(xs), max(ys)))
    def put(ts): out.extend(struct.pack('<I', len(ts)) + b''.join(struct.pack('<6f', *t[0], *t[1], *t[2]) for t in ts))
    put(tris)
    for k in LAYERS: put(_tris(root.get(k, {}).values()))
    icons = map_icons(root, unknown)
    out += struct.pack('<I', len(icons)) + b''.join(struct.pack('<2H6f', *i) for i in icons)
    return bytes(out), len(tris), sum(1 for i in icons if i[1] & F_DOOR), len(icons)


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
        unknown = {}; data, nt, nd, ni = nx2m(bmmap_read(by_id[a]), unknown)
        (out / 'companion' / f'{area}.nx2m').write_bytes(data); log(f'  {area}: {nt} triangles, {nd} door halves, {ni} icons')
        if unknown: log(f'    icons not drawn: {", ".join(f"{k} x{v}" for k, v in sorted(unknown.items()))}')
    atlas = find_asset(romfs, ICON_ATLAS)
    if atlas is None: log(f'  no {ICON_ATLAS}: the page draws plain shapes for the icons')
    else:
        try: (out / 'companion' / 'icons.nx2i').write_bytes(icons_nx2i(Texture(atlas))); log(f'  {len(ICONS)} map icons from {ICON_ATLAS}')
        except ValueError as e: log(f'  {ICON_ATLAS}: {e}: the page draws plain shapes for the icons')
    bg, logo = find_asset(romfs, TITLE_BG), find_asset(romfs, TITLE_LOGO)
    if bg is None or logo is None: log(f'  no {TITLE_BG if bg is None else TITLE_LOGO}: the page shows text before a save is loaded')
    else:
        try: (out / 'companion' / 'title.nx2t').write_bytes(title_nx2t(Texture(bg), Texture(logo))); log('  the logo and an illustration for the page before a save is loaded')
        except ValueError as e: log(f'  the title page: {e}: the page shows text before a save is loaded')
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
