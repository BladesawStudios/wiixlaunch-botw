#!/usr/bin/env python3
"""Offline viewer for the Wii U UI assets the GUI (include/wiixlaunch/botw/gui/)
borrows from the game - the same numbers docs/gui.md quotes came out of this.

    # decode a .bflim to PNG (de-tiles the GX2 surface, decodes BC4/BC5/A8/...)
    python tools/preview_ui_assets.py bflim  extracted/Common/timg/Nt_MsgWindowL_00^s.bflim -o out/

    # dump a .bffnt's FINF/TGLP and write each glyph sheet as PNG
    python tools/preview_ui_assets.py bffnt  extracted/Font_US/Normal_00.bffnt -o out/

    # print a .bflyt pane tree with materials, textures, text styles, window frames
    python tools/preview_ui_assets.py bflyt  extracted/Common/blyt/Message_00.bflyt

Extract the archives first (any SARC/Yaz0 tool; the Cemu update's
content/Pack/Bootup.pack holds Layout/Common.sblarc, content/Font/Font_US.sbfarc
holds the fonts). Needs Pillow. Wii U (big-endian) files only.

The de-tiler is a port of the AMD addrlib subset AboodXD's tools use
(tile modes 0-3 and the 2D thin macro tiles, 4 banks / 2 pipes, no AA).
"""
import argparse
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: pip install Pillow")

# --------------------------------------------------------------------------
# addrlib subset
# --------------------------------------------------------------------------
m_banks = 4; m_banksBitcount = 2; m_pipes = 2; m_pipesBitcount = 1
m_pipeInterleaveBytes = 256; m_pipeInterleaveBytesBitcount = 8
m_rowSize = 2048; m_swapSize = 256; m_splitSize = 2048
MicroTilePixels = 64


def thickness(tm):
    if tm in (3, 7, 11, 13, 15): return 4
    if tm in (16, 17): return 8
    return 1


def pix_index(x, y, bpp, tm, z=0):
    if bpp == 8:
        b = [x & 1, (x & 2) >> 1, (x & 4) >> 2, (y & 2) >> 1, y & 1, (y & 4) >> 2]
    elif bpp == 16:
        b = [x & 1, (x & 2) >> 1, (x & 4) >> 2, y & 1, (y & 2) >> 1, (y & 4) >> 2]
    elif bpp in (32, 96):
        b = [x & 1, (x & 2) >> 1, y & 1, (x & 4) >> 2, (y & 2) >> 1, (y & 4) >> 2]
    elif bpp == 64:
        b = [x & 1, y & 1, (x & 2) >> 1, (x & 4) >> 2, (y & 2) >> 1, (y & 4) >> 2]
    elif bpp == 128:
        b = [y & 1, x & 1, (x & 2) >> 1, (x & 4) >> 2, (y & 2) >> 1, (y & 4) >> 2]
    else:
        b = [x & 1, (x & 2) >> 1, y & 1, (x & 4) >> 2, (y & 2) >> 1, (y & 4) >> 2]
    r = 0
    for i, v in enumerate(b):
        r |= v << i
    th = thickness(tm)
    if th > 1:
        r |= (z & 1) << 6
        r |= ((z & 2) >> 1) << 7
    if th == 8:
        r |= ((z & 4) >> 2) << 8
    return r


def aspect(tm):
    if tm in (5, 9): return 2
    if tm in (6, 10): return 4
    return 1


