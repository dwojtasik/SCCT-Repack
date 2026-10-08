#!/usr/bin/env python3

"""
sound
~~~~~
Decode SCX USound serials to JSON and recook JSON back to serial bytes.

Stock UE2 / UE2.5 USound is an FName FileType plus a TLazyArray of WAV/OGG
bytes (umodel `USound::Serialize`). Chaos Theory replaced that with Dare
Audio cues: a bank/cue id that plays samples from MAPS.SM0 + *.SS0 / *.LS0.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from typing import Any

from scct.common import read_index, write_fstring, write_index
from scct.props import parse_props

_STOCK_TYPES = {"wav", "ogg", "mp3", "unk", "wma"}
_STOCK_MAGIC = (b"RIFF", b"OggS", b"FSB4")


def _name_text(names: Sequence[Any], index: int) -> str:
    item = names[index]
    return item if isinstance(item, str) else item.text


def _name_index(names: Sequence[Any], text: str) -> int:
    for i, item in enumerate(names):
        if _name_text(names, i) == text:
            return i
    raise ValueError(f"name {text!r} is not in the package name table")


def _split_file(rest: bytes) -> tuple[bytes, str | None]:
    """Split trailing FString path from cue bytes after the two uint16 fields."""
    for i in range(len(rest)):
        try:
            nlen, payload_off = read_index(rest, i)
        except (IndexError, ValueError):
            continue
        if nlen < 2 or payload_off + nlen != len(rest):
            continue
        payload = rest[payload_off : payload_off + nlen]
        if not payload.endswith(b"\x00"):
            continue
        text = payload[:-1].decode("latin-1", "replace")
        if "\\" in text or text.lower().endswith(".bin"):
            return rest[:i], text
    return rest, None


def _try_stock(names: Sequence[Any], rest: bytes) -> dict[str, Any] | None:
    """Return a stock UE2 USound payload if FileType + TLazyArray WAV/OGG match."""
    if len(rest) < 8:
        return None
    try:
        file_i, offset = read_index(rest, 0)
    except (IndexError, ValueError):
        return None
    if file_i < 0 or file_i >= len(names):
        return None
    file_type = _name_text(names, file_i)
    if file_type.lower() not in _STOCK_TYPES:
        return None
    for with_duration in (True, False):
        pos = offset
        duration = None
        if with_duration:
            if pos + 4 > len(rest):
                continue
            duration = struct.unpack_from("<f", rest, pos)[0]
            pos += 4
            if not 0.0 <= duration < 36_000.0:
                continue
        if pos + 4 > len(rest):
            continue
        skip = struct.unpack_from("<i", rest, pos)[0]
        pos += 4
        try:
            count, data_off = read_index(rest, pos)
        except (IndexError, ValueError):
            continue
        if count < 16 or data_off + count != len(rest):
            continue
        data = rest[data_off : data_off + count]
        if not data.startswith(_STOCK_MAGIC):
            continue
        out: dict[str, Any] = {
            "kind": "wav",
            "file_type": file_type,
            "skip": skip,
            "data": data,
        }
        if with_duration:
            out["duration"] = duration
        return out
    return None


def decode_sound(names: Sequence[Any], blob: bytes) -> dict[str, Any]:
    """Decode a Sound serial blob to a JSON-ready dict.

    Args:
        names (Sequence[Any]): Package name table.
        blob (bytes): Export serial data.

    Returns:
        dict[str, Any]: Cue fields, or stock WAV metadata plus `data` bytes.
    """
    none_index, _none_end = read_index(blob, 0)
    if _name_text(names, none_index) != "None":
        raise ValueError("Sound does not start with None")
    _props, offset = parse_props(names, blob)
    rest = blob[offset:]
    stock = _try_stock(names, rest)
    if stock is not None:
        stock["none_index"] = none_index
        return stock

    out: dict[str, Any] = {
        "kind": "cue",
        "none_index": none_index,
        "file": None,
        "extra_hex": rest.hex(),
    }
    if len(rest) < 4:
        return out
    cue_id, bank_id = struct.unpack_from("<HH", rest, 0)
    extra, path = _split_file(rest[4:])
    out["cue_id"] = cue_id
    out["bank_id"] = bank_id
    out["extra_hex"] = extra.hex()
    out["file"] = path
    if len(extra) >= 4:
        link = struct.unpack_from("<i", extra, 0)[0]
        if link == -1:
            out["link_cue_id"] = None
            out["link_bank_id"] = None
        else:
            link_cue, link_bank = struct.unpack_from("<HH", extra, 0)
            out["link_cue_id"] = link_cue
            out["link_bank_id"] = link_bank
    return out


def recook_sound(names: Sequence[Any], payload: dict[str, Any], serial_offset: int = 0) -> bytes:
    """Rebuild a Sound serial blob from decode_sound() JSON.

    Args:
        names (Sequence[Any]): Package name table (FName indices).
        payload (dict[str, Any]): Object loaded from the export `.json`.
        serial_offset (int): Absolute package offset of this export (TLazyArray skip).

    Returns:
        bytes: Serial data starting with a terminating None property.
    """
    none_index = int(payload.get("none_index", 0))
    kind = str(payload.get("kind") or "")
    if kind == "wav" or payload.get("file_type"):
        return _recook_stock(names, payload, serial_offset, none_index)

    out = bytearray(write_index(none_index))
    cue = payload.get("cue_id", payload.get("index"))
    bank = payload.get("bank_id", payload.get("flags"))
    if cue is not None and bank is not None:
        out += struct.pack("<HH", int(cue) & 0xFFFF, int(bank) & 0xFFFF)
    extra_hex = str(payload.get("extra_hex") or "")
    if extra_hex:
        out += bytes.fromhex(extra_hex)
    path = payload.get("file")
    if path:
        out += write_fstring(str(path))
    return bytes(out)


def _recook_stock(
    names: Sequence[Any],
    payload: dict[str, Any],
    serial_offset: int,
    none_index: int,
) -> bytes:
    data = payload.get("data")
    if not isinstance(data, (bytes, bytearray)):
        raise ValueError("stock Sound recook needs WAV/OGG bytes")
    body = bytearray(write_index(none_index))
    body += write_index(_name_index(names, str(payload["file_type"])))
    if "duration" in payload:
        body += struct.pack("<f", float(payload["duration"]))
    skip_at = len(body)
    body += b"\x00\x00\x00\x00"
    body += write_index(len(data))
    body += data
    skip = serial_offset + len(body)
    struct.pack_into("<i", body, skip_at, skip)
    return bytes(body)
