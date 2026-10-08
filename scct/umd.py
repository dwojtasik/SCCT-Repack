#!/usr/bin/env python3

"""
umd
~~~
Read and write Splinter Cell Chaos Theory UMD archives.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from scct.common import (
    MANIFEST_NAME,
    UMD_GAPS_DIR,
    UMD_MAX_BYTES,
    UMD_PREFIX_NAME,
    copy_stream,
    dump_json,
    load_json,
    read_index,
    write_index,
)

# Trailer: toc_offset, file_size, version, checksum (all uint32).
_TRAILER_FMT = "<IIII"
_TRAILER_SIZE = 16

# TOC entry payload: offset, size, flags (all uint32).
_ENTRY_FMT = "<III"


@dataclass(frozen=True)
class UmdEntry:
    """One file stored in a UMD archive."""

    name: str
    offset: int
    size: int
    flags: int


@dataclass
class UmdArchive:
    """Parsed UMD table of contents plus optional prefix size."""

    entries: list[UmdEntry]
    toc_offset: int
    file_size: int
    version: int
    checksum: int
    prefix_size: int


def peek_umd(path: Path) -> bool:
    """Return True if `path` has a UMD trailer that matches the file size."""
    size = path.stat().st_size
    if size < _TRAILER_SIZE:
        return False
    with path.open("rb") as fh:
        fh.seek(-_TRAILER_SIZE, 2)
        toc_off, file_size, version, _checksum = struct.unpack(_TRAILER_FMT, fh.read(_TRAILER_SIZE))
    return file_size == size and version in (1, 2) and 0 < toc_off < size


def _parse_toc(toc: bytes, file_size: int) -> list[UmdEntry]:
    """Parse TOC bytes: compact-index count, then name + offset/size/flags."""
    count, offset = read_index(toc, 0)
    if count < 0 or count > 1_000_000:
        raise ValueError(f"implausible UMD entry count {count}")
    entries: list[UmdEntry] = []
    for _ in range(count):
        nlen, offset = read_index(toc, offset)
        if nlen < 1 or nlen > 1024:
            raise ValueError(f"bad UMD name length {nlen}")
        raw = toc[offset : offset + nlen]
        offset += nlen
        name = raw.split(b"\x00", 1)[0].decode("latin-1", "replace").replace("/", "\\")
        payload_off, size, flags = struct.unpack_from(_ENTRY_FMT, toc, offset)
        offset += 12
        if not name or payload_off > file_size or payload_off + size > file_size + 1:
            raise ValueError(f"bad UMD entry {name!r} off={payload_off} size={size}")
        entries.append(UmdEntry(name, payload_off, size, flags))
    if offset != len(toc):
        raise ValueError(f"UMD TOC leftover {len(toc) - offset} bytes")
    return entries


def _read_archive(fh: BinaryIO, file_size: int) -> UmdArchive:
    fh.seek(-_TRAILER_SIZE, 2)
    toc_off, trailer_size, version, checksum = struct.unpack(_TRAILER_FMT, fh.read(_TRAILER_SIZE))
    if trailer_size != file_size:
        raise ValueError(f"UMD trailer size {trailer_size} does not match file size {file_size}")
    if not 0 < toc_off < file_size:
        raise ValueError("UMD TOC offset is past the end of the file")

    fh.seek(toc_off)
    toc = fh.read(file_size - _TRAILER_SIZE - toc_off)
    entries = _parse_toc(toc, file_size)
    first = min((e.offset for e in entries), default=0)
    return UmdArchive(entries, toc_off, file_size, version, checksum, first)


def parse_umd(data: bytes) -> UmdArchive:
    """Parse a UMD archive from a fully loaded byte buffer."""
    import io

    return _read_archive(io.BytesIO(data), len(data))


def _disk_path(root: Path, toc_name: str) -> Path:
    """Map a TOC path (backslash) onto a directory tree under `root`."""
    parts = [p for p in toc_name.replace("\\", "/").split("/") if p and p != ".."]
    return root.joinpath(*parts)


def _span_has_data(fh: BinaryIO, nbytes: int, chunk: int = 1 << 20) -> bool:
    """Return True if the next `nbytes` from `fh` contain a non-zero byte."""
    remaining = nbytes
    while remaining:
        block = fh.read(min(chunk, remaining))
        if not block:
            return False
        if any(block):
            return True
        remaining -= len(block)
    return False


def _copy_out(fh: BinaryIO, dest: Path, offset: int, size: int) -> None:
    """Write `size` bytes from `fh` at `offset` to `dest`."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    fh.seek(offset)
    with dest.open("wb") as out_fh:
        copy_stream(fh, out_fh, size)


