#!/usr/bin/env python3

"""
remaster_patch
~~~~~~~~~~~~~~
Install the Remaster.dll bloom / glow remaster into splintercell3.exe.

Adds a ``.rmst`` section that holds a copy of the import directory plus an
import of ``Remaster.dll!Remaster_Version``, so the game loads the DLL at
start-up. The DLL patches the game in memory and reads ``Remaster.ini``:

    [Pyramid]   Size of the coarse glow pyramid (bloom and vision modes)
    [Bloom]     glow fixes for every post-process path, modern HDR bloom

The EXE code itself is not modified. Separate from the UMD 4/8 GiB
``patch_exe`` (LAA/SC8G); both patches can be stacked in any order.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import shutil
import struct
from pathlib import Path

IMGBASE = 0x10900000
RMST_NAME = b".rmst\x00\x00\x00"
RMST_CHARACTERISTICS = 0xC0000040  # initialized data | read | write
SCRM_SIG = b"SCRM"  # marker of the previous TLS-based remaster patch
REMASTER_BAK_SUFFIX = ".remaster.bak"

DLL_NAME = "Remaster.dll"
DLL_IMPORT = "Remaster_Version"
INI_NAME = "Remaster.ini"
DLL_SOURCE = Path(__file__).resolve().parent / "data" / DLL_NAME

PYRAMID_MIN_SIZE = 16  # same range as Remaster.dll accepts
PYRAMID_MAX_SIZE = 8192
DEFAULT_PYRAMID_SIZE = 512

REMASTER_INI = """[Pyramid]
; Size of the glow pyramid used by bloom and vision modes
; Original game value: 256
; Qualities: [256, 512, 1024, 2048, 4096] or values in-between
Size={size}

[Bloom]
; 0 = original glow, 1 = remastered glow
Enabled=1
; Anti-aliased scene downsample before the glow is built (no flicker)
FixDownsample=1
; Smooth upsampling of the glow levels (no blocky glow)
SmoothUpsample=1

