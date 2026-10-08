#!/usr/bin/env python3

"""
texture
~~~~~~~
Decode SCX Texture exports to PNG and recook PNG back to TEXF mip chains.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from PIL import Image

from scct.common import read_index, write_index
from scct.props import parse_props, patch_props, props_to_map

TEXF = {
    0: "P8",
    1: "RGBA7",
    2: "RGB16",
    3: "DXT1",
    4: "RGB8",
    5: "RGBA8",
    6: "NODATA",
    7: "DXT3",
    8: "DXT5",
    9: "G8",
    10: "G16",
    11: "RRRGGGBBB",
    12: "DUDV16",
    13: "P4",
}


def log2_ceil(n: int) -> int:
    """Return ceil(log2(n)) for UBits/VBits."""
    n = max(1, int(n))
    return (n - 1).bit_length()


def parse_mips(blob: bytes, offset: int) -> tuple[list[tuple[int, int, bytes]], int]:
    """Parse a TArray of FMipmap (TLazyArray skip + compact size + pixels + USize/VSize/UBits/VBits)."""
    if offset >= len(blob):
        return [], offset
    count, offset = read_index(blob, offset)
    mips: list[tuple[int, int, bytes]] = []
    for _ in range(count):
        offset += 4
        nbytes, offset = read_index(blob, offset)
        data = blob[offset : offset + nbytes]
        offset += nbytes
        width, height = struct.unpack_from("<II", blob, offset)
        offset += 8
        offset += 2
        mips.append((width, height, data))
    return mips, offset


def parse_palette(names: Sequence[Any], blob: bytes) -> list[tuple[int, int, int, int]]:
    """Parse a Palette export to RGBA tuples (from BGRA on disk)."""
    _props, offset = parse_props(names, blob)
    count, offset = read_index(blob, offset)
    colors: list[tuple[int, int, int, int]] = []
    for _ in range(count):
        b, g, r, a = blob[offset : offset + 4]
        colors.append((r, g, b, a))
        offset += 4
    return colors


def _rgb565(c: int) -> tuple[int, int, int]:
    r = (c >> 11) & 31
    g = (c >> 5) & 63
    b = c & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def _to565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _lerp(a: int, b: int, na: int, nb: int, d: int) -> int:
    return (a * na + b * nb) // d


def _dxt_colors(c0: int, c1: int, opaque: bool) -> list[tuple[int, int, int, int]]:
    r0, g0, b0 = _rgb565(c0)
    r1, g1, b1 = _rgb565(c1)
    cols = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
    if opaque or c0 > c1:
        cols.append((_lerp(r0, r1, 2, 1, 3), _lerp(g0, g1, 2, 1, 3), _lerp(b0, b1, 2, 1, 3), 255))
        cols.append((_lerp(r0, r1, 1, 2, 3), _lerp(g0, g1, 1, 2, 3), _lerp(b0, b1, 1, 2, 3), 255))
    else:
        cols.append((_lerp(r0, r1, 1, 1, 2), _lerp(g0, g1, 1, 1, 2), _lerp(b0, b1, 1, 1, 2), 255))
        cols.append((0, 0, 0, 0))
    return cols


def decode_dxt(data: bytes, width: int, height: int, fmt: str) -> bytes:
    """Decode DXT1/DXT3/DXT5 to RGBA8."""
    out = bytearray(width * height * 4)
    block = 16 if fmt in ("DXT3", "DXT5") else 8
    i = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            chunk = data[i : i + block]
            i += block
            if len(chunk) < block:
                chunk = chunk + b"\x00" * (block - len(chunk))
            alpha: list[int] | None
            if fmt == "DXT1":
                alpha = None
                c0, c1, bits = struct.unpack_from("<HHI", chunk, 0)
                cols = _dxt_colors(c0, c1, opaque=False)
            elif fmt == "DXT3":
                a0 = struct.unpack_from("<Q", chunk, 0)[0]
                alpha = [((a0 >> (4 * n)) & 0xF) * 17 for n in range(16)]
                c0, c1, bits = struct.unpack_from("<HHI", chunk, 8)
                cols = _dxt_colors(c0, c1, opaque=True)
            else:
                a0, a1 = chunk[0], chunk[1]
                atab = [a0, a1]
                if a0 > a1:
                    for n in range(1, 7):
                        atab.append(_lerp(a0, a1, 7 - n, n, 7))
                else:
                    for n in range(1, 5):
                        atab.append(_lerp(a0, a1, 5 - n, n, 5))
                    atab.extend((0, 255))
                abits = int.from_bytes(chunk[2:8], "little")
                alpha = [atab[(abits >> (3 * n)) & 7] for n in range(16)]
                c0, c1, bits = struct.unpack_from("<HHI", chunk, 8)
                cols = _dxt_colors(c0, c1, opaque=True)
            for py in range(4):
                for px in range(4):
                    x, y = bx + px, by + py
                    if x >= width or y >= height:
                        continue
                    idx = (bits >> (2 * (py * 4 + px))) & 3
                    r, g, b, a = cols[idx]
                    if alpha is not None:
                        a = alpha[py * 4 + px]
                    o = (y * width + x) * 4
                    out[o : o + 4] = bytes((r, g, b, a))
    return bytes(out)


def decode_mip(fmt: int, width: int, height: int, data: bytes, palette: list[tuple[int, int, int, int]] | None) -> bytes:
    """Decode one mip to RGBA8."""
    name = TEXF.get(fmt, "")
    if name in ("DXT1", "DXT3", "DXT5"):
        return decode_dxt(data, width, height, name)
    if name in ("RGBA8", "RGBA7"):
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data) // 4)):
            b, g, r, a = data[i * 4 : i * 4 + 4]
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
        return bytes(out)
    if name == "RGB8":
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data) // 3)):
            b, g, r = data[i * 3 : i * 3 + 3]
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))
        return bytes(out)
    if name == "G8":
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data))):
            v = data[i]
            out[i * 4 : i * 4 + 4] = bytes((v, v, v, 255))
        return bytes(out)
    if name == "G16":
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data) // 2)):
            v = data[i * 2 + 1]
            out[i * 4 : i * 4 + 4] = bytes((v, v, v, 255))
        return bytes(out)
    if name == "P8":
        pal = palette or [(i, i, i, 255) for i in range(256)]
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data))):
            r, g, b, a = pal[data[i]]
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
        return bytes(out)
    if name == "RGB16":
        out = bytearray(width * height * 4)
        for i in range(min(width * height, len(data) // 2)):
            c = struct.unpack_from("<H", data, i * 2)[0]
            r, g, b = _rgb565(c)
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))
        return bytes(out)
    if name == "DUDV16":
        out = bytearray(width * height * 4)
        n = min(width * height, len(data) // 2)
        for i in range(n):
            u = data[i * 2]
            v = data[i * 2 + 1]
            nx = (u - 128) / 127.0
            ny = (v - 128) / 127.0
            nz = (max(0.0, 1.0 - nx * nx - ny * ny)) ** 0.5
            out[i * 4 : i * 4 + 4] = bytes((u, v, int(nz * 127.0 + 128.0), 255))
        return bytes(out)
    raise ValueError(f"unsupported texture format {fmt} ({name})")


def _pixel(rgba: bytes, width: int, height: int, x: int, y: int) -> tuple[int, int, int, int]:
    x = min(width - 1, max(0, x))
    y = min(height - 1, max(0, y))
    i = (y * width + x) * 4
    return rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]


def _encode_dxt1_block(px: list[tuple[int, int, int, int]], punch: bool) -> bytes:
    if punch:
        opac = [p for p in px if p[3] >= 128]
        if not opac:
            return struct.pack("<HHI", 0, 0, 0xFFFFFFFF)
        luma = [p[0] * 2 + p[1] * 3 + p[2] for p in opac]
        cmax = opac[max(range(len(opac)), key=lambda i: luma[i])]
        cmin = opac[min(range(len(opac)), key=lambda i: luma[i])]
        c0 = _to565(cmax[0], cmax[1], cmax[2])
        c1 = _to565(cmin[0], cmin[1], cmin[2])
        if c0 > c1:
            c0, c1 = c1, c0
        p0 = _rgb565(c0)
        p1 = _rgb565(c1)
        pal = [p0, p1, ((p0[0] + p1[0]) // 2, (p0[1] + p1[1]) // 2, (p0[2] + p1[2]) // 2)]
        bits = 0
        for i, p in enumerate(px):
            if p[3] < 128:
                idx = 3
            else:
                idx = min(range(3), key=lambda k: (pal[k][0] - p[0]) ** 2 + (pal[k][1] - p[1]) ** 2 + (pal[k][2] - p[2]) ** 2)
            bits |= idx << (2 * i)
        return struct.pack("<HHI", c0, c1, bits)
    luma = [p[0] * 2 + p[1] * 3 + p[2] for p in px]
    cmax = px[max(range(16), key=lambda i: luma[i])]
    cmin = px[min(range(16), key=lambda i: luma[i])]
    c0 = _to565(cmax[0], cmax[1], cmax[2])
    c1 = _to565(cmin[0], cmin[1], cmin[2])
    if c0 < c1:
        c0, c1 = c1, c0
    p0 = _rgb565(c0)
    p1 = _rgb565(c1)
    if c0 == c1:
        return struct.pack("<HHI", c0, c1, 0)
    pal = [
        p0,
        p1,
        ((2 * p0[0] + p1[0]) // 3, (2 * p0[1] + p1[1]) // 3, (2 * p0[2] + p1[2]) // 3),
        ((p0[0] + 2 * p1[0]) // 3, (p0[1] + 2 * p1[1]) // 3, (p0[2] + 2 * p1[2]) // 3),
    ]
    bits = 0
    for i, p in enumerate(px):
        idx = min(range(4), key=lambda k: (pal[k][0] - p[0]) ** 2 + (pal[k][1] - p[1]) ** 2 + (pal[k][2] - p[2]) ** 2)
        bits |= idx << (2 * i)
    return struct.pack("<HHI", c0, c1, bits)


def _encode_dxt5_alpha(alphas: list[int]) -> bytes:
    a_max = max(alphas)
    a_min = min(alphas)
    if a_max == a_min:
        return bytes([a_max, a_min]) + b"\x00" * 6
    if a_min == 0 or a_max == 255:
        a0, a1 = a_min, a_max
        atab = [a0, a1]
        for n in range(1, 5):
            atab.append(_lerp(a0, a1, 5 - n, n, 5))
        atab.extend((0, 255))
    else:
        a0, a1 = a_max, a_min
        atab = [a0, a1]
        for n in range(1, 7):
            atab.append(_lerp(a0, a1, 7 - n, n, 7))
    bits = 0
    for i, a in enumerate(alphas):
        idx = min(range(len(atab)), key=lambda k: abs(atab[k] - a))
        bits |= idx << (3 * i)
    return bytes([a0, a1]) + bits.to_bytes(6, "little")


def _has_punch(rgba: bytes) -> bool:
    amin, amax = 255, 0
    for i in range(3, len(rgba), 4):
        a = rgba[i]
        if a < amin:
            amin = a
        if a > amax:
            amax = a
    return amin < 250 and amax > 8


def encode_pixels(rgba: bytes, width: int, height: int, fmt: int) -> bytes:
    """Encode RGBA8 pixels to a TEXF mip payload."""
    name = TEXF.get(fmt, "")
    if name == "DXT1":
        punch = _has_punch(rgba)
        out = bytearray()
        for by in range(0, height, 4):
            for bx in range(0, width, 4):
                px = [_pixel(rgba, width, height, bx + x, by + y) for y in range(4) for x in range(4)]
                if punch:
                    px = [(p[0], p[1], p[2], 255 if p[3] >= 128 else 0) for p in px]
                out += _encode_dxt1_block(px, punch)
        return bytes(out)
    if name == "DXT3":
        out = bytearray()
        for by in range(0, height, 4):
            for bx in range(0, width, 4):
                px = [_pixel(rgba, width, height, bx + x, by + y) for y in range(4) for x in range(4)]
                ab = bytearray(8)
                for i, p in enumerate(px):
                    nibble = p[3] >> 4
                    if i % 2 == 0:
                        ab[i // 2] = nibble
                    else:
                        ab[i // 2] |= nibble << 4
                opaque = [(p[0], p[1], p[2], 255) for p in px]
                out += ab
                out += _encode_dxt1_block(opaque, False)
        return bytes(out)
    if name == "DXT5":
        out = bytearray()
        for by in range(0, height, 4):
            for bx in range(0, width, 4):
                px = [_pixel(rgba, width, height, bx + x, by + y) for y in range(4) for x in range(4)]
                out += _encode_dxt5_alpha([p[3] for p in px])
                opaque = [(p[0], p[1], p[2], 255) for p in px]
                out += _encode_dxt1_block(opaque, False)
        return bytes(out)
    if name in ("RGBA8", "RGBA7"):
        out = bytearray(width * height * 4)
        for i in range(width * height):
            r, g, b, a = rgba[i * 4 : i * 4 + 4]
            out[i * 4 : i * 4 + 4] = bytes((b, g, r, a))
        return bytes(out)
    if name == "RGB8":
        out = bytearray(width * height * 3)
        for i in range(width * height):
            r, g, b = rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]
            out[i * 3 : i * 3 + 3] = bytes((b, g, r))
        return bytes(out)
    if name == "G8":
        return bytes(rgba[i] for i in range(0, len(rgba), 4))
    if name == "DUDV16":
        out = bytearray(width * height * 2)
        for i in range(width * height):
            out[i * 2] = rgba[i * 4]
            out[i * 2 + 1] = rgba[i * 4 + 1]
        return bytes(out)
    if name == "RGB16":
        out = bytearray(width * height * 2)
        for i in range(width * height):
            r, g, b = rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]
            struct.pack_into("<H", out, i * 2, _to565(r, g, b))
        return bytes(out)
    if name == "P8":
        raise ValueError("P8 recook needs a palette; keep the original .bin or convert Format")
    raise ValueError(f"unsupported recook format {fmt} ({name})")


def _resize_rgba(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Resize RGB and alpha independently (avoid PIL premultiplied RGBA)."""
    if img.size == size:
        return img
    rgb = img.convert("RGB").resize(size, Image.Resampling.LANCZOS)
    alpha = img.getchannel("A").resize(size, Image.Resampling.LANCZOS)
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