def _unlisted_spans(archive: UmdArchive) -> list[tuple[int, int]]:
    """Return (offset, size) for bytes between TOC payloads, prefix excluded."""
    spans: list[tuple[int, int]] = []
    cursor = archive.prefix_size
    for entry in sorted(archive.entries, key=lambda item: (item.offset, item.size)):
        if entry.offset > cursor:
            spans.append((cursor, entry.offset - cursor))
        cursor = max(cursor, entry.offset + entry.size)
    if archive.toc_offset > cursor:
        spans.append((cursor, archive.toc_offset - cursor))
    return spans


def extract_umd(src: Path, dest: Path) -> int:
    """Extract every UMD payload and write `_scct.json`.

    Payloads are stored raw so pack.py can roundtrip them. Non-zero bytes that
    sit between TOC entries (the stock archive keeps localization / Magma data
    there) are written under `_scct_gaps/` so an unmodified pack can restore
    the original layout.

    Args:
        src (Path): Path to the `.umd` file.
        dest (Path): Output directory.

    Returns:
        int: Number of extracted files.
    """
    dest.mkdir(parents=True, exist_ok=True)
    file_size = src.stat().st_size
    with src.open("rb") as fh:
        archive = _read_archive(fh, file_size)
        if archive.prefix_size:
            _copy_out(fh, dest / UMD_PREFIX_NAME, 0, archive.prefix_size)

        files: list[dict] = []
        total = len(archive.entries)
        for i, entry in enumerate(archive.entries, 1):
            out = _disk_path(dest, entry.name)
            _copy_out(fh, out, entry.offset, entry.size)
            files.append(
                {
                    "name": entry.name,
                    "flags": entry.flags,
                    "offset": entry.offset,
                    "size": entry.size,
                    "file": str(out.relative_to(dest)).replace("\\", "/"),
                }
            )
            if i % 500 == 0 or i == total:
                print(f"  {i}/{total} files")

        unlisted: list[dict] = []
        for gap_i, (gap_off, gap_size) in enumerate(_unlisted_spans(archive)):
            fh.seek(gap_off)
            if not _span_has_data(fh, gap_size):
                continue
            rel = f"{UMD_GAPS_DIR}/{gap_i:04d}.bin"
            _copy_out(fh, dest / rel, gap_off, gap_size)
            unlisted.append({"offset": gap_off, "file": rel})

    dump_json(
        dest / MANIFEST_NAME,
        {
            "format": "umd",
            "version": 1,
            "umd_version": archive.version,
            "checksum": archive.checksum,
            "toc_offset": archive.toc_offset,
            "prefix_size": archive.prefix_size,
            "files": files,
            "unlisted": unlisted,
        },
    )
    return len(archive.entries)


def _write_toc(entries: list[tuple[str, int, int, int]]) -> bytes:
    """Build TOC bytes from (name, offset, size, flags)."""
    out = bytearray(write_index(len(entries)))
    for name, offset, size, flags in entries:
        raw = name.encode("latin-1") + b"\x00"
        out += write_index(len(raw))
        out += raw
        out += struct.pack(_ENTRY_FMT, offset, size, flags)
    return bytes(out)


def _check_umd_limit(offset: int, size: int) -> None:
    """Raise if a UMD offset or size will not fit in the uint32 TOC."""
    if offset > UMD_MAX_BYTES or size > UMD_MAX_BYTES:
        raise OverflowError(
            "UMD TOC uses 32-bit offsets and sizes "
            f"(limit {UMD_MAX_BYTES} bytes / ~4 GiB). "
            "Patch splintercell3.exe with patch_exe to read archives above 2 GiB."
        )


def _write_umd_trailer(fh: BinaryIO, toc_offset: int, manifest: dict) -> None:
    """Write TOC bytes already flushed at `toc_offset`, then the 16-byte trailer."""
    if toc_offset > UMD_MAX_BYTES:
        raise OverflowError("UMD would exceed the 32-bit TOC size limit (~4 GiB)")
    file_size = fh.tell() + _TRAILER_SIZE
    if file_size > UMD_MAX_BYTES:
        raise OverflowError("UMD would exceed the 32-bit TOC size limit (~4 GiB)")
    fh.write(
        struct.pack(
            _TRAILER_FMT,
            toc_offset,
            file_size,
            int(manifest.get("umd_version", 0)),
            int(manifest.get("checksum", 0)),
        )
    )


