#!/usr/bin/env python3

"""
scct.level
~~~~~~~~~~
Keep a level (.scl) consistent with rebuilt static meshes.

Every StaticMeshActor in a level owns a StaticMeshInstance export. Most are empty, but some carry a
per-vertex colour stream (TArray<FColor> + revision) whose length must equal the vertex count of the
StaticMesh in the USX. When a mesh gets a different vertex count (any OBJ topology change), those
instances no longer match and the level crashes while loading.

An instance with a stream is not the same as an empty one: the engine relights an actor whose instance
is empty from the level's static lights when the level loads (coloured lights then show as rainbow
gradients on meshes that were never meant to get them). `sync_level` therefore keeps the stream and
gives it the new vertex count: an all-zero stream stays all zero (the stock content of nearly every
such instance), with the stock revision.

The resized instances are placed right after the export data (where the cooker puts exports), and the
import table, export table and Havok snapshot follow in their original order, moved back by the inserted
size (summary offsets updated). No other export moves.
Keeping the tables in place and moving the import table behind the Havok data instead crashed on load.

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
    emptied: list[tuple[str, str, int, int]] = field(default_factory=list)  # resized instances
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


def _encode_fixed(value: int, length: int) -> bytes | None:
    """Compact index padded to `length` bytes (non-minimal but valid), None when it needs more."""
    enc = bytearray(write_index(value))
    if len(enc) > length:
        return None
    while len(enc) < length:
        if len(enc) == 1:
            enc[0] |= 0x40
        else:
            enc[-1] |= 0x80
        enc.append(0)
    return bytes(enc)


def _summary_offsets(data: bytes) -> tuple[int, int]:
    """Positions of the export-table and import-table offset fields in the package summary."""
    pos = 28 if bytes(data[40:44]) == b"\xde\xad\xf0\x0f" else 24
    return pos, pos + 8


def _export_entries(data: bytes, table: int, count: int) -> list[tuple[int, int, int, int, int]]:
    """(entry start, size field start, size field length, offset field length, entry end) per export."""
    out = []
    off = table
    for _ in range(count):
        start = off
        _v, off = read_index(data, off)
        _v, off = read_index(data, off)
        off += 4
        _v, off = read_index(data, off)
        off += 4
        p0 = off
        _size, off = read_index(data, off)
        p1 = off
        _v, off = read_index(data, off)  # SCCT stores the offset for every export
        out.append((start, p0, p1 - p0, off - p1, off))
    return out


def sync_level(level_data: bytes, nverts: dict[str, int], layout: str = "insert") -> LevelSync:
    """Give every StaticMeshInstance colour stream the vertex count of its (rebuilt) mesh.

    Args:
        level_data (bytes): The .scl file.
        nverts (dict[str, int]): StaticMesh name -> vertex count (see `mesh_vertex_counts`).
        layout (str): "insert" (default): the resized instances go right after the export data and the import
            table, export table and Havok data move back by that size (the cooker's own order; tested in game:
            02_CargoShip loads and has no relit / rainbow actors). "empty": in place, mismatched all-zero streams
            are emptied (loads, but the engine relights those actors from the level's coloured lights).
            "append": experimental (resized instances after the end of the file, tables untouched).
            A layout that moved the import table behind the Havok data crashed while loading.

    Returns:
        LevelSync: Patched level bytes and a report (`emptied` lists the resized instances).
    """
    data = bytes(level_data)
    lvl = parse_package(data)
    names = {n.text: i for i, n in enumerate(lvl.names)}

    def blob(exp) -> bytes:
        return data[exp.serial_offset : exp.serial_offset + exp.serial_size]

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

    res = LevelSync(b"")
    new_blobs: dict[int, bytes] = {}
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
        if idx in new_blobs:
            continue
        ib = blob(lvl.exports[idx])
        n, o = read_index(ib, 1)
        want = nverts[mesh]
        if n == 0 or n == want:
            res.consistent += 1
            continue
        if any(ib[o : o + 4 * n]):
            res.unfixable.append((exp.object_name, mesh, n, want))
            continue
        tail = ib[o + 4 * n :]
        new_blobs[idx] = ib[:1] + write_index(want) + bytes(4 * want) + tail
        res.emptied.append((exp.object_name, mesh, n, want))

    if not new_blobs:
        res.data = data
        return res
    layouts = {"empty": _layout_empty, "insert": _layout_insert, "append": _layout_append}
    out, placed = layouts[layout](data, lvl, new_blobs, res)
    chk = parse_package(bytes(out))  # the patched file must parse to the same tables
    if len(chk.exports) != len(lvl.exports) or len(chk.imports) != len(lvl.imports):
        raise ValueError("level sync changed the package tables")
    for i, (ea, eb) in enumerate(zip(lvl.exports, chk.exports)):
        if (ea.object_name, ea.class_name) != (eb.object_name, eb.class_name):
            raise ValueError(f"level sync: export {i} changed identity")
        if i in placed:
            if (eb.serial_offset, eb.serial_size) != placed[i]:
                raise ValueError(f"level sync: export {i} did not land where expected")
        elif data[ea.serial_offset : ea.serial_offset + ea.serial_size] != out[eb.serial_offset : eb.serial_offset + eb.serial_size]:
            raise ValueError(f"level sync: export {i} changed")
    res.data = bytes(out)
    return res


def _rebuild_table(data: bytes, entries, placed: dict[int, tuple[int, int]], pad: bool) -> bytes:
    parts = []
    for i, (start, p0, size_len, off_len, end) in enumerate(entries):
        if i not in placed:
            parts.append(data[start:end])
            continue
        off_new, size_new = placed[i]
        s_enc = (_encode_fixed(size_new, size_len) if pad else None) or write_index(size_new)
        o_enc = (_encode_fixed(off_new, off_len) if pad else None) or write_index(off_new)
        parts.append(data[start:p0] + s_enc + o_enc)
    return b"".join(parts)


def _layout_insert(data: bytes, lvl, new_blobs: dict[int, bytes], res: LevelSync):
    """The cooker's own order: export data (+ the grown instances right after it), import table, export table,
    Havok snapshot. Tables and Havok move back by the inserted size; no export data moves."""
    eo_pos, io_pos = _summary_offsets(data)
    table = struct.unpack_from("<I", data, eo_pos)[0]
    imports = struct.unpack_from("<I", data, io_pos)[0]
    entries = _export_entries(data, table, len(lvl.exports))
    table_end = entries[-1][4]
    data_end = max(e.serial_offset + e.serial_size for e in lvl.exports if e.serial_size)
    if not (data_end <= imports < table < table_end):
        raise ValueError("unexpected level layout (export data, import table, export table)")
    out = bytearray(data[:imports])
    placed: dict[int, tuple[int, int]] = {}
    for idx in sorted(new_blobs):
        placed[idx] = (len(out), len(new_blobs[idx]))
        out += new_blobs[idx]
    new_imports = len(out)
    out += data[imports:table]
    new_table = len(out)
    out += _rebuild_table(data, entries, placed, pad=False)
    out += data[table_end:]
    struct.pack_into("<I", out, eo_pos, new_table)
    struct.pack_into("<I", out, io_pos, new_imports)
    return out, placed


def _layout_empty(data: bytes, lvl, new_blobs: dict[int, bytes], res: LevelSync):
    """In place: every mismatched instance becomes the stock empty instance (10 bytes, padded size field)."""
    eo_pos, _io_pos = _summary_offsets(data)
    table = struct.unpack_from("<I", data, eo_pos)[0]
    entries = _export_entries(data, table, len(lvl.exports))
    out = bytearray(data)
    placed: dict[int, tuple[int, int]] = {}
    empty = b"\x00" + write_index(0) + b"\x00" * 8  # identical to the stock empty instances
    for idx in sorted(new_blobs):
        e = lvl.exports[idx]
        _start, p0, size_len, _off_len, _end = entries[idx]
        out[e.serial_offset : e.serial_offset + len(empty)] = empty
        out[p0 : p0 + size_len] = _encode_fixed(len(empty), size_len)
        placed[idx] = (e.serial_offset, len(empty))
    return out, placed


def _layout_append(data: bytes, lvl, new_blobs: dict[int, bytes], res: LevelSync):
    """Tables and Havok stay byte for byte where they are; the grown instances go after the end of the file.
    An instance whose export-table size field is too short for its new size is emptied instead."""
    eo_pos, _io_pos = _summary_offsets(data)
    table = struct.unpack_from("<I", data, eo_pos)[0]
    entries = _export_entries(data, table, len(lvl.exports))
    out = bytearray(data)
    placed: dict[int, tuple[int, int]] = {}
    for idx in sorted(new_blobs):
        blob = new_blobs[idx]
        _start, p0, size_len, off_len, _end = entries[idx]
        if _encode_fixed(len(blob), size_len) is None:
            n, _o = read_index(blob, 1)
            blob = b"\x00" + write_index(0) + b"\x00" * 8  # the stock empty instance
        off = len(out)
        if _encode_fixed(off, off_len) is None:
            raise ValueError("export offset field too short")
        placed[idx] = (off, len(blob))
        out += blob
        out[p0 : p0 + size_len + off_len] = _encode_fixed(len(blob), size_len) + _encode_fixed(off, off_len)
    return out, placed


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
        print(f"  resized {actor} ({mesh}): {n} -> {v} instance colours")
    if res.unfixable:
        lines = "\n".join(f"  {a} ({m}): {n} baked colours != {v} vertices" for a, m, n, v in res.unfixable)
        raise ValueError(f"{level.name}: instances with non-zero baked colours no longer match their mesh:\n{lines}")
    print(f"sync {level.name}: {len(res.emptied)} instances resized, {res.consistent} already consistent")
    if not res.changed:
        return None
    dest = Path(out_dir) / level.name
    if dest.resolve() == level.resolve():
        raise ValueError(f"refusing to overwrite the source level {level}; use --level-out with another folder")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(res.data)
    print(f"  -> {dest}")
    return dest
