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

# v1/v2 trailer: toc_offset, file_size, version, checksum (all uint32).
_TRAILER_FMT = "<IIII"
_TRAILER_SIZE = 16

# UMD3 trailer: toc64, size64, version=3, checksum, magic 'UMD3', reserved.
UMD3_MAGIC = 0x33444D55
UMD3_VERSION = 3
_UMD3_TRAILER_FMT = "<QQIIII"
_UMD3_TRAILER_SIZE = 32

# TOC entry payload: offset_lo, size, flags (all uint32). UMD3 stores offset_hi in flags.
_ENTRY_FMT = "<III"
_ENTRY_SIZE_MAX = 0xFFFFFFFE


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
    toc_size: int = 0


def peek_umd(path: Path) -> bool:
    """Return True if `path` has a UMD trailer that matches the file size."""
    size = path.stat().st_size
    if size < _TRAILER_SIZE:
        return False
    with path.open("rb") as fh:
        if size >= _UMD3_TRAILER_SIZE:
            fh.seek(-_UMD3_TRAILER_SIZE, 2)
            toc, file_size, version, _checksum, magic, _reserved = struct.unpack(
                _UMD3_TRAILER_FMT, fh.read(_UMD3_TRAILER_SIZE)
            )
            if magic == UMD3_MAGIC and version == UMD3_VERSION and file_size == size and 0 <= toc < size:
                return True
        fh.seek(-_TRAILER_SIZE, 2)
        toc_off, file_size, version, _checksum = struct.unpack(_TRAILER_FMT, fh.read(_TRAILER_SIZE))
    return file_size == size and version in (1, 2) and 0 < toc_off < size


def _read_index_fh(fh: BinaryIO) -> int:
    """Read an Unreal compact index from an open file."""
    raw = fh.read(1)
    if not raw:
        raise ValueError("truncated UMD TOC")
    b0 = raw[0]
    negative = bool(b0 & 0x80)
    value = b0 & 0x3F
    if b0 & 0x40:
        shift = 6
        while True:
            nxt = fh.read(1)
            if not nxt:
                raise ValueError("truncated UMD TOC")
            nb = nxt[0]
            value |= (nb & 0x7F) << shift
            if not (nb & 0x80):
                break
            shift += 7
    return -value if negative else value


def _parse_toc_fh(fh: BinaryIO, file_size: int, umd3: bool) -> tuple[list[UmdEntry], int]:
    """Parse TOC from the current file position. Returns (entries, toc_size)."""
    start = fh.tell()
    count = _read_index_fh(fh)
    if count < 0 or count > 1_000_000:
        raise ValueError(f"implausible UMD entry count {count}")
    entries: list[UmdEntry] = []
    for _ in range(count):
        nlen = _read_index_fh(fh)
        if nlen < 1 or nlen > 1024:
            raise ValueError(f"bad UMD name length {nlen}")
        raw = fh.read(nlen)
        if len(raw) != nlen:
            raise ValueError("truncated UMD TOC name")
        name = raw.split(b"\x00", 1)[0].decode("latin-1", "replace").replace("/", "\\")
        payload = fh.read(12)
        if len(payload) != 12:
            raise ValueError("truncated UMD TOC entry")
        payload_off, size, flags = struct.unpack(_ENTRY_FMT, payload)
        offset = payload_off | (flags << 32) if umd3 else payload_off
        disk_flags = 0 if umd3 else flags
        if not name or offset > file_size or offset + size > file_size + 1:
            raise ValueError(f"bad UMD entry {name!r} off={offset} size={size}")
        entries.append(UmdEntry(name, offset, size, disk_flags))
    return entries, fh.tell() - start


def _parse_toc(toc: bytes, file_size: int, umd3: bool = False) -> list[UmdEntry]:
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
        entry_off = payload_off | (flags << 32) if umd3 else payload_off
        disk_flags = 0 if umd3 else flags
        if not name or entry_off > file_size or entry_off + size > file_size + 1:
            raise ValueError(f"bad UMD entry {name!r} off={entry_off} size={size}")
        entries.append(UmdEntry(name, entry_off, size, disk_flags))
    if offset != len(toc):
        raise ValueError(f"UMD TOC leftover {len(toc) - offset} bytes")
    return entries