def _layout_regions(src_dir: Path, manifest: dict) -> list[tuple[int, Path]] | None:
    """Return (offset, path) regions when every payload still matches the original layout."""
    items = manifest["files"]
    if not items or any("offset" not in item or "size" not in item for item in items):
        return None
    if "toc_offset" not in manifest:
        return None

    prefix_size = int(manifest.get("prefix_size", 0))
    prefix_path = src_dir / UMD_PREFIX_NAME
    if prefix_size:
        if not prefix_path.is_file() or prefix_path.stat().st_size != prefix_size:
            return None
    elif prefix_path.is_file():
        return None

    regions: list[tuple[int, Path]] = []
    if prefix_size:
        regions.append((0, prefix_path))

    for item in items:
        path = src_dir / item["file"]
        if not path.is_file() or path.stat().st_size != int(item["size"]):
            return None
        regions.append((int(item["offset"]), path))

    for gap in manifest.get("unlisted", []):
        path = src_dir / gap["file"]
        if not path.is_file():
            return None
        regions.append((int(gap["offset"]), path))

    return regions


def _write_region(fh: BinaryIO, offset: int, path: Path) -> None:
    """Seek to `offset` and copy `path` into the UMD."""
    size = path.stat().st_size
    _check_umd_limit(offset, size)
    fh.seek(offset)
    with path.open("rb") as src_fh:
        copy_stream(src_fh, fh, size)


def _pack_umd_layout(src_dir: Path, dest: Path, manifest: dict) -> int:
    """Write payloads and unlisted spans back at their original offsets."""
    items = manifest["files"]
    toc_offset = int(manifest["toc_offset"])
    packed = [(item["name"], int(item["offset"]), int(item["size"]), int(item["flags"])) for item in items]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        prefix_size = int(manifest.get("prefix_size", 0))
        if prefix_size:
            _write_region(fh, 0, src_dir / UMD_PREFIX_NAME)
        for i, item in enumerate(items, 1):
            _write_region(fh, int(item["offset"]), src_dir / item["file"])
            if i % 500 == 0 or i == len(items):
                print(f"  {i}/{len(items)} files")
        for gap in manifest.get("unlisted", []):
            _write_region(fh, int(gap["offset"]), src_dir / gap["file"])
        fh.seek(toc_offset)
        fh.write(_write_toc(packed))
        _write_umd_trailer(fh, toc_offset, manifest)
    return len(packed)


def _pack_umd_tight(src_dir: Path, dest: Path, manifest: dict) -> int:
    """Concatenate prefix + TOC payloads and rewrite offsets (drops unlisted spans)."""
    prefix_path = src_dir / UMD_PREFIX_NAME
    items = manifest["files"]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        if prefix_path.is_file():
            fh.write(prefix_path.read_bytes())
        packed: list[tuple[str, int, int, int]] = []
        for i, item in enumerate(items, 1):
            payload_path = src_dir / item["file"]
            size = payload_path.stat().st_size
            offset = fh.tell()
            _check_umd_limit(offset, size)
            with payload_path.open("rb") as src_fh:
                copy_stream(src_fh, fh, size)
            packed.append((item["name"], offset, size, int(item["flags"])))
            if i % 500 == 0 or i == len(items):
                print(f"  {i}/{len(items)} files")
        toc_offset = fh.tell()
        fh.write(_write_toc(packed))
        _write_umd_trailer(fh, toc_offset, manifest)
    return len(packed)


def pack_umd(src_dir: Path, dest: Path) -> int:
    """Rebuild a UMD from an unpack.py directory.

    Unmodified extracts keep the original offsets and unlisted padding so the
    packed file matches the source archive. If any payload size changed, the
    archive is rebuilt tightly (same as the old replace-in-place packer).

    Args:
        src_dir (Path): Directory that contains `_scct.json` and payloads.
        dest (Path): Output `.umd` path.

    Returns:
        int: Number of packed files.

    Raises:
        OverflowError: If any offset/size exceeds the uint32 TOC limit.
        FileNotFoundError: If a listed payload is missing.
        ValueError: If the directory is not a UMD unpack tree.
    """
    manifest = load_json(src_dir / MANIFEST_NAME)
    if manifest.get("format") != "umd":
        raise ValueError(f"{src_dir} is not a UMD unpack directory")

    regions = _layout_regions(src_dir, manifest)
    if regions is not None:
        return _pack_umd_layout(src_dir, dest, manifest)
    if manifest.get("unlisted"):
        print("payload size changed; rebuilding UMD without unlisted padding")
    return _pack_umd_tight(src_dir, dest, manifest)
