#!/usr/bin/env python3

"""
patch
~~~~~
Patch splintercell3.exe so UMD3 archives past 4 GiB can be read.

v2: Large Address Aware + unsigned SetFilePointer (high dword 0) so v1/v2
    UMDs work up to the 4 GiB uint32 trailer.

SC8G: Magma 64-bit windowed Serialize/Seek and a 32-byte UMD3 trailer, gated
    so stock / remaster v1 UMDs keep the original Magma path.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import shutil
import struct
from pathlib import Path

IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMGBASE = 0x10900000

CAVE_VA = 0x10FA49C1
CAVE2_OFF = 14
CAVE2_VA = CAVE_VA + CAVE2_OFF

SEEK_SETUP_VA = 0x10909C26  # push 0; push 0; mov esi, ecx
SEEK_CMP_VA = 0x10909C37  # cmp eax, -1; jne success
SEEK_SUCCESS_VA = 0x10909C65
SEEK_ERROR_VA = 0x10909C3C
ATEND_JL_VA = 0x10909A89
STOPPER_JL_VA = 0x10909AB7
REMAIN_JG_VA = 0x1090AC59
CHUNK_JLE_VA = 0x10CD0428  # checksum min(remain, 0x4000); signed -> unsigned

ORIG_SEEK_SETUP = bytes.fromhex("6a006a008bf1")
V1_SEEK_SETUP = bytes.fromhex("e896ad690090")
ORIG_SEEK_CMP = bytes.fromhex("83f8ff7529")
ORIG_ATEND = bytes.fromhex("7c08")
ORIG_STOPPER = bytes.fromhex("7c07")
ORIG_REMAIN = bytes.fromhex("7f02")
ORIG_CHUNK = bytes.fromhex("7e05")


def _rva(va: int) -> int:
    return va - IMGBASE


def _rel32(src_va: int, dst_va: int) -> bytes:
    return struct.pack("<i", dst_va - (src_va + 5))


def _near_jnz(src_va: int, dst_va: int) -> bytes:
    return b"\x0f\x85" + struct.pack("<i", dst_va - (src_va + 6))


CAVE1 = bytes.fromhex(
    "5a"  # pop edx          ; return address from `call`
    "6a00"  # push 0           ; high-dword scratch
    "6a00"  # push 0           ; FILE_BEGIN
    "8d442404"  # lea eax, [esp+4]
    "50"  # push eax         ; lpDistanceToMoveHigh
    "8bf1"  # mov esi, ecx
    "ffe2"  # jmp edx
)
assert len(CAVE1) == CAVE2_OFF

CAVE2 = (
    bytes.fromhex("83c404")  # add esp, 4  ; drop scratch after stdcall
    + bytes.fromhex("83f8ff")  # cmp eax, -1
    + _near_jnz(CAVE2_VA + 6, SEEK_SUCCESS_VA)
    + b"\xe9"
    + _rel32(CAVE2_VA + 12, SEEK_ERROR_VA)
)

V2_SEEK_SETUP = b"\xe8" + _rel32(SEEK_SETUP_VA, CAVE_VA) + b"\x90"
V2_SEEK_CMP = b"\xe9" + _rel32(SEEK_CMP_VA, CAVE2_VA)


def _pe_fields(data: bytes) -> dict:
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    chars_off = coff + 18
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    imgbase = struct.unpack_from("<I", data, opt + 28)[0]
    checksum_off = opt + 64
    size_opt = struct.unpack_from("<H", data, coff + 16)[0]
    return {
        "chars_off": chars_off,
        "chars": struct.unpack_from("<H", data, chars_off)[0],
        "magic": magic,
        "imgbase": imgbase,
        "checksum_off": checksum_off,
        "text_vsize_off": coff + 20 + size_opt + 8,
    }


def _pe_checksum(buf: bytearray, checksum_off: int) -> int:
    struct.pack_into("<I", buf, checksum_off, 0)
    total = 0
    n = len(buf)
    i = 0
    while i + 1 < n:
        total += buf[i] | (buf[i + 1] << 8)
        total = (total & 0xFFFF) + (total >> 16)
        i += 2
    if n & 1:
        total += buf[-1]
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (total & 0xFFFF) + n


def _is_v2(data: bytes) -> bool:
    return data[_rva(SEEK_SETUP_VA) : _rva(SEEK_SETUP_VA) + 6] == V2_SEEK_SETUP and data[
        _rva(CAVE_VA) : _rva(CAVE_VA) + len(CAVE1)
    ] == CAVE1


def _original_ok(data: bytes) -> bool:
    return data[_rva(SEEK_SETUP_VA) : _rva(SEEK_SETUP_VA) + 6] in (
        ORIG_SEEK_SETUP,
        V1_SEEK_SETUP,
        V2_SEEK_SETUP,
    )


def _apply(data: bytearray, pe: dict) -> list[str]:
    log: list[str] = []
    chars = pe["chars"]
    if not (chars & IMAGE_FILE_LARGE_ADDRESS_AWARE):
        struct.pack_into("<H", data, pe["chars_off"], chars | IMAGE_FILE_LARGE_ADDRESS_AWARE)
        log.append(f"Characteristics {chars:#x} -> {chars | IMAGE_FILE_LARGE_ADDRESS_AWARE:#x} (LARGEADDRESSAWARE)")
    else:
        log.append("LAA already set")

    def put(va: int, allowed: tuple[bytes, ...], new: bytes, name: str) -> None:
        off = _rva(va)
        cur = bytes(data[off : off + len(new)])
        if cur == new:
            log.append(f"{name}: already patched")
            return
        if cur not in allowed:
            raise RuntimeError(f"{name}: unexpected {cur.hex()} at {va:#x}")
        data[off : off + len(new)] = new
        log.append(f"{name}: {cur.hex()} -> {new.hex()} @ {va:#x}")

    put(SEEK_SETUP_VA, (ORIG_SEEK_SETUP, V1_SEEK_SETUP, V2_SEEK_SETUP), V2_SEEK_SETUP, "Seek arg setup")
    put(SEEK_CMP_VA, (ORIG_SEEK_CMP, V2_SEEK_CMP), V2_SEEK_CMP, "Seek post-call")
    cave_len = len(CAVE1) + len(CAVE2)
    cave_new = CAVE1 + CAVE2
    cave_off = _rva(CAVE_VA)
    cur_cave = bytes(data[cave_off : cave_off + cave_len])
    if cur_cave != cave_new:
        data[cave_off : cave_off + cave_len] = cave_new
        log.append(f"Seek trampoline cave v2 @ {CAVE_VA:#x} ({cave_len} bytes)")
    else:
        log.append("Seek trampoline cave: already v2")

    put(ATEND_JL_VA, (ORIG_ATEND, b"\x72\x08"), b"\x72\x08", "AtEnd jl->jb")
    put(STOPPER_JL_VA, (ORIG_STOPPER, b"\x72\x07"), b"\x72\x07", "Close jl->jb")
    put(REMAIN_JG_VA, (ORIG_REMAIN, b"\x77\x02"), b"\x77\x02", "Read remaining jg->ja")
    put(CHUNK_JLE_VA, (ORIG_CHUNK, b"\x76\x05"), b"\x76\x05", "UMD checksum chunk jle->jbe")

    csum = _pe_checksum(data, pe["checksum_off"])
    struct.pack_into("<I", data, pe["checksum_off"], csum)
    log.append(f"PE CheckSum {csum:#x}")
    return log


SC8G_SIG = b"SC8G"
SC8G_CAVE_VA = 0x110B644A
SC8G_CAVE_MAX = 0xBB6
UMD3_MAGIC = 0x33444D55
IAT_READ_FILE = 0x110B72CC
IAT_SET_FILE_POINTER = 0x110B73C4
WIN_SERIALIZE = 0x10CCFED0
WIN_SERIALIZE_CONT = 0x10CCFED6
WIN_SEEK = 0x10CCFEA0
WIN_SEEK_CONT = 0x10CCFEA6
UMD_AFTER_TRAILER = 0x10CD0385
UMD_AFTER_TRAILER_CONT = 0x10CD038B
CREATE_READER_ALLOC = 0x10CCFE06
CREATE_READER_ALLOC_CONT = 0x10CCFE0D
CREATE_READER_COPY = 0x10CCFE15
CREATE_READER_CTOR = 0x10CCFE1E
FARCHIVE_CTOR = 0x10909B30
UMD_SEEK_TOC = 0x10CD0446
AFTER_CREATE_READER_COPY = 0x10CCFE2F
ALLOC_TAG = 0x110B78AC

_SC8G_SITES = {
    WIN_SERIALIZE: (bytes.fromhex("56 8bf1 8b463c"), 6),
    WIN_SEEK: (bytes.fromhex("8b542404 8bc1"), 6),
    CREATE_READER_ALLOC: (bytes.fromhex("68 ac780b11 6a4c"), 7),
    CREATE_READER_COPY: (bytes.fromhex("55 8b6f10 8b7f0c 8bce"), 9),
    UMD_AFTER_TRAILER: (bytes.fromhex("8b4634 83c408"), 6),
}


class _Cave:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.names: dict[str, int] = {}

    def va(self) -> int:
        return SC8G_CAVE_VA + len(self.buf)

    def emit(self, b: bytes) -> int:
        a = self.va()
        self.buf.extend(b)
        return a

    def label(self, name: str) -> int:
        self.names[name] = self.va()
        return self.va()

    def call(self, dst: int) -> None:
        src = self.va()
        self.emit(b"\xe8" + _rel32(src, dst))

    def jmp(self, dst: int) -> None:
        self.emit(b"\xe9" + _rel32(self.va(), dst))

    def patch_u8(self, off: int, val: int) -> None:
        self.buf[off] = val & 0xFF


def _build_sc8g_cave() -> tuple[bytes, dict[str, int]]:
    """Magma 64-bit I/O cave. Trampolines to original bytes unless extra==UMD3."""
    c = _Cave()
    c.label("sig")
    c.emit(SC8G_SIG)
    n = c.names

    def sfp_begin() -> None:
        c.emit(b"\x53")
        c.emit(b"\x8b\xd9")
        c.emit(b"\x52")
        c.emit(b"\x6a\x00")
        c.emit(b"\x8d\x4c\x24\x04")
        c.emit(b"\x51")
        c.emit(b"\x50")
        c.emit(b"\x53")
        c.emit(b"\xff\x15" + struct.pack("<I", IAT_SET_FILE_POINTER))
        c.emit(b"\x83\xc4\x04")
        c.emit(b"\x5b")

    c.label("serialize")
    c.emit(b"\x8b\x41\x3c")
    c.emit(b"\x81\x78\x0c" + struct.pack("<I", UMD3_MAGIC))
    jz = len(c.buf)
    c.emit(b"\x74\x00")
    c.emit(b"\x56\x8b\xf1\x8b\x46\x3c")
    c.jmp(WIN_SERIALIZE_CONT)
    c.label("ser_v3")
    c.patch_u8(jz + 1, n["ser_v3"] - (SC8G_CAVE_VA + jz + 2))
    c.emit(b"\x56\x57\x53\x8b\xf1")
    c.emit(b"\x8b\x46\x3c\x39\x70\x30")
    je_rd = len(c.buf)
    c.emit(b"\x74\x00")
    c.emit(b"\x89\x70\x30")
    c.emit(b"\x8b\x46\x3c\x8b\x40\x2c\x8b\x48\x3c")
    c.emit(b"\x8b\x46\x48\x03\x46\x40\x8b\x56\x4c\x83\xd2\x00")
    sfp_begin()
    c.label("ser_read")
    c.patch_u8(je_rd + 1, n["ser_read"] - (SC8G_CAVE_VA + je_rd + 2))
    c.emit(b"\x8b\x46\x3c\x8b\x40\x2c\x8b\x58\x3c")
    c.emit(b"\x8b\x7c\x24\x14")
    c.emit(b"\x8b\x44\x24\x10")
    c.emit(b"\x6a\x00")
    c.emit(b"\x6a\x00")
    c.emit(b"\x8d\x4c\x24\x04")
    c.emit(b"\x51\x57\x50\x53")
    c.emit(b"\xff\x15" + struct.pack("<I", IAT_READ_FILE))
    c.emit(b"\x83\xc4\x04")
    c.emit(b"\x01\x7e\x48")
    c.emit(b"\x5b\x5f\x5e\xc2\x08\x00")

    c.label("win_seek")
    c.emit(b"\x8b\x41\x3c")
    c.emit(b"\x81\x78\x0c" + struct.pack("<I", UMD3_MAGIC))
    jz = len(c.buf)
    c.emit(b"\x74\x00")
    c.emit(b"\x8b\x54\x24\x04\x8b\xc1")
    c.jmp(WIN_SEEK_CONT)
    c.label("seek_v3")
    c.patch_u8(jz + 1, n["seek_v3"] - (SC8G_CAVE_VA + jz + 2))
    c.emit(b"\x56\x53\x8b\xf1")
    c.emit(b"\x8b\x44\x24\x0c\x89\x46\x48")
    c.emit(b"\x8b\x56\x3c\x39\x72\x30")
    jnz = len(c.buf)
    c.emit(b"\x75\x00")
    c.emit(b"\x8b\x4a\x2c\x8b\x49\x3c")
    c.emit(b"\x8b\x46\x48\x03\x46\x40\x8b\x56\x4c\x83\xd2\x00")
    sfp_begin()
    c.label("seek_done")
    c.patch_u8(jnz + 1, n["seek_done"] - (SC8G_CAVE_VA + jnz + 2))
    c.emit(b"\x5b\x5e\xc2\x04\x00")

    c.label("alloc_size")
    c.emit(b"\x68" + struct.pack("<I", ALLOC_TAG))
    c.emit(b"\x81\x7b\x0c" + struct.pack("<I", UMD3_MAGIC))
    jnz = len(c.buf)
    c.emit(b"\x75\x00")
    c.emit(b"\x6a\x50")
    c.jmp(CREATE_READER_ALLOC_CONT)
    c.label("alloc_v1")
    c.patch_u8(jnz + 1, n["alloc_v1"] - (SC8G_CAVE_VA + jnz + 2))
    c.emit(b"\x6a\x4c")
    c.jmp(CREATE_READER_ALLOC_CONT)

    c.label("create_reader")
    c.emit(b"\x81\x7b\x0c" + struct.pack("<I", UMD3_MAGIC))
    jnz = len(c.buf)
    c.emit(b"\x75\x00")
    c.emit(b"\x55\x8b\x6f\x10\xff\x77\x14\x8b\x7f\x0c\x8b\xce")
    c.call(FARCHIVE_CTOR)
    c.emit(b"\x89\x6e\x44\x5d\x89\x6e\x4c\x5d")
    c.emit(b"\xb8\x01\x00\x00\x00\x89\x7e\x40")
    c.jmp(AFTER_CREATE_READER_COPY)
    c.label("copy_v1")
    c.patch_u8(jnz + 1, n["copy_v1"] - (SC8G_CAVE_VA + jnz + 2))
    c.emit(b"\x55\x8b\x6f\x10\x8b\x7f\x0c\x8b\xce")
    c.jmp(CREATE_READER_CTOR)

    c.label("after_trailer")
    c.emit(b"\x83\x7e\x18\x01")
    je1 = len(c.buf)
    c.emit(b"\x74\x00")
    c.emit(b"\x83\x7e\x18\x02")
    je2 = len(c.buf)
    c.emit(b"\x74\x00")
    c.emit(b"\x55\x8b\xec\x83\xec\x28")
    c.emit(b"\x8b\x4e\x2c\x8b\x49\x3c")
    c.emit(b"\x53\x8b\xd9")
    c.emit(b"\x6a\xff\x6a\x02\x8d\x4c\x24\x04\x51\x6a\xe0\x53")
    c.emit(b"\xff\x15" + struct.pack("<I", IAT_SET_FILE_POINTER))
    c.emit(b"\x83\xc4\x04\x5b")
    c.emit(b"\x8b\x4e\x2c\x8b\x49\x3c")
    c.emit(b"\x6a\x00\x8d\x45\xf8\x50\x6a\x20\x8d\x45\xd8\x50\x51")
    c.emit(b"\xff\x15" + struct.pack("<I", IAT_READ_FILE))
    c.emit(b"\x83\x7d\xf8\x20")
    jne_v1 = len(c.buf)
    c.emit(b"\x75\x00")
    c.emit(b"\x81\x7d\xf0" + struct.pack("<I", UMD3_MAGIC))
    jne_v1b = len(c.buf)
    c.emit(b"\x75\x00")
    c.emit(b"\x8b\x45\xd8\x89\x46\x10")
    c.emit(b"\xc7\x46\x0c" + struct.pack("<I", UMD3_MAGIC))
    c.emit(b"\x8b\x45\xe0\x89\x46\x14")
    c.emit(b"\x8b\x45\xe8\x89\x46\x18")
    c.emit(b"\x8b\x45\xec\x89\x46\x1c")
    c.emit(b"\x8b\xe5\x5d\x83\xc4\x08")
    c.jmp(UMD_SEEK_TOC)
    c.label("umd_v1")
    for site in (je1, je2, jne_v1, jne_v1b):
        dist = n["umd_v1"] - (SC8G_CAVE_VA + site + 2)
        if not 0 <= dist <= 127:
            raise RuntimeError(f"after_trailer rel8 {dist}")
        c.patch_u8(site + 1, dist)
    c.emit(b"\x8b\x46\x34\x83\xc4\x08")
    c.jmp(UMD_AFTER_TRAILER_CONT)

    if len(c.buf) > SC8G_CAVE_MAX:
        raise RuntimeError(f"SC8G cave {len(c.buf):#x} exceeds {SC8G_CAVE_MAX:#x}")
    n["size"] = len(c.buf)
    return bytes(c.buf), n


def _jmp_fill(data: bytearray, src: int, dst: int, fill: int) -> None:
    payload = b"\xe9" + _rel32(src, dst) + b"\x90" * (fill - 5)
    off = _rva(src)
    data[off : off + fill] = payload


def _apply_sc8g(data: bytearray, pe: dict) -> list[str]:
    """Hook Magma windowed I/O and UMD trailer for UMD3 (64-bit offsets)."""
    log: list[str] = []
    if not _is_v2(data):
        raise RuntimeError("SC8G requires the v2 unsigned-seek patch first")
    for va, (want, _fill) in _SC8G_SITES.items():
        got = bytes(data[_rva(va) : _rva(va) + len(want)])
        if got != want:
            raise RuntimeError(f"unexpected Magma bytes at {va:#x}: {got.hex()} != {want.hex()}")
    pad = bytes(data[_rva(SC8G_CAVE_VA) : _rva(SC8G_CAVE_VA) + 16])
    if pad != b"\x00" * 16:
        raise RuntimeError(f"SC8G cave {SC8G_CAVE_VA:#x} is not padding ({pad.hex()})")
    text_chars = struct.unpack_from("<I", data, pe["text_vsize_off"] + 28)[0]
    if text_chars & 0x80000000:
        raise RuntimeError(f".text is writable ({text_chars:#x}); refusing IMAGE_SCN_MEM_WRITE")

    cave, names = _build_sc8g_cave()
    data[_rva(SC8G_CAVE_VA) : _rva(SC8G_CAVE_VA) + len(cave)] = cave
    _jmp_fill(data, WIN_SERIALIZE, names["serialize"], fill=6)
    _jmp_fill(data, WIN_SEEK, names["win_seek"], fill=6)
    _jmp_fill(data, CREATE_READER_ALLOC, names["alloc_size"], fill=7)
    _jmp_fill(data, CREATE_READER_COPY, names["create_reader"], fill=9)
    _jmp_fill(data, UMD_AFTER_TRAILER, names["after_trailer"], fill=6)

    csum = _pe_checksum(data, pe["checksum_off"])
    struct.pack_into("<I", data, pe["checksum_off"], csum)
    log.append(f"SC8G Magma cave {names['size']} bytes @ {SC8G_CAVE_VA:#x}")
    log.append("Magma Serialize/Seek/CreateReader trampoline unless UMD extra==UMD3")
    log.append("v1/v2 trailer unchanged; UMD3 only if trailer version is not 1 or 2")
    log.append(f"PE CheckSum {csum:#x}")
    return log


def _is_sc8g(data: bytes) -> bool:
    return bytes(data[_rva(SC8G_CAVE_VA) : _rva(SC8G_CAVE_VA) + 4]) == SC8G_SIG


def patch_exe(path: Path) -> list[str]:
    """Patch a PE32 `splintercell3.exe` in place.

    A `.bak` copy of the original file is written next to the EXE on the first
    run. Applies the 4 GiB unsigned-seek patch, then Magma 64-bit I/O (SC8G)
    for UMD3 archives past 4 GiB (including files larger than 8 GiB).
    Stock and v1/v2 UMDs keep the original Magma path.

    Args:
        path (Path): Path to `splintercell3.exe`.

    Returns:
        list[str]: Human-readable log lines.

    Raises:
        RuntimeError: If the EXE is not the expected PE32 build.
    """
    data = bytearray(path.read_bytes())
    pe = _pe_fields(data)
    if pe["magic"] != 0x10B:
        raise RuntimeError(f"{path}: not PE32")
    if pe["imgbase"] != IMGBASE:
        raise RuntimeError(
            f"{path}: ImageBase {pe['imgbase']:#x} (expected {IMGBASE:#x}). "
            "This patch is only for splintercell3.exe with ImageBase 0x10900000."
        )
    if not _original_ok(data):
        site = data[_rva(SEEK_SETUP_VA) : _rva(SEEK_SETUP_VA) + 6]
        raise RuntimeError(f"{path}: Seek site not recognized ({site.hex()})")

    bak = path.with_suffix(path.suffix + ".bak")
    log: list[str] = []
    if bak.exists():
        log.append(f"backup exists {bak}")
    else:
        shutil.copy2(path, bak)
        log.append(f"backup {bak}")

    already_v2 = _is_v2(data) and (pe["chars"] & IMAGE_FILE_LARGE_ADDRESS_AWARE)
    already_sc8g = _is_sc8g(data)
    if already_v2 and already_sc8g:
        log.append(f"{path}: already patched (v2 + SC8G Magma 64-bit I/O)")
        return log

    if not already_v2:
        log.extend(_apply(data, pe))
    else:
        log.append("v2 unsigned seek / LAA: already patched")

    if not already_sc8g:
        log.extend(_apply_sc8g(data, pe))
    else:
        log.append("SC8G Magma 64-bit I/O: already patched")

    path.write_bytes(data)
    log.append(f"wrote {path} ({len(data)} bytes)")
    return log
