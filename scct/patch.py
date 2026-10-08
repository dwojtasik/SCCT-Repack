#!/usr/bin/env python3

"""
patch
~~~~~
Patch splintercell3.exe so UMD archives up to ~4 GiB can be read.
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
    return {
        "chars_off": chars_off,
        "chars": struct.unpack_from("<H", data, chars_off)[0],
        "magic": magic,
        "imgbase": imgbase,
        "checksum_off": checksum_off,
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


def patch_exe(path: Path) -> list[str]:
    """Patch a PE32 `splintercell3.exe` in place.

    A `.bak` copy is written next to the EXE on the first run.

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

    if _is_v2(data) and (pe["chars"] & IMAGE_FILE_LARGE_ADDRESS_AWARE):
        log.append(f"{path}: already patched (v2)")
        return log

    log.extend(_apply(data, pe))
    path.write_bytes(data)
    log.append(f"wrote {path} ({len(data)} bytes)")
    return log
