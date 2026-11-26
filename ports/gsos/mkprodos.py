#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Pure-Python ProDOS (.po) 3.5" / SmartPort image builder, injector, and extractor."""

import argparse
import os
import shutil
import struct
import sys

BLK_SZ = 512


def prodos_datetime():
    # Encode fixed valid ProDOS date/time bytes (2026-11-26 17:00)
    # Year 26, Month 11, Day 26 -> word1 = (26 << 9) | (11 << 5) | 26
    w1 = ((26 & 0x7F) << 9) | ((11 & 0x0F) << 5) | (26 & 0x1F)
    w2 = (17 << 8) | 0
    return struct.pack("<HH", w1, w2)


class ProDosImage:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.buf = bytearray(f.read())
        if len(self.buf) < 6 * BLK_SZ or len(self.buf) % BLK_SZ != 0:
            raise RuntimeError(f"Invalid ProDOS image size: {len(self.buf)}")
        self.total_blocks = len(self.buf) // BLK_SZ
        self.bitmap_block = struct.unpack_from("<H", self.buf, 2 * BLK_SZ + 0x27)[0]

    def save(self, out_path=None):
        dst = out_path or self.path
        with open(dst, "wb") as f:
            f.write(self.buf)

    def _blk(self, b):
        return b * BLK_SZ

    def _is_free(self, blk):
        if (6 + (self.total_blocks + 4095) // 4096) > blk or blk >= self.total_blocks:
            return False
        bm_blk = self.bitmap_block + (blk // 4096)
        off = self._blk(bm_blk) + ((blk % 4096) // 8)
        bit = 7 - (blk & 7)
        return (self.buf[off] & (1 << bit)) != 0

    def _set_used(self, blk, used=True):
        if (6 + (self.total_blocks + 4095) // 4096) > blk or blk >= self.total_blocks:
            return
        bm_blk = self.bitmap_block + (blk // 4096)
        off = self._blk(bm_blk) + ((blk % 4096) // 8)
        bit = 7 - (blk & 7)
        if used:
            self.buf[off] &= ~(1 << bit) & 0xFF
        else:
            self.buf[off] |= (1 << bit)

    def _alloc_block(self):
        bm_start = 6 + (self.total_blocks + 4095) // 4096
        for b in range(bm_start, self.total_blocks):
            if self._is_free(b):
                self._set_used(b, True)
                off = self._blk(b)
                self.buf[off:off + BLK_SZ] = b"\x00" * BLK_SZ
                return b
        raise RuntimeError("ProDOS image out of free blocks")

    def _free_file_blocks(self, stype, key_ptr):
        if key_ptr <= 0 or key_ptr >= self.total_blocks:
            return
        kind = (stype >> 4) & 0xF
        if kind == 1:
            self._set_used(key_ptr, False)
        elif kind == 2:
            koff = self._blk(key_ptr)
            for i in range(256):
                db = self.buf[koff + i] | (self.buf[koff + 256 + i] << 8)
                if db:
                    self._set_used(db, False)
            self._set_used(key_ptr, False)
        elif kind == 3:
            koff = self._blk(key_ptr)
            for j in range(128):
                sb = self.buf[koff + j] | (self.buf[koff + 256 + j] << 8)
                if sb:
                    soff = self._blk(sb)
                    for i in range(256):
                        db = self.buf[soff + i] | (self.buf[soff + 256 + i] << 8)
                        if db:
                            self._set_used(db, False)
                    self._set_used(sb, False)
            self._set_used(key_ptr, False)
        elif kind == 5:
            # GS/OS extended file: key_ptr -> 512B fork info block (data + rsrc mini-entries)
            eoff = self._blk(key_ptr)
            for fk in (0, 256):
                fk_kind = self.buf[eoff + fk + 0]
                fk_blk = struct.unpack_from("<H", self.buf, eoff + fk + 1)[0]
                if fk_blk:
                    self._free_file_blocks(fk_kind << 4, fk_blk)
            self._set_used(key_ptr, False)

    def _iter_dir(self, start_blk):
        blk = start_blk
        idx = 0
        while blk != 0:
            off = self._blk(blk)
            next_b = struct.unpack_from("<H", self.buf, off + 2)[0]
            pos = off + 4
            if idx == 0:
                elen = self.buf[off + 0x23] or 0x27
                pos += elen
            else:
                elen = 0x27
            while pos + elen <= off + BLK_SZ:
                stype = self.buf[pos]
                yield blk, pos, elen, stype
                idx += 1
                pos += elen
            blk = next_b

    def _find_dir_block(self, parts):
        dir_blk = 2
        for comp in parts:
            up = comp.upper()
            found = None
            for _, pos, _, stype in self._iter_dir(dir_blk):
                if stype == 0:
                    continue
                kind = (stype >> 4) & 0xF
                nlen = stype & 0xF
                name = self.buf[pos + 1:pos + 1 + nlen].decode("ascii", "replace").upper()
                if name == up and kind == 0xD:
                    found = struct.unpack_from("<H", self.buf, pos + 0x11)[0]
                    break
            if found is None:
                raise RuntimeError(f"Subdirectory not found: {comp}")
            dir_blk = found
        return dir_blk

    def _read_fork(self, kind, kptr, eof):
        if eof == 0:
            return b""
        chunks = []
        if kind == 1:
            chunks.append(bytes(self.buf[self._blk(kptr):self._blk(kptr) + eof]))
        elif kind == 2:
            koff = self._blk(kptr)
            rem = eof
            i = 0
            while rem > 0 and i < 256:
                db = self.buf[koff + i] | (self.buf[koff + 256 + i] << 8)
                take = min(rem, BLK_SZ)
                if db == 0:
                    chunks.append(b"\x00" * take)
                else:
                    chunks.append(bytes(self.buf[self._blk(db):self._blk(db) + take]))
                rem -= take
                i += 1
        elif kind == 3:
            koff = self._blk(kptr)
            rem = eof
            j = 0
            while rem > 0 and j < 128:
                sb = self.buf[koff + j] | (self.buf[koff + 256 + j] << 8)
                soff = self._blk(sb)
                for i in range(256):
                    if rem <= 0:
                        break
                    db = self.buf[soff + i] | (self.buf[soff + 256 + i] << 8)
                    take = min(rem, BLK_SZ)
                    if db == 0:
                        chunks.append(b"\x00" * take)
                    else:
                        chunks.append(bytes(self.buf[self._blk(db):self._blk(db) + take]))
                    rem -= take
                j += 1
        return b"".join(chunks)

    def read_file(self, prodos_path):
        parts = [p for p in prodos_path.strip("/").split("/") if p]
        dir_blk = self._find_dir_block(parts[:-1])
        target = parts[-1].upper()
        for _, pos, _, stype in self._iter_dir(dir_blk):
            if stype == 0:
                continue
            kind = (stype >> 4) & 0xF
            nlen = stype & 0xF
            name = self.buf[pos + 1:pos + 1 + nlen].decode("ascii", "replace").upper()
            if name == target and kind in (1, 2, 3, 5):
                kptr = struct.unpack_from("<H", self.buf, pos + 0x11)[0]
                eof = self.buf[pos + 0x15] | (self.buf[pos + 0x16] << 8) | (self.buf[pos + 0x17] << 16)
                if kind == 5:
                    eoff = self._blk(kptr)
                    dk = self.buf[eoff]
                    dkptr = struct.unpack_from("<H", self.buf, eoff + 1)[0]
                    deof = self.buf[eoff + 5] | (self.buf[eoff + 6] << 8) | (self.buf[eoff + 7] << 16)
                    return self._read_fork(dk, dkptr, deof)
                return self._read_fork(kind, kptr, eof)
        return None

    def list_dir(self, dir_path=""):
        parts = [p for p in dir_path.strip("/").split("/") if p]
        dir_blk = self._find_dir_block(parts)
        out = []
        for _, pos, _, stype in self._iter_dir(dir_blk):
            if stype == 0:
                continue
            kind = (stype >> 4) & 0xF
            nlen = stype & 0xF
            name = self.buf[pos + 1:pos + 1 + nlen].decode("ascii", "replace")
            out.append((name, kind))
        return out

    def delete_file(self, prodos_path):
        parts = [p for p in prodos_path.strip("/").split("/") if p]
        dir_blk = self._find_dir_block(parts[:-1])
        target = parts[-1].upper()
        for _, pos, _, stype in self._iter_dir(dir_blk):
            if stype == 0:
                continue
            nlen = stype & 0xF
            name = self.buf[pos + 1:pos + 1 + nlen].decode("ascii", "replace").upper()
            if name == target:
                kptr = struct.unpack_from("<H", self.buf, pos + 0x11)[0]
                self._free_file_blocks(stype, kptr)
                self.buf[pos:pos + 0x27] = b"\x00" * 0x27
                hdr = self._blk(dir_blk)
                fc = struct.unpack_from("<H", self.buf, hdr + 0x25)[0]
                if fc > 0:
                    struct.pack_into("<H", self.buf, hdr + 0x25, fc - 1)
                return True
        return False

    def write_file(self, prodos_path, data_bytes, file_type=0xB3, aux_type=0xDB03):
        parts = [p for p in prodos_path.strip("/").split("/") if p]
        fname = parts[-1].upper()[:15]
        dir_blk = self._find_dir_block(parts[:-1])
        self.delete_file(prodos_path)

        # Find free directory slot
        slot_pos = None
        for _, pos, _, stype in self._iter_dir(dir_blk):
            if stype == 0:
                slot_pos = pos
                break
        if slot_pos is None:
            raise RuntimeError(f"No free directory slots in {prodos_path}")

        n = len(data_bytes)
        if n <= BLK_SZ:
            kind = 1
            kptr = self._alloc_block()
            self.buf[self._blk(kptr):self._blk(kptr) + n] = data_bytes
            used_blks = 1
        elif n <= 256 * BLK_SZ:
            kind = 2
            kptr = self._alloc_block()
            used_blks = 1
            num_d = (n + BLK_SZ - 1) // BLK_SZ
            for i in range(num_d):
                db = self._alloc_block()
                chunk = data_bytes[i * BLK_SZ:(i + 1) * BLK_SZ]
                self.buf[self._blk(db):self._blk(db) + len(chunk)] = chunk
                self.buf[self._blk(kptr) + i] = db & 0xFF
                self.buf[self._blk(kptr) + 256 + i] = (db >> 8) & 0xFF
                used_blks += 1
        else:
            kind = 3
            kptr = self._alloc_block()
            used_blks = 1
            num_d = (n + BLK_SZ - 1) // BLK_SZ
            sub_idx = -1
            sub_blk = 0
            for i in range(num_d):
                if i % 256 == 0:
                    sub_idx += 1
                    sub_blk = self._alloc_block()
                    self.buf[self._blk(kptr) + sub_idx] = sub_blk & 0xFF
                    self.buf[self._blk(kptr) + 256 + sub_idx] = (sub_blk >> 8) & 0xFF
                    used_blks += 1
                db = self._alloc_block()
                chunk = data_bytes[i * BLK_SZ:(i + 1) * BLK_SZ]
                self.buf[self._blk(db):self._blk(db) + len(chunk)] = chunk
                in_s = i % 256
                self.buf[self._blk(sub_blk) + in_s] = db & 0xFF
                self.buf[self._blk(sub_blk) + 256 + in_s] = (db >> 8) & 0xFF
                used_blks += 1

        entry = bytearray(0x27)
        entry[0] = (kind << 4) | len(fname)
        entry[1:1 + len(fname)] = fname.encode("ascii")
        entry[0x10] = file_type & 0xFF
        struct.pack_into("<H", entry, 0x11, kptr)
        struct.pack_into("<H", entry, 0x13, used_blks)
        entry[0x15] = n & 0xFF
        entry[0x16] = (n >> 8) & 0xFF
        entry[0x17] = (n >> 16) & 0xFF
        dt = prodos_datetime()
        entry[0x18:0x1C] = dt
        entry[0x1C] = 0x24  # version/min_version (GS/OS)
        entry[0x1E] = 0xE3  # full r/w/rename/destroy access
        struct.pack_into("<H", entry, 0x1F, aux_type & 0xFFFF)
        entry[0x21:0x25] = dt
        struct.pack_into("<H", entry, 0x25, dir_blk)
        self.buf[slot_pos:slot_pos + 0x27] = entry

        hdr = self._blk(dir_blk)
        fc = struct.unpack_from("<H", self.buf, hdr + 0x25)[0]
        struct.pack_into("<H", self.buf, hdr + 0x25, fc + 1)


def create_empty_po(out_path, vol_name="OYKOUT", total_blocks=1600):
    vname = vol_name.upper()[:15]
    img = bytearray(total_blocks * BLK_SZ)
    # Volume directory chain: blocks 2..5
    for b in range(2, 6):
        prev_b = 0 if b == 2 else b - 1
        next_b = 0 if b == 5 else b + 1
        struct.pack_into("<HH", img, b * BLK_SZ, prev_b, next_b)

    # Volume header in block 2
    vh = bytearray(0x27)
    vh[0] = 0xF0 | len(vname)
    vh[1:1 + len(vname)] = vname.encode("ascii")
    vh[0x1C - 4:0x20 - 4] = prodos_datetime()
    vh[0x20 - 4] = 0x00
    vh[0x21 - 4] = 0x00
    vh[0x22 - 4] = 0xE3
    vh[0x23 - 4] = 0x27
    vh[0x24 - 4] = 13
    struct.pack_into("<H", vh, 0x25 - 4, 0)
    struct.pack_into("<H", vh, 0x27 - 4, 6)  # bitmap block
    struct.pack_into("<H", vh, 0x29 - 4, total_blocks)
    img[2 * BLK_SZ + 4:2 * BLK_SZ + 4 + 0x27] = vh

    # Bitmap starts at block 6 and spans bm_blocks (each block holds 4096 block bits)
    bm_blocks = (total_blocks + 4095) // 4096
    bm_bytes = (total_blocks + 7) // 8
    for i in range(bm_bytes):
        img[6 * BLK_SZ + i] = 0xFF
    for u in range(6 + bm_blocks):
        img[6 * BLK_SZ + (u // 8)] &= ~(1 << (7 - (u & 7))) & 0xFF
    rem_bits = total_blocks % 8
    if rem_bits != 0:
        img[6 * BLK_SZ + bm_bytes - 1] = (0xFF << (8 - rem_bits)) & 0xFF

    with open(out_path, "wb") as f:
        f.write(img)


def main():
    ap = argparse.ArgumentParser(description="ProDOS (.po) disk tool")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_mk = sub.add_parser("format", help="Create empty formatted ProDOS .po disk")
    p_mk.add_argument("image")
    p_mk.add_argument("--name", default="OYKOUT")
    p_mk.add_argument("--blocks", type=int, default=1600)

    p_put = sub.add_parser("put", help="Write/replace host file into ProDOS image")
    p_put.add_argument("image")
    p_put.add_argument("src_file")
    p_put.add_argument("prodos_path")
    p_put.add_argument("--type", default="0xB3")
    p_put.add_argument("--aux", default="0xDB03")
    p_put.add_argument("--base", default=None, help="Optional base .po to copy first")

    p_get = sub.add_parser("get", help="Extract file from ProDOS image")
    p_get.add_argument("image")
    p_get.add_argument("prodos_path")
    p_get.add_argument("dst_file")

    p_ex = sub.add_parser("extract-bmps", help="Extract all .BMP files from root dir")
    p_ex.add_argument("image")
    p_ex.add_argument("out_dir")

    args = ap.parse_args()
    if args.cmd == "format":
        create_empty_po(args.image, args.name, args.blocks)
    elif args.cmd == "put":
        if args.base:
            shutil.copyfile(args.base, args.image)
        img = ProDosImage(args.image)
        with open(args.src_file, "rb") as f:
            data = f.read()
        img.write_file(args.prodos_path, data, int(args.type, 0), int(args.aux, 0))
        img.save()
    elif args.cmd == "get":
        img = ProDosImage(args.image)
        data = img.read_file(args.prodos_path)
        if data is None:
            sys.stderr.write(f"Not found in ProDOS image: {args.prodos_path}\n")
            return 1
        with open(args.dst_file, "wb") as f:
            f.write(data)
    elif args.cmd == "extract-bmps":
        os.makedirs(args.out_dir, exist_ok=True)
        img = ProDosImage(args.image)
        for name, kind in img.list_dir(""):
            if kind in (1, 2, 3) and name.upper().endswith(".BMP"):
                data = img.read_file(name)
                if data:
                    with open(os.path.join(args.out_dir, name.lower()), "wb") as f:
                        f.write(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