def bank_swapped_width(tm, bpp, pitch):
    if tm not in (8, 9, 10, 11, 14, 15): return 0
    bytes_per_sample = 8 * bpp
    samples_per_tile = m_splitSize // bytes_per_sample if bytes_per_sample else 1
    slices_per_tile = max(1, 1 // samples_per_tile) if samples_per_tile else 1
    bytes_per_tile_slice = bytes_per_sample // slices_per_tile
    swap_tiles = max(1, (m_swapSize >> 1) // bpp)
    swap_width = swap_tiles * 8 * m_banks
    height_bytes = aspect(tm) * m_pipes * bpp // slices_per_tile
    swap_max = m_pipes * m_banks * m_rowSize // height_bytes
    swap_min = m_pipeInterleaveBytes * 8 * m_banks // bytes_per_tile_slice
    w = min(swap_max, max(swap_min, swap_width))
    while not w < 2 * pitch:
        w >>= 1
    return w


def addr_linear(x, y, bpp, pitch):
    return ((y * pitch + x) * bpp) // 8


def addr_micro(x, y, bpp, pitch, tm):
    th = 4 if tm == 3 else 1
    mtb = (MicroTilePixels * th * bpp + 7) // 8
    off = mtb * ((x >> 3) + (y >> 3) * (pitch >> 3))
    return off + ((bpp * pix_index(x, y, bpp, tm)) >> 3)


def addr_macro(x, y, bpp, pitch, height, tm, pipe_sw, bank_sw):
    th = thickness(tm)
    micro_tile_bits = bpp * th * MicroTilePixels
    micro_tile_bytes = (micro_tile_bits + 7) // 8
    elem_offset = bpp * pix_index(x, y, bpp, tm)
    sample_slice = 0
    if micro_tile_bytes > m_splitSize:
        samples_per_slice = m_splitSize // micro_tile_bytes
        num_sample_splits = max(1, 1 // samples_per_slice) if samples_per_slice else 1
        sample_slice = elem_offset // (micro_tile_bits // num_sample_splits)
        elem_offset %= micro_tile_bits // num_sample_splits
    elem_offset = (elem_offset + 7) // 8
    pipe = ((y >> 3) ^ (x >> 3)) & 1
    bank = (((y // (16 * m_pipes)) ^ (x >> 3)) & 1) | 2 * (((y // (8 * m_pipes)) ^ (x >> 4)) & 1)
    bank_pipe = pipe + m_pipes * bank
    swz = pipe_sw + m_pipes * bank_sw
    bank_pipe ^= m_pipes * sample_slice * ((m_banks >> 1) + 1) ^ swz
    bank_pipe %= m_pipes * m_banks
    pipe = bank_pipe % m_pipes
    bank = bank_pipe // m_pipes
    slice_bytes = (height * pitch * th * bpp + 7) // 8
    slice_offset = slice_bytes * (sample_slice // th)
    mtp = 8 * m_banks
    mth = 8 * m_pipes
    if tm in (5, 9):
        mtp >>= 1; mth *= 2
    elif tm in (6, 10):
        mtp >>= 2; mth *= 4
    mtpr = pitch // mtp
    mt_bytes = (th * bpp * mth * mtp + 7) // 8
    mtx = x // mtp
    mty = y // mth
    mt_off = (mtx + mtpr * mty) * mt_bytes
    if tm in (8, 9, 10, 11, 14, 15):
        order = [0, 1, 3, 2, 6, 7, 5, 4]
        bsw = bank_swapped_width(tm, bpp, pitch)
        bank ^= order[(mtp * mtx // bsw) & (m_banks - 1)]
    group_mask = (1 << m_pipeInterleaveBytesBitcount) - 1
    nsb = m_banksBitcount + m_pipesBitcount
    total = elem_offset + ((mt_off + slice_offset) >> nsb)
    hi = (total & ~group_mask) << nsb
    lo = total & group_mask
    return (bank << (m_pipesBitcount + m_pipeInterleaveBytesBitcount)) | (pipe << m_pipeInterleaveBytesBitcount) | lo | hi


def align(v, a):
    return (v + a - 1) // a * a


def surface_pitch_height(wblk, hblk, bpp, tm):
    if tm in (0, 1): return wblk, hblk
    if tm in (2, 3): return align(wblk, 8), align(hblk, 8)
    mtw = 8 * m_banks // aspect(tm)
    mth = aspect(tm) * 8 * m_pipes
    pitch_align = max(mtw, mtw * (m_pipeInterleaveBytes // bpp // (8 * thickness(tm))))
    return align(wblk, pitch_align), align(hblk, mth)


def detile(data, width, height, bpp, tm, swizzle, blocked):
    wb = (width + 3) // 4 if blocked else width
    hb = (height + 3) // 4 if blocked else height
    pitch, ph = surface_pitch_height(wb, hb, bpp, tm)
    bytes_per = bpp // 8
    out = bytearray(wb * hb * bytes_per)
    pipe_sw = (swizzle >> 8) & 1
    bank_sw = (swizzle >> 9) & 3
    for y in range(hb):
        for x in range(wb):
            if tm in (0, 1): pos = addr_linear(x, y, bpp, pitch)
            elif tm in (2, 3): pos = addr_micro(x, y, bpp, pitch, tm)
            else: pos = addr_macro(x, y, bpp, pitch, ph, tm, pipe_sw, bank_sw)
            o = (y * wb + x) * bytes_per
            if pos + bytes_per <= len(data):
                out[o:o + bytes_per] = data[pos:pos + bytes_per]
    return bytes(out), pitch, ph


# --------------------------------------------------------------------------
# pixel decoders
# --------------------------------------------------------------------------
def dec565(c):
    return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31


def bc1_block(b):
    c0, c1 = struct.unpack("<HH", b[:4])
    idx = struct.unpack("<I", b[4:8])[0]
    p0, p1 = dec565(c0), dec565(c1)
    if c0 > c1:
        pal = [p0, p1, tuple((2 * a + bb) // 3 for a, bb in zip(p0, p1)), tuple((a + 2 * bb) // 3 for a, bb in zip(p0, p1))]
        alpha = [255] * 4
    else:
        pal = [p0, p1, tuple((a + bb) // 2 for a, bb in zip(p0, p1)), (0, 0, 0)]
        alpha = [255, 255, 255, 0]
    return [pal[(idx >> (2 * i)) & 3] + (alpha[(idx >> (2 * i)) & 3],) for i in range(16)]


def bc4_block(b):
    a0, a1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(raw, width, height, fmt):
    img = Image.new("RGBA", (width, height))
    px = img.load()
    wb = (width + 3) // 4
    if fmt in ("BC1", "BC4A", "BC4L", "BC5"):
        bs = 16 if fmt == "BC5" else 8
        for by in range((height + 3) // 4):
            for bx in range(wb):
                blk = raw[(by * wb + bx) * bs:(by * wb + bx) * bs + bs]
                if len(blk) < bs: continue
                if fmt == "BC1":
                    vals = bc1_block(blk)
                elif fmt == "BC4A":
                    vals = [(255, 255, 255, v) for v in bc4_block(blk)]
                elif fmt == "BC4L":
                    vals = [(v, v, v, 255) for v in bc4_block(blk)]
                else:
                    r = bc4_block(blk[:8]); g = bc4_block(blk[8:])
                    vals = [(r[i], r[i], r[i], g[i]) for i in range(16)]
                for i in range(16):
                    x = bx * 4 + (i & 3); y = by * 4 + (i >> 2)
                    if x < width and y < height:
                        px[x, y] = vals[i]
    elif fmt == "A8":
        for y in range(height):
            for x in range(width):
                v = raw[y * width + x]; px[x, y] = (255, 255, 255, v)
    elif fmt == "L8":
        for y in range(height):
            for x in range(width):
                v = raw[y * width + x]; px[x, y] = (v, v, v, 255)
    elif fmt == "RGB565":
        for y in range(height):
            for x in range(width):
                c = struct.unpack("<H", raw[(y * width + x) * 2:(y * width + x) * 2 + 2])[0]
                px[x, y] = dec565(c) + (255,)
    elif fmt == "RGBA8":
        for y in range(height):
            for x in range(width):
                o = (y * width + x) * 4; px[x, y] = tuple(raw[o:o + 4])
    else:
        raise ValueError("unsupported format " + fmt)
    return img


BPP = {"BC1": 64, "BC4A": 64, "BC4L": 64, "BC5": 128, "A8": 8, "L8": 8, "RGB565": 16, "RGBA8": 32}
BLOCKED = {"BC1": 1, "BC4A": 1, "BC4L": 1, "BC5": 1, "A8": 0, "L8": 0, "RGB565": 0, "RGBA8": 0}
# BFLIM (Cafe) format byte -> name. Measured: ^s=16, ^t=17, ^d=1, ^r=15, ^f=3? (see docs/gui.md)
BFLIM_FMT = {0: "L8", 1: "A8", 3: "RGB565", 5: "RGB565", 9: "RGBA8", 12: "BC1", 15: "BC4L", 16: "BC4A", 17: "BC5", 20: "RGBA8", 21: "BC1"}
# BFFNT sheet format -> name. 12 is BC4 in the game's fonts (NOT BC1 as some tables say).
BFFNT_FMT = {7: "L8", 8: "A8", 12: "BC4A", 15: "BC4A"}


# --------------------------------------------------------------------------
# BFLIM
# --------------------------------------------------------------------------
def bflim_info(data):
    f = data[-0x28:]
    if f[:4] != b"FLIM" or f[0x14:0x18] != b"imag":
        raise ValueError("not a BFLIM")
    im = f[0x14:]
    w, h, al, fmt, sw = struct.unpack(">HHHBB", im[8:16])
    dsz = struct.unpack(">I", im[16:20])[0]
    return dict(width=w, height=h, align=al, format=fmt, tileMode=sw & 0x1F, swizzle=(sw >> 5) << 8, dataSize=dsz)


def load_bflim(path):
    """Decode one .bflim to an RGBA PIL image (used by preview_ui_compose)."""
    data = open(path, "rb").read()
    info = bflim_info(data)
    name = BFLIM_FMT.get(info["format"])
    if not name:
        raise ValueError("unsupported BFLIM format %d in %s" % (info["format"], path))
    raw, pitch, ph = detile(data[:info["dataSize"]], info["width"], info["height"],
                            BPP[name], info["tileMode"], info["swizzle"], BLOCKED[name])
    return decode(raw, info["width"], info["height"], name), info


def cmd_bflim(args):
    for path in args.files:
        data = open(path, "rb").read()
        info = bflim_info(data)
        name = BFLIM_FMT.get(info["format"])
        print("%s: %dx%d format %d (%s) tileMode %d swizzle 0x%x dataSize %d" % (
            os.path.basename(path), info["width"], info["height"], info["format"], name,
            info["tileMode"], info["swizzle"], info["dataSize"]))
        if not name or not args.out:
            continue
        raw, pitch, ph = detile(data[:info["dataSize"]], info["width"], info["height"], BPP[name], info["tileMode"], info["swizzle"], BLOCKED[name])
        img = decode(raw, info["width"], info["height"], name)
        if args.on_black:
            bg = Image.new("RGBA", img.size, (0, 0, 0, 255)); bg.alpha_composite(img); img = bg
        os.makedirs(args.out, exist_ok=True)
        outp = os.path.join(args.out, os.path.basename(path).replace("^", "_") + ".png")
        img.save(outp)
        print("  -> %s (pitch %d, padded height %d)" % (outp, pitch, ph))


# --------------------------------------------------------------------------
# BFFNT
# --------------------------------------------------------------------------
def cmd_bffnt(args):
    for path in args.files:
        d = open(path, "rb").read()
        if d[:4] != b"FFNT":
            print(path, "is not a BFFNT"); continue
        hdrsz = struct.unpack(">H", d[6:8])[0]
        off = hdrsz
        finf = d[off + 8:off + 8 + 0x18]
        ftype, height, width, ascent = struct.unpack(">BBBB", finf[0:4])
        linefeed, alt = struct.unpack(">HH", finf[4:8])
        dl, dgw, dcw, enc = struct.unpack(">bBBB", finf[8:12])
        tglp_off, cwdh_off, cmap_off = struct.unpack(">III", finf[12:24])
        t = d[tglp_off:tglp_off + 0x18]
        cw, ch, ns, mcw = struct.unpack(">BBBB", t[0:4])
        ssz = struct.unpack(">I", t[4:8])[0]
        bl, fmt, cols, rows, sw, sh = struct.unpack(">HHHHHH", t[8:20])
        doff = struct.unpack(">I", t[20:24])[0]
        print("%s: type %d width %d height %d ascent %d lineFeed %d alt %d defaults(left %d, glyph %d, char %d) enc %d" % (
            os.path.basename(path), ftype, width, height, ascent, linefeed, alt, dl, dgw, dcw, enc))
        print("  TGLP: cell %dx%d, %d sheet(s) of %dx%d, %d bytes each, format %d (%s), %d cols x %d rows, baseline %d, data @0x%x" % (
            cw, ch, ns, sw, sh, ssz, fmt, BFFNT_FMT.get(fmt), cols, rows, bl, doff))
        print("  CWDH @0x%x CMAP @0x%x" % (cwdh_off, cmap_off))
        name = BFFNT_FMT.get(fmt)
        if not name or not args.out:
            continue
        os.makedirs(args.out, exist_ok=True)
        for s in range(ns):
            sheet = d[doff + s * ssz:doff + (s + 1) * ssz]
            raw, pitch, ph = detile(sheet, sw, sh, BPP[name], 4, 0, BLOCKED[name])
            img = decode(raw, sw, sh, name)
            # Sheets are stored upside down relative to the layout art.
            img = img.transpose(Image.FLIP_TOP_BOTTOM)
            if args.on_black:
                bg = Image.new("RGBA", img.size, (0, 0, 0, 255)); bg.alpha_composite(img); img = bg
            outp = os.path.join(args.out, "%s_sheet%d.png" % (os.path.splitext(os.path.basename(path))[0], s))
            img.save(outp)
            print("  -> %s" % outp)


# --------------------------------------------------------------------------
# BFLYT
# --------------------------------------------------------------------------
def cstr(b):
    i = b.find(b"\0")
    return (b[:i] if i >= 0 else b).decode("ascii", "replace")


def cmd_bflyt(args):
    for path in args.files:
        d = open(path, "rb").read()
        if d[:4] != b"FLYT":
            print(path, "is not a BFLYT"); continue
        hdrsz = struct.unpack(">H", d[6:8])[0]
        ver = struct.unpack(">I", d[8:12])[0]
        nsec = struct.unpack(">H", d[16:18])[0]
        print("# %s  version %08x sections %d" % (path, ver, nsec))
        off = hdrsz
        textures, fonts, materials = [], [], []
        depth = 0
        for _ in range(nsec):
            mg = d[off:off + 4]
            sz = struct.unpack(">I", d[off + 4:off + 8])[0]
            body = d[off + 8:off + sz]
            if mg == b"lyt1":
                w, h = struct.unpack(">ff", body[4:12])
                print("lyt1 %s %gx%g" % (cstr(body[20:]), w, h))
            elif mg in (b"txl1", b"fnl1"):
                n = struct.unpack(">H", body[:2])[0]
                offs = struct.unpack(">%dI" % n, body[4:4 + 4 * n])
                names = [cstr(body[4 + o:]) for o in offs]
                (textures if mg == b"txl1" else fonts).extend(names)
                print(mg.decode(), names)
            elif mg == b"mat1":
                n = struct.unpack(">H", body[:2])[0]
                offs = struct.unpack(">%dI" % n, body[4:4 + 4 * n])
                for i, o in enumerate(offs):
                    m = d[off + o:]
                    name = cstr(m[:28])
                    fore, back = tuple(m[28:32]), tuple(m[32:36])
                    flags = struct.unpack(">I", m[36:40])[0]
                    tex_count = flags & 3
                    p = 40
                    texs = []
                    for _t in range(tex_count):
                        ti = struct.unpack(">H", m[p:p + 2])[0]; p += 4
                        texs.append(textures[ti] if ti < len(textures) else "?%d" % ti)
                    materials.append(dict(name=name, texs=texs))
                    if args.materials:
                        print("  mat[%d] %s fore=%s back=%s tex=%s flags=%#x" % (i, name, fore, back, texs, flags))
            elif mg in (b"pan1", b"pic1", b"txt1", b"wnd1", b"bnd1", b"prt1"):
                flags, origin, alpha, _ = struct.unpack(">BBBB", body[:4])
                name = cstr(body[4:28])
                tx, ty, tz, rx, ry, rz, sx, sy, w, h = struct.unpack(">10f", body[36:76])
                line = "%s%s %s vis=%d origin=%d alpha=%d pos=(%g,%g) rot=(%g,%g,%g) scale=(%g,%g) size=%gx%g" % (
                    "  " * depth, mg.decode(), name, flags & 1, origin, alpha, tx, ty, rx, ry, rz, sx, sy, w, h)
                b = body[76:]
                if mg == b"pic1":
                    colors = [tuple(b[i * 4:i * 4 + 4]) for i in range(4)]
                    mat = struct.unpack(">H", b[16:18])[0]
                    mm = materials[mat] if mat < len(materials) else dict(name="?", texs=[])
                    line += " colors=%s mat=%s tex=%s" % (colors, mm["name"], mm["texs"])
                elif mg == b"txt1":
                    tl, sl, mat, font = struct.unpack(">HHHH", b[:8])
                    al, la, tf = struct.unpack(">BBB", b[8:11])
                    text_off = struct.unpack(">I", b[16:20])[0]
                    top, bot = tuple(b[20:24]), tuple(b[24:28])
                    fsx, fsy, cs, ls = struct.unpack(">ffff", b[28:44])
                    mm = materials[mat] if mat < len(materials) else dict(name="?")
                    line += " font=%s size=(%g,%g) charSp=%g lineSp=%g top=%s bottom=%s align=%d lineAlign=%d mat=%s" % (
                        fonts[font] if font < len(fonts) else "?", fsx, fsy, cs, ls, top, bot, al, la, mm["name"])
                    try:
                        _, sox, soy, ssx, ssy = struct.unpack(">Iffff", b[44:64])
                        line += " shadow=(%g,%g) top=%s bot=%s" % (sox, soy, tuple(b[64:68]), tuple(b[68:72]))
                    except struct.error:
                        pass
                    if text_off and tl:
                        s = body[text_off - 8:text_off - 8 + tl]
                        line += " text=%r" % s.decode("utf-16-be", "replace").rstrip("\0")
                elif mg == b"wnd1":
                    fl, fr, ft, fb = struct.unpack(">HHHH", b[8:16])
                    nframes, wflags = struct.unpack(">BB", b[16:18])
                    coff, foff = struct.unpack(">II", b[20:28])
                    c = body[coff - 8:]
                    colors = [tuple(c[i * 4:i * 4 + 4]) for i in range(4)]
                    mat = struct.unpack(">H", c[16:18])[0]
                    mm = materials[mat] if mat < len(materials) else dict(name="?", texs=[])
                    fo = struct.unpack(">%dI" % nframes, body[foff - 8:foff - 8 + 4 * nframes])
                    frames = []
                    for f in fo:
                        fm, ftf = struct.unpack(">HB", body[f - 8:f - 8 + 3])
                        fmm = materials[fm] if fm < len(materials) else dict(name="?", texs=[])
                        frames.append((fmm["name"], fmm["texs"], ftf))
                    line += " frameSize=(%d,%d,%d,%d) frames=%d wflags=%#x content: colors=%s mat=%s tex=%s frames=%s" % (
                        fl, fr, ft, fb, nframes, wflags, colors, mm["name"], mm["texs"], frames)
                print(line)
            elif mg == b"pas1":
                depth += 1
            elif mg == b"pae1":
                depth -= 1
            off += sz


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("bflim", help="decode .bflim textures to PNG"); p.add_argument("files", nargs="+")
    p.add_argument("-o", "--out", help="output directory for PNGs"); p.add_argument("--on-black", action="store_true")
    p.set_defaults(func=cmd_bflim)
    p = sub.add_parser("bffnt", help="dump a .bffnt and its glyph sheets"); p.add_argument("files", nargs="+")
    p.add_argument("-o", "--out"); p.add_argument("--on-black", action="store_true")
    p.set_defaults(func=cmd_bffnt)
    p = sub.add_parser("bflyt", help="dump a .bflyt pane tree"); p.add_argument("files", nargs="+")
    p.add_argument("--materials", action="store_true", help="also list materials")
    p.set_defaults(func=cmd_bflyt)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