def _read_archive(fh: BinaryIO, file_size: int) -> UmdArchive:
    umd3 = False
    if file_size >= _UMD3_TRAILER_SIZE:
        fh.seek(-_UMD3_TRAILER_SIZE, 2)
        toc_off, trailer_size, version, checksum, magic, _reserved = struct.unpack(
            _UMD3_TRAILER_FMT, fh.read(_UMD3_TRAILER_SIZE)
        )
        if magic == UMD3_MAGIC and version == UMD3_VERSION and trailer_size == file_size:
            umd3 = True
    if not umd3:
        fh.seek(-_TRAILER_SIZE, 2)
        toc_off, trailer_size, version, checksum = struct.unpack(_TRAILER_FMT, fh.read(_TRAILER_SIZE))
        if trailer_size != file_size:
            raise ValueError(f"UMD trailer size {trailer_size} does not match file size {file_size}")
        if version not in (1, 2):
            raise ValueError(f"UMD trailer version {version} is not 1 or 2")
        if not 0 < toc_off < file_size:
            raise ValueError("UMD TOC offset is past the end of the file")
    elif not 0 <= toc_off < file_size:
        raise ValueError("UMD3 TOC offset is past the end of the file")

    fh.seek(toc_off)
    entries, toc_size = _parse_toc_fh(fh, file_size, umd3)
    if umd3:
        prefix_size = toc_off
    else:
        prefix_size = min((e.offset for e in entries), default=0)
    return UmdArchive(entries, toc_off, file_size, version, checksum, prefix_size, toc_size)


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
    """Return (offset, size) for bytes between TOC payloads, prefix/TOC excluded."""
    spans: list[tuple[int, int]] = []
    cursor = archive.prefix_size
    if archive.version == UMD3_VERSION:
        cursor = max(cursor, archive.toc_offset + archive.toc_size)
    for entry in sorted(archive.entries, key=lambda item: (item.offset, item.size)):
        if entry.offset > cursor:
            spans.append((cursor, entry.offset - cursor))
        cursor = max(cursor, entry.offset + entry.size)
    trailer = _UMD3_TRAILER_SIZE if archive.version == UMD3_VERSION else _TRAILER_SIZE
    limit = archive.toc_offset if archive.version != UMD3_VERSION else archive.file_size - trailer
    if limit > cursor:
        spans.append((cursor, limit - cursor))
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


def _write_toc(entries: list[tuple[str, int, int, int]], *, umd3: bool = False) -> bytes:
    """Build TOC bytes from (name, offset, size, flags)."""
    out = bytearray(write_index(len(entries)))
    for name, offset, size, flags in entries:
        raw = name.encode("latin-1") + b"\x00"
        out += write_index(len(raw))
        out += raw
        if umd3:
            flags = (offset >> 32) & 0xFFFFFFFF
            offset = offset & 0xFFFFFFFF
        out += struct.pack(_ENTRY_FMT, offset, size, flags)
    return bytes(out)


def _check_entry_size(size: int) -> None:
    if size > _ENTRY_SIZE_MAX:
        raise OverflowError(
            f"UMD payload is {size} bytes; a single file must fit in a uint32 size "
            f"(limit {_ENTRY_SIZE_MAX} bytes / ~4 GiB)."
        )


def _check_umd_limit(offset: int, size: int) -> None:
    """Raise if a v1/v2 UMD offset or size will not fit in the uint32 TOC."""
    _check_entry_size(size)
    if offset > UMD_MAX_BYTES or size > UMD_MAX_BYTES:
        raise OverflowError(
            "UMD v1/v2 TOC uses 32-bit offsets and sizes "
            f"(limit {UMD_MAX_BYTES} bytes / ~4 GiB). "
            "Packing a larger archive writes UMD3 for patch_exe (Magma 64-bit I/O)."
        )


def _write_umd_trailer(fh: BinaryIO, toc_offset: int, manifest: dict) -> None:
    """Write the 16-byte v1/v2 trailer at the current position."""
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


def _write_umd3_trailer(fh: BinaryIO, toc_offset: int, checksum: int) -> None:
    """Write the 32-byte UMD3 trailer at the current position."""
    if toc_offset > UMD_MAX_BYTES:
        raise OverflowError("UMD3 TOC must stay below 4 GiB so Magma can parse it")
    file_size = fh.tell() + _UMD3_TRAILER_SIZE
    fh.write(
        struct.pack(
            _UMD3_TRAILER_FMT,
            toc_offset,
            file_size,
            UMD3_VERSION,
            checksum,
            UMD3_MAGIC,
            0,
        )
    )


