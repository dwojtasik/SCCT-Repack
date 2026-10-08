#!/usr/bin/env python3

"""
anim
~~~~
Decode SCX MeshAnimation serials to JSON and recook JSON back to serial bytes.

Bones, motion-chunk headers, and FMeshAnimSeq records are fully parsed.
Per-bone analog keys stay as packed `analog_hex` (Chaos Theory uses a custom
compressed track layout, not stock UE2 float PSA keys).
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from collections.abc import Sequence
from typing import Any

from scct.common import read_index, write_index
from scct.props import parse_props


def _name_text(names: Sequence[Any], index: int) -> str:
    item = names[index]
    return item if isinstance(item, str) else item.text


def _name_index(names: Sequence[Any], text: str) -> int:
    for i, item in enumerate(names):
        if _name_text(names, i) == text:
            return i
    raise ValueError(f"name {text!r} is not in the package name table")


def _allowed_ntracks(nbones: int) -> set[int]:
    allowed = {nbones, nbones + 1}
    if nbones > 1:
        allowed.add(nbones - 1)
        allowed.add(nbones * 2)
    return allowed


def _plausible_move_header(
    blob: bytes,
    offset: int,
    nbones: int,
    expect_ntracks: int | None = None,
    expect_flags: int | None = None,
) -> tuple[int, int] | None:
    """Return (ntracks, analog_offset) if `offset` looks like a MotionChunk header."""
    if offset + 16 > len(blob):
        return None
    track_time, start_bone, flags = struct.unpack_from("<fii", blob, offset)
    if not 0.5 <= track_time < 1000.0:
        return None
    if start_bone < 0 or start_bone > 4:
        return None
    if flags < 0 or flags > 8:
        return None
    if expect_flags is not None and flags != expect_flags:
        return None
    try:
        ntracks, analog = read_index(blob, offset + 12)
    except (IndexError, ValueError):
        return None
    if expect_ntracks is not None:
        if ntracks != expect_ntracks:
            return None
    elif ntracks not in _allowed_ntracks(nbones):
        return None
    if analog - (offset + 12) not in (1, 2, 3, 4):
        return None
    if analog > len(blob):
        return None
    return ntracks, analog


def _next_move_header(
    blob: bytes,
    analog_start: int,
    nbones: int,
    expect_ntracks: int,
    expect_flags: int | None = None,
) -> int | None:
    """Return the next MotionChunk offset at or after analog_start, if any."""
    offset = analog_start
    end = len(blob) - 16
    while offset <= end:
        if _plausible_move_header(blob, offset, nbones, expect_ntracks, expect_flags) is not None:
            return offset
        offset += 1
    return None


def _parse_seqs(names: Sequence[Any], blob: bytes, offset: int) -> tuple[list[dict[str, Any]], int]:
    nseq, offset = read_index(blob, offset)
    if nseq < 0 or nseq > 8000:
        raise ValueError(f"bad AnimSeqs count {nseq}")
    seqs: list[dict[str, Any]] = []
    for _ in range(nseq):
        ni, offset = read_index(blob, offset)
        name = _name_text(names, ni)
        ng, offset = read_index(blob, offset)
        if ng < 0 or ng > 16:
            raise ValueError(f"bad Groups count {ng}")
        groups: list[str] = []
        for _g in range(ng):
            gi, offset = read_index(blob, offset)
            groups.append(_name_text(names, gi))
        start_frame, num_frames = struct.unpack_from("<ii", blob, offset)
        offset += 8
        if num_frames < 0 or num_frames > 100_000:
            raise ValueError(f"bad NumFrames {num_frames}")
        nn, offset = read_index(blob, offset)
        if nn < 0 or nn > 256:
            raise ValueError(f"bad Notifys count {nn}")
        notifies: list[dict[str, Any]] = []
        for _n in range(nn):
            time = struct.unpack_from("<f", blob, offset)[0]
            offset += 4
            fields: list[str] = []
            for _f in range(3):
                fi, offset = read_index(blob, offset)
                fields.append(_name_text(names, fi))
            notifies.append(
                {
                    "time": time,
                    "function": fields[0],
                    "object": fields[1],
                    "target": fields[2],
                }
            )
        rate = struct.unpack_from("<f", blob, offset)[0]
        offset += 4
        extra_a, extra_b = struct.unpack_from("<ii", blob, offset)
        offset += 8
        seqs.append(
            {
                "name": name,
                "groups": groups,
                "start_frame": start_frame,
                "num_frames": num_frames,
                "rate": rate,
                "notifies": notifies,
                "extra_a": extra_a,
                "extra_b": extra_b,
            }
        )
    return seqs, offset


def _find_seqs(names: Sequence[Any], blob: bytes, start: int, nmoves: int) -> tuple[int, list[dict[str, Any]]]:
    """Locate AnimSeqs at `start` or the compact-index of `nmoves` nearby."""
    if nmoves == 0:
        seqs, end = _parse_seqs(names, blob, start)
        if end != len(blob):
            raise ValueError("trailing bytes after empty MeshAnimation")
        return start, seqs

    enc = write_index(nmoves)
    pos = start
    while True:
        hit = blob.find(enc, pos)
        if hit < 0:
            break
        try:
            seqs, end = _parse_seqs(names, blob, hit)
        except (ValueError, IndexError, struct.error, KeyError):
            pos = hit + 1
            continue
        if end == len(blob) and len(seqs) == nmoves:
            return hit, seqs
        pos = hit + 1
    seqs, end = _parse_seqs(names, blob, start)
    if end != len(blob):
        raise ValueError("cannot locate AnimSeqs tail")
    return start, seqs


def parse_mesh_animation(names: Sequence[Any], blob: bytes) -> dict[str, Any]:
    """Parse a MeshAnimation serial blob.

    Args:
        names (Sequence[Any]): Package name table.
        blob (bytes): Export serial data.

    Returns:
        dict[str, Any]: JSON-ready animation set.

    Raises:
        ValueError: If the native layout does not match SCX MeshAnimation.
    """
    none_index, _none_end = read_index(blob, 0)
    if _name_text(names, none_index) != "None":
        raise ValueError("MeshAnimation does not start with None")
    _props, offset = parse_props(names, blob)
    if offset + 4 > len(blob):
        raise ValueError("truncated MeshAnimation")
    version = struct.unpack_from("<i", blob, offset)[0]
    offset += 4
    nbones, offset = read_index(blob, offset)
    if nbones < 1 or nbones > 512:
        raise ValueError(f"bad RefBones count {nbones}")
    bones: list[dict[str, Any]] = []
    for _ in range(nbones):
        ni, offset = read_index(blob, offset)
        flags, parent = struct.unpack_from("<Ii", blob, offset)
        offset += 8
        bones.append({"name": _name_text(names, ni), "flags": flags, "parent": parent})
    after_bones = offset
    nmoves, offset = read_index(blob, offset)
    if nmoves < 0 or nmoves > 8000:
        raise ValueError(f"bad Moves count {nmoves}")

    moves: list[dict[str, Any]] = []
    seqs: list[dict[str, Any]] = []
    moves_hex = ""
    if nmoves:
        try:
            hdr_off = offset
            for index in range(nmoves):
                parsed = _plausible_move_header(blob, hdr_off, nbones)
                if parsed is None:
                    raise ValueError(f"Moves[{index}] header does not match SCX MotionChunk")
                ntracks, analog = parsed
                track_time, start_bone, flags = struct.unpack_from("<fii", blob, hdr_off)
                if index + 1 < nmoves:
                    nxt = _next_move_header(blob, analog, nbones, ntracks, None)
                    if nxt is None:
                        raise ValueError(f"Moves[{index + 1}] header not found")
                    analog_end = nxt
                    hdr_off = nxt
                else:
                    analog_end = analog
                moves.append(
                    {
                        "track_time": track_time,
                        "start_bone": start_bone,
                        "flags": flags,
                        "ntracks": ntracks,
                        "analog_hex": blob[analog:analog_end].hex() if analog_end > analog else "",
                        "analog_off": analog,
                    }
                )
            last_analog = int(moves[-1]["analog_off"])
            try:
                seq_start, seqs = _find_seqs(names, blob, last_analog, nmoves)
                moves[-1]["analog_hex"] = blob[last_analog:seq_start].hex()
            except (ValueError, IndexError, struct.error, KeyError):
                seqs = []
                moves[-1]["analog_hex"] = blob[last_analog:].hex()
            for move in moves:
                del move["analog_off"]
        except ValueError:
            moves = []
            seqs = []
            moves_hex = blob[after_bones:].hex()
    else:
        try:
            _seq_start, seqs = _find_seqs(names, blob, offset, 0)
        except (ValueError, IndexError, struct.error, KeyError):
            seqs = []
            if offset < len(blob):
                raise ValueError("empty Moves tail is not AnimSeqs") from None

    out: dict[str, Any] = {
        "none_index": none_index,
        "version": version,
        "bones": bones,
        "moves": moves,
        "sequences": seqs,
    }
    if moves_hex:
        out["moves_hex"] = moves_hex
    return out


def recook_mesh_animation(names: Sequence[Any], payload: dict[str, Any]) -> bytes:
    """Rebuild a MeshAnimation serial blob from parse_mesh_animation() JSON.

    Args:
        names (Sequence[Any]): Package name table (FName indices).
        payload (dict[str, Any]): Object loaded from the export `.json`.

    Returns:
        bytes: Serial data starting with a terminating None property.
    """
    out = bytearray(write_index(int(payload.get("none_index", 0))))
    out += struct.pack("<i", int(payload.get("version", 0)))
    bones = list(payload.get("bones") or [])
    out += write_index(len(bones))
    for bone in bones:
        out += write_index(_name_index(names, str(bone["name"])))
        out += struct.pack("<Ii", int(bone.get("flags", 0)), int(bone.get("parent", 0)))
    moves = list(payload.get("moves") or [])
    moves_hex = str(payload.get("moves_hex") or "")
    if moves_hex:
        out += bytes.fromhex(moves_hex)
        return bytes(out)
    out += write_index(len(moves))
    for move in moves:
        out += struct.pack(
            "<fii",
            float(move["track_time"]),
            int(move.get("start_bone", 0)),
            int(move.get("flags", 1)),
        )
        analog = bytes.fromhex(str(move.get("analog_hex") or ""))
        out += write_index(int(move.get("ntracks", len(bones))))
        out += analog
    seqs = list(payload.get("sequences") or [])
    if seqs or not moves:
        out += write_index(len(seqs))
    for seq in seqs:
        out += write_index(_name_index(names, str(seq["name"])))
        groups = list(seq.get("groups") or [])
        out += write_index(len(groups))
        for group in groups:
            out += write_index(_name_index(names, str(group)))
        out += struct.pack("<ii", int(seq.get("start_frame", 0)), int(seq["num_frames"]))
        notifies = list(seq.get("notifies") or [])
        out += write_index(len(notifies))
        for note in notifies:
            out += struct.pack("<f", float(note.get("time", 0.0)))
            out += write_index(_name_index(names, str(note.get("function", "None"))))
            out += write_index(_name_index(names, str(note.get("object", "None"))))
            out += write_index(_name_index(names, str(note.get("target", "None"))))
        out += struct.pack("<f", float(seq.get("rate", 15.0)))
        out += struct.pack("<ii", int(seq.get("extra_a", 0)), int(seq.get("extra_b", 0)))
    return bytes(out)
