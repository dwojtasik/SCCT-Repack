#!/usr/bin/env python3

"""
dare
~~~~
Unpack and recook Ubisoft DARE MAP/SB audio (SM0/LM0 + SS0/LS0).

Chaos Theory PC uses version 0x00120012. Stream types: 0/1 PCM16, 3 UBI IMA, 4 OGG.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import hashlib
import struct
import wave
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from scct.common import EXPORTS_DIR, MANIFEST_NAME, dump_json, load_json, sanitize_name
from scct.ubi_ima import decode_ima, default_ima_template, encode_ima, parse_header

# Reused for one process so -r only asks once per map kind.
_MAP_CACHE: dict[str, Path] = {}

_SM_VERSION = 0x00120012
_MAP_ENTRY = 0x34
_S2_ENTRY = 0x60
_S3_ENTRY = 0x14
_NAME_SIZE = 0x28

_CODEC_PCM = "pcm"
_CODEC_IMA = "ima"
_CODEC_OGG = "ogg"

_STREAM_CODEC = {
    0: _CODEC_PCM,
    1: _CODEC_PCM,
    3: _CODEC_IMA,
    4: _CODEC_OGG,
}


def peek_dare(path: Path) -> str | None:
    """Return sm0/lm0/ss0/ls0 when `path` looks like a DARE file."""
    suffix = path.suffix.lower()
    if suffix in {".sm0", ".lm0", ".ss0", ".ls0"}:
        return suffix.lstrip(".")
    return None


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _pcm_bytes(samples: list[int]) -> bytes:
    return struct.pack("<" + "h" * len(samples), *[max(-32768, min(32767, int(s))) for s in samples])


def _read_wav(path: Path) -> tuple[int, int, list[int]]:
    with wave.open(str(path), "rb") as fh:
        channels = fh.getnchannels()
        rate = fh.getframerate()
        width = fh.getsampwidth()
        nframes = fh.getnframes()
        if width != 2:
            raise ValueError(f"{path.name}: WAV must be 16-bit PCM")
        raw = fh.readframes(nframes)
    count = len(raw) // 2
    samples = list(struct.unpack("<" + "h" * count, raw))
    return rate, channels, samples


def _write_wav(path: Path, rate: int, channels: int, samples: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = _pcm_bytes(samples)
    with wave.open(str(path), "wb") as fh:
        fh.setnchannels(max(channels, 1))
        fh.setsampwidth(2)
        fh.setframerate(rate)
        fh.writeframes(frames)


def _read_name(data: bytes, offset: int) -> str:
    return data[offset : offset + _NAME_SIZE].split(b"\x00", 1)[0].decode("latin-1", "replace")


def _codec_for(stream_type: int) -> str | None:
    return _STREAM_CODEC.get(stream_type)


def _set_u32(buf: bytearray, offset: int, value: int) -> bool:
    if offset < 0 or offset + 4 > len(buf):
        raise ValueError(
            f"MAPS header offset {offset} is past the end of this map file "
            f"({len(buf)} bytes); use the MAPS.SM0/MAPS.LM0 this SS0/LS0 was unpacked with"
        )
    old = struct.unpack_from("<I", buf, offset)[0]
    if old == value:
        return False
    struct.pack_into("<I", buf, offset, value)
    return True


@dataclass
class DareStream:
    """One unique DARE audio payload plus the SM0 headers that reference it."""

    codec: str
    rate: int
    channels: int
    samples: int
    streamed: bool
    stream_file: str
    stream_offset: int
    stream_size: int
    wav_name: str = ""
    ogg_name: str = ""
    wav_sha256: str = ""
    raw_sha256: str = ""
    headers: list[dict[str, Any]] = field(default_factory=list)


def _resolve_internal(sm: bytes, s3_off: int, s3_num: int, header_index: int, subblock_id: int) -> int | None:
    for i in range(s3_num):
        entry = s3_off + _S3_ENTRY * i
        table_off = struct.unpack_from("<I", sm, entry + 4)[0] + s3_off
        table_num = struct.unpack_from("<I", sm, entry + 8)[0]
        table2_off = struct.unpack_from("<I", sm, entry + 12)[0] + s3_off
        table2_num = struct.unpack_from("<I", sm, entry + 16)[0]
        if table_num > 200_000 or table2_num > 64:
            continue
        for j in range(table_num):
            idx = struct.unpack_from("<I", sm, table_off + 8 * j)[0] & 0x3FFFFFFF
            rel = struct.unpack_from("<I", sm, table_off + 8 * j + 4)[0]
            if idx != header_index:
                continue
            for k in range(table2_num):
                sid = struct.unpack_from("<I", sm, table2_off + 16 * k)[0]
                if sid == subblock_id:
                    return rel + struct.unpack_from("<I", sm, table2_off + 16 * k + 12)[0]
            return rel
    return None


def parse_map_streams(sm: bytes) -> list[tuple[str, DareStream]]:
    """Parse SM0/LM0 into (key, stream) rows; keys collapse duplicate payloads."""
    version, map_start, map_num = struct.unpack_from("<III", sm, 0)
    if version != _SM_VERSION:
        raise ValueError(f"unsupported DARE map version {version:#x}")
    if map_num < 1 or map_num > 1024:
        raise ValueError(f"bad DARE map count {map_num}")
    unique: dict[tuple, DareStream] = {}
    order: list[tuple] = []
    for mi in range(map_num):
        base = map_start + mi * _MAP_ENTRY
        map_off, map_size = struct.unpack_from("<II", sm, base + 8)
        map_name = _read_name(sm, base + 0x10) or f"map{mi}"
        if map_off + 0x24 > len(sm):
            continue
        s2_off = struct.unpack_from("<I", sm, map_off + 0x0C)[0] + map_off
        s2_num = struct.unpack_from("<I", sm, map_off + 0x10)[0]
        s3_off = struct.unpack_from("<I", sm, map_off + 0x14)[0] + map_off
        s3_num = struct.unpack_from("<I", sm, map_off + 0x18)[0]
        sx_off = struct.unpack_from("<I", sm, map_off + 0x1C)[0] + map_off
        sx_size = struct.unpack_from("<I", sm, map_off + 0x20)[0]
        if s2_num > 200_000:
            continue
        for index in range(s2_num):
            hdr = s2_off + index * _S2_ENTRY
            header_id, header_type = struct.unpack_from("<II", sm, hdr)
            if header_type != 1:
                continue
            stream_size = struct.unpack_from("<I", sm, hdr + 0x08)[0]
            rel_off = struct.unpack_from("<I", sm, hdr + 0x10)[0]
            streamed = bool(struct.unpack_from("<I", sm, hdr + 0x24)[0] & 1)
            loop = bool(struct.unpack_from("<I", sm, hdr + 0x28)[0] & 1)
            software = bool(struct.unpack_from("<I", sm, hdr + 0x2C)[0] & 1)
            num_samples = struct.unpack_from("<I", sm, hdr + 0x30)[0]
            num_samples2 = struct.unpack_from("<I", sm, hdr + 0x38)[0]
            rate = struct.unpack_from("<I", sm, hdr + 0x44)[0]
            channels = struct.unpack_from("<H", sm, hdr + 0x4C)[0]
            stream_type = struct.unpack_from("<I", sm, hdr + 0x50)[0]
            name_off = struct.unpack_from("<I", sm, hdr + 0x54)[0]
            codec = _codec_for(stream_type)
            if codec is None or stream_size < 4 or rate < 1000 or channels not in (1, 2):
                continue
            resource = ""
            if streamed and name_off != 0xFFFFFFFF and 0 <= name_off < sx_size:
                resource = _read_name(sm, sx_off + name_off)
            if streamed:
                abs_off = rel_off
            else:
                subblock = 1 if software else 0
                resolved = _resolve_internal(sm, s3_off, s3_num, index, subblock)
                if resolved is None:
                    continue
                abs_off = resolved
            key = (resource if streamed else "", abs_off, stream_size, codec)
            info = {
                "map": map_name,
                "index": index,
                "header_id": header_id,
                "header_offset": hdr,
                "rel_offset": rel_off,
                "loop": loop,
                "num_samples": num_samples,
                "num_samples2": num_samples2,
            }
            if key not in unique:
                unique[key] = DareStream(
                    codec=codec,
                    rate=rate,
                    channels=channels,
                    samples=num_samples or num_samples2,
                    streamed=streamed,
                    stream_file=resource,
                    stream_offset=abs_off,
                    stream_size=stream_size,
                )
                order.append(key)
            unique[key].headers.append(info)
    return [(f"{k[0] or '_internal'}:{k[1]}", unique[k]) for k in order]


def _payload(map_bytes: bytes, companions: dict[str, Path], stream: DareStream) -> bytes:
    if stream.streamed:
        path = companions.get(stream.stream_file.lower())
        if path is None or not path.is_file():
            raise FileNotFoundError(f"missing stream file {stream.stream_file}")
        with path.open("rb") as fh:
            fh.seek(stream.stream_offset)
            data = fh.read(stream.stream_size)
        if len(data) != stream.stream_size:
            raise ValueError(f"truncated {stream.stream_file} at {stream.stream_offset}")
        return data
    end = stream.stream_offset + stream.stream_size
    if end > len(map_bytes):
        raise ValueError("internal DARE stream past end of map")
    return map_bytes[stream.stream_offset : end]


def _companion_files(folder: Path) -> dict[str, Path]:
    found: dict[str, Path] = {}
    if not folder.is_dir():
        return found
    for path in folder.iterdir():
        if path.is_file() and path.suffix.lower() in {".ss0", ".ls0"}:
            found[path.name.lower()] = path
    return found


def _map_names(kind: str) -> tuple[str, ...]:
    if kind in {"ls0", "lm0"}:
        return ("MAPS.LM0",)
    return ("MAPS.SM0",)


def local_map(folder: Path, kind: str) -> Path | None:
    """Return MAPS.SM0 / MAPS.LM0 if it exists in `folder`."""
    for name in _map_names(kind):
        path = folder / name
        if path.is_file():
            return path
    return None


def _as_map_file(path: Path, kind: str) -> Path | None:
    if path.is_dir():
        return local_map(path, kind)
    want = {name.lower() for name in _map_names(kind)}
    if path.is_file() and path.name.lower() in want:
        return path
    if path.is_file() and path.suffix.lower() in {".sm0", ".lm0"}:
        # Allow an explicit --map even if the filename is not MAPS.*
        if kind in {"ls0", "lm0"} and path.suffix.lower() == ".lm0":
            return path
        if kind in {"ss0", "sm0"} and path.suffix.lower() == ".sm0":
            return path
    return None


def _ask_map_path(kind: str, beside: Path) -> Path:
    need = _map_names(kind)[0]
    print(f"{need} is not in {beside}")
    while True:
        try:
            raw = input(f"Path to {need}: ").strip().strip('"').strip("'")
        except EOFError as exc:
            raise ValueError(f"missing {need}; pass --map") from exc
        if not raw:
            print(f"enter the path to {need}")
            continue
        path = Path(raw).expanduser()
        try:
            path = path.resolve()
        except OSError:
            print(f"not found: {raw}")
            continue
        found = _as_map_file(path, kind)
        if found is None:
            print(f"not a {need}: {path}")
            continue
        return found


def resolve_map_file(
    beside: Path,
    kind: str,
    given: Path | None = None,
    extra: Path | None = None,
) -> Path:
    """Use MAPS next to the SS0/LS0, else `--map`, else ask."""
    if given is not None:
        found = _as_map_file(given.expanduser().resolve(), kind)
        if found is None:
            raise FileNotFoundError(f"--map is not a MAPS.SM0/MAPS.LM0 file: {given}")
        return found
    for folder in (beside, extra):
        if folder is None:
            continue
        found = local_map(folder, kind)
        if found is not None:
            return found
    cache_key = "lm0" if kind in {"ls0", "lm0"} else "sm0"
    cached = _MAP_CACHE.get(cache_key)
    if cached is not None and cached.is_file():
        return cached
    path = _ask_map_path(kind, beside)
    _MAP_CACHE[cache_key] = path
    return path


def extract_dare(src: Path, dest: Path, map_file: Path | None = None) -> int:
    """Extract DARE map/stream audio into WAV/OGG plus `_scct.json`."""
    input_kind = peek_dare(src)
    if input_kind is None:
        raise ValueError(f"{src.name}: not a DARE map or stream")
    dest.mkdir(parents=True, exist_ok=True)
    filter_file = ""
    map_path = src
    rows: list[tuple[str, DareStream]] = []
    if input_kind in {"ss0", "ls0"}:
        filter_file = src.name
        map_path = resolve_map_file(src.parent, input_kind, map_file)
        map_bytes = map_path.read_bytes()
        parsed = parse_map_streams(map_bytes)
        rows = [row for row in parsed if row[1].stream_file.lower() == filter_file.lower()]
        if not rows:
            raise ValueError(f"{src.name} is not referenced by {map_path.name}")
        if map_path.parent.resolve() != src.parent.resolve():
            print(f"using {map_path}")
        folder = map_path.parent
        companions = _companion_files(folder)
        companions[src.name.lower()] = src
    else:
        folder = src.parent
        companions = _companion_files(folder)
        map_bytes = map_path.read_bytes()
        rows = parse_map_streams(map_bytes)
    exports: list[dict[str, Any]] = []
    written: dict[tuple, str] = {}
    for key, stream in rows:
        ident = (stream.stream_file, stream.stream_offset, stream.stream_size)
        if ident in written:
            rel = written[ident]
        else:
            raw = _payload(map_bytes, companions, stream)
            stem = sanitize_name(Path(stream.stream_file).stem if stream.stream_file else "_internal")
            group = dest / EXPORTS_DIR / stem
            group.mkdir(parents=True, exist_ok=True)
            if stream.codec == _CODEC_OGG:
                rel = f"{EXPORTS_DIR}/{stem}/{stream.stream_offset:08x}.ogg"
                (dest / rel).write_bytes(raw)
            elif stream.codec == _CODEC_PCM:
                samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw[: len(raw) // 2 * 2]))
                rel = f"{EXPORTS_DIR}/{stem}/{stream.stream_offset:08x}.wav"
                _write_wav(dest / rel, stream.rate, stream.channels, samples)
            else:
                samples = decode_ima(raw, stream.channels)
                rel = f"{EXPORTS_DIR}/{stem}/{stream.stream_offset:08x}.wav"
                _write_wav(dest / rel, stream.rate, stream.channels, samples)
            written[ident] = rel.replace("\\", "/")
            rel = written[ident]
            stream.wav_sha256 = _sha256((dest / rel).read_bytes())
            stream.raw_sha256 = _sha256(raw)
        exports.append(
            {
                "key": key,
                "file": rel.replace("\\", "/"),
                "codec": stream.codec,
                "rate": stream.rate,
                "channels": stream.channels,
                "samples": stream.samples,
                "streamed": stream.streamed,
                "stream_file": stream.stream_file,
                "stream_offset": stream.stream_offset,
                "stream_size": stream.stream_size,
                "wav_sha256": stream.wav_sha256 or _sha256((dest / rel).read_bytes()),
                "raw_sha256": stream.raw_sha256,
                "headers": stream.headers,
            }
        )
        if len(exports) % 500 == 0:
            print(f"  {len(exports)} streams")
    dump_json(
        dest / MANIFEST_NAME,
        {
            "format": input_kind,
            "version": 1,
            "source_name": map_path.name,
            "source_dir": str(folder.resolve()),
            "filter_file": filter_file,
            "exports": exports,
        },
    )
    return len(exports)


def _extract_loose_stream(src: Path, dest: Path) -> int:
    """Extract an SS0/LS0 that has no companion map (single OGG or IMA blob)."""
    data = src.read_bytes()
    dest.mkdir(parents=True, exist_ok=True)
    (dest / EXPORTS_DIR).mkdir(exist_ok=True)
    if data.startswith(b"OggS"):
        rel = f"{EXPORTS_DIR}/00000000.ogg"
        (dest / rel).write_bytes(data)
        codec = _CODEC_OGG
        rate, channels, samples = 0, 0, 0
    else:
        try:
            parse_header(data, 1)
        except (ValueError, struct.error, IndexError) as exc:
            need = "MAPS.LM0" if src.suffix.lower() == ".ls0" else "MAPS.SM0"
            raise ValueError(
                f"{src.name}: missing {need} (needed to locate streams). "
                f"Copy {need} next to this file or unpack from Data\\Sounds."
            ) from exc
        samples_pcm = decode_ima(data, 1)
        rel = f"{EXPORTS_DIR}/00000000.wav"
        _write_wav(dest / rel, 32000, 1, samples_pcm)
        codec = _CODEC_IMA
        rate, channels, samples = 32000, 1, len(samples_pcm)
    dump_json(
        dest / MANIFEST_NAME,
        {
            "format": src.suffix.lower().lstrip("."),
            "version": 1,
            "source_name": src.name,
            "source_dir": str(src.parent.resolve()),
            "filter_file": src.name,
            "loose": True,
            "exports": [
                {
                    "key": f"{src.name}:0",
                    "file": rel,
                    "codec": codec,
                    "rate": rate,
                    "channels": channels,
                    "samples": samples,
                    "streamed": True,
                    "stream_file": src.name,
                    "stream_offset": 0,
                    "stream_size": len(data),
                    "wav_sha256": _sha256((dest / rel).read_bytes()),
                    "raw_sha256": _sha256(data),
                    "headers": [],
                }
            ],
        },
    )
    return 1


def _encode_payload(item: dict[str, Any], path: Path, original: bytes | None) -> tuple[bytes, int]:
    codec = item["codec"]
    if codec == _CODEC_OGG:
        return path.read_bytes(), int(item.get("samples") or 0)
    rate, channels, samples = _read_wav(path)
    if rate != int(item["rate"]) or channels != int(item["channels"]):
        raise ValueError(
            f"{path.name}: WAV is {rate} Hz {channels}ch, expected {item['rate']} Hz {item['channels']}ch"
        )
    frames = len(samples) // max(channels, 1)
    if codec == _CODEC_PCM:
        return _pcm_bytes(samples), frames
    template = original if original else default_ima_template(int(item["channels"]))
    return encode_ima(samples, template, int(item["channels"])), frames


def _header_frames(item: dict[str, Any]) -> int:
    headers = item.get("headers") or []
    if headers:
        return int(headers[0].get("num_samples") or headers[0].get("num_samples2") or 0)
    return int(item.get("samples") or 0)


def pack_dare(src_dir: Path, dest: Path, map_file: Path | None = None) -> int:
    """Rebuild SM0/LM0 and companion SS0/LS0 from an unpack directory."""
    manifest = load_json(src_dir / MANIFEST_NAME)
    fmt = str(manifest.get("format", "")).lower()
    if fmt not in {"sm0", "lm0", "ss0", "ls0"}:
        raise ValueError(f"{src_dir} is not a DARE unpack directory")
    filter_file = str(manifest.get("filter_file") or "")
    dest.parent.mkdir(parents=True, exist_ok=True)
    write_map = fmt in {"sm0", "lm0"}
    if manifest.get("loose"):
        map_template = dest
        template_dir = src_dir.parent
    else:
        map_template = resolve_map_file(src_dir.parent, fmt, map_file, extra=dest.parent)
        template_dir = map_template.parent
        if map_template.parent.resolve() not in {src_dir.parent.resolve(), dest.parent.resolve()}:
            print(f"using {map_template}")
    companions = _companion_files(template_dir)
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    unchanged = True
    for item in manifest["exports"]:
        path = src_dir / item["file"]
        if not path.is_file():
            raise FileNotFoundError(f"missing audio {path}")
        current = _sha256(path.read_bytes())
        if current != item.get("wav_sha256"):
            unchanged = False
        groups[str(item.get("stream_file") or "")].append(item)
    if unchanged and not manifest.get("loose"):
        if write_map:
            if dest.resolve() != map_template.resolve():
                dest.write_bytes(map_template.read_bytes())
            return len(manifest["exports"])
        src_stream = template_dir / (filter_file or dest.name)
        if src_stream.is_file():
            if dest.resolve() != src_stream.resolve():
                dest.write_bytes(src_stream.read_bytes())
            return len(manifest["exports"])
        # No original SS0/LS0; rebuild from unpacked WAV/OGG below.

    map_bytes = bytearray(map_template.read_bytes())

    rebuilt_files: dict[str, bytes] = {}
    map_dirty = False
    if manifest.get("loose"):
        item = manifest["exports"][0]
        original = (template_dir / item["stream_file"]).read_bytes()
        if _sha256((src_dir / item["file"]).read_bytes()) == item.get("wav_sha256"):
            payload = original
        else:
            payload, _frames = _encode_payload(item, src_dir / item["file"], original)
        dest.write_bytes(payload)
        return 1

    for stream_name, items in groups.items():
        items = sorted(items, key=lambda row: int(row["stream_offset"]))
        unique: list[dict[str, Any]] = []
        seen: set[tuple] = set()
        for item in items:
            ident = (int(item["stream_offset"]), int(item["stream_size"]))
            if ident in seen:
                continue
            seen.add(ident)
            unique.append(item)
        if not stream_name:
            for item in unique:
                path = src_dir / item["file"]
                if _sha256(path.read_bytes()) == item.get("wav_sha256"):
                    continue
                original = bytes(
                    map_bytes[int(item["stream_offset"]) : int(item["stream_offset"]) + int(item["stream_size"])]
                )
                payload, _frames = _encode_payload(item, path, original)
                if len(payload) != int(item["stream_size"]):
                    raise ValueError(
                        f"{path.name}: internal stream size changed {item['stream_size']} -> {len(payload)}"
                    )
                start = int(item["stream_offset"])
                map_bytes[start : start + len(payload)] = payload
                map_dirty = True
            continue
        original_path = companions.get(stream_name.lower())
        original_file = original_path.read_bytes() if original_path is not None else None
        pieces: list[tuple[dict[str, Any], bytes, int]] = []
        for item in unique:
            path = src_dir / item["file"]
            orig_slice: bytes | None = None
            if original_file is not None:
                start = int(item["stream_offset"])
                end = start + int(item["stream_size"])
                if end <= len(original_file):
                    orig_slice = original_file[start:end]
            if orig_slice is not None and _sha256(path.read_bytes()) == item.get("wav_sha256"):
                payload = orig_slice
                frames = _header_frames(item)
            else:
                payload, frames = _encode_payload(item, path, orig_slice)
            pieces.append((item, payload, frames))
        keep_layout = all(len(payload) == int(item["stream_size"]) for item, payload, _frames in pieces)
        new_offsets: list[tuple[dict[str, Any], int, int, int]] = []
        if keep_layout:
            end = max(int(item["stream_offset"]) + len(payload) for item, payload, _frames in pieces)
            buf = bytearray(end)
            for item, payload, frames in pieces:
                off = int(item["stream_offset"])
                buf[off : off + len(payload)] = payload
                new_offsets.append((item, off, len(payload), frames))
            rebuilt_files[stream_name] = bytes(buf)
        else:
            cursor = 0
            chunks: list[bytes] = []
            for item, payload, frames in pieces:
                new_offsets.append((item, cursor, len(payload), frames))
                chunks.append(payload)
                cursor += len(payload)
            rebuilt_files[stream_name] = b"".join(chunks)
        for item, new_off, new_size, frames in new_offsets:
            for header in item.get("headers") or []:
                hdr = int(header["header_offset"])
                map_dirty |= _set_u32(map_bytes, hdr + 0x08, new_size)
                map_dirty |= _set_u32(map_bytes, hdr + 0x10, new_off)
                if item["codec"] == _CODEC_PCM:
                    map_dirty |= _set_u32(map_bytes, hdr + 0x30, frames)
                elif item["codec"] == _CODEC_IMA and frames:
                    map_dirty |= _set_u32(map_bytes, hdr + 0x38, frames)
                elif item["codec"] == _CODEC_OGG and frames:
                    map_dirty |= _set_u32(map_bytes, hdr + 0x30, frames)

    out_dir = dest.parent.resolve()
    live = out_dir == template_dir.resolve()
    if write_map:
        dest.write_bytes(bytes(map_bytes))
        for name, payload in rebuilt_files.items():
            (out_dir / name).write_bytes(payload)
    else:
        payload = rebuilt_files.get(filter_file)
        if payload is None:
            want = (filter_file or dest.name).lower()
            for name, data in rebuilt_files.items():
                if name.lower() == want:
                    payload = data
                    break
        if payload is None:
            raise ValueError(f"no stream data for {filter_file or dest.name}")
        if map_dirty and not live:
            raise ValueError(
                f"{filter_file or dest.name}: stream size/offset changed; "
                f"pack into {template_dir} so {map_template.name} is updated"
            )
        dest.write_bytes(payload)
        if map_dirty:
            map_template.write_bytes(bytes(map_bytes))
            print(f"updated {map_template.name}")
    return len(manifest["exports"])
