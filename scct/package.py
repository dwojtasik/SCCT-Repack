#!/usr/bin/env python3

"""
package
~~~~~~~
Read and write Unreal Engine 2.5 packages (UTX / USX / UAX / UKX).
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from scct.common import (
    EXPORTS_DIR,
    MANIFEST_NAME,
    PACKAGE_HEADER_NAME,
    PACKAGE_IMPORTS_NAME,
    PACKAGE_TAG,
    dump_json,
    kind_from_path,
    load_json,
    read_index,
    sanitize_name,
    write_index,
)
from scct.anim import parse_mesh_animation, recook_mesh_animation
from scct.mesh import parse_static_mesh, recook_static_mesh, write_obj
from scct.sound import decode_sound, recook_sound
from scct.texture import decode_texture_png, parse_palette, recook_texture

# Heritage marker that precedes the XOR-obfuscated build fingerprint.
_HERITAGE = b"\xDE\xAD\xF0\x0F"


@dataclass
class UName:
    """One entry in the package name table."""

    text: str
    flags: int


@dataclass
class UImport:
    """One import-table entry."""

    class_package: str
    class_name: str
    package: int
    object_name: str


@dataclass
class UExport:
    """One export-table entry."""

    class_ref: int
    super_ref: int
    package_ref: int
    object_name: str
    name_index: int
    flags: int
    serial_size: int
    serial_offset: int
    class_name: str


@dataclass
class UPackage:
    """Parsed Unreal package (header + tables + raw file bytes)."""

    data: bytes
    version: int
    licensee: int
    flags: int
    names: list[UName]
    imports: list[UImport]
    exports: list[UExport]
    import_offset: int
    export_offset: int
    header_end: int
    cooked: bool


def peek_package(path: Path) -> bool:
    """Return True if `path` starts with the Unreal package tag."""
    with path.open("rb") as fh:
        head = fh.read(4)
    return len(head) == 4 and struct.unpack("<I", head)[0] == PACKAGE_TAG


def package_index_layout(data: bytes) -> dict[str, int | bool]:
    """Locate name/import/export tables.

    Cooked SCX inserts a heritage count at offset 12 (DEADF00F at 40).
    Editor packages use the stock UE2 table (DEADF00F at 36).
    """
    cooked = data[40:44] == _HERITAGE
    base = 16 if cooked else 12
    name_count, name_off, exp_count, exp_off, imp_count, imp_off = struct.unpack_from(
        "<IIIIII", data, base
    )
    return {
        "cooked": cooked,
        "name_count": name_count,
        "name_off": name_off,
        "exp_count": exp_count,
        "exp_off": exp_off,
        "imp_count": imp_count,
        "imp_off": imp_off,
        "exp_off_field": base + 12,
        "imp_off_field": base + 20,
    }


def _read_names(data: bytes, offset: int, count: int) -> list[UName]:
    names: list[UName] = []
    for _ in range(count):
        nlen, offset = read_index(data, offset)
        raw = data[offset : offset + nlen]
        offset += nlen
        if raw.endswith(b"\x00"):
            text = raw[:-1].decode("latin-1", "replace")
        else:
            text = raw.decode("latin-1", "replace")
            if offset < len(data) and data[offset] == 0:
                offset += 1
        flags = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        names.append(UName(text, flags))
    return names


def _name_at(names: list[UName], index: int) -> str:
    if 0 <= index < len(names):
        return names[index].text
    return f"?{index}"


def resolve_obj_name(pkg: UPackage, ref: int) -> str:
    """Resolve an object reference to a name (0=None, >0 export, <0 import)."""
    if ref == 0:
        return "None"
    if ref > 0:
        idx = ref - 1
        if 0 <= idx < len(pkg.exports):
            return pkg.exports[idx].object_name
        return f"Export[{ref}]"
    idx = -ref - 1
    if 0 <= idx < len(pkg.imports):
        return pkg.imports[idx].object_name
    return f"Import[{ref}]"


def parse_package(data: bytes) -> UPackage:
    """Parse names, imports, and exports from an Unreal package.

    Args:
        data (bytes): Entire package file.

    Returns:
        UPackage: Parsed tables plus the original bytes.
    """
    if len(data) < 4 or struct.unpack_from("<I", data, 0)[0] != PACKAGE_TAG:
        raise ValueError("not an Unreal package (missing 0x9E2A83C1 tag)")

    version, licensee = struct.unpack_from("<HH", data, 4)
    flags = struct.unpack_from("<I", data, 8)[0]
    layout = package_index_layout(data)
    names = _read_names(data, int(layout["name_off"]), int(layout["name_count"]))

    imports: list[UImport] = []
    offset = int(layout["imp_off"])
    for _ in range(int(layout["imp_count"])):
        class_pkg, offset = read_index(data, offset)
        class_name, offset = read_index(data, offset)
        pkg_ref = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        obj_name, offset = read_index(data, offset)
        imports.append(
            UImport(
                _name_at(names, class_pkg),
                _name_at(names, class_name),
                pkg_ref,
                _name_at(names, obj_name),
            )
        )

    exports: list[UExport] = []
    offset = int(layout["exp_off"])
    for _ in range(int(layout["exp_count"])):
        class_ref, offset = read_index(data, offset)
        super_ref, offset = read_index(data, offset)
        pkg_ref = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        name_i, offset = read_index(data, offset)
        eflags = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        serial_size, offset = read_index(data, offset)
        serial_offset, offset = read_index(data, offset)
        exports.append(
            UExport(
                class_ref,
                super_ref,
                pkg_ref,
                _name_at(names, name_i),
                name_i,
                eflags,
                serial_size,
                serial_offset,
                "",
            )
        )

    sized = [e.serial_offset for e in exports if e.serial_size]
    header_end = min(sized) if sized else len(data)
    pkg = UPackage(
        data=data,
        version=version,
        licensee=licensee,
        flags=flags,
        names=names,
        imports=imports,
        exports=exports,
        import_offset=int(layout["imp_off"]),
        export_offset=int(layout["exp_off"]),
        header_end=header_end,
        cooked=bool(layout["cooked"]),
    )
    for exp in pkg.exports:
        exp.class_name = resolve_obj_name(pkg, exp.class_ref)
    return pkg


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _unchanged_raw(src_dir: Path, item: dict, path: Path) -> bytes | None:
    """Return the original serial blob when the decoded export was not edited."""
    raw_name = item.get("raw_file")
    expected = item.get("content_sha256")
    if not raw_name or not expected:
        return None
    raw_path = src_dir / str(raw_name)
    if not raw_path.is_file():
        return None
    if _sha256_file(path) != expected:
        return None
    return raw_path.read_bytes()


def extract_package(src: Path, dest: Path) -> int:
    """Extract exports: Texture as PNG, Sound as JSON (or WAV), MeshAnimation as JSON, StaticMesh as OBJ, else `.bin`.

    Args:
        src (Path): Path to `.utx` / `.usx` / `.uax` / `.ukx`.
        dest (Path): Output directory.

    Returns:
        int: Number of extracted exports.
    """
    pkg = parse_package(src.read_bytes())
    dest.mkdir(parents=True, exist_ok=True)
    (dest / EXPORTS_DIR).mkdir(exist_ok=True)
    (dest / PACKAGE_HEADER_NAME).write_bytes(pkg.data[: pkg.header_end])
    (dest / PACKAGE_IMPORTS_NAME).write_bytes(pkg.data[pkg.import_offset : pkg.export_offset])

    palettes: dict[int, list] = {}
    for i, exp in enumerate(pkg.exports):
        if exp.class_name != "Palette":
            continue
        blob = pkg.data[exp.serial_offset : exp.serial_offset + exp.serial_size]
        try:
            palettes[i] = parse_palette(pkg.names, blob)
        except (ValueError, IndexError, struct.error):
            pass

    export_rows: list[dict] = []
    for i, exp in enumerate(pkg.exports):
        blob = pkg.data[exp.serial_offset : exp.serial_offset + exp.serial_size]
        stem = f"{EXPORTS_DIR}/{i:04d}_{sanitize_name(exp.class_name)}_{sanitize_name(exp.object_name)}"
        row: dict = {
            "class_ref": exp.class_ref,
            "super_ref": exp.super_ref,
            "package_ref": exp.package_ref,
            "name_index": exp.name_index,
            "object_name": exp.object_name,
            "class_name": exp.class_name,
            "flags": exp.flags,
        }
        if exp.class_name == "Texture":
            try:
                decoded = decode_texture_png(pkg.names, blob, palettes)
            except (ValueError, IndexError, struct.error):
                decoded = None
            if decoded is not None:
                img, fmt, prefix = decoded
                rel = f"{stem}.png"
                img.save(dest / rel)
                raw_rel = f"{stem}.bin"
                (dest / raw_rel).write_bytes(blob)
                row["file"] = rel.replace("\\", "/")
                row["raw_file"] = raw_rel.replace("\\", "/")
                row["content_sha256"] = _sha256_file(dest / rel)
                row["tex_format"] = fmt
                row["props_hex"] = prefix.hex()
                export_rows.append(row)
                continue
        if exp.class_name == "Sound":
            try:
                cue = decode_sound(pkg.names, blob)
            except (ValueError, IndexError, struct.error, KeyError):
                cue = None
            if cue is not None:
                if cue.get("kind") == "wav" and isinstance(cue.get("data"), (bytes, bytearray)):
                    sample = bytes(cue.pop("data"))
                    ext = "wav"
                    if sample.startswith(b"OggS"):
                        ext = "ogg"
                    elif not sample.startswith(b"RIFF"):
                        ext = "bin"
                    rel = f"{stem}.{ext}"
                    (dest / rel).write_bytes(sample)
                    json_rel = f"{stem}.json"
                    dump_json(dest / json_rel, cue)
                    raw_rel = f"{stem}.bin"
                    (dest / raw_rel).write_bytes(blob)
                    row["file"] = rel.replace("\\", "/")
                    row["sound_json"] = json_rel.replace("\\", "/")
                    row["raw_file"] = raw_rel.replace("\\", "/")
                    row["content_sha256"] = _sha256_file(dest / rel)
                    export_rows.append(row)
                    continue
                cue.pop("data", None)
                rel = f"{stem}.json"
                dump_json(dest / rel, cue)
                raw_rel = f"{stem}.bin"
                (dest / raw_rel).write_bytes(blob)
                row["file"] = rel.replace("\\", "/")
                row["raw_file"] = raw_rel.replace("\\", "/")
                row["content_sha256"] = _sha256_file(dest / rel)
                export_rows.append(row)
                continue
        if exp.class_name == "MeshAnimation":
            try:
                anim = parse_mesh_animation(pkg.names, blob)
            except (ValueError, IndexError, struct.error, KeyError):
                anim = None
            if anim is not None:
                rel = f"{stem}.json"
                dump_json(dest / rel, anim)
                raw_rel = f"{stem}.bin"
                (dest / raw_rel).write_bytes(blob)
                row["file"] = rel.replace("\\", "/")
                row["raw_file"] = raw_rel.replace("\\", "/")
                row["content_sha256"] = _sha256_file(dest / rel)
                export_rows.append(row)
                continue
        if exp.class_name == "StaticMesh":
            try:
                mesh = parse_static_mesh(pkg.names, blob)
            except (ValueError, IndexError, struct.error):
                mesh = None
            if mesh is not None:
                rel = f"{stem}.obj"
                raw_rel = f"{stem}.bin"
                (dest / rel).write_text(write_obj(mesh, exp.object_name), encoding="utf-8")
                (dest / raw_rel).write_bytes(blob)
                row["file"] = rel.replace("\\", "/")
                row["raw_file"] = raw_rel.replace("\\", "/")
                row["content_sha256"] = _sha256_file(dest / rel)
                export_rows.append(row)
                continue
        rel = f"{stem}.bin"
        (dest / rel).write_bytes(blob)
        row["file"] = rel.replace("\\", "/")
        export_rows.append(row)

    dump_json(
        dest / MANIFEST_NAME,
        {
            "format": kind_from_path(src) or "package",
            "version": 1,
            "package_version": pkg.version,
            "licensee": pkg.licensee,
            "flags": pkg.flags,
            "cooked": pkg.cooked,
            "exports": export_rows,
        },
    )
    return len(pkg.exports)


def _read_array_index(blob: bytes, offset: int) -> tuple[int, int]:
    b = blob[offset]
    offset += 1
    if b < 0x80:
        return b, offset
    if b < 0xC0:
        return ((b & 0x7F) << 8) | blob[offset], offset + 1
    return struct.unpack_from("<I", blob, offset - 1)[0], offset + 3


def _skip_props(blob: bytes, names: list[UName]) -> int:
    """Return the byte offset just after the terminating None property."""
    offset = 0
    while offset < len(blob):
        name_i, offset = read_index(blob, offset)
        if name_i < 0 or name_i >= len(names):
            raise ValueError(f"bad property name index {name_i}")
        if names[name_i].text == "None":
            return offset
        info = blob[offset]
        offset += 1
        typ = info & 0x0F
        size_code = (info >> 4) & 7
        is_array = bool(info & 0x80)
        if size_code == 0:
            size = 1
        elif size_code == 1:
            size = 2
        elif size_code == 2:
            size = 4
        elif size_code == 3:
            size = 12
        elif size_code == 4:
            size = 16
        elif size_code == 5:
            size = blob[offset]
            offset += 1
        elif size_code == 6:
            size = struct.unpack_from("<H", blob, offset)[0]
            offset += 2
        else:
            size = struct.unpack_from("<I", blob, offset)[0]
            offset += 4
        if typ == 3:
            continue
        if is_array:
            _arr, offset = _read_array_index(blob, offset)
        if typ == 10:
            _si, offset = read_index(blob, offset)
        offset += size
    return offset


def relocate_mip_skips(blob: bytes, names: list[UName], serial_off: int) -> bytes:
    """Rewrite TLazyArray skip offsets so they match a new serial position.

    Texture mip data is stored as TLazyArray: a uint32 absolute file offset
    that the engine seeks to. Copying a blob after earlier exports changed
    size leaves those seeks pointing at the wrong bytes.

    Args:
        blob (bytes): Export serial data.
        names (list[UName]): Package name table (for property parsing).
        serial_off (int): New file offset of this export's serial data.

    Returns:
        bytes: Blob with skip fields updated, or the original blob on failure.
    """
    try:
        prop_end = _skip_props(blob, names)
    except (ValueError, IndexError, struct.error):
        return blob
    if prop_end >= len(blob):
        return blob
    out = bytearray(blob)
    try:
        count, offset = read_index(blob, prop_end)
        if count < 0 or count > 32:
            return blob
        for _ in range(count):
            if offset + 4 > len(blob):
                return blob
            skip_field = offset
            offset += 4
            nbytes, offset = read_index(blob, offset)
            idx_len = offset - (skip_field + 4)
            skip = serial_off + skip_field + 4 + idx_len + nbytes
            struct.pack_into("<I", out, skip_field, skip)
            offset += nbytes + 10
            if offset > len(blob):
                return blob
    except (ValueError, IndexError, struct.error):
        return blob
    return bytes(out)


def _write_export_entry(item: dict, serial_size: int, serial_offset: int) -> bytes:
    out = bytearray()
    out += write_index(int(item["class_ref"]))
    out += write_index(int(item["super_ref"]))
    out += struct.pack("<i", int(item["package_ref"]))
    out += write_index(int(item["name_index"]))
    out += struct.pack("<I", int(item["flags"]))
    out += write_index(serial_size)
    out += write_index(serial_offset)
    return bytes(out)


def pack_package(src_dir: Path, dest: Path) -> int:
    """Rebuild an Unreal package from an unpack.py directory.

    Header, name table, and import table are preserved. PNG Texture, JSON/WAV
    Sound, JSON MeshAnimation, and OBJ StaticMesh exports are recooked; other
    files are copied as serial blobs. Texture mip skip offsets are rewritten to
    match the new serial positions.

    Args:
        src_dir (Path): Directory that contains `_scct.json` and export bins.
        dest (Path): Output package path.

    Returns:
        int: Number of packed exports.
    """
    manifest = load_json(src_dir / MANIFEST_NAME)
    if str(manifest.get("format", "")).lower() == "umd":
        raise ValueError(f"{src_dir} is a UMD unpack directory, not an Unreal package")

    header_path = src_dir / PACKAGE_HEADER_NAME
    imports_path = src_dir / PACKAGE_IMPORTS_NAME
    if not header_path.is_file() or not imports_path.is_file():
        raise FileNotFoundError(f"missing {PACKAGE_HEADER_NAME} or {PACKAGE_IMPORTS_NAME} in {src_dir}")

    prefix = bytearray(header_path.read_bytes())
    import_bytes = imports_path.read_bytes()
    layout = package_index_layout(bytes(prefix))
    if int(layout["imp_off"]) > int(layout["exp_off"]):
        raise ValueError("unsupported package layout (imports after exports)")
    names = _read_names(bytes(prefix), int(layout["name_off"]), int(layout["name_count"]))

    first = len(prefix)
    blobs: list[bytes] = []
    serial: list[tuple[dict, int, int]] = []
    cursor = first
    for item in manifest["exports"]:
        path = src_dir / item["file"]
        suffix = path.suffix.lower()
        kept = _unchanged_raw(src_dir, item, path)
        if kept is not None:
            raw = kept
            if item.get("class_name") == "Texture":
                raw = relocate_mip_skips(raw, names, cursor)
        elif suffix == ".png":
            raw = recook_texture(
                names,
                path,
                str(item["props_hex"]),
                int(item["tex_format"]),
                cursor,
            )
        elif item.get("class_name") == "Sound" and suffix in {".wav", ".ogg", ".bin"} and item.get("sound_json"):
            meta = json.loads((src_dir / str(item["sound_json"])).read_text(encoding="utf-8"))
            meta["data"] = path.read_bytes()
            raw = recook_sound(names, meta, cursor)
        elif suffix == ".json" and item.get("class_name") == "Sound":
            raw = recook_sound(names, json.loads(path.read_text(encoding="utf-8")), cursor)
        elif suffix == ".json" and item.get("class_name") == "MeshAnimation":
            raw = recook_mesh_animation(names, json.loads(path.read_text(encoding="utf-8")))
        elif suffix == ".obj" and item.get("class_name") == "StaticMesh":
            raw_name = item.get("raw_file")
            if not raw_name:
                raise ValueError(f"StaticMesh {item.get('object_name')} missing raw_file template")
            raw_path = src_dir / str(raw_name)
            if not raw_path.is_file():
                raise FileNotFoundError(f"missing StaticMesh template {raw_path}")
            raw = recook_static_mesh(names, raw_path.read_bytes(), path.read_text(encoding="utf-8"))
        else:
            raw = path.read_bytes()
            if item.get("class_name") == "Texture":
                raw = relocate_mip_skips(raw, names, cursor)
        blobs.append(raw)
        serial.append((item, len(raw), cursor))
        cursor += len(raw)

    imp_off = cursor
    exp_off = imp_off + len(import_bytes)
    struct.pack_into("<I", prefix, int(layout["imp_off_field"]), imp_off)
    struct.pack_into("<I", prefix, int(layout["exp_off_field"]), exp_off)

    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        fh.write(prefix)
        for blob in blobs:
            fh.write(blob)
        fh.write(import_bytes)
        for item, ssize, soff in serial:
            fh.write(_write_export_entry(item, ssize, soff))

    chk = parse_package(dest.read_bytes())
    if len(chk.exports) != len(manifest["exports"]) or len(chk.imports) != int(layout["imp_count"]):
        raise RuntimeError(
            f"pack parse mismatch: exports {len(chk.exports)}/{len(manifest['exports'])} "
            f"imports {len(chk.imports)}/{layout['imp_count']}"
        )
    return len(blobs)