def _is_umd3(manifest: dict) -> bool:
    return int(manifest.get("umd_version", 0)) == UMD3_VERSION


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


def _write_region(fh: BinaryIO, offset: int, path: Path, *, umd3: bool) -> None:
    """Seek to `offset` and copy `path` into the UMD."""
    size = path.stat().st_size
    if umd3:
        _check_entry_size(size)
    else:
        _check_umd_limit(offset, size)
    fh.seek(offset)
    with path.open("rb") as src_fh:
        copy_stream(src_fh, fh, size)


def _pack_umd_layout(src_dir: Path, dest: Path, manifest: dict) -> int:
    """Write payloads and unlisted spans back at their original offsets."""
    items = manifest["files"]
    toc_offset = int(manifest["toc_offset"])
    umd3 = _is_umd3(manifest)
    packed = [(item["name"], int(item["offset"]), int(item["size"]), int(item["flags"])) for item in items]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        prefix_size = int(manifest.get("prefix_size", 0))
        if prefix_size:
            _write_region(fh, 0, src_dir / UMD_PREFIX_NAME, umd3=umd3)
        for i, item in enumerate(items, 1):
            _write_region(fh, int(item["offset"]), src_dir / item["file"], umd3=umd3)
            if i % 500 == 0 or i == len(items):
                print(f"  {i}/{len(items)} files")
        for gap in manifest.get("unlisted", []):
            _write_region(fh, int(gap["offset"]), src_dir / gap["file"], umd3=umd3)
        toc = _write_toc(packed, umd3=umd3)
        fh.seek(toc_offset)
        fh.write(toc)
        if umd3:
            end = fh.tell()
            for item in items:
                end = max(end, int(item["offset"]) + int(item["size"]))
            for gap in manifest.get("unlisted", []):
                gap_path = src_dir / gap["file"]
                end = max(end, int(gap["offset"]) + gap_path.stat().st_size)
            fh.seek(end)
            _write_umd3_trailer(fh, toc_offset, int(manifest.get("checksum", 0)))
        else:
            _write_umd_trailer(fh, toc_offset, manifest)
    return len(packed)


def _payloads(src_dir: Path, items: list[dict]) -> list[tuple[str, Path, int, int]]:
    out: list[tuple[str, Path, int, int]] = []
    for item in items:
        path = src_dir / item["file"]
        if not path.is_file():
            raise FileNotFoundError(path)
        size = path.stat().st_size
        _check_entry_size(size)
        out.append((item["name"], path, size, int(item.get("flags", 0))))
    return out


def _prefix_size(src_dir: Path, manifest: dict) -> int:
    prefix_path = src_dir / UMD_PREFIX_NAME
    prefix_size = int(manifest.get("prefix_size", 0))
    if prefix_size:
        if not prefix_path.is_file() or prefix_path.stat().st_size != prefix_size:
            raise FileNotFoundError(f"prefix {prefix_path} missing or size mismatch")
        return prefix_size
    if prefix_path.is_file():
        return prefix_path.stat().st_size
    return 0


def _tight_v1_size(prefix_size: int, payloads: list[tuple[str, Path, int, int]]) -> int:
    dummy = [(name, 0, size, flags) for name, _path, size, flags in payloads]
    toc = _write_toc(dummy, umd3=False)
    return prefix_size + sum(size for _n, _p, size, _f in payloads) + len(toc) + _TRAILER_SIZE


def _pack_umd_tight(src_dir: Path, dest: Path, manifest: dict) -> int:
    """Concatenate prefix + TOC payloads and rewrite offsets (drops unlisted spans)."""
    prefix_path = src_dir / UMD_PREFIX_NAME
    payloads = _payloads(src_dir, manifest["files"])
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as fh:
        if prefix_path.is_file():
            fh.write(prefix_path.read_bytes())
        packed: list[tuple[str, int, int, int]] = []
        for i, (name, payload_path, size, flags) in enumerate(payloads, 1):
            offset = fh.tell()
            _check_umd_limit(offset, size)
            with payload_path.open("rb") as src_fh:
                copy_stream(src_fh, fh, size)
            packed.append((name, offset, size, flags))
            if i % 500 == 0 or i == len(payloads):
                print(f"  {i}/{len(payloads)} files")
        toc_offset = fh.tell()
        fh.write(_write_toc(packed, umd3=False))
        _write_umd_trailer(fh, toc_offset, manifest)
    return len(packed)


