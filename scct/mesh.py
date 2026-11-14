#!/usr/bin/env python3

"""
mesh
~~~~
Decode SCX StaticMesh serials to Wavefront OBJ and recook OBJ back into the
cooked vertex/index streams (collision tail is preserved).

Cooked SCCT StaticMesh layout (after tagged properties)::

    FBox BoundingBox (6f + u8) / FSphere BoundingSphere (4f)
    TArray<Vertex> VertexStream   Vertex = pos 3f, uv 2*s16 (/2048),
                                  tangent, binormal, normal (3x D3DCOLOR, BGRA,
                                  component = byte/127.5 - 1)
    int   VertexRevision
    TArray<u16> IndexBuffer       triangle list
    int   IndexRevision
    TArray<u16> WireframeIndexBuffer (line list)
    int   WireframeRevision
    int   Unknown (0x20EA)
    int   bHasColorStream         if set: TArray<FColor> + int Revision
    TArray<Section>               Section = int A, int IsStrip, u16 FirstIndex,
                                  u16 MinVertexIndex, u16 MaxVertexIndex,
                                  u16 NumTriangles, u16 NumPrimitives, int B,
                                  compact Material
    ... collision / kDOP data (kept verbatim)

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import math
import re
import struct
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field

from scct.common import read_index, write_index
from scct.props import parse_props

_VERT_STRIDE = 28
# Legacy (OBJ v1) UV mapping: unsigned u16 / 32767.
_UV_SCALE = 32767.0
# Real engine UV mapping: signed s16 / 2048.
_UV_S16_SCALE = 2048.0
# OBJ v2 marker written by write_obj.
OBJ_V2_MARKER = "# scct-obj 2"

_SECTION_FMT = "<ii5Hi"
_SECTION_SIZE = struct.calcsize(_SECTION_FMT)


@dataclass
class MeshSection:
    """One render section (material slot) of a StaticMesh."""

    a: int
    is_strip: int
    first_index: int
    min_vertex: int
    max_vertex: int
    num_triangles: int
    num_primitives: int
    b: int
    material: int


@dataclass
class MeshTail:
    """Decoded streams that follow the index buffer."""

    wire: list[int]
    wire_rev: int
    unknown: int
    colors: list[bytes] | None
    color_rev: int
    sections: list[MeshSection]
    rest: bytes


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
    parsed_tail: MeshTail | None = field(default=None)


def _read_box_sphere(blob: bytes, offset: int) -> int:
    """Skip FBox (6 floats + IsValid) and FSphere (4 floats)."""
    return offset + 25 + 16


def parse_mesh_tail(tail: bytes, nverts: int, nindices: int) -> MeshTail | None:
    """Decode wireframe, color stream and sections; None if the layout does not match."""
    try:
        nwire, off = read_index(tail, 0)
        if nwire < 0 or off + nwire * 2 + 12 > len(tail):
            return None
        wire = list(struct.unpack_from(f"<{nwire}H", tail, off))
        off += nwire * 2
        wire_rev, unknown, has_colors = struct.unpack_from("<iii", tail, off)
        off += 12
        colors: list[bytes] | None = None
        color_rev = 0
        if has_colors not in (0, 1):
            return None
        if has_colors:
            ncol, off = read_index(tail, off)
            if ncol != nverts:
                return None
            colors = [bytes(tail[off + 4 * i : off + 4 * i + 4]) for i in range(ncol)]
            off += 4 * ncol
            color_rev = struct.unpack_from("<i", tail, off)[0]
            off += 4
        nsec, off = read_index(tail, off)
        if nsec < 1 or nsec > 64:
            return None
        sections: list[MeshSection] = []
        for _ in range(nsec):
            a, strip, first, mn, mx, ntri, nprim, b = struct.unpack_from(_SECTION_FMT, tail, off)
            off += _SECTION_SIZE
            mat, off = read_index(tail, off)
            sections.append(MeshSection(a, strip, first, mn, mx, ntri, nprim, b, mat))
        # triangle-strip sections (common in shared object packages) are supported: num_primitives + 2 indices
        if sum(s.num_triangles for s in sections if not s.is_strip) * 3 > nindices:
            return None
        for s in sections:
            count = s.num_primitives + 2 if s.is_strip else s.num_triangles * 3
            if s.first_index + count > nindices or s.max_vertex >= max(nverts, 1):
                return None
        return MeshTail(wire, wire_rev, unknown, colors, color_rev, sections, bytes(tail[off:]))
    except (IndexError, struct.error):
        return None


def section_triangles(indices: Sequence[int], sec: "MeshSection") -> list[tuple[int, int, int]]:
    """Triangles of a render section in engine winding (triangle lists and strips).

    Strips hold num_primitives + 2 indices; every odd triangle is flipped back to the
    winding of the first one and degenerate (stitching) triangles are dropped.
    """
    s = sec.first_index
    if not sec.is_strip:
        return [(indices[i], indices[i + 1], indices[i + 2]) for i in range(s, s + sec.num_triangles * 3, 3)]
    strip = indices[s : s + sec.num_primitives + 2]
    out: list[tuple[int, int, int]] = []
    for i in range(len(strip) - 2):
        a, b, c = strip[i], strip[i + 1], strip[i + 2]
        if a == b or b == c or a == c:
            continue
        out.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return out


def encode_mesh_tail(mt: MeshTail) -> bytes:
    """Serialize a MeshTail back to bytes."""
    out = bytearray(write_index(len(mt.wire)))
    if mt.wire:
        out += struct.pack(f"<{len(mt.wire)}H", *mt.wire)
    out += struct.pack("<iii", mt.wire_rev, mt.unknown, 1 if mt.colors is not None else 0)
    if mt.colors is not None:
        out += write_index(len(mt.colors))
        for col in mt.colors:
            out += col
        out += struct.pack("<i", mt.color_rev)
    out += write_index(len(mt.sections))
    for s in mt.sections:
        out += struct.pack(
            _SECTION_FMT,
            s.a,
            s.is_strip,
            s.first_index,
            s.min_vertex,
            s.max_vertex,
            s.num_triangles,
            s.num_primitives,
            s.b,
        )
        out += write_index(s.material)
    out += mt.rest
    return bytes(out)


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
    tail = bytes(blob[offset:])
    return StaticMeshData(
        prefix, verts, uvs, extras, vrev, indices, irev, tail, parse_mesh_tail(tail, nverts, nidx)
    )


def encode_static_mesh(mesh: StaticMeshData) -> bytes:
    """Serialize StaticMeshData to a cooked serial blob (uses `tail` as-is)."""
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


# --------------------------------------------------------------------------- helpers


def _float_to_uv(u: float, v: float) -> tuple[int, int]:
    """Legacy v1 mapping (unsigned / 32767)."""

    def conv(x: float) -> int:
        return max(0, min(65535, int(round(x * _UV_SCALE))))

    return conv(u), conv(v)


def _s16(value: int) -> int:
    return value - 0x10000 if value >= 0x8000 else value


def uv_raw_to_obj(u: int, v: int) -> tuple[float, float]:
    """Engine u16 pair -> OBJ vt (s16/2048, V flipped so textures map like in-game)."""
    return _s16(u) / _UV_S16_SCALE, 1.0 - _s16(v) / _UV_S16_SCALE


def uv_obj_to_raw(u: float, v: float) -> tuple[int, int]:
    """OBJ vt -> engine u16 pair (inverse of uv_raw_to_obj, saturated to s16)."""

    def conv(x: float) -> int:
        return max(-32768, min(32767, int(round(x * _UV_S16_SCALE)))) & 0xFFFF

    return conv(u), conv(1.0 - v)


def unpack_vec(packed: bytes, slot: int) -> tuple[float, float, float]:
    """Decode one D3DCOLOR packed vector (slot 0 tangent, 1 binormal, 2 normal)."""
    b = packed[slot * 4 : slot * 4 + 4]
    return (b[2] / 127.5 - 1.0, b[1] / 127.5 - 1.0, b[0] / 127.5 - 1.0)


def _pack_byte(x: float) -> int:
    return max(0, min(255, int(round((x * 0.5 + 0.5) * 255.0))))


def pack_vec(vec: tuple[float, float, float], w: int = 0) -> bytes:
    """Encode a unit vector to D3DCOLOR (BGRA) bytes."""
    return bytes((_pack_byte(vec[2]), _pack_byte(vec[1]), _pack_byte(vec[0]), w & 0xFF))


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a, fallback=(0.0, 0.0, 1.0)):
    length = math.sqrt(_dot(a, a))
    if length < 1e-12:
        return fallback
    return (a[0] / length, a[1] / length, a[2] / length)


def _any_perp(n):
    ref = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    return _norm(_cross(n, ref))


# --------------------------------------------------------------------------- OBJ I/O


def write_obj(
    mesh: StaticMeshData,
    name: str,
    material_name: Callable[[int], str] | None = None,
) -> str:
    """Return Wavefront OBJ text for a StaticMesh (engine Z-up coordinates).

    OBJ v2 layout: `v` (+ vertex color when the mesh has a color stream),
    `vt` (signed s16/2048, V flipped), `vn` (packed vertex normal), one
    `g section_N` / `usemtl` block per render section. Faces keep engine
    winding. Vertex, UV and normal indices are identical per corner.
    """
    tail = mesh.parsed_tail
    lines = [
        f"# SCCT StaticMesh {name}",
        OBJ_V2_MARKER,
        "# coordinates: Unreal Z-up, as stored; vt = (s16/2048, 1 - s16/2048); vn = packed vertex normal",
        "# keep one `g section_N` group per render section; faces may be added/removed/subdivided",
    ]
    colors = tail.colors if tail is not None else None
    for i, (x, y, z) in enumerate(mesh.verts):
        if colors is not None:
            c = colors[i]
            lines.append(f"v {x:.9g} {y:.9g} {z:.9g} {c[2] / 255:.6g} {c[1] / 255:.6g} {c[0] / 255:.6g}")
        else:
            lines.append(f"v {x:.9g} {y:.9g} {z:.9g}")
    for u, v in mesh.uvs:
        fu, fv = uv_raw_to_obj(u, v)
        lines.append(f"vt {fu:.9g} {fv:.9g}")
    for extra in mesh.extras:
        n = unpack_vec(extra, 2)
        lines.append(f"vn {n[0]:.6g} {n[1]:.6g} {n[2]:.6g}")
    idxs = mesh.indices
    if tail is not None:
        groups = [(k, s) for k, s in enumerate(tail.sections)]
    else:
        groups = [(0, MeshSection(0, 0, 0, 0, len(mesh.verts) - 1, len(idxs) // 3, len(idxs) // 3, 0, 0))]
    for k, sec in groups:
        mat = material_name(sec.material) if (material_name and tail is not None) else f"material_{k}"
        mat = re.sub(r"\s+", "_", mat) or f"material_{k}"
        lines.append(f"g section_{k}")
        lines.append(f"usemtl {mat}")
        for a, b, c in section_triangles(idxs, sec):
            a, b, c = a + 1, b + 1, c + 1
            lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
    lines.append("")
    return "\n".join(lines)


@dataclass
class ObjData:
    positions: list[tuple[float, float, float]]
    colors: list[tuple[float, float, float] | None]
    texcoords: list[tuple[float, float]]
    normals: list[tuple[float, float, float]]
    # triangles: (group_key, ((vi, ti, ni), (vi, ti, ni), (vi, ti, ni)))
    triangles: list[tuple[int, tuple[tuple[int, int, int], ...]]]
    group_order: list[int]


def is_obj_v2(text: str) -> bool:
    """True when the OBJ was written by write_obj v2 (or keeps its marker)."""
    return OBJ_V2_MARKER in text[:4096]


def parse_obj_full(text: str) -> ObjData:
    """Parse an OBJ keeping per-corner v/vt/vn indices and `g section_N` groups."""
    positions: list[tuple[float, float, float]] = []
    colors: list[tuple[float, float, float] | None] = []
    texcoords: list[tuple[float, float]] = []
    normals: list[tuple[float, float, float]] = []
    triangles: list[tuple[int, tuple[tuple[int, int, int], ...]]] = []
    group_order: list[int] = []
    mat_groups: dict[str, int] = {}
    cur_group = 0
    explicit_group = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        tag, _, rest = line.partition(" ")
        if tag == "v":
            parts = rest.split()
            positions.append((float(parts[0]), float(parts[1]), float(parts[2])))
            colors.append((float(parts[3]), float(parts[4]), float(parts[5])) if len(parts) >= 6 else None)
        elif tag == "vt":
            parts = rest.split()
            texcoords.append((float(parts[0]), float(parts[1]) if len(parts) > 1 else 0.0))
        elif tag == "vn":
            parts = rest.split()
            normals.append((float(parts[0]), float(parts[1]), float(parts[2])))
        elif tag == "g":
            m = re.search(r"section_(\d+)", rest)
            if m:
                cur_group = int(m.group(1))
                explicit_group = True
        elif tag == "usemtl" and not explicit_group:
            cur_group = mat_groups.setdefault(rest.strip(), len(mat_groups))
        elif tag == "f":
            corners: list[tuple[int, int, int]] = []
            for item in rest.split():
                bits = item.split("/")
                vi = int(bits[0])
                vi = len(positions) + vi if vi < 0 else vi - 1
                ti = int(bits[1]) if len(bits) > 1 and bits[1] else 0
                ti = len(texcoords) + ti if ti < 0 else ti - 1
                ni = int(bits[2]) if len(bits) > 2 and bits[2] else 0
                ni = len(normals) + ni if ni < 0 else ni - 1
                corners.append((vi, ti, ni))
            if len(corners) < 3:
                continue
            if cur_group not in group_order:
                group_order.append(cur_group)
            for i in range(1, len(corners) - 1):
                triangles.append((cur_group, (corners[0], corners[i], corners[i + 1])))
    return ObjData(positions, colors, texcoords, normals, triangles, group_order)


def parse_obj(text: str) -> tuple[list[tuple[float, float, float]], list[tuple[float, float]], list[int]]:
    """Parse a Wavefront OBJ into positions, UVs, and triangle indices.

    Faces with more than three corners are fan-triangulated. Missing UVs
    become (0, 0). (Legacy helper: one UV per position.)

    Args:
        text (str): OBJ file contents.

    Returns:
        tuple: (positions, uvs per vertex, triangle indices).
    """
    data = parse_obj_full(text)
    positions = data.positions
    texcoords = data.texcoords
    uvs: list[tuple[float, float]] = [(0.0, 0.0)] * len(positions)
    if len(texcoords) == len(positions):
        uvs = list(texcoords)
    elif texcoords:
        for _g, tri in data.triangles:
            for vi, ti, _ni in tri:
                if 0 <= vi < len(uvs) and 0 <= ti < len(texcoords):
                    uvs[vi] = texcoords[ti]
    indices: list[int] = []
    for _g, tri in data.triangles:
        indices.extend(vi for vi, _ti, _ni in tri)
    return positions, uvs, indices


# --------------------------------------------------------------------------- recook


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


def _tangent_frames(
    verts: list[tuple[float, float, float]],
    uvs_raw: list[tuple[int, int]],
    normals: list[tuple[float, float, float]],
    indices: list[int],
) -> tuple[list[tuple[float, float, float]], list[tuple[float, float, float]]]:
    """Per-vertex tangent (dP/du) and binormal (dP/dv), engine UV space, orthogonal to N."""
    n = len(verts)
    tan = [(0.0, 0.0, 0.0)] * n
    bit = [(0.0, 0.0, 0.0)] * n
    uv = [(_s16(u) / _UV_S16_SCALE, _s16(v) / _UV_S16_SCALE) for u, v in uvs_raw]
    for i in range(0, len(indices) - 2, 3):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        e1 = _sub(verts[b], verts[a])
        e2 = _sub(verts[c], verts[a])
        du1, dv1 = uv[b][0] - uv[a][0], uv[b][1] - uv[a][1]
        du2, dv2 = uv[c][0] - uv[a][0], uv[c][1] - uv[a][1]
        r = du1 * dv2 - du2 * dv1
        if abs(r) < 1e-12:
            continue
        t = _mul(_sub(_mul(e1, dv2), _mul(e2, dv1)), 1.0 / r)
        bt = _mul(_sub(_mul(e2, du1), _mul(e1, du2)), 1.0 / r)
        # weight by triangle area so slivers do not dominate
        area = math.sqrt(_dot(_cross(e1, e2), _cross(e1, e2)))
        t = _mul(_norm(t, (0.0, 0.0, 0.0)), area)
        bt = _mul(_norm(bt, (0.0, 0.0, 0.0)), area)
        for k in (a, b, c):
            tan[k] = _add(tan[k], t)
            bit[k] = _add(bit[k], bt)
    out_t: list[tuple[float, float, float]] = []
    out_b: list[tuple[float, float, float]] = []
    for k in range(n):
        nn = normals[k]
        t = _sub(tan[k], _mul(nn, _dot(nn, tan[k])))
        b = _sub(bit[k], _mul(nn, _dot(nn, bit[k])))
        t = _norm(t, (0.0, 0.0, 0.0))
        b = _norm(b, (0.0, 0.0, 0.0))
        if _dot(t, t) < 0.5 and _dot(b, b) < 0.5:
            t = _any_perp(nn)
            b = _cross(nn, t)
        elif _dot(t, t) < 0.5:
            t = _norm(_cross(b, nn))
        elif _dot(b, b) < 0.5:
            b = _norm(_cross(nn, t))
        out_t.append(t)
        out_b.append(b)
    return out_t, out_b


def _smooth_normals(verts, indices):
    """Area-weighted vertex normals in engine winding (front = clockwise)."""
    acc = [(0.0, 0.0, 0.0)] * len(verts)
    for i in range(0, len(indices) - 2, 3):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        fn = _cross(_sub(verts[c], verts[a]), _sub(verts[b], verts[a]))
        for k in (a, b, c):
            acc[k] = _add(acc[k], fn)
    return [_norm(v) for v in acc]


class _NearestIndex:
    """Tiny uniform-grid nearest-neighbour lookup over template vertices."""

    def __init__(self, points: list[tuple[float, float, float]]):
        self.points = points
        xs = [p[0] for p in points] or [0.0]
        ys = [p[1] for p in points] or [0.0]
        zs = [p[2] for p in points] or [0.0]
        ext = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), 1e-3)
        self.cell = ext / max(1.0, round(len(points) ** (1.0 / 3.0)))
        self.grid: dict[tuple[int, int, int], list[int]] = {}
        for i, p in enumerate(points):
            self.grid.setdefault(self._key(p), []).append(i)

    def _key(self, p):
        c = self.cell
        return (math.floor(p[0] / c), math.floor(p[1] / c), math.floor(p[2] / c))

    def query(self, p) -> int:
        kx, ky, kz = self._key(p)
        best, best_d = -1, float("inf")
        radius = 0
        while True:
            for dx in range(-radius, radius + 1):
                for dy in range(-radius, radius + 1):
                    for dz in range(-radius, radius + 1):
                        if max(abs(dx), abs(dy), abs(dz)) != radius:
                            continue
                        for i in self.grid.get((kx + dx, ky + dy, kz + dz), ()):
                            d = math.dist(p, self.points[i])
                            if d < best_d:
                                best, best_d = i, d
            if best >= 0 and best_d <= radius * self.cell:
                return best
            radius += 1
            if radius > 4096:
                return max(best, 0)


class _TemplateTriangles:
    """Find the template triangle a new vertex came from (UV-space barycentrics).

    New vertices of a subdivided / edited mesh usually lie on (or near) an
    original triangle and carry linearly interpolated UVs. Interpolating the
    authored tangent frames / colors of that triangle keeps normal-mapped
    lighting continuous with the untouched vertices.
    """

    def __init__(self, mesh: StaticMeshData):
        self.mesh = mesh
        self.uv = [(_s16(u) / _UV_S16_SCALE, _s16(v) / _UV_S16_SCALE) for u, v in mesh.uvs]
        idx = mesh.indices
        if mesh.parsed_tail is not None and any(s.is_strip for s in mesh.parsed_tail.sections):
            self.tris = [t for sec in mesh.parsed_tail.sections for t in section_triangles(idx, sec)]
        else:
            self.tris = [(idx[i], idx[i + 1], idx[i + 2]) for i in range(0, len(idx) - 2, 3)]
        sizes = []
        cents = []
        for a, b, c in self.tris:
            pa, pb, pc = mesh.verts[a], mesh.verts[b], mesh.verts[c]
            cents.append(((pa[0] + pb[0] + pc[0]) / 3, (pa[1] + pb[1] + pc[1]) / 3, (pa[2] + pb[2] + pc[2]) / 3))
            sizes.append(max(math.dist(pa, pb), math.dist(pb, pc), math.dist(pc, pa)))
        self.sizes = sizes
        self.cents = cents
        self.cell = max(max(sizes, default=1.0), 1e-3)
        self.grid: dict[tuple[int, int, int], list[int]] = {}
        for t, cp in enumerate(cents):
            self.grid.setdefault(self._key(cp), []).append(t)

    def _key(self, p):
        c = self.cell
        return (math.floor(p[0] / c), math.floor(p[1] / c), math.floor(p[2] / c))

    def find(self, p, uv_raw) -> tuple[tuple[int, int, int], tuple[float, float, float]] | None:
        u = _s16(uv_raw[0]) / _UV_S16_SCALE
        v = _s16(uv_raw[1]) / _UV_S16_SCALE
        kx, ky, kz = self._key(p)
        best = None
        best_d = float("inf")
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for t in self.grid.get((kx + dx, ky + dy, kz + dz), ()):
                        a, b, c = self.tris[t]
                        (u0, v0), (u1, v1), (u2, v2) = self.uv[a], self.uv[b], self.uv[c]
                        det = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)
                        if abs(det) < 1e-12:
                            continue
                        l1 = ((u - u0) * (v2 - v0) - (u2 - u0) * (v - v0)) / det
                        l2 = ((u1 - u0) * (v - v0) - (u - u0) * (v1 - v0)) / det
                        l0 = 1.0 - l1 - l2
                        if min(l0, l1, l2) < -1e-3:
                            continue
                        pa, pb, pc = self.mesh.verts[a], self.mesh.verts[b], self.mesh.verts[c]
                        q = (
                            pa[0] * l0 + pb[0] * l1 + pc[0] * l2,
                            pa[1] * l0 + pb[1] * l1 + pc[1] * l2,
                            pa[2] * l0 + pb[2] * l1 + pc[2] * l2,
                        )
                        d = math.dist(p, q)
                        if d <= 0.35 * self.sizes[t] + 1e-4 and d < best_d:
                            best, best_d = ((a, b, c), (l0, l1, l2)), d
        return best


def _section_key_map(order: list[int], nsec: int) -> dict[int, int]:
    """Map OBJ group keys to template section indices."""
    if all(0 <= g < nsec for g in order):
        return {g: g for g in order}
    return {g: k for k, g in enumerate(order)}


# New vertices keep the interpolated stock tangent frame only where it agrees with the frame computed from the
# new geometry / UVs within this angle; otherwise the computed frame is used. (A loose limit lets rounded meshes
# inherit the piecewise-constant tangents of the old facets: smooth normals with faceted tangents light as flat
# bands in game.)
STOCK_TANGENT_AGREE_DEG = 8.0


def _rebuild_v2(mesh: StaticMeshData, raw_blob: bytes, obj: ObjData) -> bytes:
    """Rebuild a StaticMesh from an OBJ v2 with any topology, append-only.

    The original vertex stream and index buffer are kept byte for byte at the
    start of the new streams; new render geometry is appended after them and
    the render sections point at the appended ranges. Everything else in the
    cooked serial (collision model, the second section table with its own
    index list and precomputed physics data) keeps referring to the original
    vertices/triangles, which therefore stay valid and unchanged.

    Tangent frames (SCCT lights static meshes per pixel in tangent space; corners of one face whose
    tangents point different ways light as bright diagonal "metallic" streaks):

    * the render geometry never shares an original vertex (a stock vertex keeps its stock tangent,
      which is wrong for a new triangle whose UVs run another way);
    * a corner shared by triangles whose UVs run different ways (mirrored / rotated islands meeting
      at one vertex with the same UV) gets one vertex per UV direction;
    * a new vertex keeps the interpolated stock tangent only where it agrees with the tangent computed
      from the new geometry (STOCK_TANGENT_AGREE_DEG), and every frame is made orthonormal (B = +-N x T);
    * UV-degenerate triangles (flat colour) get a frame from the normal only.
    """
    tail = mesh.parsed_tail
    if tail is None:
        raise ValueError("StaticMesh tail layout not understood; topology changes are not supported for this mesh")
    nsec = len(tail.sections)
    keymap = _section_key_map(obj.group_order, nsec)
    if len(obj.group_order) > nsec or any(k >= nsec for k in keymap.values()):
        raise ValueError(f"OBJ has {len(obj.group_order)} groups but StaticMesh has {nsec} sections")
    per_sec: list[list[tuple[tuple[int, int, int], ...]]] = [[] for _ in range(nsec)]
    for g, tri in obj.triangles:
        per_sec[keymap[g]].append(tri)
    for k, tris in enumerate(per_sec):
        if not tris:
            raise ValueError(f"StaticMesh section {k} has no triangles in the OBJ")

    n0 = len(mesh.verts)
    has_normals = bool(obj.normals)

    def corner_data(corner):
        vi, ti, ni = corner
        p = obj.positions[vi]
        uv = uv_obj_to_raw(*obj.texcoords[ti]) if 0 <= ti < len(obj.texcoords) else (0, 0)
        n_raw = obj.normals[ni] if has_normals and 0 <= ni < len(obj.normals) else None
        return p, uv, n_raw

    def face_frame(tri):
        (pa, ua, _), (pb, ub, _), (pc, uc, _) = (corner_data(c) for c in tri)
        ua, ub, uc = ((_s16(u[0]) / 2048.0, _s16(u[1]) / 2048.0) for u in (ua, ub, uc))
        e1, e2 = _sub(pb, pa), _sub(pc, pa)
        du1, dv1, du2, dv2 = ub[0] - ua[0], ub[1] - ua[1], uc[0] - ua[0], uc[1] - ua[1]
        r = du1 * dv2 - du2 * dv1
        if abs(r) < 1e-12:
            return None
        t = _norm(_mul(_sub(_mul(e1, dv2), _mul(e2, dv1)), 1.0 / r), (0.0, 0.0, 0.0))
        b = _norm(_mul(_sub(_mul(e2, du1), _mul(e1, du2)), 1.0 / r), (0.0, 0.0, 0.0))
        return t, b

    # one vertex per (corner, UV direction cluster); -1 = UV-degenerate triangle
    clusters: dict[tuple[int, int, int], list] = {}
    tri_key: dict[tuple[int, int, tuple[int, int, int]], tuple] = {}
    for k in range(nsec):
        for ti, tri in enumerate(per_sec[k]):
            fr = face_frame(tri)
            for c in tri:
                lst = clusters.setdefault(c, [])
                cid = -1
                if fr is not None:
                    for q, ref in enumerate(lst):
                        if _dot(ref[0], fr[0]) > 0.3 and _dot(ref[1], fr[1]) > 0.3:
                            cid = q
                            break
                    else:
                        lst.append(fr)
                        cid = len(lst) - 1
                tri_key[(k, ti, c)] = (c, cid)

    # Index buffer: original indices first (untouched), then the render sections.
    # Section.FirstIndex / NumTriangles are u16: the largest section goes last.
    order = sorted(range(nsec), key=lambda k: (len(per_sec[k]), k))
    vmap: dict[tuple, int] = {}
    new_keys: list[tuple[int, int, int]] = []
    degenerate: list[bool] = []
    # A mesh rebuilt by this function already starts with the hidden original
    # indices (no section starts at 0): keep only that part, not the old render ranges.
    min_first = min(sec.first_index for sec in tail.sections)
    keep = mesh.indices[:min_first] if min_first > 0 else mesh.indices
    indices: list[int] = list(keep)
    placed: dict[int, MeshSection] = {}
    for k in order:
        tris = per_sec[k]
        first = len(indices)
        for ti, tri in enumerate(tris):
            for corner in tri:
                key = tri_key[(k, ti, corner)]
                idx = vmap.get(key)
                if idx is None:
                    idx = n0 + len(new_keys)  # never share an original vertex
                    new_keys.append(corner)
                    degenerate.append(key[1] == -1)
                    vmap[key] = idx
                indices.append(idx)
        used = indices[first:]
        src = tail.sections[k]
        if first > 0xFFFF or len(tris) > 0xFFFF:
            raise ValueError(
                f"StaticMesh section {k}: first index {first} / {len(tris)} triangles exceed the u16 section "
                "limits (all sections but the largest must fit in 65535 indices, each section <= 65535 triangles)"
            )
        placed[k] = MeshSection(src.a, 0, first, min(used), max(used), len(tris), len(tris), src.b, src.material)
    sections = [placed[k] for k in range(nsec)]
    nv = n0 + len(new_keys)
    if nv > 0xFFFF:
        raise ValueError(f"StaticMesh would need {nv} vertices (max 65535)")

    verts = list(mesh.verts)
    uvs = list(mesh.uvs)
    normals_raw: list = [unpack_vec(e, 2) for e in mesh.extras]
    for corner in new_keys:
        p, uv, n_raw = corner_data(corner)
        verts.append((float(p[0]), float(p[1]), float(p[2])))
        uvs.append(uv)
        normals_raw.append(n_raw)
    render_idx = indices[len(keep):]
    if any(n is None for n in normals_raw):
        smooth = _smooth_normals(verts, render_idx)
        normals_raw = [smooth[i] if n is None else n for i, n in enumerate(normals_raw)]
    normals = [_norm(n) for n in normals_raw]
    tangents, binormals = _tangent_frames(verts, uvs, normals, render_idx)

    near = _NearestIndex(mesh.verts)
    loc = _TemplateTriangles(mesh)
    extras: list[bytes] = list(mesh.extras)
    colors: list[bytes] | None = list(tail.colors) if tail.colors is not None else None
    agree = math.cos(math.radians(STOCK_TANGENT_AGREE_DEG))
    for j, corner in enumerate(new_keys):
        i = n0 + j
        p, uv, n, n_raw = verts[i], uvs[i], normals[i], normals_raw[i]
        hit = loc.find(p, uv)
        t_new, b_new = tangents[i], binormals[i]
        ref = mesh.extras[near.query(p)]
        if degenerate[j]:
            t_new = _any_perp(n)
            b_new = _norm(_cross(n, t_new))
        elif hit is not None:
            (ta, tb, tc), bary = hit
            tsum = (0.0, 0.0, 0.0)
            bsum = (0.0, 0.0, 0.0)
            for vid, w in zip((ta, tb, tc), bary):
                tsum = _add(tsum, _mul(_norm(unpack_vec(mesh.extras[vid], 0)), w))
                bsum = _add(bsum, _mul(_norm(unpack_vec(mesh.extras[vid], 1)), w))
            t_i = _norm(_sub(tsum, _mul(n, _dot(n, tsum))), tangents[i])
            b_i = _norm(_sub(bsum, _mul(n, _dot(n, bsum))), binormals[i])
            if _dot(t_i, tangents[i]) >= agree and _dot(b_i, binormals[i]) >= agree:
                t_new, b_new = t_i, b_i
            ref = mesh.extras[(ta, tb, tc)[max(range(3), key=lambda q: bary[q])]]
        # orthonormal frame: the engine treats (T, B, N) as a rotation; nearly parallel T / B light as bands
        t_o = _norm(_sub(t_new, _mul(n, _dot(n, t_new))), (0.0, 0.0, 0.0))
        if _dot(t_o, t_o) < 0.5:
            t_o = _any_perp(n)
        b_o = _norm(_cross(n, t_o))
        if _dot(b_o, b_new) < 0.0:
            b_o = _mul(b_o, -1.0)
        extras.append(pack_vec(t_o, ref[3]) + pack_vec(b_o, ref[7]) + pack_vec(n_raw, ref[11]))
        if colors is not None:
            tcol = tail.colors[near.query(p)]
            if hit is not None:
                (ta, tb, tc), bary = hit
                tcol = bytes(
                    max(0, min(255, int(round(sum(tail.colors[vid][ch] * w for vid, w in zip((ta, tb, tc), bary))))))
                    for ch in range(4)
                )
            c = obj.colors[corner[0]] if 0 <= corner[0] < len(obj.colors) else None
            if c is not None:
                tcol = bytes((_clamp_byte(c[2]), _clamp_byte(c[1]), _clamp_byte(c[0]), tcol[3]))
            colors.append(tcol)

    # wireframe (editor only) keeps the original lines
    new_tail = MeshTail(tail.wire, tail.wire_rev, tail.unknown, colors, tail.color_rev, sections, tail.rest)
    out = StaticMeshData(
        mesh.prefix, verts, uvs, extras, mesh.vrev, indices, mesh.irev, encode_mesh_tail(new_tail), new_tail
    )
    box, sphere = _bounds(out.verts)
    out.prefix = mesh.prefix[:-41] + box + sphere
    return encode_static_mesh(out)


def _clamp_byte(x: float) -> int:
    return max(0, min(255, int(round(x * 255.0))))


def _recook_v2(mesh: StaticMeshData, raw_blob: bytes, obj_text: str) -> bytes:
    obj = parse_obj_full(obj_text)
    if not obj.positions or not obj.triangles:
        raise ValueError("OBJ has no geometry")
    # Fast path: untouched export (same corners in the same order) -> original bytes.
    nv = len(mesh.verts)
    sec_tris = (
        [section_triangles(mesh.indices, sec) for sec in mesh.parsed_tail.sections]
        if mesh.parsed_tail is not None
        else None
    )
    n_expected = sum(len(t) for t in sec_tris) if sec_tris is not None else -1
    same_layout = (
        len(obj.positions) == nv
        and len(obj.texcoords) == nv
        and len(obj.triangles) == n_expected
        and all(c[0] == c[1] == c[2] for _g, tri in obj.triangles for c in tri)
    )
    if same_layout and mesh.parsed_tail is not None:
        flat = [c[0] for _g, tri in obj.triangles for c in tri]
        tail = mesh.parsed_tail
        groups_ok = True
        pos = 0
        for k, tris_k in enumerate(sec_tris):
            for _ in range(len(tris_k)):
                if obj.triangles[pos][0] not in (k,):
                    groups_ok = False
                pos += 1
        expected = [i for tris_k in sec_tris for t in tris_k for i in t]
        packed_uvs = [uv_obj_to_raw(*t) for t in obj.texcoords]
        if groups_ok and flat == expected and packed_uvs == mesh.uvs:
            normals_same = len(obj.normals) == nv and all(
                pack_vec(n)[:3] == mesh.extras[i][8:11] for i, n in enumerate(obj.normals)
            )
            colors_same = tail.colors is None or all(
                c is None or bytes((_clamp_byte(c[2]), _clamp_byte(c[1]), _clamp_byte(c[0]))) == tail.colors[i][:3]
                for i, c in enumerate(obj.colors)
            )
            if normals_same and colors_same:
                if _positions_close(obj.positions, mesh.verts):
                    return raw_blob
                # position-only edit: rebuilt append-only like any other edit so the
                # collision / physics data keeps its original vertices
                return _rebuild_v2(mesh, raw_blob, obj)
    return _rebuild_v2(mesh, raw_blob, obj)


def recook_static_mesh(names: Sequence[object], raw_blob: bytes, obj_text: str) -> bytes:
    """Rebuild a StaticMesh serial blob from the original bytes plus an edited OBJ.

    OBJ v2 (written by this version): any topology. Sections come from the
    `g section_N` groups, tangent frames are recomputed for new vertices
    (authored ones are kept for vertices that still match the template),
    wireframe and section ranges are regenerated. Collision is kept.

    Legacy OBJ v1 (no marker): matching vertex/index counts patch positions
    and UVs in place; other topology changes are only allowed for
    single-section meshes.

    Args:
        names (Sequence[object]): Package name table.
        raw_blob (bytes): Original serial blob (the `.bin` template).
        obj_text (str): Wavefront OBJ contents.

    Returns:
        bytes: Recooked serial data.
    """
    mesh = parse_static_mesh(names, raw_blob)
    if is_obj_v2(obj_text):
        return _recook_v2(mesh, raw_blob, obj_text)

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
        box, sphere = _bounds(mesh.verts)
        mesh.prefix = mesh.prefix[:-41] + box + sphere
        return encode_static_mesh(mesh)
    # legacy topology change: convert to the v2 path (single-section meshes only)
    if mesh.parsed_tail is None or len(mesh.parsed_tail.sections) != 1:
        raise ValueError(
            "legacy OBJ (no '# scct-obj 2' marker) changes topology of a multi-section StaticMesh; "
            "re-unpack with this version and edit the v2 OBJ instead"
        )
    tris = []
    for i in range(0, len(indices) - 2, 3):
        tris.append((0, tuple((indices[i + k], indices[i + k], -1) for k in range(3))))
    obj = ObjData(
        list(positions),
        [None] * len(positions),
        [uv_raw_to_obj(*_float_to_uv(u, v)) for u, v in uvs_f],
        [],
        tris,
        [0],
    )
    obj.triangles = [(0, tuple((vi, vi, -1) for vi, _t, _n in tri)) for _g, tri in tris]
    return _rebuild_v2(mesh, raw_blob, obj)
