#!/usr/bin/env python3

"""
scct.level
~~~~~~~~~~
Keep a level (.scl) consistent with rebuilt static meshes.

Every StaticMeshActor in a level owns a StaticMeshInstance export. Most are empty, but some carry
baked per-vertex lighting (TArray<FColor> + revision) whose length must equal the vertex count of
the StaticMesh in the USX. When a mesh gets a different vertex count (any OBJ topology change),
those instances no longer match and the level crashes while loading or the mesh renders wrong.

`sync_level` patches such instances in place (file size, export order and the Havok snapshot behind
the export table stay untouched): all-zero colour streams are replaced by the standard empty
instance used by most actors (the engine then lights the mesh dynamically). Streams with non-zero
colours cannot be fixed in place and are reported.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

from scct.common import read_index, write_index
from scct.mesh import parse_static_mesh
from scct.package import parse_package


@dataclass
class LevelSync:
    """Result of `sync_level`."""

    data: bytes
    emptied: list[tuple[str, str, int, int]] = field(default_factory=list)
    unfixable: list[tuple[str, str, int, int]] = field(default_factory=list)
    consistent: int = 0

    @property
    def changed(self) -> bool:
        return bool(self.emptied)


def mesh_vertex_counts(usx_files: list[Path]) -> dict[str, int]:
    """Vertex count of every StaticMesh in the given USX packages.

    Args:
        usx_files (list[Path]): Packed USX files.

    Returns:
        dict[str, int]: StaticMesh object name -> number of vertices.
    """
    counts: dict[str, int] = {}
    for usx in usx_files:
        data = Path(usx).read_bytes()
        pkg = parse_package(data)
        for exp in pkg.exports:
            if exp.class_name == "StaticMesh" and exp.serial_size:
                mesh = parse_static_mesh(pkg.names, data[exp.serial_offset : exp.serial_offset + exp.serial_size])
                counts[exp.object_name] = len(mesh.verts)
    return counts


def _encode_fixed(value: int, length: int) -> bytes:
    """Compact index padded to `length` bytes (non-minimal but valid: extra zero groups)."""
    enc = bytearray(write_index(value))
    if len(enc) > length:
        raise ValueError("value does not fit")
    while len(enc) < length:
        if len(enc) == 1:
            enc[0] |= 0x40
        else:
            enc[-1] |= 0x80
        enc.append(0)
    return bytes(enc)


def sync_level(level_data: bytes, nverts: dict[str, int]) -> LevelSync:
    """Empty the baked colour streams that no longer match the meshes' vertex counts.

    Args:
        level_data (bytes): The .scl file.
        nverts (dict[str, int]): StaticMesh name -> vertex count (see `mesh_vertex_counts`).

    Returns:
        LevelSync: Patched level bytes and a report.
    """
    data = bytearray(level_data)
    lvl = parse_package(bytes(data))
    names = {n.text: i for i, n in enumerate(lvl.names)}

    def blob(exp) -> bytes:
        return bytes(data[exp.serial_offset : exp.serial_offset + exp.serial_size])

    def object_props(b: bytes, prop: str) -> list[int]:
        if prop not in names:
            return []
        pat = write_index(names[prop])
        out, k = [], 0
        while True:
            k = b.find(pat, k)
            if k < 0:
                return out
            q = k + len(pat)
            if q < len(b) and (b[q] & 0x0F) == 5:  # object property
                try:
                    out.append(read_index(b, q + 1)[0])
                except IndexError:
                    pass
            k += 1

    # export-table position / length of every export's serial_size field
    size_pos: list[tuple[int, int]] = []
    off = struct.unpack_from("<I", data, 28 if bytes(data[40:44]) == b"\xde\xad\xf0\x0f" else 24)[0]
    for _ in lvl.exports:
        _v, off = read_index(data, off)
        _v, off = read_index(data, off)
        off += 4
        _v, off = read_index(data, off)
        off += 4
        p0 = off
        _v, off = read_index(data, off)
        size_pos.append((p0, off - p0))
        _v, off = read_index(data, off)

    res = LevelSync(b"")
    for exp in lvl.exports:
        if not exp.serial_size:
            continue
        b = blob(exp)
        inst_refs = [x for x in object_props(b, "StaticMeshInstance") if 0 < x <= len(lvl.exports)]
        inst_refs = [x for x in inst_refs if lvl.exports[x - 1].class_name == "StaticMeshInstance"]
        if not inst_refs:
            continue
        mesh_refs = [
            x
            for x in object_props(b, "StaticMesh")
            if x < 0 and -x - 1 < len(lvl.imports) and lvl.imports[-x - 1].class_name == "StaticMesh"
        ]
        if not mesh_refs:
            continue
        mesh = lvl.imports[-mesh_refs[0] - 1].object_name
        if mesh not in nverts:
            continue
        idx = inst_refs[0] - 1
        inst = lvl.exports[idx]
        ib = blob(inst)
        n, o = read_index(ib, 1)
        if n == 0 or n == nverts[mesh]:
            res.consistent += 1
            continue
        if any(ib[o : o + 4 * n]):
            res.unfixable.append((exp.object_name, mesh, n, nverts[mesh]))
            continue
        new_blob = b"\x00" + write_index(0) + b"\x00" * 8  # identical to the stock empty instances
        if len(new_blob) > inst.serial_size:
            raise ValueError(f"unexpected StaticMeshInstance layout for {exp.object_name}")
        data[inst.serial_offset : inst.serial_offset + len(new_blob)] = new_blob
        p, length = size_pos[idx]
        data[p : p + length] = _encode_fixed(len(new_blob), length)
        res.emptied.append((exp.object_name, mesh, n, nverts[mesh]))

    chk = parse_package(bytes(data))  # the patched file must parse to the same tables
    if len(chk.exports) != len(lvl.exports) or len(data) != len(level_data):
        raise ValueError("level sync changed the package layout")
    res.data = bytes(data)
    return res


def sync_level_file(level: Path, usx_files: list[Path], out_dir: Path) -> Path | None:
    """Sync one level against packed USX files and write it to `out_dir` when it changed.

    Args:
        level (Path): Source .scl (never overwritten).
        usx_files (list[Path]): Packed USX files with the rebuilt meshes.
        out_dir (Path): Folder for the synced level.

    Returns:
        Path | None: Written level, or None when nothing had to change.

    Raises:
        ValueError: An instance with non-zero baked colours no longer matches its mesh (the level would
            crash; keep that mesh's vertex count or re-bake the level in an editor).
    """
    level = Path(level)
    res = sync_level(level.read_bytes(), mesh_vertex_counts(usx_files))
    for actor, mesh, n, v in res.emptied:
        print(f"  emptied {actor} ({mesh}): {n} baked colours != {v} vertices")
    if res.unfixable:
        lines = "\n".join(f"  {a} ({m}): {n} baked colours != {v} vertices" for a, m, n, v in res.unfixable)
        raise ValueError(f"{level.name}: instances with non-zero baked colours no longer match their mesh:\n{lines}")
    print(f"sync {level.name}: {len(res.emptied)} instances emptied, {res.consistent} already consistent")
    if not res.changed:
        return None
    dest = Path(out_dir) / level.name
    if dest.resolve() == level.resolve():
        raise ValueError(f"refusing to overwrite the source level {level}; use --level-out with another folder")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(res.data)
    print(f"  -> {dest}")
    return dest