; Modern bloom for HDR rendering (only used with HDR on)
ModernHDRBloom=1
; Overall strength of the modern HDR bloom
Intensity=0.9
; Weights of the four glow levels
Weight256=1.4
Weight128=1.3
Weight64=1.2
Weight32=1.6
; Weight of the two finer glow levels (0 = off)
FineWeight=0.15
; Upsample radius in texels (0.5 - 1.5)
UpsampleRadius=0.75
; Anti-flicker average on the first downsample
KarisAverage=1
; Bloom resolution: 2 = half screen, 4 = quarter screen
OutputDivisor=2
; Feed eye adaptation from the filtered bloom (no exposure pumping)
FixLuminanceSource=1
; 1 = RGBA16F render targets, 0 = RGBA8
UseFP16=1
"""


def _align(v: int, a: int) -> int:
    return (v + a - 1) // a * a


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


def _pe_fields(data: bytes) -> dict:
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    opt = e_lfanew + 24
    size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    return {
        "e_lfanew": e_lfanew,
        "magic": struct.unpack_from("<H", data, opt)[0],
        "imgbase": struct.unpack_from("<I", data, opt + 28)[0],
        "sect_align": struct.unpack_from("<I", data, opt + 32)[0],
        "file_align": struct.unpack_from("<I", data, opt + 36)[0],
        "size_image_off": opt + 56,
        "size_headers": struct.unpack_from("<I", data, opt + 60)[0],
        "checksum_off": opt + 64,
        "import_dir_off": opt + 96 + 1 * 8,
        "bound_dir_off": opt + 96 + 11 * 8,
        "nsec": struct.unpack_from("<H", data, e_lfanew + 6)[0],
        "sec0": opt + size_opt,
    }


def _sections(data: bytes, pe: dict) -> list[dict]:
    out = []
    for i in range(pe["nsec"]):
        off = pe["sec0"] + i * 40
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
        out.append({"hdr": off, "name": bytes(data[off : off + 8]), "vsize": vsize, "va": va, "rsize": rsize, "raw": raw})
    return out


def _rva_to_off(sections: list[dict], rva: int) -> int:
    for s in sections:
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
            return rva - s["va"] + s["raw"]
    raise RuntimeError(f"RVA {rva:#x} is outside every section")


def _read_cstr(data: bytes, off: int) -> str:
    return data[off : data.index(b"\x00", off)].decode("latin-1")


def _import_descriptors(data: bytes, pe: dict) -> list[bytes]:
    """Return the raw 20-byte import descriptors (without the terminator)."""
    sections = _sections(data, pe)
    rva = struct.unpack_from("<I", data, pe["import_dir_off"])[0]
    off = _rva_to_off(sections, rva)
    out = []
    while True:
        desc = bytes(data[off : off + 20])
        if desc == bytes(20):
            return out
        out.append(desc)
        off += 20


def _imported_dlls(data: bytes, pe: dict) -> list[str]:
    sections = _sections(data, pe)
    names = []
    for desc in _import_descriptors(data, pe):
        name_rva = struct.unpack_from("<I", desc, 12)[0]
        names.append(_read_cstr(data, _rva_to_off(sections, name_rva)))
    return names


def _find_rmst(data: bytes, pe: dict) -> dict | None:
    for s in _sections(data, pe):
        if s["name"] == RMST_NAME:
            return s
    return None


def _add_import_section(data: bytearray, pe: dict) -> int:
    """Append ``.rmst`` with the old import descriptors + Remaster.dll. Returns the section RVA."""
    sections = _sections(data, pe)
    descs = _import_descriptors(data, pe)
    last = sections[-1]
    new_rva = _align(last["va"] + max(last["vsize"], last["rsize"]), pe["sect_align"])
    new_raw = _align(len(data), pe["file_align"])

    hdr = pe["sec0"] + pe["nsec"] * 40
    first_raw = min(s["raw"] for s in sections if s["raw"])
    if hdr + 40 > min(pe["size_headers"], first_raw) or any(data[hdr : hdr + 40]):
        raise RuntimeError("no room for another section header")

    # Layout: descriptors (old + new + terminator) | ILT | IAT | hint/name | DLL name
    body = bytearray(b"".join(descs) + bytes(40))
    ilt_off = len(body)
    body += bytes(8)
    iat_off = len(body)
    body += bytes(8)
    hint_off = len(body)
    body += b"\x00\x00" + DLL_IMPORT.encode("ascii") + b"\x00"
    if len(body) % 2:
        body += b"\x00"
    name_off = len(body)
    body += DLL_NAME.encode("ascii") + b"\x00"
    struct.pack_into("<I", body, ilt_off, new_rva + hint_off)
    struct.pack_into("<I", body, iat_off, new_rva + hint_off)
    struct.pack_into("<IIIII", body, len(descs) * 20, new_rva + ilt_off, 0, 0, new_rva + name_off, new_rva + iat_off)
    vsize = len(body)
    rsize = _align(vsize, pe["file_align"])
    body += bytes(rsize - vsize)

    data.extend(bytes(new_raw - len(data)))
    data.extend(body)
    data[hdr : hdr + 8] = RMST_NAME
    struct.pack_into("<IIIIIIHHI", data, hdr + 8, vsize, new_rva, rsize, new_raw, 0, 0, 0, 0, RMST_CHARACTERISTICS)
    struct.pack_into("<H", data, pe["e_lfanew"] + 6, pe["nsec"] + 1)
    struct.pack_into("<I", data, pe["size_image_off"], _align(new_rva + vsize, pe["sect_align"]))
    struct.pack_into("<II", data, pe["import_dir_off"], new_rva, (len(descs) + 1) * 20)
    struct.pack_into("<II", data, pe["bound_dir_off"], 0, 0)
    return new_rva


def _check_pe(data: bytes, path: Path) -> dict:
    pe = _pe_fields(data)
    if pe["magic"] != 0x10B:
        raise RuntimeError(f"{path}: not PE32")
    if pe["imgbase"] != IMGBASE:
        raise RuntimeError(
            f"{path}: ImageBase {pe['imgbase']:#x} (expected {IMGBASE:#x}). "
            "This remaster patch is only for splintercell3.exe ImageBase 0x10900000."
        )
    return pe


def remaster_bak_path(exe: Path) -> Path:
    return Path(str(exe) + REMASTER_BAK_SUFFIX)


def write_remaster_ini(ini_path: Path, size: int = DEFAULT_PYRAMID_SIZE) -> bool:
    """Write Remaster.ini unless a current one (with ``[Pyramid]``) already exists.

    Args:
        ini_path (Path): Path to Remaster.ini.
        size (int): ``[Pyramid] Size`` written to a new INI.

    Returns:
        bool: True if the INI was written, False if the existing one was kept.
    """
    if ini_path.is_file() and "[pyramid]" in ini_path.read_text(encoding="utf-8", errors="replace").lower():
        return False
    ini_path.write_text(REMASTER_INI.format(size=size), encoding="utf-8")
    return True


def patch_remaster_exe(path: Path, pyramid_size: int = DEFAULT_PYRAMID_SIZE) -> list[str]:
    """Add the Remaster.dll import and write Remaster.dll + Remaster.ini next to the EXE.

    Args:
        path (Path): Path to splintercell3.exe.
        pyramid_size (int): ``[Pyramid] Size`` written to a new Remaster.ini.

    Returns:
        list[str]: Human-readable log lines.

    Raises:
        RuntimeError: If the EXE is not the supported build or the DLL is missing.
    """
    if not PYRAMID_MIN_SIZE <= pyramid_size <= PYRAMID_MAX_SIZE:
        raise RuntimeError(f"Pyramid Size {pyramid_size} invalid (use {PYRAMID_MIN_SIZE}..{PYRAMID_MAX_SIZE})")
    if not DLL_SOURCE.is_file():
        raise RuntimeError(f"bundled {DLL_NAME} not found: {DLL_SOURCE}")

    path = path.resolve()
    bak = remaster_bak_path(path)
    log: list[str] = []
    data = bytearray(path.read_bytes())
    pe = _check_pe(data, path)

    rmst = _find_rmst(data, pe)
    if rmst is not None and DLL_NAME.lower() in (n.lower() for n in _imported_dlls(data, pe)):
        log.append(f"{path.name} already imports {DLL_NAME}")
    else:
        if rmst is not None:
            # Previous TLS remaster patch: start again from its backup
            if not bak.is_file():
                raise RuntimeError(
                    f"{path}: contains the previous remaster patch ({SCRM_SIG.decode()} TLS) "
                    f"and {bak.name} is missing. Restore a clean splintercell3.exe first."
                )
            data = bytearray(bak.read_bytes())
            pe = _check_pe(data, bak)
            if _find_rmst(data, pe) is not None:
                raise RuntimeError(f"{bak}: backup is already patched. Restore a clean splintercell3.exe first.")
            log.append(f"base {bak}")
        else:
            shutil.copy2(path, bak)
            log.append(f"backup {bak}")
        section_rva = _add_import_section(data, pe)
        csum = _pe_checksum(data, pe["checksum_off"])
        struct.pack_into("<I", data, pe["checksum_off"], csum)
        path.write_bytes(data)
        log.append(f"import {DLL_NAME}!{DLL_IMPORT} in .rmst @ {IMGBASE + section_rva:#x}")
        log.append(f"wrote {path} ({len(data)} bytes) CheckSum={csum:#x}")

    dll = path.parent / DLL_NAME
    shutil.copyfile(DLL_SOURCE, dll)
    log.append(f"wrote {dll}")
    ini = path.parent / INI_NAME
    if write_remaster_ini(ini, pyramid_size):
        log.append(f"wrote {ini} [Pyramid] Size={pyramid_size}")
    else:
        log.append(f"kept {ini}")
    return log


def restore_remaster_exe(path: Path) -> list[str]:
    """Restore the EXE from ``.remaster.bak`` and remove Remaster.dll / Remaster.ini.

    Args:
        path (Path): Path to splintercell3.exe.

    Returns:
        list[str]: Human-readable log lines.

    Raises:
        RuntimeError: If the backup is missing.
    """
    path = path.resolve()
    bak = remaster_bak_path(path)
    if not bak.is_file():
        raise RuntimeError(f"backup not found: {bak}")
    shutil.copy2(bak, path)
    log = [f"restored {bak} -> {path}"]
    for name in (DLL_NAME, INI_NAME):
        f = path.parent / name
        if f.is_file():
            f.unlink()
            log.append(f"removed {f}")
    return log
