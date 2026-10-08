#!/usr/bin/env python3

"""
props
~~~~~
UE2 tagged-property walking used by Texture mip recook.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from typing import Any

from scct.common import read_index


def _name_text(names: Sequence[Any], index: int) -> str:
    item = names[index]
    return item if isinstance(item, str) else item.text


def read_array_index(blob: bytes, offset: int) -> tuple[int, int]:
    """Read a UE1/UE2 property array index."""
    b = blob[offset]
    offset += 1
    if b < 0x80:
        return b, offset
    if b < 0xC0:
        return ((b & 0x7F) << 8) | blob[offset], offset + 1
    return struct.unpack_from("<I", blob, offset - 1)[0], offset + 3


def parse_props(names: Sequence[Any], blob: bytes) -> tuple[list[tuple], int]:
    """Parse tagged properties until None.

    Returns:
        tuple[list[tuple], int]: (properties, offset after None).
    """
    offset = 0
    props: list[tuple] = []
    while offset < len(blob):
        name_i, offset = read_index(blob, offset)
        if name_i < 0 or name_i >= len(names):
            raise ValueError(f"bad property name index {name_i}")
        name = _name_text(names, name_i)
        if name == "None":
            break
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
        arr = 0
        if typ == 3:
            props.append((name, "Bool", arr, is_array))
            continue
        if is_array:
            arr, offset = read_array_index(blob, offset)
        if typ == 10:
            si, offset = read_index(blob, offset)
            struct_name = _name_text(names, si) if 0 <= si < len(names) else f"?{si}"
            raw = blob[offset : offset + size]
            offset += size
            props.append((name, "Struct", arr, (struct_name, raw)))
            continue
        raw = blob[offset : offset + size]
        offset += size
        if typ == 2 and size == 4:
            val: object = struct.unpack("<i", raw)[0]
        elif typ == 4 and size == 4:
            val = struct.unpack("<f", raw)[0]
        elif typ == 1 and size >= 1:
            val = raw[0]
        elif typ in (5, 6, 8) and raw:
            val, _ = read_index(raw + b"\x00" * 8, 0)
        else:
            val = raw
        tname = {1: "Byte", 2: "Int", 4: "Float", 5: "Object", 6: "Name"}.get(typ, f"T{typ}")
        props.append((name, tname, arr, val))
    return props, offset


def props_to_map(props: list[tuple]) -> dict:
    """Flatten parsed properties to a name -> value map."""
    out: dict = {}
    for name, _t, arr, val in props:
        if arr:
            out.setdefault(name, {})
            if isinstance(out[name], dict):
                out[name][arr] = val
        else:
            if name in out and not isinstance(out[name], dict):
                out[name] = {0: out[name], arr: val}
            else:
                out[name] = val
    return out


def parse_prop_layout(names: Sequence[Any], blob: bytes) -> tuple[list[dict], int]:
    """Locate value bytes of each tagged property (for in-place patches)."""
    offset = 0
    fields: list[dict] = []
    while offset < len(blob):
        name_i, offset = read_index(blob, offset)
        name = _name_text(names, name_i)
        if name == "None":
            break
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
        arr = 0
        if typ == 3:
            fields.append({"name": name, "arr": arr, "typ": typ, "val_off": None, "size": 0})
            continue
        if is_array:
            arr, offset = read_array_index(blob, offset)
        if typ == 10:
            _si, offset = read_index(blob, offset)
        val_off = offset
        offset += size
        fields.append({"name": name, "arr": arr, "typ": typ, "val_off": val_off, "size": size})
    return fields, offset


def patch_props(names: Sequence[Any], blob: bytes, updates: dict[tuple[str, int], int]) -> bytes:
    """Patch integer property values in the tagged header; return the header only."""
    fields, end = parse_prop_layout(names, blob)
    out = bytearray(blob[:end])
    for field in fields:
        key = (field["name"], field["arr"])
        if key not in updates or field["val_off"] is None:
            continue
        val = int(updates[key])
        off, sz, typ = field["val_off"], field["size"], field["typ"]
        if typ == 2 and sz == 4:
            struct.pack_into("<i", out, off, val)
        elif typ == 1 and sz >= 1:
            out[off] = val & 0xFF
        else:
            raise ValueError(f"cannot patch {key} typ={typ} size={sz}")
    return bytes(out)
