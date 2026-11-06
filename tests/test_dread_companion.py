# porter/dread_companion.py (kalasyan fork): the map files for Metroid Dread's companion page
import gzip, struct
from porter import dread_companion as d


def bctex(w, h, fmt, bh_log2, texel_bytes, unit, pixels_at):
    """A .bctex as the game writes it: "MTXT", gzip(XTX: "DFvN" header, a texture block (type 2), a data block (type 3))"""
    bh = 1 << bh_log2; wide = (w + unit - 1) // unit; tall = (h + unit - 1) // unit; gobs = (wide * texel_bytes + 63) // 64
    size = gobs * 512 * bh * ((tall + 8 * bh - 1) // (8 * bh)); pix = bytearray(size)
    for y in range(tall):
        for x in range(wide):
            at = d._bl_offset(x, y, texel_bytes, bh, gobs); pix[at:at + texel_bytes] = pixels_at(x, y)
    info = bytearray(0x100); struct.pack_into('<II', info, 12, w, h); struct.pack_into('<I', info, 28, fmt); struct.pack_into('<I', info, 40 + 17 * 4, bh_log2)
    def block(kind, data): return b'HBvN' + struct.pack('<IQQI', 0, len(data), 36, kind) + b'\0' * 8 + bytes(data)
    xtx = b'DFvN' + struct.pack('<I', 16) + b'\0' * 8 + block(2, info) + block(3, pix)
    return b'MTXT' + b'\0' * 12 + gzip.compress(xtx)


def test_rgba8_deswizzle():
    t = d.Texture(bctex(40, 24, 0x25, 1, 4, 1, lambda x, y: bytes((x, y, x ^ y, 255))))
    assert (t.w, t.h) == (40, 24)
    assert all(t.get(x, y) == (x, y, x ^ y, 255) for y in range(24) for x in range(40))


def test_bc1_blocks():   # each block one solid colour: c0 = c1 = the block's 565 colour, all indices 0
    col = lambda x, y: (x * 2) << 11 | (y * 4) << 5 | 7
    t = d.Texture(bctex(64, 32, 0x42, 2, 8, 4, lambda x, y: struct.pack('<HHI', col(x, y), col(x, y), 0)))
    for by in range(8):
        for bx in range(16):
            assert t.get(bx * 4 + 3, by * 4 + 1)[:3] == d._rgb565(col(bx, by))


def test_icons_and_map():
    t = d.Texture(bctex(2048, 1024, 0x25, 4, 4, 1, lambda x, y: bytes((255, 0, 0, 255 if (x // 128 + y // 128) % 2 else 0))))
    out = d.icons_nx2i(t)
    assert out[:4] == b'NX2I' and struct.unpack_from('<III', out, 4) == (1, len(d.ICONS), d.ICON_PX)
    assert len(out) == 16 + len(d.ICONS) * (8 + d.ICON_PX ** 2 * 4)
    geo = {'aVertex': [(0, 0, 0), (1000, 0, 0), (0, 500, 0)], 'aIndex': [0, 1, 2]}
    root = {'aNavmeshGeos': [geo], 'mapHeatRoomGeos': {'a': geo},
            'mapDoors': {'d': {'vPos': (100, 50), 'oBoxL': {'Min': (80, 0), 'Max': (100, 100)}, 'oBoxR': {'Min': (100, 0), 'Max': (120, 100)},
                               'sLeftIconId': 'DoorPowerL', 'sRightIconId': 'DoorPowerR'}},
            'mapItems': {'i': {'vPos': (300, 60), 'oBox': {'Min': (3.4e38, 3.4e38), 'Max': (-3.4e38, -3.4e38)}, 'sIconId': 'ItemMissileTank'},
                         'j': {'vPos': (1, 1), 'sIconId': 'NoSuchIcon'}}}
    unknown = {}; data, nt, nd, ni = d.nx2m(root, unknown)
    assert (nt, nd, ni) == (1, 2, 3) and unknown == {'NoSuchIcon': 1}
    assert data[:8] == b'NX2M\2\0\0\0'
    at = 24 + 4 + 24
    counts = []
    for _ in d.LAYERS: n, = struct.unpack_from('<I', data, at); counts.append(n); at += 4 + 24 * n
    assert counts == [0, 1, 0, 0]
    n, = struct.unpack_from('<I', data, at); icons = [struct.unpack_from('<2H6f', data, at + 4 + 28 * i) for i in range(n)]
    assert icons[0][:2] == (d.ICON_NAMES.index('DoorPowerL'), d.F_BOX | d.F_DOOR) and icons[0][4:] == (80, 0, 100, 100)
    assert icons[1][4:] == (100, 0, 120, 100)
    assert icons[2][:4] == (d.ICON_NAMES.index('ItemMissileTank'), d.F_ITEM, 300, 60)


def test_title():
    def px(x, y): return bytes((250, 200, 200, 255)) if x < 1600 and y < 900 else bytes((255, 255, 255, 255))
    bg = d.Texture(bctex(2048, 1024, 0x25, 4, 4, 1, px))
    assert d._content(bg)[2:] == (1600, 900)
    out = d.title_nx2t(bg, bg, width=160)
    w, h = struct.unpack_from('<II', out, 8); assert (w, h) == (160, 90)
    at = 16 + w * h * 4; lw, lh = struct.unpack_from('<II', out, at); assert lw == 640
    a = out[at + 8 + 3]; mid = out[at + 8 + ((lh // 2) * lw + lw // 2) * 4 + 3]
    assert a == 0 and mid == 255