def mip_chain(img: Image.Image, fmt: int) -> list[tuple[int, int, bytes]]:
    """Build a full mip chain from an RGBA image."""
    img = img.convert("RGBA")
    w0, h0 = img.size
    min_dim = 4 if TEXF.get(fmt) in ("DXT1", "DXT3", "DXT5") else 1
    levels: list[tuple[int, int, bytes]] = []
    w, h = w0, h0
    while True:
        level = _resize_rgba(img, (w, h))
        levels.append((w, h, encode_pixels(level.tobytes("raw", "RGBA"), w, h, fmt)))
        if w <= min_dim and h <= min_dim:
            break
        w, h = max(1, w // 2), max(1, h // 2)
    return levels


def write_mips(file_off: int, levels: list[tuple[int, int, bytes]]) -> bytes:
    """Serialize FMipmap TLazyArray at `file_off` (absolute package offset of this blob)."""
    out = bytearray(write_index(len(levels)))
    pos = file_off + len(out)
    for w, h, data in levels:
        idx = write_index(len(data))
        skip = pos + 4 + len(idx) + len(data)
        out += struct.pack("<I", skip)
        out += idx
        out += data
        out += struct.pack("<IIBB", w, h, log2_ceil(w), log2_ceil(h))
        pos += 4 + len(idx) + len(data) + 10
    return bytes(out)


def decode_texture_png(
    names: Sequence[Any],
    blob: bytes,
    palettes: dict[int, list[tuple[int, int, int, int]]],
) -> tuple[Image.Image, int, bytes] | None:
    """Decode a Texture serial blob to PNG-ready image plus format and property prefix.

    Returns:
        tuple | None: (image, tex_format, props_prefix) or None if the texture has no mips.
    """
    props, prop_end = parse_props(names, blob)
    pd = props_to_map(props)
    fmt = int(pd.get("Format", 3))
    if fmt == 6:
        return None
    mips, _off = parse_mips(blob, prop_end)
    if not mips:
        return None
    width, height, data = mips[0]
    pal = None
    pref = pd.get("Palette")
    if isinstance(pref, int) and pref > 0:
        pal = palettes.get(pref - 1)
    rgba = decode_mip(fmt, width, height, data, pal)
    img = Image.frombytes("RGBA", (width, height), rgba)
    return img, fmt, blob[:prop_end]


def recook_texture(
    names: Sequence[Any],
    png_path: Path,
    props_hex: str,
    tex_format: int,
    serial_off: int,
) -> bytes:
    """Build a Texture serial blob from a PNG, patched property header, and new mips."""
    img = Image.open(png_path).convert("RGBA")
    w, h = img.size
    updates: dict[tuple[str, int], int] = {
        ("USize", 0): w,
        ("VSize", 0): h,
        ("UClamp", 0): w,
        ("VClamp", 0): h,
        ("UBits", 0): log2_ceil(w),
        ("VBits", 0): log2_ceil(h),
    }
    if tex_format in (0, 13):
        tex_format = 5
        updates[("Format", 0)] = 5
    prefix = patch_props(names, bytes.fromhex(props_hex), updates)
    return prefix + write_mips(serial_off + len(prefix), mip_chain(img, tex_format))
