#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Render captured ANSI (.ansi), DOS 80x25 buffer (.dosbuf), and Windows DIB (.bmp) screens to PNG."""

import argparse
import gzip
import os
import struct
import zlib


COLS = 80
ROWS = 25
GLYPH_W = 8
GLYPH_H = 16

# Classic CGA/EGA/VGA 16-color RGB palette (bit-7 blink disabled => idx 8-15 are valid backgrounds)
VGA16_RGB = [
    (0, 0, 0),        # 0: Black
    (0, 0, 170),      # 1: Blue
    (0, 170, 0),      # 2: Green
    (0, 170, 170),    # 3: Cyan
    (170, 0, 0),      # 4: Red
    (170, 0, 170),    # 5: Magenta
    (170, 85, 0),     # 6: Brown
    (170, 170, 170),  # 7: Light Grey
    (85, 85, 85),     # 8: Dark Grey
    (85, 85, 255),    # 9: Bright Blue
    (85, 255, 85),    # 10: Bright Green
    (85, 255, 255),   # 11: Bright Cyan
    (255, 85, 85),    # 12: Bright Red
    (255, 85, 255),   # 13: Bright Magenta
    (255, 255, 85),   # 14: Yellow
    (255, 255, 255),  # 15: Bright White
]

# ANSI SGR indices (0..7) use RGB order where 1=Red, 4=Blue, 3=Yellow, 6=Cyan
ANSI_TO_VGA = [0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15]


