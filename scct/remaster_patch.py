#!/usr/bin/env python3

"""
remaster_patch
~~~~~~~~~~~~~~
Install the Remaster.dll bloom / glow remaster for splintercell3.exe.

The DLL is written next to the EXE as ``AVIFIL32.dll``: the game imports that
Windows DLL, so it loads the remaster, which forwards every AVIFIL32 call to
the real one in the system directory. The EXE itself is not modified, so the
signed Ubisoft Connect EXE keeps working, as does the Steam EXE. Settings are
read from ``Remaster.ini``:

    [Pyramid]   Size of the coarse glow pyramid (bloom and vision modes)
    [Bloom]     glow fixes for every post-process path, modern HDR bloom

Older versions added a ``Remaster.dll`` import to the EXE (``.rmst`` section,
backup ``.remaster.bak``); such an EXE is restored from its backup first.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import shutil
import struct
from pathlib import Path

IMGBASE = 0x10900000
RMST_NAME = b".rmst\x00\x00\x00"  # section added by older versions of this patch
REMASTER_BAK_SUFFIX = ".remaster.bak"

DLL_SOURCE = Path(__file__).resolve().parent / "data" / "Remaster.dll"
PROXY_NAME = "AVIFIL32.dll"  # name the game loads the remaster under
LEGACY_DLL_NAME = "Remaster.dll"  # name used by older versions
DLL_MARKER = "Remaster_Version"  # export that identifies the remaster DLL
INI_NAME = "Remaster.ini"

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


def _pe_fields(data: bytes) -> dict:
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    opt = e_lfanew + 24
    size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    return {
        "magic": struct.unpack_from("<H", data, opt)[0],
        "imgbase": struct.unpack_from("<I", data, opt + 28)[0],
        "export_dir_off": opt + 96,
        "nsec": struct.unpack_from("<H", data, e_lfanew + 6)[0],
        "sec0": opt + size_opt,
    }


def _sections(data: bytes, pe: dict) -> list[dict]:
    out = []
    for i in range(pe["nsec"]):
        off = pe["sec0"] + i * 40
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
        out.append({"name": bytes(data[off : off + 8]), "vsize": vsize, "va": va, "rsize": rsize, "raw": raw})
    return out


def _rva_to_off(sections: list[dict], rva: int) -> int:
    for s in sections:
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
            return rva - s["va"] + s["raw"]
    raise RuntimeError(f"RVA {rva:#x} is outside every section")


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


def _has_rmst(data: bytes, pe: dict) -> bool:
    return any(s["name"] == RMST_NAME for s in _sections(data, pe))


def _export_names(data: bytes) -> list[str]:
    """Return the exported names of a PE32 DLL (empty if it has none or is not PE32)."""
    try:
        pe = _pe_fields(data)
        if pe["magic"] != 0x10B:
            return []
        rva = struct.unpack_from("<I", data, pe["export_dir_off"])[0]
        if not rva:
            return []
        sections = _sections(data, pe)
        off = _rva_to_off(sections, rva)
        count, names_rva = struct.unpack_from("<I", data, off + 24)[0], struct.unpack_from("<I", data, off + 32)[0]
        names_off = _rva_to_off(sections, names_rva)
        out = []
        for i in range(count):
            name_off = _rva_to_off(sections, struct.unpack_from("<I", data, names_off + 4 * i)[0])
            out.append(data[name_off : data.index(b"\x00", name_off)].decode("latin-1"))
        return out
    except (struct.error, ValueError, RuntimeError):
        return []


def is_remaster_dll(path: Path) -> bool:
    """True if ``path`` is a Remaster.dll build (any name, any version)."""
    return path.is_file() and DLL_MARKER in _export_names(path.read_bytes())


def remaster_bak_path(exe: Path) -> Path:
    return Path(str(exe) + REMASTER_BAK_SUFFIX)


def _restore_legacy_exe(path: Path, log: list[str]) -> None:
    """Undo the EXE patch of older versions (``.rmst`` section) from ``.remaster.bak``."""
    data = path.read_bytes()
    if not _has_rmst(data, _check_pe(data, path)):
        return
    bak = remaster_bak_path(path)
    if not bak.is_file():
        raise RuntimeError(
            f"{path}: was patched by an older remaster version and {bak.name} is missing. "
            "Restore a clean splintercell3.exe first."
        )
    if _has_rmst(bak.read_bytes(), _check_pe(bak.read_bytes(), bak)):
        raise RuntimeError(f"{bak}: backup is already patched. Restore a clean splintercell3.exe first.")
    shutil.copy2(bak, path)
    bak.unlink()
    log.append(f"restored {path.name} from {bak.name} (the EXE is no longer modified)")


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
    """Write the remaster (AVIFIL32.dll) and Remaster.ini next to splintercell3.exe.

    Args:
        path (Path): Path to splintercell3.exe.
        pyramid_size (int): ``[Pyramid] Size`` written to a new Remaster.ini.

    Returns:
        list[str]: Human-readable log lines.

    Raises:
        RuntimeError: If the EXE is not the supported build, the DLL is missing, or a
            foreign AVIFIL32.dll is already next to the EXE.
    """
    if not PYRAMID_MIN_SIZE <= pyramid_size <= PYRAMID_MAX_SIZE:
        raise RuntimeError(f"Pyramid Size {pyramid_size} invalid (use {PYRAMID_MIN_SIZE}..{PYRAMID_MAX_SIZE})")
    if not DLL_SOURCE.is_file():
        raise RuntimeError(f"bundled {DLL_SOURCE.name} not found: {DLL_SOURCE}")

    path = path.resolve()
    log: list[str] = []
    _check_pe(path.read_bytes(), path)
    proxy = path.parent / PROXY_NAME
    if proxy.is_file() and not is_remaster_dll(proxy):
        raise RuntimeError(f"{proxy} exists and is not the remaster DLL; move it away first")

    _restore_legacy_exe(path, log)
    legacy = path.parent / LEGACY_DLL_NAME
    if is_remaster_dll(legacy):
        legacy.unlink()
        log.append(f"removed {legacy} (older version)")

    shutil.copyfile(DLL_SOURCE, proxy)
    log.append(f"wrote {proxy}")
    ini = path.parent / INI_NAME
    if write_remaster_ini(ini, pyramid_size):
        log.append(f"wrote {ini} [Pyramid] Size={pyramid_size}")
    else:
        log.append(f"kept {ini}")
    return log


def restore_remaster_exe(path: Path) -> list[str]:
    """Remove the remaster (AVIFIL32.dll, Remaster.ini) and undo EXE patches of older versions.

    Args:
        path (Path): Path to splintercell3.exe.

    Returns:
        list[str]: Human-readable log lines.

    Raises:
        RuntimeError: If an EXE patched by an older version has no backup.
    """
    path = path.resolve()
    log: list[str] = []
    _restore_legacy_exe(path, log)
    for name in (PROXY_NAME, LEGACY_DLL_NAME):
        f = path.parent / name
        if is_remaster_dll(f):
            f.unlink()
            log.append(f"removed {f}")
    ini = path.parent / INI_NAME
    if ini.is_file():
        ini.unlink()
        log.append(f"removed {ini}")
    if not log:
        log.append(f"remaster is not installed next to {path}")
    return log
