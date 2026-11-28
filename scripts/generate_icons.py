#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Generate platform native icons and C/C++98 logo headers from staged assets/."""

import os
import struct
import subprocess
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT, "assets")


def read_rgba_png(path: str):
    """Decode an 8-bit PNG into (width, height, list_of_(r,g,b,a))."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG file: {path}")
    off = 8
    w = h = depth = ctype = interlace = 0
    palette = []
    trns = None
    idat = bytearray()
    while off + 8 <= len(raw):
        length, chunk_type = struct.unpack_from(">I4s", raw, off)
        data = raw[off + 8:off + 8 + length]
        off += 12 + length
        if chunk_type == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack_from(">IIBBBBB", data, 0)
        elif chunk_type == b"PLTE":
            palette = [(data[i], data[i + 1], data[i + 2]) for i in range(0, len(data) - 2, 3)]
        elif chunk_type == b"tRNS":
            trns = data
        elif chunk_type == b"IDAT":
            idat.extend(data)
        elif chunk_type == b"IEND":
            break
    if depth != 8 or interlace != 0 or ctype not in (0, 2, 3, 4, 6):
        raise ValueError(f"Unsupported PNG format depth={depth} ctype={ctype} in {path}")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * bpp
    decomp = zlib.decompress(bytes(idat))
    prev = bytearray(stride)
    pos = 0
    pixels = []
    for _ in range(h):
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
        if ctype == 6:
            for x in range(w):
                s = x * 4
                pixels.append((cur[s], cur[s + 1], cur[s + 2], cur[s + 3]))
        elif ctype == 2:
            for x in range(w):
                s = x * 3
                pixels.append((cur[s], cur[s + 1], cur[s + 2], 255))
        elif ctype == 4:
            for x in range(w):
                g, a = cur[x * 2], cur[x * 2 + 1]
                pixels.append((g, g, g, a))
        elif ctype == 0:
            for x in range(w):
                g = cur[x]
                pixels.append((g, g, g, 255))
        else:
            for x in range(w):
                idx = cur[x]
                r, g, b = palette[idx] if idx < len(palette) else (0, 0, 0)
                a = trns[idx] if (trns is not None and idx < len(trns)) else 255
                pixels.append((r, g, b, a))
    return w, h, pixels


def write_rgba_png(path: str, w: int, h: int, pixels) -> None:
    """Write an 8-bit RGBA PNG using Python stdlib zlib."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row_off = y * w
        for x in range(w):
            r, g, b, a = pixels[row_off + x]
            raw.extend((r & 0xFF, g & 0xFF, b & 0xFF, a & 0xFF))

    def chunk(ctype: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def rasterize_svg_square(svg_path: str, size: int, tmp_dir: str, tag: str):
    """Rasterize SVG proportionally via Inkscape and center inside size x size RGBA."""
    raw_png = os.path.join(tmp_dir, f"{tag}_{size}.png")
    subprocess.run(
        [
            "inkscape",
            svg_path,
            "--export-type=png",
            f"--export-filename={raw_png}",
            f"--export-height={size}",
            "--export-background-opacity=0",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    rw, rh, rpix = read_rgba_png(raw_png)
    out = [(0, 0, 0, 0)] * (size * size)
    ox = (size - rw) // 2
    oy = (size - rh) // 2
    for y in range(rh):
        dy = oy + y
        if dy < 0 or dy >= size:
            continue
        for x in range(rw):
            dx = ox + x
            if dx < 0 or dx >= size:
                continue
            out[dy * size + dx] = rpix[y * rw + x]
    return out


def make_line_bits_and_mask(w: int, h: int, pixels):
    """Extract 1-bit MSB-first foreground bits and flood-filled silhouette mask."""
    ink = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            lum = (r * 30 + g * 59 + b * 11) // 100
            if a >= 96 and lum < 175:
                ink[y][x] = 1
    # Flood-fill transparent exterior from edges to compute solid icon mask
    ext = [[False] * w for _ in range(h)]
    stack = []
    for x in range(w):
        if not ink[0][x]:
            ext[0][x] = True
            stack.append((0, x))
        if not ink[h - 1][x]:
            ext[h - 1][x] = True
            stack.append((h - 1, x))
    for y in range(h):
        if not ink[y][0]:
            ext[y][0] = True
            stack.append((y, 0))
        if not ink[y][w - 1]:
            ext[y][w - 1] = True
            stack.append((y, w - 1))
    while stack:
        cy, cx = stack.pop()
        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < h and 0 <= nx < w and not ext[ny][nx] and not ink[ny][nx]:
                ext[ny][nx] = True
                stack.append((ny, nx))

    row_bytes = (w + 7) // 8
    bits = bytearray(h * row_bytes)
    mask = bytearray(h * row_bytes)
    for y in range(h):
        for x in range(w):
            byte_i = y * row_bytes + (x >> 3)
            bit_m = 0x80 >> (x & 7)
            if ink[y][x]:
                bits[byte_i] |= bit_m
            if ink[y][x] or not ext[y][x]:
                mask[byte_i] |= bit_m
    return bytes(bits), bytes(mask)


def quantize_solid_4color(pixels):
    """Map RGBA pixels into 4 palette IDs: 0=transparent, 1=navy, 2=white, 3=dark border."""
    out = []
    for r, g, b, a in pixels:
        if a < 96:
            out.append(0)
        else:
            lum = (r * 30 + g * 59 + b * 11) // 100
            if lum >= 150:
                out.append(2)
            elif lum <= 30 and b < 45:
                out.append(3)
            else:
                out.append(1)
    return out


def format_c_byte_array(name: str, data, cols: int = 16) -> str:
    lines = [f"OYK_LOGO_UNUSED static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), cols):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + cols])
        comma = "," if (i + cols < len(data)) else ""
        lines.append(f"    {chunk}{comma}")
    lines.append("};")
    return "\n".join(lines)


def write_oyk_logo_headers(ascii_lines, solid16, solid32, solid64, line16, line32):
    l16_bits, l16_mask = make_line_bits_and_mask(16, 16, line16)
    l32_bits, l32_mask = make_line_bits_and_mask(32, 32, line32)
    pal32 = quantize_solid_4color(solid32)

    s16_bytes = bytearray()
    for r, g, b, a in solid16:
        s16_bytes.extend((r, g, b, a))
    s32_bytes = bytearray()
    for r, g, b, a in solid32:
        s32_bytes.extend((r, g, b, a))
    s64_bytes = bytearray()
    for r, g, b, a in solid64:
        s64_bytes.extend((r, g, b, a))

    esc_lines = []
    for ln in ascii_lines:
        esc = ln.replace("\\", "\\\\").replace('"', '\\"')
        esc_lines.append(f'    "{esc}"')

    hpp = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* Copyright (C) 2026 KR4OYK - David Kuder */",
        "/* Auto-generated from assets/ by scripts/generate_icons.py. */",
        "",
        "#ifndef OYK_LOGO_DATA_HPP",
        "#define OYK_LOGO_DATA_HPP",
        "",
        "#if (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__)",
        "#define OYK_LOGO_UNUSED __attribute__((unused))",
        "#else",
        "#define OYK_LOGO_UNUSED",
        "#endif",
        "",
        f"#define OYK_LOGO_ASCII_ROWS {len(ascii_lines)}",
        f"#define OYK_LOGO_ASCII_COLS {max(len(x) for x in ascii_lines)}",
        "",
        f"OYK_LOGO_UNUSED static const char* const kOykLogoAscii[{len(ascii_lines)}] = {{",
        ",\n".join(esc_lines),
        "};",
        "",
        format_c_byte_array("kOykLogoSolid16Rgba", s16_bytes),
        "",
        format_c_byte_array("kOykLogoSolid32Rgba", s32_bytes),
        "",
        format_c_byte_array("kOykLogoSolid64Rgba", s64_bytes),
        "",
        format_c_byte_array("kOykLogoLine16Bits", l16_bits),
        "",
        format_c_byte_array("kOykLogoLine16Mask", l16_mask),
        "",
        format_c_byte_array("kOykLogoLine32Bits", l32_bits),
        "",
        format_c_byte_array("kOykLogoLine32Mask", l32_mask),
        "",
        format_c_byte_array("kOykLogoSolid32Pal", pal32, 32),
        "",
        "#endif /* OYK_LOGO_DATA_HPP */",
        "",
    ]
    with open(os.path.join(ROOT, "ports", "compat", "oyk_logo_data.hpp"), "w", encoding="utf-8") as f:
        f.write("\n".join(hpp))

    gsos_h = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* Copyright (C) 2026 KR4OYK - David Kuder */",
        "/* Auto-generated from assets/ by scripts/generate_icons.py. */",
        "",
        "#ifndef OYK_GSOS_LOGO_DATA_H",
        "#define OYK_GSOS_LOGO_DATA_H",
        "",
        "#if (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__)",
        "#define OYK_LOGO_UNUSED __attribute__((unused))",
        "#else",
        "#define OYK_LOGO_UNUSED",
        "#endif",
        "",
        format_c_byte_array("kOykGsosLogo32", pal32, 32),
        "",
        "#endif /* OYK_GSOS_LOGO_DATA_H */",
        "",
    ]
    with open(os.path.join(ROOT, "ports", "gsos", "gsos_logo_data.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(gsos_h))

    sysv_h = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* Copyright (C) 2026 KR4OYK - David Kuder */",
        "/* Auto-generated from assets/ by scripts/generate_icons.py. */",
        "",
        "#ifndef OYK_SYSV_LOGO_DATA_H",
        "#define OYK_SYSV_LOGO_DATA_H",
        "",
        "#if (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__)",
        "#define OYK_LOGO_UNUSED __attribute__((unused))",
        "#else",
        "#define OYK_LOGO_UNUSED",
        "#endif",
        "",
        f"#define OYK_SYSV_LOGO_ASCII_ROWS {len(ascii_lines)}",
        f"#define OYK_SYSV_LOGO_ASCII_COLS {max(len(x) for x in ascii_lines)}",
        "",
        f"OYK_LOGO_UNUSED static const char * const kOykSysvLogoAscii[{len(ascii_lines)}] = {{",
        ",\n".join(esc_lines),
        "};",
        "",
        format_c_byte_array("kOykSysvLogoSolid32Pal", pal32, 32),
        "",
        format_c_byte_array("kOykSysvLogoLine16Bits", l16_bits),
        "",
        format_c_byte_array("kOykSysvLogoLine16Mask", l16_mask),
        "",
        format_c_byte_array("kOykSysvLogoLine32Bits", l32_bits),
        "",
        format_c_byte_array("kOykSysvLogoLine32Mask", l32_mask),
        "",
        "#endif /* OYK_SYSV_LOGO_DATA_H */",
        "",
    ]
    sysv_dir = os.path.join(ROOT, "ports", "sysv")
    os.makedirs(sysv_dir, exist_ok=True)
    with open(os.path.join(sysv_dir, "sysv_logo_data.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(sysv_h))

    return l16_bits, l16_mask, l32_bits, l32_mask, pal32


def build_ico_rgba_entry(w: int, h: int, pixels) -> bytes:
    bih = struct.pack("<IIIHHIIIIII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    xor_buf = bytearray()
    mask_stride = ((w + 31) // 32) * 4
    and_buf = bytearray(mask_stride * h)
    for row_idx in range(h):
        sy = h - 1 - row_idx
        for x in range(w):
            r, g, b, a = pixels[sy * w + x]
            xor_buf.extend((b, g, r, a))
            if a < 128:
                and_buf[row_idx * mask_stride + (x >> 3)] |= (0x80 >> (x & 7))
    return bih + bytes(xor_buf) + bytes(and_buf)


def build_ico_4bpp_entry(w: int, h: int, pixels) -> bytes:
    # Standard 16-color BGRX palette: 0=black, 1=navy (#003366), 7=silver, 15=white
    pal = [(0, 0, 0, 0)] * 16
    pal[1] = (0x4D, 0x35, 0x1E, 0)
    pal[7] = (0xC0, 0xC0, 0xC0, 0)
    pal[15] = (0xFF, 0xFF, 0xFF, 0)
    pal_bytes = b"".join(struct.pack("<BBBB", b, g, r, x) for b, g, r, x in pal)
    xor_stride = ((w * 4 + 31) // 32) * 4
    mask_stride = ((w + 31) // 32) * 4
    bih = struct.pack("<IIIHHIIIIII", 40, w, h * 2, 1, 4, 0, xor_stride * h, 0, 0, 16, 16)
    xor_buf = bytearray(xor_stride * h)
    and_buf = bytearray(mask_stride * h)
    for row_idx in range(h):
        sy = h - 1 - row_idx
        for x in range(w):
            r, g, b, a = pixels[sy * w + x]
            if a < 128:
                cidx = 0
                and_buf[row_idx * mask_stride + (x >> 3)] |= (0x80 >> (x & 7))
            else:
                lum = (r * 30 + g * 59 + b * 11) // 100
                cidx = 15 if lum >= 150 else (0 if lum <= 28 and b < 45 else 1)
            byte_i = row_idx * xor_stride + (x >> 1)
            if (x & 1) == 0:
                xor_buf[byte_i] |= (cidx << 4)
            else:
                xor_buf[byte_i] |= cidx
    return bih + pal_bytes + bytes(xor_buf) + bytes(and_buf)


def write_ico(out_path: str, entries):
    count = len(entries)
    dir_size = 6 + 16 * count
    offset = dir_size
    dirs = bytearray()
    blobs = bytearray()
    for w, h, bpp, blob in entries:
        colors = 16 if bpp == 4 else 0
        wb = 0 if w >= 256 else w
        hb = 0 if h >= 256 else h
        dirs.extend(struct.pack("<BBBBHHII", wb, hb, colors, 0, 1, bpp, len(blob), offset))
        blobs.extend(blob)
        offset += len(blob)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, count))
        f.write(dirs)
        f.write(blobs)


def write_icns(out_path: str, chunk_pairs):
    body = bytearray()
    for code, png_path in chunk_pairs:
        with open(png_path, "rb") as f:
            pdata = f.read()
        body.extend(struct.pack(">4sI", code, 8 + len(pdata)))
        body.extend(pdata)
    with open(out_path, "wb") as f:
        f.write(struct.pack(">4sI", b"icns", 8 + len(body)))
        f.write(body)


def write_mac_icons_r(l32_bits, l32_mask, l16_bits, l16_mask, solid32, solid16):
    # Apple 8-bit System Palette indices: 0x00=#FFFFFF, 0xFF=#000000, 0xCF=#003366 (navy)
    def pal_bytes(pixels):
        out = bytearray()
        for r, g, b, a in pixels:
            if a < 96:
                out.append(0x00)
            else:
                lum = (r * 30 + g * 59 + b * 11) // 100
                if lum >= 150:
                    out.append(0x00)
                elif lum <= 28 and b < 45:
                    out.append(0xFF)
                else:
                    out.append(0xCF)
        return bytes(out)

    def rez_hex_block(blob: bytes) -> str:
        lines = []
        for i in range(0, len(blob), 16):
            hx = " ".join(blob[i:i + 16].hex()[j:j + 4].upper() for j in range(0, min(16, len(blob) - i) * 2, 4))
            lines.append(f'    $"{hx}"')
        return "\n".join(lines)

    icl8 = pal_bytes(solid32)
    ics8 = pal_bytes(solid16)
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* Copyright (C) 2026 KR4OYK - David Kuder */",
        "/* Auto-generated from assets/ by scripts/generate_icons.py. */",
        "",
        "data 'OYKN' (0) {",
        '    $"1B 4F 59 4B 20 41 6D 61 74 65 75 72 20 52 61 64 69 6F 20 50 61 63 6B 65 74 20 4E 6F"',
        "};",
        "",
        "resource 'FREF' (128, purgeable) {",
        "    'APPL', 0, \"\"",
        "};",
        "",
        "resource 'BNDL' (128, purgeable) {",
        "    'OYKN', 0,",
        "    {",
        "        'ICN#', { 0, 128 };",
        "        'FREF', { 0, 128 };",
        "    }",
        "};",
        "",
        "data 'ICN#' (128, purgeable) {",
        rez_hex_block(l32_bits + l32_mask),
        "};",
        "",
        "data 'ics#' (128, purgeable) {",
        rez_hex_block(l16_bits + l16_mask),
        "};",
        "",
        "data 'icl8' (128, purgeable) {",
        rez_hex_block(icl8),
        "};",
        "",
        "data 'ics8' (128, purgeable) {",
        rez_hex_block(ics8),
        "};",
        "",
    ]
    out_r = os.path.join(ROOT, "ports", "macclassic", "oyk_mac_icons.r")
    with open(out_r, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def write_amiga_info(out_path: str, pal32):
    # Classic Workbench 4-color DiskObject (32x32, depth 2): 0=grey, 1=black, 2=white, 3=blue
    w, h, depth = 32, 32, 2
    plane_words = 2  # (32 + 15) // 16
    planes = bytearray(depth * h * plane_words * 2)
    for y in range(h):
        for x in range(w):
            src = pal32[y * w + x]
            wb_c = {0: 0, 1: 3, 2: 2, 3: 1}[src]
            byte_off = (y * 4) + (x >> 3)
            bit_m = 0x80 >> (x & 7)
            if wb_c & 1:
                planes[byte_off] |= bit_m
            if wb_c & 2:
                planes[h * 4 + byte_off] |= bit_m

    # DiskObject header (78 bytes): Magic=0xE310, Version=1, Gadget(44), Type=WBTOOL(3)...
    do_magic = 0xE310
    do_ver = 1
    gadget = struct.pack(
        ">IhhhhHHHHHIIIIIH",
        0, 0, 0, w, h + 1, 0x0005, 0x0003, 0x0001,
        1, 0, 0, 0, 0, 0, 0, 0,
    )
    # 78-byte DiskObject: magic(2), ver(2), gadget(44), type(1), pad(1), deftool(4),
    # tooltypes(4), curx(4), cury(4), drawerdata(4), toolwindow(4), stacksize(4) = 78 bytes
    disk_hdr = (
        struct.pack(">HH", do_magic, do_ver)
        + gadget
        + struct.pack(">BBIIiiII", 3, 0, 0, 0, -0x80000000, -0x80000000, 0, 0)
        + struct.pack(">I", 16384)
    )
    # Image struct (20 bytes): LeftEdge, TopEdge, Width, Height, Depth, ImageData ptr(non-zero), PlanePick, PlaneOnOff, NextImage
    img_hdr = struct.pack(">hhhhhIBBI", 0, 0, w, h, depth, 1, 0x03, 0x00, 0)
    with open(out_path, "wb") as f:
        f.write(disk_hdr)
        f.write(img_hdr)
        f.write(planes)


def main():
    with open(os.path.join(ASSETS_DIR, "oyk-logo.txt"), "r", encoding="utf-8") as f:
        ascii_lines = [ln.rstrip("\r\n") for ln in f.read().splitlines() if ln.strip()]

    _, _, solid16 = read_rgba_png(os.path.join(ASSETS_DIR, "oyk-logo-solid-16x16.png"))
    _, _, solid32 = read_rgba_png(os.path.join(ASSETS_DIR, "oyk-logo-solid-32x32.png"))
    _, _, line16 = read_rgba_png(os.path.join(ASSETS_DIR, "oyk-logo-line-16x16.png"))
    _, _, line32 = read_rgba_png(os.path.join(ASSETS_DIR, "oyk-logo-line-32x32.png"))

    solid_svg = os.path.join(ASSETS_DIR, "oyk-logo-solid.svg")
    line_svg = os.path.join(ASSETS_DIR, "oyk-logo-line.svg")

    with tempfile.TemporaryDirectory() as tmp:
        solid48 = rasterize_svg_square(solid_svg, 48, tmp, "solid")
        solid64 = rasterize_svg_square(solid_svg, 64, tmp, "solid")
        solid72 = rasterize_svg_square(solid_svg, 72, tmp, "solid")
        solid96 = rasterize_svg_square(solid_svg, 96, tmp, "solid")
        solid128 = rasterize_svg_square(solid_svg, 128, tmp, "solid")
        solid144 = rasterize_svg_square(solid_svg, 144, tmp, "solid")
        solid192 = rasterize_svg_square(solid_svg, 192, tmp, "solid")
        solid256 = rasterize_svg_square(solid_svg, 256, tmp, "solid")
        solid512 = rasterize_svg_square(solid_svg, 512, tmp, "solid")
        line48 = rasterize_svg_square(line_svg, 48, tmp, "line")

        l16_b, l16_m, l32_b, l32_m, pal32 = write_oyk_logo_headers(
            ascii_lines, solid16, solid32, solid64, line16, line32
        )

        # Windows 32/64 & Win16 ICOs
        write_ico(
            os.path.join(ROOT, "ports", "windows", "oyk_node.ico"),
            [
                (16, 16, 32, build_ico_rgba_entry(16, 16, solid16)),
                (32, 32, 32, build_ico_rgba_entry(32, 32, solid32)),
                (48, 48, 32, build_ico_rgba_entry(48, 48, solid48)),
                (64, 64, 32, build_ico_rgba_entry(64, 64, solid64)),
            ],
        )
        write_ico(
            os.path.join(ROOT, "ports", "windows", "oyk_win16.ico"),
            [
                (16, 16, 4, build_ico_4bpp_entry(16, 16, solid16)),
                (32, 32, 4, build_ico_4bpp_entry(32, 32, solid32)),
            ],
        )
        write_ico(
            os.path.join(ROOT, "ports", "os2", "oyk_node.ico"),
            [
                (16, 16, 4, build_ico_4bpp_entry(16, 16, solid16)),
                (32, 32, 4, build_ico_4bpp_entry(32, 32, solid32)),
            ],
        )

        # macOS ICNS
        icns_chunks = []
        for code, sz, px in (
            (b"icp4", 16, solid16),
            (b"icp5", 32, solid32),
            (b"ic07", 128, solid128),
            (b"ic08", 256, solid256),
            (b"ic09", 512, solid512),
        ):
            p = os.path.join(tmp, f"icns_{sz}.png")
            write_rgba_png(p, sz, sz, px)
            icns_chunks.append((code, p))
        write_icns(os.path.join(ROOT, "ports", "macos", "OYKNode.icns"), icns_chunks)

        # Classic Mac Rez icons
        write_mac_icons_r(l32_b, l32_m, l16_b, l16_m, solid32, solid16)

        # Amiga Workbench DiskObject .info
        write_amiga_info(os.path.join(ROOT, "ports", "amiga", "OYKNode.info"), pal32)

        # Android mipmap launcher & drawable icons
        res_dir = os.path.join(ROOT, "ports", "android", "app", "src", "main", "res")
        for density, sz, px in (
            ("mdpi", 48, solid48),
            ("hdpi", 72, solid72),
            ("xhdpi", 96, solid96),
            ("xxhdpi", 144, solid144),
            ("xxxhdpi", 192, solid192),
        ):
            mdir = os.path.join(res_dir, f"mipmap-{density}")
            write_rgba_png(os.path.join(mdir, "ic_launcher.png"), sz, sz, px)
            # Round variant clipped cleanly to circle diameter = 92% of box
            rad_sq = (sz * 0.46) ** 2
            cx = (sz - 1) * 0.5
            cy = (sz - 1) * 0.5
            rnd = []
            for y in range(sz):
                for x in range(sz):
                    r, g, b, a = px[y * sz + x]
                    if ((x - cx) ** 2 + (y - cy) ** 2) > rad_sq:
                        rnd.append((r, g, b, 0))
                    else:
                        rnd.append((r, g, b, a))
            write_rgba_png(os.path.join(mdir, "ic_launcher_round.png"), sz, sz, rnd)

        write_rgba_png(os.path.join(res_dir, "drawable", "ic_oyk_logo.png"), 128, 128, solid128)
        # Monochrome pure-white alpha silhouette for Android status bar notification
        notif_px = []
        for r, g, b, a in line48:
            lum = (r * 30 + g * 59 + b * 11) // 100
            ink_a = a if lum < 210 else 0
            notif_px.append((255, 255, 255, ink_a))
        write_rgba_png(os.path.join(res_dir, "drawable", "ic_oyk_notif.png"), 48, 48, notif_px)

    print("[PASS] Generated cross-port logo headers and native icons from assets/")


if __name__ == "__main__":
    main()
