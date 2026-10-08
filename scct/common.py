#!/usr/bin/env python3

"""
common
~~~~~~
Shared constants, compact-index I/O, and CLI helpers.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, BinaryIO

# Unreal package magic (UE2 / UE2.5).
PACKAGE_TAG = 0x9E2A83C1

# Manifest filename written next to extracted files.
MANIFEST_NAME = "_scct.json"

# Optional raw bytes stored before the first UMD payload.
UMD_PREFIX_NAME = "_scct_prefix.bin"

# Non-zero bytes that sit between UMD TOC payloads (not listed in the TOC).
UMD_GAPS_DIR = "_scct_gaps"

# Unreal package bytes stored before the first export serial.
PACKAGE_HEADER_NAME = "_scct_header.bin"

# Raw import table copied between serial data and the export table.
PACKAGE_IMPORTS_NAME = "_scct_imports.bin"

# Directory that holds Unreal export blobs.
EXPORTS_DIR = "exports"

# Package extensions this tool packs (lowercase, without the dot).
PACKAGE_MODES = ("utx", "usx", "uax", "ukx")
DARE_MODES = ("sm0", "lm0", "ss0", "ls0")
ALL_MODES = ("umd",) + PACKAGE_MODES + DARE_MODES
ARCHIVE_SUFFIXES = {f".{mode}" for mode in ALL_MODES}

# UMD offsets / sizes are uint32, so the archive cannot reach 4 GiB.
UMD_MAX_BYTES = 0xFFFFFFFE

def kind_from_path(path: Path) -> str:
    """Return archive kind from a filename suffix (`umd`, `utx`, ...)."""
    return path.suffix.lower().lstrip(".")


def kind_from_manifest(manifest: dict[str, Any]) -> str:
    """Return archive kind from `_scct.json` `format`.

    Args:
        manifest (dict[str, Any]): Parsed `_scct.json`.

    Returns:
        str: Lowercase kind (`umd`, `utx`, `usx`, `uax`, `ukx`, `sm0`, …).

    Raises:
        ValueError: If `format` is missing.
    """
    fmt = str(manifest.get("format", "")).lower()
    if not fmt:
        raise ValueError(f"missing format in {MANIFEST_NAME}")
    return fmt


def is_frozen() -> bool:
    """Return True when running from a PyInstaller bundle."""
    return bool(getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"))


def pause_if_frozen() -> None:
    """Keep a console window open after drag-and-drop onto a frozen EXE."""
    if is_frozen():
        try:
            input("Press Enter to exit...")
        except EOFError:
            pass


def read_index(data: bytes, offset: int) -> tuple[int, int]:
    """Read an Unreal compact index.

    Args:
        data (bytes): Buffer that contains the index.
        offset (int): Byte offset of the first index byte.

    Returns:
        tuple[int, int]: (decoded value, offset after the index).
    """
    b0 = data[offset]
    offset += 1
    negative = bool(b0 & 0x80)
    value = b0 & 0x3F
    if b0 & 0x40:
        shift = 6
        while True:
            nxt = data[offset]
            offset += 1
            value |= (nxt & 0x7F) << shift
            if not (nxt & 0x80):
                break
            shift += 7
    return (-value if negative else value), offset


def write_index(value: int) -> bytes:
    """Encode an Unreal compact index.

    Args:
        value (int): Signed integer to encode.

    Returns:
        bytes: Compact-index bytes.
    """
    negative = value < 0
    value = abs(value)
    out = bytearray()
    b0 = value & 0x3F
    if negative:
        b0 |= 0x80
    value >>= 6
    if value:
        b0 |= 0x40
    out.append(b0)
    while value:
        nxt = value & 0x7F
        value >>= 7
        if value:
            nxt |= 0x80
        out.append(nxt)
    return bytes(out)


def read_fstring(data: bytes, offset: int) -> tuple[str, int]:
    """Read a UE2 FString (compact length including the trailing NUL).

    Args:
        data (bytes): Buffer that contains the string.
        offset (int): Byte offset of the compact length.

    Returns:
        tuple[str, int]: (decoded text without NUL, offset after the string).
    """
    nlen, offset = read_index(data, offset)
    if nlen < 0:
        nchar = -nlen
        raw = data[offset : offset + nchar * 2]
        offset += nchar * 2
        return raw.decode("utf-16-le", "replace").rstrip("\x00"), offset
    raw = data[offset : offset + nlen]
    offset += nlen
    if raw.endswith(b"\x00"):
        raw = raw[:-1]
    return raw.decode("latin-1", "replace"), offset


def write_fstring(text: str) -> bytes:
    """Encode a UE2 FString as latin-1 with a trailing NUL."""
    raw = text.encode("latin-1", "replace") + b"\x00"
    return write_index(len(raw)) + raw


def sanitize_name(name: str) -> str:
    """Replace path separators so a name can be used as a filename."""
    return name.replace("/", "_").replace("\\", "_").replace(":", "_")


def dump_json(path: Path, payload: dict[str, Any]) -> None:
    """Write a UTF-8 JSON manifest with a trailing newline."""
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def load_json(path: Path) -> dict[str, Any]:
    """Load a UTF-8 JSON manifest.

    Args:
        path (Path): Path to `_scct.json`.

    Returns:
        dict[str, Any]: Parsed object.

    Raises:
        FileNotFoundError: If the unpack manifest is missing.
    """
    if not path.is_file():
        raise FileNotFoundError(
            f"missing {MANIFEST_NAME} in {path.parent} "
            "(this directory was not created by unpack.py)"
        )
    return json.loads(path.read_text(encoding="utf-8"))


def copy_stream(src: BinaryIO, dst: BinaryIO, nbytes: int, chunk: int = 1 << 20) -> None:
    """Copy exactly nbytes from src to dst."""
    remaining = nbytes
    while remaining:
        block = src.read(min(chunk, remaining))
        if not block:
            raise EOFError("unexpected end of file while copying")
        dst.write(block)
        remaining -= len(block)
