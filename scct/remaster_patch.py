#!/usr/bin/env python3

"""
remaster_patch
~~~~~~~~~~~~~~
Install Remaster.dll for splintercell3.exe.

The DLL is written next to the EXE as ``AVIFIL32.dll``: the game imports that
Windows DLL, so it loads the remaster, which forwards every AVIFIL32 call to
the real one in the system directory. The EXE itself is not modified, so the
signed Ubisoft Connect EXE keeps working, as does the Steam EXE. Settings are
read from ``Remaster.ini``:

    [Pyramid]   Size of the coarse glow pyramid (bloom and vision modes)
    [Bloom]     glow fixes for every post-process path, modern HDR bloom
    [Camera]    DisableCargoShipSway - turn off cargo-ship camera rocking
    [Physics]   HighFpsForcedTickrate - hanging-lamp / cable Tick Hz (0 = original)
    [UI]        DisplayDialogs - spoken conversation overlay (language from Settings.ini)

4K Bink support is always on (no INI key).

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

[Camera]
; 0 = keep cargo-ship camera rocking (original)
; 1 = disable it (clears bCameraRocking on 02_CargoShip)
DisableCargoShipSway=0

[Physics]
; EXPERIMENTAL OPTION !
; Tick frequency for hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)
; Default is ~35 Hz
; 0 = original high-FPS tick behaviour.
HighFpsForcedTickrate=35

[UI]
; 1 = show spoken conversation text at the bottom of the screen
; Uses language from System/Settings.ini
; When native languages are enabled still displays set language from Settings.ini
DisplayDialogs=1
; Leave empty value to synchronize language with Settings.ini
; Can override language of dialogs with value of: deu, esp, fra, int, ita
OverrideDialogsLanguage=
; 1 = additionally displays also chat box comms (Lambert, Grim, etc)
DialogShowEchelonComms=0
; 1 = shows interrogation subtitles
DialogShowInterrogations=1

; Gap from the bottom of the text to the bottom of the current screen resolution
DialogBottomPaddingPercent=5
; Max width of one text line as % of the current screen resolution
DialogMaxWidthSizePercent=50

DialogFontSize=38
DialogBold=1
; 0 = invisible, 1-255 = text alpha
DialogAlpha=255
DialogColor=FFFFFF
; 0 = no backdrop, 1-255 = backdrop alpha
DialogBackgroundAlpha=0
DialogBackgroundColor=000000

; 1 = hide the SPEAKER: prefix from the overlay
HideSpeakerName=0
; 1 = trims the speaker name from some debug format of NKSold_NAME to simply NAME
DialogSpeakerTrim=1
; 1 = when speaker name is not available use audio cue shortcut
DisplaySpeakerCueGuess=1
; Up to 12 RGB hex colours for speaker names
DialogSpeakerColors=FFFF00,00FFFF,FF7F00,7FFF00,FF77FF,7F7FFF,FF4444,44FFAA,AACCFF,FFAA44,DD88FF,88FF88
; 1 = if enabled then each new speaker in conversation uses new color from DialogSpeakerColors
DialogSpeakerUseDifferentColors=1
; 1 = whole text line has matching color with speaker color
DialogMatchEntireLineColorWithSpeaker=0
"""

CAMERA_PHYSICS_BLOCK = """
[Camera]
; 0 = keep cargo-ship camera rocking (original)
; 1 = disable it (clears bCameraRocking on 02_CargoShip)
DisableCargoShipSway=0

[Physics]
; EXPERIMENTAL OPTION !
; Tick frequency for hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)
; Default is ~35 Hz
; 0 = original high-FPS tick behaviour.
HighFpsForcedTickrate=35
"""