def color_from_index(idx: int) -> tuple:
    if 0 <= idx < 16:
        return VGA16_RGB[ANSI_TO_VGA[idx]]
    if 16 <= idx < 232:
        n = idx - 16
        r = n // 36
        g = (n // 6) % 6
        b = n % 6
        conv = lambda v: 0 if v == 0 else 55 + v * 40
        return (conv(r), conv(g), conv(b))
    if 232 <= idx <= 255:
        v = 8 + (idx - 232) * 10
        return (v, v, v)
    return (170, 170, 170)


def build_box_glyphs() -> dict:
    """Construct crisp 8x16 bitmaps for single/double box drawing and block elements."""
    glyphs = {}

    def make(up=False, dn=False, lf=False, rt=False):
        rows = [0] * GLYPH_H
        for y in range(GLYPH_H):
            b = 0
            if (up and y <= 7) or (dn and y >= 7):
                b |= 0x10  # x=3 (0-based from left bit 7)
            if y == 7:
                if lf:
                    b |= 0xF0
                if rt:
                    b |= 0x1F
            rows[y] = b
        return bytes(rows)

    glyphs["\u250c"] = make(dn=True, rt=True)
    glyphs["\u2510"] = make(dn=True, lf=True)
    glyphs["\u2514"] = make(up=True, rt=True)
    glyphs["\u2518"] = make(up=True, lf=True)
    glyphs["\u2500"] = make(lf=True, rt=True)
    glyphs["\u2502"] = make(up=True, dn=True)
    glyphs["\u251c"] = make(up=True, dn=True, rt=True)
    glyphs["\u2524"] = make(up=True, dn=True, lf=True)
    glyphs["\u252c"] = make(dn=True, lf=True, rt=True)
    glyphs["\u2534"] = make(up=True, lf=True, rt=True)
    glyphs["\u253c"] = make(up=True, dn=True, lf=True, rt=True)
    glyphs["\u2588"] = bytes([0xFF] * GLYPH_H)
    glyphs["\u2593"] = bytes([0xAA, 0x55] * (GLYPH_H // 2))
    glyphs["\u2592"] = bytes([0xAA, 0x00] * (GLYPH_H // 2))
    glyphs["\u2591"] = bytes([0x88, 0x22] * (GLYPH_H // 2))
    glyphs["\u25c6"] = bytes([0x00, 0x00, 0x00, 0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10] + [0x00] * 6)
    glyphs["\u00b7"] = bytes([0x00] * 7 + [0x18, 0x18] + [0x00] * 7)
    return glyphs


BOX_GLYPHS = build_box_glyphs()

CP437_UNICODE = {
    0xB0: "\u2591",
    0xB1: "\u2592",
    0xB2: "\u2593",
    0xB3: "\u2502",
    0xB4: "\u2524",
    0xBF: "\u2510",
    0xC0: "\u2514",
    0xC1: "\u2534",
    0xC2: "\u252c",
    0xC3: "\u251c",
    0xC4: "\u2500",
    0xC5: "\u253c",
    0xD9: "\u2518",
    0xDA: "\u250c",
    0xDB: "\u2588",
}

DEC_ACS_UNICODE = {
    "l": "\u250c",
    "k": "\u2510",
    "m": "\u2514",
    "j": "\u2518",
    "q": "\u2500",
    "x": "\u2502",
    "t": "\u251c",
    "u": "\u2524",
    "w": "\u252c",
    "v": "\u2534",
    "n": "\u253c",
    "a": "\u2592",
    "`": "\u25c6",
    "~": "\u00b7",
}


def load_psf_font() -> dict:
    """Load an 8x16 PSF1/PSF2 bitmap console font into a {char: 16-byte bitmap} map."""
    candidates = [
        "/usr/share/consolefonts/Lat15-VGA16.psf.gz",
        "/usr/share/consolefonts/Lat15-Terminus16.psf.gz",
        "/usr/share/consolefonts/Lat15-Fixed16.psf.gz",
        "/usr/share/consolefonts/CyrSlav-VGA16.psf.gz",
    ]
    raw = b""
    for path in candidates:
        if os.path.exists(path):
            with gzip.open(path, "rb") as f:
                raw = f.read()
            break
    font = dict(BOX_GLYPHS)
    if not raw:
        return font

    glyph_list = []
    uni_map = {}

    if raw[:2] == b"\x36\x04":
        mode = raw[2]
        charsize = raw[3]
        nglyphs = 512 if (mode & 0x01) else 256
        has_uni = bool(mode & 0x02 or mode & 0x04)
        off = 4
        for _ in range(nglyphs):
            glyph_list.append(raw[off:off + charsize])
            off += charsize
        if has_uni and off < len(raw):
            idx = 0
            while off + 1 < len(raw) and idx < nglyphs:
                val = struct.unpack_from("<H", raw, off)[0]
                off += 2
                if val == 0xFFFF:
                    idx += 1
                elif val == 0xFFFE:
                    while off + 1 < len(raw):
                        sval = struct.unpack_from("<H", raw, off)[0]
                        off += 2
                        if sval == 0xFFFF:
                            idx += 1
                            break
                else:
                    uni_map.setdefault(chr(val), glyph_list[idx])
    elif raw[:4] == b"\x72\xb5\x4a\x86":
        _, _, hdr_sz, flags, nglyphs, bpg, _, _ = struct.unpack_from("<IIIIIIII", raw, 0)
        off = hdr_sz
        for _ in range(nglyphs):
            glyph_list.append(raw[off:off + bpg])
            off += bpg
        if (flags & 1) and off < len(raw):
            idx = 0
            start = off
            while off < len(raw) and idx < nglyphs:
                if raw[off] == 0xFF:
                    seq = raw[start:off]
                    for part in seq.split(b"\xFE"):
                        try:
                            s = part.decode("utf-8")
                            if len(s) == 1:
                                uni_map.setdefault(s, glyph_list[idx])
                        except UnicodeDecodeError:
                            pass
                    idx += 1
                    off += 1
                    start = off
                else:
                    off += 1

    font.update(uni_map)
    for k, v in BOX_GLYPHS.items():
        font[k] = v
    return font


FONT_MAP = load_psf_font()


def write_png(out_path: str, width: int, height: int, rgb_rows: list) -> None:
    """Encode packed RGB scanlines as a lossless PNG using Python stdlib zlib only."""
    def chunk(ctype: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

    raw_img = b"".join(b"\x00" + row for row in rgb_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png_bytes = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw_img, 6))
        + chunk(b"IEND", b"")
    )
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(png_bytes)


def render_cells_to_png(grid, out_path: str) -> None:
    """Render ROWS x COLS cells [(ch, fg_rgb, bg_rgb), ...] to a 640x400 PNG."""
    width = COLS * GLYPH_W
    height = ROWS * GLYPH_H
    blank_bmp = FONT_MAP.get(" ", b"\x00" * GLYPH_H)
    rgb_rows = []
    for r in range(ROWS):
        row_cells = grid[r]
        for gy in range(GLYPH_H):
            scan = bytearray(width * 3)
            pos = 0
            for c in range(COLS):
                ch, fg, bg = row_cells[c]
                bmp = FONT_MAP.get(ch, blank_bmp)
                bits = bmp[gy] if gy < len(bmp) else 0
                for gx in range(GLYPH_W):
                    pix = fg if (bits & (0x80 >> gx)) else bg
                    scan[pos] = pix[0]
                    scan[pos + 1] = pix[1]
                    scan[pos + 2] = pix[2]
                    pos += 3
            rgb_rows.append(bytes(scan))
    write_png(out_path, width, height, rgb_rows)


def parse_ansi_screen(raw: bytes):
    grid = [[(" ", 7, 0, False, False) for _ in range(COLS)] for _ in range(ROWS)]
    r, c = 0, 0
    fg, bg, bold, rev = 7, 0, False, False
    g0_acs = False
    state = "TEXT"
    esc_buf = ""

    def clear_cell(rr, cc):
        if 0 <= rr < ROWS and 0 <= cc < COLS:
            grid[rr][cc] = (" ", fg, bg, False, False)

    def put_ch(ch):
        nonlocal c
        if g0_acs and ch in DEC_ACS_UNICODE:
            ch = DEC_ACS_UNICODE[ch]
        if 0 <= r < ROWS and 0 <= c < COLS:
            grid[r][c] = (ch, fg, bg, bold, rev)
        if c + 1 < COLS:
            c += 1

    s = raw.decode("utf-8", errors="replace")
    for ch in s:
        if state == "TEXT":
            if ch == "\x1b":
                state = "ESC"
                esc_buf = ""
            elif ch == "\r":
                c = 0
            elif ch == "\n":
                if r + 1 < ROWS:
                    r += 1
            elif ch == "\x08":
                if c > 0:
                    c -= 1
            elif ch == "\x0e":
                g0_acs = True
            elif ch == "\x0f":
                g0_acs = False
            elif ord(ch) >= 0x20:
                put_ch(ch)
        elif state == "ESC":
            if ch == "[":
                state = "CSI"
                esc_buf = ""
            elif ch == "(":
                state = "SCS0"
            elif ch == ")":
                state = "SCS1"
            else:
                state = "TEXT"
        elif state == "SCS0":
            g0_acs = (ch == "0")
            state = "TEXT"
        elif state == "SCS1":
            state = "TEXT"
        elif state == "CSI":
            if 0x40 <= ord(ch) <= 0x7E:
                if not esc_buf.startswith("?"):
                    nums = [int(t) if t.isdigit() else 0 for t in esc_buf.split(";")] if esc_buf else []
                    p1 = nums[0] if len(nums) >= 1 else 0
                    p2 = nums[1] if len(nums) >= 2 else 0
                    if ch in ("H", "f"):
                        r = max(0, min(ROWS - 1, (p1 or 1) - 1))
                        c = max(0, min(COLS - 1, (p2 or 1) - 1))
                    elif ch == "A":
                        r = max(0, r - (p1 or 1))
                    elif ch == "B":
                        r = min(ROWS - 1, r + (p1 or 1))
                    elif ch == "C":
                        c = min(COLS - 1, c + (p1 or 1))
                    elif ch == "D":
                        c = max(0, c - (p1 or 1))
                    elif ch == "G":
                        c = max(0, min(COLS - 1, (p1 or 1) - 1))
                    elif ch == "d":
                        r = max(0, min(ROWS - 1, (p1 or 1) - 1))
                    elif ch == "J":
                        if p1 == 2:
                            for rr in range(ROWS):
                                for cc in range(COLS):
                                    clear_cell(rr, cc)
                            r, c = 0, 0
                        elif p1 == 0:
                            for cc in range(c, COLS):
                                clear_cell(r, cc)
                            for rr in range(r + 1, ROWS):
                                for cc in range(COLS):
                                    clear_cell(rr, cc)
                    elif ch == "K":
                        if p1 == 0:
                            for cc in range(c, COLS):
                                clear_cell(r, cc)
                        elif p1 == 1:
                            for cc in range(0, c + 1):
                                clear_cell(r, cc)
                        elif p1 == 2:
                            for cc in range(COLS):
                                clear_cell(r, cc)
                    elif ch == "m":
                        parts = nums if nums else [0]
                        i = 0
                        while i < len(parts):
                            p = parts[i]
                            if p == 0:
                                fg, bg, bold, rev = 7, 0, False, False
                            elif p == 1:
                                bold = True
                            elif p == 22:
                                bold = False
                            elif p == 7:
                                rev = True
                            elif p == 27:
                                rev = False
                            elif 30 <= p <= 37:
                                fg = p - 30
                            elif p == 39:
                                fg = 7
                            elif 40 <= p <= 47:
                                bg = p - 40
                            elif p == 49:
                                bg = 0
                            elif 90 <= p <= 97:
                                fg = (p - 90) + 8
                            elif 100 <= p <= 107:
                                bg = (p - 100) + 8
                            elif p == 38 and i + 2 < len(parts) and parts[i + 1] == 5:
                                fg = parts[i + 2]
                                i += 2
                            elif p == 48 and i + 2 < len(parts) and parts[i + 1] == 5:
                                bg = parts[i + 2]
                                i += 2
                            i += 1
                    elif ch == "X":
                        for k in range(p1 or 1):
                            if c + k < COLS:
                                clear_cell(r, c + k)
                state = "TEXT"
            else:
                esc_buf += ch

    rgb_grid = []
    for rr in range(ROWS):
        row_out = []
        for cc in range(COLS):
            ch, fgi, bgi, is_bold, is_rev = grid[rr][cc]
            if is_bold and fgi < 8:
                fgi += 8
            if is_rev:
                fgi, bgi = bgi, fgi
            row_out.append((ch, color_from_index(fgi), color_from_index(bgi)))
        rgb_grid.append(row_out)
    return rgb_grid


def render_ansi_to_png(in_path: str, out_path: str) -> None:
    with open(in_path, "rb") as f:
        raw = f.read()
    render_cells_to_png(parse_ansi_screen(raw), out_path)


def render_dosbuf_to_png(in_path: str, out_path: str) -> None:
    with open(in_path, "rb") as f:
        raw = f.read()
    if len(raw) < COLS * ROWS * 2:
        raise ValueError(f"Expected >= {COLS * ROWS * 2} bytes in {in_path}, got {len(raw)}")
    grid = []
    idx = 0
    for _ in range(ROWS):
        row_out = []
        for _ in range(COLS):
            code = raw[idx]
            attr = raw[idx + 1]
            idx += 2
            ch = CP437_UNICODE.get(code, chr(code) if 0x20 <= code <= 0x7E else " ")
            fg = VGA16_RGB[attr & 0x0F]
            bg = VGA16_RGB[(attr >> 4) & 0x0F]
            row_out.append((ch, fg, bg))
        grid.append(row_out)
    render_cells_to_png(grid, out_path)


def render_bmp_to_png(in_path: str, out_path: str) -> None:
    with open(in_path, "rb") as f:
        raw = f.read()
    if raw[:2] != b"BM":
        raise ValueError(f"Not a BMP file: {in_path}")
    bf_off = struct.unpack_from("<I", raw, 10)[0]
    width, height_signed = struct.unpack_from("<ii", raw, 18)
    bpp, comp = struct.unpack_from("<HI", raw, 28)
    if comp != 0 or bpp not in (24, 32):
        raise ValueError(f"Unsupported BMP format bpp={bpp} comp={comp} in {in_path}")
    top_down = height_signed < 0
    height = abs(height_signed)
    bytes_px = bpp // 8
    stride = (width * bytes_px + 3) & ~3
    rgb_rows = []
    for y in range(height):
        src_y = y if top_down else (height - 1 - y)
        row_off = bf_off + src_y * stride
        scan = bytearray(width * 3)
        for x in range(width):
            p = row_off + x * bytes_px
            b, g, r = raw[p], raw[p + 1], raw[p + 2]
            o = x * 3
            scan[o] = r
            scan[o + 1] = g
            scan[o + 2] = b
        rgb_rows.append(bytes(scan))
    write_png(out_path, width, height, rgb_rows)


def read_png(in_path: str) -> tuple:
    """Decode an 8-bit RGB, RGBA, or indexed PNG using Python stdlib zlib only."""
    with open(in_path, "rb") as f:
        raw = f.read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG file: {in_path}")
    off = 8
    width = height = bit_depth = color_type = interlace = 0
    palette = []
    idat = bytearray()
    while off + 8 <= len(raw):
        length, ctype = struct.unpack_from(">I4s", raw, off)
        data = raw[off + 8:off + 8 + length]
        off += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack_from(">IIBBBBB", data, 0)
        elif ctype == b"PLTE":
            palette = [(data[i], data[i + 1], data[i + 2]) for i in range(0, len(data) - 2, 3)]
        elif ctype == b"IDAT":
            idat.extend(data)
        elif ctype == b"IEND":
            break
    if bit_depth != 8 or interlace != 0 or color_type not in (2, 3, 6):
        raise ValueError(f"Unsupported PNG format depth={bit_depth} type={color_type} interlace={interlace} in {in_path}")

    bpp = 3 if color_type == 2 else (4 if color_type == 6 else 1)
    stride = width * bpp
    decomp = zlib.decompress(bytes(idat))
    prev = bytearray(stride)
    pos = 0
    rgb_rows = []
    for _ in range(height):
        flt = decomp[pos]
        pos += 1
        cur = bytearray(decomp[pos:pos + stride])
        pos += stride
        if flt == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 0xFF
        elif flt == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif flt == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif flt == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 0xFF
        prev = cur

        if color_type == 2:
            rgb_rows.append(bytes(cur))
        elif color_type == 6:
            out = bytearray(width * 3)
            for x in range(width):
                s = x * 4
                a = cur[s + 3]
                d = x * 3
                if a == 255:
                    out[d] = cur[s]
                    out[d + 1] = cur[s + 1]
                    out[d + 2] = cur[s + 2]
                else:
                    inv = 255 - a
                    out[d] = (cur[s] * a + 16 * inv) // 255
                    out[d + 1] = (cur[s + 1] * a + 20 * inv) // 255
                    out[d + 2] = (cur[s + 2] * a + 26 * inv) // 255
            rgb_rows.append(bytes(out))
        else:
            out = bytearray(width * 3)
            for x in range(width):
                r, g, b = palette[cur[x]] if cur[x] < len(palette) else (0, 0, 0)
                d = x * 3
                out[d], out[d + 1], out[d + 2] = r, g, b
            rgb_rows.append(bytes(out))
    return width, height, rgb_rows


def scale_rgb_rows(src_w: int, src_h: int, rgb_rows: list, max_w: int, max_h: int) -> tuple:
    """Aspect-preserving area-averaged downscale using stdlib integers only."""
    if src_w <= max_w and src_h <= max_h:
        return src_w, src_h, rgb_rows
    scale = min(max_w / float(src_w), max_h / float(src_h))
    dst_w = max(1, int(round(src_w * scale)))
    dst_h = max(1, int(round(src_h * scale)))
    x_ranges = [((dx * src_w) // dst_w, max(((dx * src_w) // dst_w) + 1, ((dx + 1) * src_w) // dst_w))
                for dx in range(dst_w)]
    out_rows = []
    for dy in range(dst_h):
        sy0 = (dy * src_h) // dst_h
        sy1 = max(sy0 + 1, ((dy + 1) * src_h) // dst_h)
        y_cnt = sy1 - sy0
        scan = bytearray(dst_w * 3)
        for dx, (sx0, sx1) in enumerate(x_ranges):
            cnt = y_cnt * (sx1 - sx0)
            rs = gs = bs = 0
            for sy in range(sy0, sy1):
                row = rgb_rows[sy]
                for sx in range(sx0, sx1):
                    p = sx * 3
                    rs += row[p]
                    gs += row[p + 1]
                    bs += row[p + 2]
            dp = dx * 3
            scan[dp] = rs // cnt
            scan[dp + 1] = gs // cnt
            scan[dp + 2] = bs // cnt
        out_rows.append(bytes(scan))
    return dst_w, dst_h, out_rows


def draw_bitmap_text(canvas: list, cw: int, ch: int, x0: int, y0: int, text: str,
                     fg: tuple, scale: int = 1) -> None:
    blank = FONT_MAP.get(" ", b"\x00" * GLYPH_H)
    cx = x0
    for ch_val in text:
        bmp = FONT_MAP.get(ch_val, blank)
        for gy in range(GLYPH_H):
            bits = bmp[gy] if gy < len(bmp) else 0
            for sy in range(scale):
                py = y0 + gy * scale + sy
                if not (0 <= py < ch):
                    continue
                row = canvas[py]
                for gx in range(GLYPH_W):
                    if bits & (0x80 >> gx):
                        for sx in range(scale):
                            px = cx + gx * scale + sx
                            if 0 <= px < cw:
                                p = px * 3
                                row[p] = fg[0]
                                row[p + 1] = fg[1]
                                row[p + 2] = fg[2]
        cx += GLYPH_W * scale


def render_contact_sheet(out_png: str, title: str, cols: int, cell_w: int, cell_h: int, items: list) -> None:
    """Compose a labeled COLS x N grid PNG contact sheet from LABEL:IMG_PATH entries."""
    parsed = []
    for item in items:
        label, path = item.split(":", 1)
        if not os.path.exists(path):
            continue
        iw, ih, irows = read_png(path)
        sw, sh, srows = scale_rgb_rows(iw, ih, irows, cell_w, cell_h)
        parsed.append((label, sw, sh, srows))
    if not parsed:
        raise ValueError(f"No valid images provided for contact sheet {out_png}")

    rows_n = (len(parsed) + cols - 1) // cols
    pad = 12
    header_h = 44
    tile_w = cell_w + 16
    tile_h = cell_h + 34
    out_w = pad * 2 + cols * tile_w + (cols - 1) * pad
    out_h = header_h + pad + rows_n * tile_h + (rows_n - 1) * pad + pad

    bg_rgb = (15, 23, 42)
    card_rgb = (24, 32, 42)
    border_rgb = (56, 189, 248)
    title_rgb = (248, 250, 252)
    lbl_rgb = (250, 204, 21)

    row_template = bytearray([bg_rgb[0], bg_rgb[1], bg_rgb[2]] * out_w)
    canvas = [bytearray(row_template) for _ in range(out_h)]

    # Header bar accent divider
    for x in range(pad, out_w - pad):
        p = x * 3
        canvas[header_h - 4][p:p + 3] = bytes(border_rgb)
    draw_bitmap_text(canvas, out_w, out_h, pad, 6, title, title_rgb, scale=2)

    for idx, (label, sw, sh, srows) in enumerate(parsed):
        r = idx // cols
        c = idx % cols
        tx = pad + c * (tile_w + pad)
        ty = header_h + pad + r * (tile_h + pad)
        # Card background + border
        for py in range(ty, ty + tile_h):
            row = canvas[py]
            is_edge_y = (py == ty or py == ty + tile_h - 1)
            for px in range(tx, tx + tile_w):
                col_v = border_rgb if (is_edge_y or px == tx or px == tx + tile_w - 1) else card_rgb
                p = px * 3
                row[p:p + 3] = bytes(col_v)
        # Centered thumbnail
        ix = tx + (tile_w - sw) // 2
        iy = ty + 8 + (cell_h - sh) // 2
        for sy in range(sh):
            dst_row = canvas[iy + sy]
            src_row = srows[sy]
            dst_off = ix * 3
            dst_row[dst_off:dst_off + sw * 3] = src_row
        # Centered caption below thumbnail
        cap = label[:max(1, (tile_w - 8) // GLYPH_W)]
        lx = tx + max(4, (tile_w - len(cap) * GLYPH_W) // 2)
        ly = ty + cell_h + 12
        draw_bitmap_text(canvas, out_w, out_h, lx, ly, cap, lbl_rgb, scale=1)

    write_png(out_png, out_w, out_h, [bytes(row) for row in canvas])


def main() -> int:
    ap = argparse.ArgumentParser(description="Render OYK UI capture files to PNG")
    grp = ap.add_mutually_exclusive_group(required=True)
    grp.add_argument("--ansi", nargs=2, metavar=("IN_ANSI", "OUT_PNG"))
    grp.add_argument("--dosbuf", nargs=2, metavar=("IN_DOSBUF", "OUT_PNG"))
    grp.add_argument("--bmp", nargs=2, metavar=("IN_BMP", "OUT_PNG"))
    grp.add_argument("--scale", nargs=4, metavar=("IN_PNG", "OUT_PNG", "MAX_W", "MAX_H"))
    grp.add_argument("--contact-sheet", nargs="+", metavar="ARG")
    args = ap.parse_args()
    if args.ansi:
        render_ansi_to_png(args.ansi[0], args.ansi[1])
    elif args.dosbuf:
        render_dosbuf_to_png(args.dosbuf[0], args.dosbuf[1])
    elif args.bmp:
        render_bmp_to_png(args.bmp[0], args.bmp[1])
    elif args.scale:
        w, h, rows = read_png(args.scale[0])
        sw, sh, srows = scale_rgb_rows(w, h, rows, int(args.scale[2]), int(args.scale[3]))
        write_png(args.scale[1], sw, sh, srows)
    elif args.contact_sheet:
        if len(args.contact_sheet) < 6:
            raise SystemExit("Usage: --contact-sheet OUT_PNG TITLE COLS CELL_W CELL_H LABEL:IMG ...")
        out_png, title, cols_s, cw_s, ch_s = args.contact_sheet[:5]
        render_contact_sheet(out_png, title, int(cols_s), int(cw_s), int(ch_s), args.contact_sheet[5:])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
