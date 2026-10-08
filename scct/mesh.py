#!/usr/bin/env python3

"""
mesh
~~~~
Decode SCX StaticMesh serials to Wavefront OBJ and recook OBJ back into the
cooked vertex/index streams (collision tail is preserved).
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import math
import struct
from collections.abc import Sequence
from dataclasses import dataclass

from scct.common import read_index, write_index
from scct.props import parse_props

_VERT_STRIDE = 28
_UV_SCALE = 32767.0


@dataclass
class StaticMeshData:
    """Parsed StaticMesh geometry plus the leftover collision tail."""

    prefix: bytes
    verts: list[tuple[float, float, float]]
    uvs: list[tuple[int, int]]
    extras: list[bytes]
    vrev: int
    indices: list[int]
    irev: int
    tail: bytes


def _read_box_sphere(blob: bytes, offset: int) -> int:
    """Skip FBox (6 floats + IsValid) and FSphere (4 floats)."""
    return offset + 25 + 16


def parse_static_mesh(names: Sequence[object], blob: bytes) -> StaticMeshData:
    """Parse a StaticMesh serial blob.

    Args:
        names (Sequence[object]): Package name table (tagged properties).
        blob (bytes): Export serial data.

    Returns:
        StaticMeshData: Geometry streams and the unparsed tail.

    Raises:
        ValueError: If the native layout does not match SCX StaticMesh.
    """
    _props, offset = parse_props(names, blob)
    offset = _read_box_sphere(blob, offset)
    prefix = blob[:offset]
    nverts, offset = read_index(blob, offset)
    if nverts < 1 or nverts > 500_000:
        raise ValueError(f"bad StaticMesh vertex count {nverts}")
    end_verts = offset + nverts * _VERT_STRIDE
    if end_verts + 8 > len(blob):
        raise ValueError("truncated StaticMesh vertex stream")
    verts: list[tuple[float, float, float]] = []
    uvs: list[tuple[int, int]] = []
    extras: list[bytes] = []
    for _ in range(nverts):
        x, y, z = struct.unpack_from("<3f", blob, offset)
        u, v = struct.unpack_from("<2H", blob, offset + 12)
        extras.append(bytes(blob[offset + 16 : offset + 28]))
        verts.append((x, y, z))
        uvs.append((u, v))
        offset += _VERT_STRIDE
    vrev = struct.unpack_from("<i", blob, offset)[0]
    offset += 4
    nidx, offset = read_index(blob, offset)
    if nidx < 0 or offset + nidx * 2 + 4 > len(blob):
        raise ValueError(f"bad StaticMesh index count {nidx}")
    indices = list(struct.unpack_from(f"<{nidx}H", blob, offset))
    offset += nidx * 2
    if indices and max(indices) >= nverts:
        raise ValueError("StaticMesh index out of range")
    irev = struct.unpack_from("<i", blob, offset)[0]
    offset += 4
    return StaticMeshData(prefix, verts, uvs, extras, vrev, indices, irev, blob[offset:])


def encode_static_mesh(mesh: StaticMeshData) -> bytes:
    """Serialize StaticMeshData to a cooked serial blob."""
    out = bytearray(mesh.prefix)
    out += write_index(len(mesh.verts))
    for pos, uv, extra in zip(mesh.verts, mesh.uvs, mesh.extras, strict=True):
        if len(extra) != 12:
            raise ValueError("StaticMesh vertex extra must be 12 bytes")
        out += struct.pack("<3f2H", pos[0], pos[1], pos[2], uv[0] & 0xFFFF, uv[1] & 0xFFFF)
        out += extra
    out += struct.pack("<i", mesh.vrev)
    out += write_index(len(mesh.indices))
    if mesh.indices:
        out += struct.pack(f"<{len(mesh.indices)}H", *[i & 0xFFFF for i in mesh.indices])
    out += struct.pack("<i", mesh.irev)
    out += mesh.tail
    return bytes(out)


def _uv_to_float(u: int, v: int) -> tuple[float, float]:
    return u / _UV_SCALE, v / _UV_SCALE


def _float_to_uv(u: float, v: float) -> tuple[int, int]:
    def conv(x: float) -> int:
        return max(0, min(65535, int(round(x * _UV_SCALE))))

    return conv(u), conv(v)


def write_obj(mesh: StaticMeshData, name: str) -> str:
    """Return Wavefront OBJ text for a StaticMesh (engine Z-up coordinates)."""
    lines = [
        f"# SCCT StaticMesh {name}",
        "# coordinates: Unreal Z-up, as stored",
    ]
    for x, y, z in mesh.verts:
        lines.append(f"v {x:.17g} {y:.17g} {z:.17g}")
    for u, v in mesh.uvs:
        fu, fv = _uv_to_float(u, v)
        lines.append(f"vt {fu:.17g} {fv:.17g}")
    idxs = mesh.indices
    for i in range(0, len(idxs) - 2, 3):
        a, b, c = idxs[i] + 1, idxs[i + 1] + 1, idxs[i + 2] + 1
        lines.append(f"f {a}/{a} {b}/{b} {c}/{c}")
    lines.append("")
    return "\n".join(lines)


def parse_obj(text: str) -> tuple[list[tuple[float, float, float]], list[tuple[float, float]], list[int]]:
    """Parse a Wavefront OBJ into positions, UVs, and triangle indices.

    Faces with more than three corners are fan-triangulated. Missing UVs
    become (0, 0).

    Args:
        text (str): OBJ file contents.

    Returns:
        tuple: (positions, uvs per vertex, triangle indices).
    """
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    faces: list[list[tuple[int, int | None]]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("v "):
            parts = line.split()
            positions.append((float(parts[1]), float(parts[2]), float(parts[3])))
        elif line.startswith("vt "):
            parts = line.split()
            texcoords.append((float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.0))
        elif line.startswith("f "):
            corners: list[tuple[int, int | None]] = []
            for item in line.split()[1:]:
                bits = item.split("/")
                vi = int(bits[0])
                if vi < 0:
                    vi = len(positions) + vi + 1
                ti = int(bits[1]) if len(bits) > 1 and bits[1] else None
                if ti is not None and ti < 0:
                    ti = len(texcoords) + ti + 1
                corners.append((vi - 1, None if ti is None else ti - 1))
            if len(corners) < 3:
                continue
            for i in range(1, len(corners) - 1):
                faces.append([corners[0], corners[i], corners[i + 1]])
    uvs: list[tuple[float, float]] = [(0.0, 0.0)] * len(positions)
    if len(texcoords) == len(positions):
        uvs = list(texcoords)
    elif texcoords:
        for tri in faces:
            for vi, ti in tri:
                if ti is not None and 0 <= vi < len(uvs) and 0 <= ti < len(texcoords):
                    uvs[vi] = texcoords[ti]
    indices: list[int] = []
    for tri in faces:
        indices.extend(vi for vi, _ti in tri)
    return positions, uvs, indices


def _bounds(verts: list[tuple[float, float, float]]) -> tuple[bytes, bytes]:
    """Return FBox (25 bytes) and FSphere (16 bytes) for `verts`."""
    xs = [p[0] for p in verts]
    ys = [p[1] for p in verts]
    zs = [p[2] for p in verts]
    mn = (min(xs), min(ys), min(zs))
    mx = (max(xs), max(ys), max(zs))
    box = struct.pack("<6fB", *mn, *mx, 1)
    cx = (mn[0] + mx[0]) * 0.5
    cy = (mn[1] + mx[1]) * 0.5
    cz = (mn[2] + mx[2]) * 0.5
    radius = 0.0
    for x, y, z in verts:
        radius = max(radius, math.dist((x, y, z), (cx, cy, cz)))
    sphere = struct.pack("<4f", cx, cy, cz, radius)
    return box, sphere


_DEFAULT_EXTRA = bytes.fromhex("7fff8000007f800080808000")


def _positions_close(
    a: list[tuple[float, float, float]],
    b: list[tuple[float, float, float]],
    eps: float = 1e-4,
) -> bool:
    """Return True if two position lists match within a relative epsilon."""
    if len(a) != len(b):
        return False
    for (x0, y0, z0), (x1, y1, z1) in zip(a, b):
        for left, right in ((x0, x1), (y0, y1), (z0, z1)):
            if abs(left - right) > eps * max(1.0, abs(left), abs(right)):
                return False
    return True


def recook_static_mesh(names: Sequence[object], raw_blob: bytes, obj_text: str) -> bytes:
    """Rebuild a StaticMesh serial blob from the original bytes plus an edited OBJ.

    Matching vertex and index counts patch positions/UVs in place (bit-identical
    when the OBJ was not changed). A different topology rebuilds the streams and
    keeps the original collision tail.

    Args:
        names (Sequence[object]): Package name table.
        raw_blob (bytes): Original serial blob (the `.bin` template).
        obj_text (str): Wavefront OBJ contents.

    Returns:
        bytes: Recooked serial data.
    """
    mesh = parse_static_mesh(names, raw_blob)
    positions, uvs_f, indices = parse_obj(obj_text)
    if not positions:
        raise ValueError("OBJ has no vertices")
    packed_uvs = [_float_to_uv(u, v) for u, v in uvs_f]
    core = len(mesh.indices) - (len(mesh.indices) % 3)
    leftover = mesh.indices[core:]
    if (
        len(positions) == len(mesh.verts)
        and indices == mesh.indices[:core]
        and packed_uvs == mesh.uvs
        and _positions_close(positions, mesh.verts)
    ):
        return raw_blob
    if len(positions) == len(mesh.verts) and len(indices) == core:
        mesh.verts = [(float(p[0]), float(p[1]), float(p[2])) for p in positions]
        mesh.uvs = packed_uvs
        mesh.indices = [int(i) for i in indices] + leftover
    else:
        extras = []
        for i in range(len(positions)):
            extras.append(mesh.extras[i] if i < len(mesh.extras) else _DEFAULT_EXTRA)
        mesh.verts = [(float(p[0]), float(p[1]), float(p[2])) for p in positions]
        mesh.uvs = packed_uvs
        mesh.extras = extras
        mesh.indices = [int(i) for i in indices]
        if mesh.indices and max(mesh.indices) >= len(mesh.verts):
            raise ValueError("OBJ face index out of range")
    box, sphere = _bounds(mesh.verts)
    mesh.prefix = mesh.prefix[:-41] + box + sphere
    return encode_static_mesh(mesh)
