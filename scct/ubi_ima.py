#!/usr/bin/env python3

"""
ubi_ima
~~~~~~~
Ubisoft IMA ADPCM (vgmstream coding_UBI_IMA), used in Chaos Theory SS0 streams.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

_STEP = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
)
_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def _clamp16(value: int) -> int:
    if value < -32768:
        return -32768
    if value > 32767:
        return 32767
    return value


def _expand(nibble: int, hist: int, index: int) -> tuple[int, int]:
    step = _STEP[index]
    delta = nibble & 7
    delta = ((delta * 2 + 1) * step) >> 3
    if nibble & 8:
        delta = -delta
    hist = _clamp16(hist + delta)
    index += _INDEX[nibble]
    if index < 0:
        index = 0
    if index > 88:
        index = 88
    return hist, index


def _nibble_from_sample(sample: int, hist: int, index: int) -> tuple[int, int, int]:
    step = _STEP[index]
    diff = sample - hist
    nibble = 0
    if diff < 0:
        nibble = 8
        diff = -diff
    if diff >= step:
        nibble |= 4
        diff -= step
    if diff >= (step >> 1):
        nibble |= 2
        diff -= step >> 1
    if diff >= (step >> 2):
        nibble |= 1
    hist, index = _expand(nibble, hist, index)
    return nibble, hist, index


@dataclass
class UbiImaHeader:
    """Parsed UBI IMA stream header."""

    version: int
    channels: int
    header_size: int
    header_samples: int
    hist: list[int]
    step: list[int]


def parse_header(data: bytes, channels: int = 1) -> UbiImaHeader:
    """Parse a UBI IMA header and return the byte size of that header."""
    if not data:
        raise ValueError("empty UBI IMA stream")
    version = data[0]
    offset = 0
    if version == 0:
        version = 7
    else:
        offset = 2
    if version < 1 or version > 8:
        raise ValueError(f"unsupported UBI IMA version {data[0]}")
    header_samples = struct.unpack_from("<h", data, offset + 0x0C)[0]
    if header_samples < 1 or header_samples > 32:
        raise ValueError(f"bad UBI IMA header sample count {header_samples}")
    hist: list[int] = []
    step: list[int] = []
    for channel in range(channels):
        hist.append(struct.unpack_from("<h", data, offset + 0x0E + channel * 4)[0])
        step.append(data[offset + 0x10 + channel * 4])
    offset += 0x0E + 8
    if version >= 3:
        offset += 4
    if version == 6:
        seek_size = struct.unpack_from("<i", data, offset + 4)[0]
        offset += 8 + max(seek_size, 0)
    elif version >= 7:
        offset += 6
        seek_entries = struct.unpack_from("<i", data, offset)[0]
        if seek_entries:
            offset += 4 + (seek_entries + 1) * 8
    offset += header_samples * channels * 2
    if offset > len(data):
        raise ValueError("truncated UBI IMA header")
    return UbiImaHeader(version, channels, offset, header_samples, hist, step)


def decode_ima(data: bytes, channels: int = 1) -> list[int]:
    """Decode a UBI IMA stream to interleaved PCM16 samples."""
    hdr = parse_header(data, channels)
    samples: list[int] = []
    pcm_off = hdr.header_size - hdr.header_samples * channels * 2
    for i in range(hdr.header_samples):
        for channel in range(channels):
            samples.append(struct.unpack_from("<h", data, pcm_off + (i * channels + channel) * 2)[0])
    hist = list(hdr.hist)
    step = list(hdr.step)
    ima = data[hdr.header_size :]
    if channels == 1:
        total = len(ima) * 2
        for i in range(total):
            byte = ima[i >> 1]
            nibble = (byte >> 4) if (i % 2 == 0) else (byte & 0x0F)
            hist[0], step[0] = _expand(nibble, hist[0], step[0])
            samples.append(hist[0])
    else:
        for byte in ima:
            high, low = byte >> 4, byte & 0x0F
            hist[0], step[0] = _expand(high, hist[0], step[0])
            hist[1], step[1] = _expand(low, hist[1], step[1])
            samples.append(hist[0])
            samples.append(hist[1])
    return samples


def default_ima_template(channels: int = 1, header_samples: int = 10) -> bytes:
    """Build a UBI IMA v5 header when the original encoded stream is missing."""
    channels = max(channels, 1)
    header_samples = max(1, min(header_samples, 32))
    out = bytearray(128 + header_samples * channels * 2)
    out[0] = 5
    offset = 2
    struct.pack_into("<h", out, offset + 0x0C, header_samples)
    offset += 0x0E + 8
    offset += 4
    size = offset + header_samples * channels * 2
    return bytes(out[:size])


def encode_ima(samples: list[int], template: bytes, channels: int = 1) -> bytes:
    """Encode interleaved PCM16 as UBI IMA, reusing `template`'s header bytes."""
    hdr = parse_header(template, channels)
    if len(samples) < hdr.header_samples * channels:
        raise ValueError("WAV is shorter than the UBI IMA header PCM")
    header = bytearray(template[: hdr.header_size])
    pcm_off = hdr.header_size - hdr.header_samples * channels * 2
    for i, sample in enumerate(samples[: hdr.header_samples * channels]):
        struct.pack_into("<h", header, pcm_off + i * 2, int(sample))
    hist = list(hdr.hist)
    step = list(hdr.step)
    rest = samples[hdr.header_samples * channels :]
    out = bytearray(header)
    if channels == 1:
        packed = bytearray()
        for i, sample in enumerate(rest):
            nibble, hist[0], step[0] = _nibble_from_sample(int(sample), hist[0], step[0])
            if i % 2 == 0:
                packed.append(nibble << 4)
            else:
                packed[-1] |= nibble
        out += packed
    else:
        frames = len(rest) // 2
        packed = bytearray()
        for i in range(frames):
            n_l, hist[0], step[0] = _nibble_from_sample(int(rest[i * 2]), hist[0], step[0])
            n_r, hist[1], step[1] = _nibble_from_sample(int(rest[i * 2 + 1]), hist[1], step[1])
            packed.append(((n_l & 0x0F) << 4) | (n_r & 0x0F))
        out += packed
    return bytes(out)