def _pack_umd_tight_umd3(src_dir: Path, dest: Path, manifest: dict) -> int:
    """UMD3 tight pack: prefix + TOC (below 4 GiB) + payloads + 32-byte trailer.

    Magma's original TOC reader is still 32-bit, so the table of contents stays
    in the first 4 GiB. Payload offsets past 4 GiB use the TOC flags slot as
    offset_hi. Requires patch_exe (SC8G Magma 64-bit I/O).
    """
    prefix_path = src_dir / UMD_PREFIX_NAME
    prefix_size = _prefix_size(src_dir, manifest)
    payloads = _payloads(src_dir, manifest["files"])
    dummy = [(name, 0, size, 0) for name, _path, size, _flags in payloads]
    toc = _write_toc(dummy, umd3=True)
    toc_offset = prefix_size
    cursor = prefix_size + len(toc)
    packed: list[tuple[str, Path, int, int]] = []
    for name, path, size, _flags in payloads:
        packed.append((name, path, cursor, size))
        cursor += size
    toc = _write_toc([(name, offset, size, 0) for name, _path, offset, size in packed], umd3=True)
    if prefix_size + len(toc) != (packed[0][2] if packed else prefix_size + len(toc)):
        raise RuntimeError("TOC size changed after assigning UMD3 offsets")
    file_size = cursor + _UMD3_TRAILER_SIZE
    if toc_offset > UMD_MAX_BYTES:
        raise OverflowError("UMD3 TOC must stay below 4 GiB so Magma can parse it")

    dest.parent.mkdir(parents=True, exist_ok=True)
    checksum = int(manifest.get("checksum", 0))
    past = sum(1 for _n, _p, off, _s in packed if off >= 0x100000000)
    print(
        f"pack umd3: {len(packed)} files, toc={len(toc)} @ {toc_offset}, "
        f"total={file_size:,} ({file_size / (1024 ** 3):.3f} GiB), "
        f"{past} payloads past 4 GiB"
    )
    with dest.open("wb") as fh:
        if prefix_size:
            with prefix_path.open("rb") as src_fh:
                copy_stream(src_fh, fh, prefix_size)
        fh.write(toc)
        copied = 0
        for i, (name, path, offset, size) in enumerate(packed, 1):
            if fh.tell() != offset:
                raise RuntimeError(f"offset drift at {name}: tell={fh.tell()} expected={offset}")
            with path.open("rb") as src_fh:
                copy_stream(src_fh, fh, size)
            copied += size
            if i % 500 == 0 or i == len(packed):
                print(f"  {i}/{len(packed)} files")
        _write_umd3_trailer(fh, toc_offset, checksum)
        if fh.tell() != file_size:
            raise RuntimeError(f"final size {fh.tell()} != {file_size}")
    return len(packed)


def pack_umd(src_dir: Path, dest: Path) -> int:
    """Rebuild a UMD from an unpack.py directory.

    Unmodified extracts keep the original offsets and unlisted padding so the
    packed file matches the source archive. If any payload size changed, the
    archive is rebuilt tightly (same as the old replace-in-place packer).

    Archives that would exceed the v1/v2 4 GiB trailer, or that were unpacked
    from UMD3, are written as UMD3 (32-byte trailer, TOC below 4 GiB, 64-bit
    payload offsets). Those files need `patch_exe` (Magma 64-bit I/O). Smaller
    v1/v2 archives keep the original 16-byte trailer so an unpatched / 4 GiB
    EXE can still open them.

    Args:
        src_dir (Path): Directory that contains `_scct.json` and payloads.
        dest (Path): Output `.umd` path.

    Returns:
        int: Number of packed files.

    Raises:
        OverflowError: If any offset/size exceeds the format limit.
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

    payloads = _payloads(src_dir, manifest["files"])
    prefix_size = _prefix_size(src_dir, manifest)
    want_umd3 = _is_umd3(manifest)
    if not want_umd3:
        try:
            want_umd3 = _tight_v1_size(prefix_size, payloads) > UMD_MAX_BYTES
        except OverflowError:
            want_umd3 = True
    if want_umd3:
        if not _is_umd3(manifest):
            print("archive exceeds 4 GiB; writing UMD3 (needs patch_exe Magma 64-bit I/O)")
        return _pack_umd_tight_umd3(src_dir, dest, manifest)
    return _pack_umd_tight(src_dir, dest, manifest)