UI_BLOCK = """
[UI]
; 1 = show spoken conversation text at the bottom of the screen
; Uses language from System/Settings.ini
; When native languages are enabled still displays set language from Settings.ini
DisplayDialogs=1
; Leave empty value to synchronize language with Settings.ini
; Can override language of dialogs with value of: deu, esp, fra, int, ita
OverrideDialogsLanguage=
; 1 = additionally displays also chat box comms (Lambert, Grim, etc)
DialogShowEchelonComms=0
; 1 = shows interrogation subtitles
DialogShowInterrogations=1

; Gap from the bottom of the text to the bottom of the current screen resolution
DialogBottomPaddingPercent=5
; Max width of one text line as % of the current screen resolution
DialogMaxWidthSizePercent=50

DialogFontSize=38
DialogBold=1
; 0 = invisible, 1-255 = text alpha
DialogAlpha=255
DialogColor=FFFFFF
; 0 = no backdrop, 1-255 = backdrop alpha
DialogBackgroundAlpha=0
DialogBackgroundColor=000000

; 1 = hide the SPEAKER: prefix from the overlay
HideSpeakerName=0
; 1 = trims the speaker name from some debug format of NKSold_NAME to simply NAME
DialogSpeakerTrim=1
; 1 = when speaker name is not available use audio cue shortcut
DisplaySpeakerCueGuess=1
; Up to 12 RGB hex colours for speaker names
DialogSpeakerColors=FFFF00,00FFFF,FF7F00,7FFF00,FF77FF,7F7FFF,FF4444,44FFAA,AACCFF,FFAA44,DD88FF,88FF88
; 1 = if enabled then each new speaker in conversation uses new color from DialogSpeakerColors
DialogSpeakerUseDifferentColors=1
; 1 = whole text line has matching color with speaker color
DialogMatchEntireLineColorWithSpeaker=0
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


def _migrate_ini_keys(text: str) -> str:
    if "ShowAllDialogs=" in text:
        text = text.replace("ShowAllDialogs=", "DisplayDialogs=")
    if "DialogMaxWidthSizePercent=" not in text:
        insert = (
            "; Max width of one text line as % of the current screen resolution\n"
            "DialogMaxWidthSizePercent=50\n"
        )
        needle = "DialogBottomPaddingPercent="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
        elif "[UI]" in text or "[ui]" in text:
            text = text.rstrip() + "\n" + insert
    if "OverrideDialogsLanguage=" not in text:
        needle = "DisplayDialogs="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl >= 0:
                text = text[: nl + 1] + (
                    "; Leave empty value to synchronize language with Settings.ini\n"
                    "; Can override language of dialogs with value of: deu, esp, fra, int, ita\n"
                    "OverrideDialogsLanguage=\n"
                ) + text[nl + 1 :]
    if "DialogAlpha=" not in text:
        needle = "DialogColor="
        i = text.find(needle)
        if i >= 0:
            text = text[:i] + (
                "; 0 = invisible, 1-255 = text alpha\n"
                "DialogAlpha=255\n"
            ) + text[i:]
    if "DialogMatchEntireLineColorWithSpeaker=" not in text:
        needle = "DialogSpeakerUseDifferentColors="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + (
                    "\n; 1 = whole text line has matching color with speaker color\n"
                    "DialogMatchEntireLineColorWithSpeaker=0\n"
                )
            else:
                text = text[: nl + 1] + (
                    "; 1 = whole text line has matching color with speaker color\n"
                    "DialogMatchEntireLineColorWithSpeaker=0\n"
                ) + text[nl + 1 :]
    if "DisplaySpeakerQueGuess=" in text:
        text = text.replace("DisplaySpeakerQueGuess=", "DisplaySpeakerCueGuess=")
        text = text.replace("audio que shortcut", "audio cue shortcut")
    if "DisplaySpeakerCueGuess=" not in text:
        needle = "DialogSpeakerTrim="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            insert = (
                "; 1 = when speaker name is not available use audio cue shortcut\n"
                "DisplaySpeakerCueGuess=1\n"
            )
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
    if "HighFpsForcedTickrate=" not in text:
        if "FixHighFPSForcedTickrate=" in text:
            text = text.replace("FixHighFPSForcedTickrate=", "HighFpsForcedTickrate=")
        else:
            hz = "0" if "\nFixHighFPS=0" in text or text.startswith("FixHighFPS=0") else "35"
            new_phys = (
                "[Physics]\n"
                "; EXPERIMENTAL OPTION !\n"
                "; Tick frequency for hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)\n"
                "; Default is ~35 Hz\n"
                "; 0 = original high-FPS tick behaviour.\n"
                f"HighFpsForcedTickrate={hz}\n"
            )
            old_phys_on = (
                "[Physics]\n"
                "; EXPERIMENTAL FLAG !\n"
                "; 1 = tick hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)\n"
                "; at ~35 Hz with elapsed dt. NPCs, camera and player animation always use\n"
                "; the real frame delta.\n"
                "; 0 = original high-FPS behaviour.\n"
                "FixHighFPS=1\n"
            )
            old_phys_off = old_phys_on.replace("FixHighFPS=1", "FixHighFPS=0")
            if old_phys_on in text:
                text = text.replace(old_phys_on, new_phys, 1)
            elif old_phys_off in text:
                text = text.replace(old_phys_off, new_phys, 1)
            elif "FixHighFPS=" in text:
                text = text.replace("FixHighFPS=0", "HighFpsForcedTickrate=0").replace(
                    "FixHighFPS=1", "HighFpsForcedTickrate=35"
                )
    return text


def _append_missing_ini_sections(text: str) -> str | None:
    """Append [Camera] / [Physics] / [UI] to an older Remaster.ini. None if already present."""
    lower = text.lower()
    extra = ""
    if "[camera]" not in lower or "[physics]" not in lower:
        extra += CAMERA_PHYSICS_BLOCK
    if "[ui]" not in lower:
        extra += UI_BLOCK
    if not extra:
        return None
    out = text.rstrip() + "\n" + extra
    if not out.endswith("\n"):
        out += "\n"
    return out


def write_remaster_ini(ini_path: Path, size: int = DEFAULT_PYRAMID_SIZE) -> bool:
    """Write Remaster.ini unless a current one (with ``[Pyramid]``) already exists.

    An existing INI that predates [Camera] / [Physics] / [UI] gets those sections appended.

    Args:
        ini_path (Path): Path to Remaster.ini.
        size (int): ``[Pyramid] Size`` written to a new INI.

    Returns:
        bool: True if the INI was written or updated, False if the existing one was kept.
    """
    if ini_path.is_file() and "[pyramid]" in ini_path.read_text(encoding="utf-8", errors="replace").lower():
        text = ini_path.read_text(encoding="utf-8", errors="replace")
        orig = text
        text = _migrate_ini_keys(text)
        updated = _append_missing_ini_sections(text)
        if updated is not None:
            text = updated
        if text == orig:
            return False
        ini_path.write_text(text, encoding="utf-8")
        return True
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
