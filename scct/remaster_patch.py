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

    [Localization] OverrideTextLanguage / OverrideAudioLanguage (empty = Settings.ini)
    [Pyramid]   Size of the coarse glow pyramid (bloom and vision modes)
    [Bloom]     glow fixes for every post-process path, modern HDR bloom
    [Camera]    DisableCargoShipSway / DisableFishEyeVision
    [Weapons]   Pistol/RifleAccuracyBase, Pistol/RifleMoveSpeedPenaltyMult,
                Pistol/RifleReticleSettleMult
    [Gameplay]  EnableCustomLoadouts, CustomLoadoutMax*Amount (CUSTOM briefing loadout),
                EnableOpsatStatistics (OPSAT MISSION STATISTICS page),
                EnableOpsatCentering (OPSAT menu centered),
                FixBathhouseVentEnemySpotted (Bathhouse vent sighting not counted)
    [Physics]   HighFpsForcedTickrate - hanging-lamp / cable Tick Hz (0 = original)
    [UI]        DisplayDialogs overlay, DialogShowVideos Bink transcript, DialogShowBarks P_Barks cues

4K Bink support is always on (no INI key).

Older versions added a ``Remaster.dll`` import to the EXE (``.rmst`` section,
backup ``.remaster.bak``); such an EXE is restored from its backup first.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import re
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

REMASTER_INI = """[Localization]
; Language codes: cze, deu, esp, fra, hun, int, ita, pol, prt, rus, svk
; Empty override = use [Localization] Language= from Settings.ini
; Text / HUD / menus: System\\Localization\\*.<lang> when present for that language
; Otherwise Magma reads Data\\System\\Localization\\ from the UMD for that language
; If neither disk nor UMD has the language, text falls back to int (English)
; HUD fonts load from System\\Localization\\Fonts\\<lang>\\ (else UMD Fonts\\<lang>\\)

; Leave empty to follow Settings.ini. Overrides Magma UI / HUD / menu text language.
OverrideTextLanguage=
; Leave empty to follow Settings.ini. Overrides spoken audio + lipsync language.
OverrideAudioLanguage=
; Leave empty to follow OverrideTextLanguage (or Settings.ini). Overrides subtitle / dialog overlay language only.
OverrideDialogsLanguage=

; 1 = show spoken conversation text at the bottom of the screen
; Uses language from System/Settings.ini
; When native languages are enabled still displays set language from Settings.ini
DisplayDialogs=1
; 1 = additionally displays also chat box comms (Lambert, Grim, etc)
DialogShowEchelonComms=0
; 1 = shows interrogation subtitles
DialogShowInterrogations=1
; 1 = shows transcript subtitles on videos
; Requires JSON transcripts in System\\Localization\\Transcripts\\.
DialogShowVideos=1
; 1 = shows NPC bark / callout subtitles from P_Barks.<lang>
; Requires UTF-8 P_Barks.<lang> in System\\Localization\\Barks\\.
DialogShowBarks=1

; Gap from the bottom of the text to the bottom of the current screen resolution
DialogBottomPaddingPercent=5
; Max width of one text line as % of the current screen resolution
DialogMaxWidthSizePercent=50

; Dialog font settings for in-game subtitles
DialogFontSize=38
DialogBold=1
; 0 = invisible, 1-255 = text alpha
DialogAlpha=255
DialogColor=FFFFFF
; Width of outline in pixels, 0 = disable outline
DialogOutlinePixels=1
; Color of outline
DialogOutlineColor=000000
; 0 = no backdrop, 1-255 = background alpha
DialogBackgroundAlpha=0
DialogBackgroundColor=000000

; Dialog font settings for video subtitles
DialogVideoFontSize=38
DialogVideoBold=1
; 0 = invisible, 1-255 = text alpha
DialogVideoAlpha=255
DialogVideoColor=FFFFFF
; Width of outline in pixels, 0 = disable outline
DialogVideoOutlinePixels=1
; Color of outline
DialogVideoOutlineColor=000000
; 0 = no backdrop, 1-255 = backdrop alpha
DialogVideoBackgroundAlpha=128
DialogVideoBackgroundColor=000000

; 1 = hide the SPEAKER: prefix from the overlay
; Can display speaker name when available
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


[Camera]
; 0 = keep cargo-ship camera rocking (original)
; 1 = disable it (clears bCameraRocking on 02_CargoShip)
DisableCargoShipSway=0
; 0 = keep fish-eye warp on goggles (original)
; 1 = disable barrel warp; night / thermal / optic / cam colour stays
DisableFishEyeVision=0


[Weapons]
; Focused reticle spread floor (the game multiplies it by 0.6 while crouched)
; Lower = tighter when fully focused and more accurate.
; Game defaults: pistol 0.8, rifle 0.5
PistolAccuracyBase=0.4
RifleAccuracyBase=0.35
; Reticle spread multiplier while moving
; 0 = no movement penalty, 0.5 = half, 1 = original, 2 = double
PistolMoveSpeedPenaltyMult=0.75
RifleMoveSpeedPenaltyMult=0.75
; Settle-time multiplier for the aiming reticle
; 0 = always focused (ready to shoot, no settle wait)
; 0.5 = half of original settle time, 0.75 = 75%, etc.
; 1 = original settle time
PistolReticleSettleMult=0.75
RifleReticleSettleMult=0.75


[Gameplay]
; 1 = add CUSTOM to the mission briefing loadouts (Redding's recommendation, Stealth,
;     Assault). With CUSTOM picked, the equipped-gear list edits the kit in two
;     tabs, WEAPONS and GADGETS (the last row switches tabs): left click an item
;     = one more, right click = one less; the rifle row above the list changes
;     SC-20K ammo (keys: PgUp / PgDn = pick item, Left / Right = change amount).
;     Ammo is the total carried, by full magazines. CUSTOM always starts as a
;     copy of the mission's Stealth loadout and changes are not saved.
; 0 = original loadouts only
EnableCustomLoadouts=1
; Highest custom amounts. Ammo is in rounds
; (per clip: SC-20K 30, pistol 20, shotgun 7, sniper 6).
; Attachments are ON / OFF; the rifle carries two. Switching on a third switches
; off the foregrip first, then sniper / shotgun; the launcher only goes by hand.
; Without the launcher its rounds (sticky camera / shocker, airfoil, gas) are OFF.
CustomLoadoutMaxRifleAmmoAmount=300
CustomLoadoutMaxPistolAmmoAmount=100
CustomLoadoutMaxShotgunAmmoAmount=35
CustomLoadoutMaxSniperAmmoAmount=30
CustomLoadoutMaxGadgetAmount=10
CustomLoadoutMaxGrenadeAmount=5
CustomLoadoutMaxMineAmount=5
; 1 = add MISSION STATISTICS to the OPSAT menu: the end-of-mission statistics page
;     (time, alarms, knockouts...) with the counters of the mission being played.
;     OPSAT / EXIT buttons as on the other OPSAT pages (Esc = back to the OPSAT);
;     the mission time keeps running while it is open.
; 0 = original OPSAT menu
EnableOpsatStatistics=1
; 1 = center the OPSAT main menu in its panel (with or without MISSION STATISTICS)
; 0 = original position (a bit low)
EnableOpsatCentering=1
; 1 = Bathhouse: the night vision guard below the vent room seeing Sam in the vent does
;     not count as TIMES IDENTIFIED AS INTRUDER (the guard still reacts)
; 0 = original
FixBathhouseVentEnemySpotted=1


[LocalizationKeys]
; Texts the remaster adds to the menus that the game has no translation for.
; One line per language: <Key>.<language code>=<text> (codes as in [Localization]);
; a language without a line uses the .int (English) one. Save this file as UTF-8.
; CUSTOM entry of the mission briefing loadouts (also starts the editor title)
CustomLoadout.cze=VLASTNÍ VÝBAVA
CustomLoadout.deu=EIGENE AUSRÜSTUNG
CustomLoadout.esp=PERSONALIZADO
CustomLoadout.fra=PERSONNALISÉ
CustomLoadout.hun=EGYÉNI FELSZERELÉS
CustomLoadout.int=CUSTOM
CustomLoadout.ita=PERSONALIZZATO
CustomLoadout.pol=WŁASNE WYPOSAŻENIE
CustomLoadout.prt=PERSONALIZADO
CustomLoadout.rus=СВОЁ СНАРЯЖЕНИЕ
CustomLoadout.svk=VLASTNÁ VÝBAVA
; Mouse hint shown after it in the editor title
CustomLoadoutHint.cze=LTM: VÍCE   PTM: MÉNĚ
CustomLoadoutHint.deu=LMT: MEHR   RMT: WENIGER
CustomLoadoutHint.esp=CLIC IZQ: MÁS   CLIC DER: MENOS
CustomLoadoutHint.fra=CLIC G. : PLUS   CLIC D. : MOINS
CustomLoadoutHint.hun=BAL: TÖBB   JOBB: KEVESEBB
CustomLoadoutHint.int=LEFT CLICK: MORE   RIGHT CLICK: LESS
CustomLoadoutHint.ita=CLIC SX: PIÙ   CLIC DX: MENO
CustomLoadoutHint.pol=LPM: WIĘCEJ   PPM: MNIEJ
CustomLoadoutHint.prt=ESQ.: MAIS   DIR.: MENOS
CustomLoadoutHint.rus=ЛКМ: БОЛЬШЕ   ПКМ: МЕНЬШЕ
CustomLoadoutHint.svk=ĽTM: VIAC   PTM: MENEJ


[Physics]
; EXPERIMENTAL OPTION !
; Tick frequency for hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)
; Default is ~35 Hz
; 0 = original high-FPS tick behaviour.
HighFpsForcedTickrate=35


[Pyramid]
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

LOCALIZATION_KEYS_BLOCK = """
[LocalizationKeys]
; Texts the remaster adds to the menus that the game has no translation for.
; One line per language: <Key>.<language code>=<text> (codes as in [Localization]);
; a language without a line uses the .int (English) one. Save this file as UTF-8.
; CUSTOM entry of the mission briefing loadouts (also starts the editor title)
CustomLoadout.cze=VLASTNÍ VÝBAVA
CustomLoadout.deu=EIGENE AUSRÜSTUNG
CustomLoadout.esp=PERSONALIZADO
CustomLoadout.fra=PERSONNALISÉ
CustomLoadout.hun=EGYÉNI FELSZERELÉS
CustomLoadout.int=CUSTOM
CustomLoadout.ita=PERSONALIZZATO
CustomLoadout.pol=WŁASNE WYPOSAŻENIE
CustomLoadout.prt=PERSONALIZADO
CustomLoadout.rus=СВОЁ СНАРЯЖЕНИЕ
CustomLoadout.svk=VLASTNÁ VÝBAVA
; Mouse hint shown after it in the editor title
CustomLoadoutHint.cze=LTM: VÍCE   PTM: MÉNĚ
CustomLoadoutHint.deu=LMT: MEHR   RMT: WENIGER
CustomLoadoutHint.esp=CLIC IZQ: MÁS   CLIC DER: MENOS
CustomLoadoutHint.fra=CLIC G. : PLUS   CLIC D. : MOINS
CustomLoadoutHint.hun=BAL: TÖBB   JOBB: KEVESEBB
CustomLoadoutHint.int=LEFT CLICK: MORE   RIGHT CLICK: LESS
CustomLoadoutHint.ita=CLIC SX: PIÙ   CLIC DX: MENO
CustomLoadoutHint.pol=LPM: WIĘCEJ   PPM: MNIEJ
CustomLoadoutHint.prt=ESQ.: MAIS   DIR.: MENOS
CustomLoadoutHint.rus=ЛКМ: БОЛЬШЕ   ПКМ: МЕНЬШЕ
CustomLoadoutHint.svk=ĽTM: VIAC   PTM: MENEJ
"""

CAMERA_PHYSICS_BLOCK = """
[Camera]
; 0 = keep cargo-ship camera rocking (original)
; 1 = disable it (clears bCameraRocking on 02_CargoShip)
DisableCargoShipSway=0
; 0 = keep fish-eye warp on goggles (original)
; 1 = disable barrel warp; night / thermal / optic / cam colour stays
DisableFishEyeVision=0

[Weapons]
; Focused reticle spread floor (the game multiplies it by 0.6 while crouched)
; Lower = tighter when fully focused and more accurate.
; Game defaults: pistol 0.8, rifle 0.5
PistolAccuracyBase=0.4
RifleAccuracyBase=0.35
; Reticle spread multiplier while moving
; 0 = no movement penalty, 0.5 = half, 1 = original, 2 = double
PistolMoveSpeedPenaltyMult=0.75
RifleMoveSpeedPenaltyMult=0.75
; Settle-time multiplier for the aiming reticle
; 0 = always focused (ready to shoot, no settle wait)
; 0.5 = half of original settle time, 0.75 = 75%, etc.
; 1 = original settle time
PistolReticleSettleMult=0.75
RifleReticleSettleMult=0.75

[Gameplay]
; 1 = add CUSTOM to the mission briefing loadouts (Redding's recommendation, Stealth,
;     Assault). With CUSTOM picked, the equipped-gear list edits the kit in two
;     tabs, WEAPONS and GADGETS (the last row switches tabs): left click an item
;     = one more, right click = one less; the rifle row above the list changes
;     SC-20K ammo (keys: PgUp / PgDn = pick item, Left / Right = change amount).
;     Ammo is the total carried, by full magazines. CUSTOM always starts as a
;     copy of the mission's Stealth loadout and changes are not saved.
; 0 = original loadouts only
EnableCustomLoadouts=1
; Highest custom amounts. Ammo is in rounds
; (per clip: SC-20K 30, pistol 20, shotgun 7, sniper 6).
; Attachments are ON / OFF; the rifle carries two. Switching on a third switches
; off the foregrip first, then sniper / shotgun; the launcher only goes by hand.
; Without the launcher its rounds (sticky camera / shocker, airfoil, gas) are OFF.
CustomLoadoutMaxRifleAmmoAmount=300
CustomLoadoutMaxPistolAmmoAmount=100
CustomLoadoutMaxShotgunAmmoAmount=35
CustomLoadoutMaxSniperAmmoAmount=30
CustomLoadoutMaxGadgetAmount=10
CustomLoadoutMaxGrenadeAmount=5
CustomLoadoutMaxMineAmount=5
; 1 = add MISSION STATISTICS to the OPSAT menu: the end-of-mission statistics page
;     (time, alarms, knockouts...) with the counters of the mission being played.
;     OPSAT / EXIT buttons as on the other OPSAT pages (Esc = back to the OPSAT);
;     the mission time keeps running while it is open.
; 0 = original OPSAT menu
EnableOpsatStatistics=1
; 1 = center the OPSAT main menu in its panel (with or without MISSION STATISTICS)
; 0 = original position (a bit low)
EnableOpsatCentering=1
; 1 = Bathhouse: the night vision guard below the vent room seeing Sam in the vent does
;     not count as TIMES IDENTIFIED AS INTRUDER (the guard still reacts)
; 0 = original
FixBathhouseVentEnemySpotted=1

[Physics]
; EXPERIMENTAL OPTION !
; Tick frequency for hanging lamps and swaying cables (ESoftBody / ESBRope / ERope)
; Default is ~35 Hz
; 0 = original high-FPS tick behaviour.
HighFpsForcedTickrate=35
"""

LOCALIZATION_BLOCK = """
[Localization]
; Language codes: cze, deu, esp, fra, hun, int, ita, pol, prt, rus, svk
; Empty override = use [Localization] Language= from Settings.ini
; Text / HUD / menus: System\\Localization\\*.<lang> when present for that language
; Otherwise Magma reads Data\\System\\Localization\\ from the UMD for that language
; If neither disk nor UMD has the language, text falls back to int (English)
; HUD fonts load from System\\Localization\\Fonts\\<lang>\\ (else UMD Fonts\\<lang>\\)

; Leave empty to follow Settings.ini. Overrides Magma UI / HUD / menu text language.
OverrideTextLanguage=
; Leave empty to follow Settings.ini. Overrides spoken audio + lipsync language.
OverrideAudioLanguage=
"""

UI_BLOCK = """
[UI]
; 1 = show spoken conversation text at the bottom of the screen
; Uses language from System/Settings.ini
; When native languages are enabled still displays set language from Settings.ini
DisplayDialogs=1
; Leave empty to follow OverrideTextLanguage (or Settings.ini). Overrides subtitle / dialog overlay language only.
OverrideDialogsLanguage=
; 1 = additionally displays also chat box comms (Lambert, Grim, etc)
DialogShowEchelonComms=0
; 1 = shows interrogation subtitles
DialogShowInterrogations=1
; 1 = shows transcript subtitles on videos
; Requires JSON transcripts in System\\Localization\\Transcripts\\.
DialogShowVideos=1
; 1 = shows NPC bark / callout subtitles from P_Barks.<lang>
; Requires UTF-8 P_Barks.<lang> in System\\Localization\\Barks\\.
DialogShowBarks=1

; Gap from the bottom of the text to the bottom of the current screen resolution
DialogBottomPaddingPercent=5
; Max width of one text line as % of the current screen resolution
DialogMaxWidthSizePercent=50

DialogFontSize=38
DialogBold=1
; 0 = invisible, 1-255 = text alpha
DialogAlpha=255
DialogColor=FFFFFF
; Width of outline in pixels, 0 = disable outline
DialogOutlinePixels=1
; Color of outline
DialogOutlineColor=000000
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


def _relocate_section_to_top(text: str, name: str) -> str:
    """Move an existing [section] to the start of the INI. No-op if missing or already first."""
    lower = text.lower()
    tag = f"[{name.lower()}]"
    start = lower.find(tag)
    if start < 0:
        return text
    if not text[:start].strip():
        return text
    nxt = lower.find("\n[", start + 1)
    if nxt < 0:
        block = text[start:].strip("\n")
        rest = text[:start].strip("\n")
    else:
        block = text[start:nxt].strip("\n")
        rest = (text[:start].rstrip() + "\n\n" + text[nxt + 1 :].lstrip()).strip("\n")
    if not rest:
        return block + "\n"
    return block + "\n\n" + rest + "\n"


def _migrate_ini_keys(text: str) -> str:
    if "ShowAllDialogs=" in text:
        text = text.replace("ShowAllDialogs=", "DisplayDialogs=")
    if "DialogShowVideos=" not in text:
        needle = "DialogShowInterrogations="
        i = text.find(needle)
        insert = (
            "; 1 = shows transcript subtitles on videos\n"
            "; Requires JSON transcripts in System\\Localization\\Transcripts\\.\n"
            "DialogShowVideos=1\n"
        )
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
        elif "[UI]" in text or "[ui]" in text:
            text = text.rstrip() + "\n" + insert
    if "DialogShowBarks=" not in text:
        needle = "DialogShowVideos="
        i = text.find(needle)
        insert = (
            "; 1 = shows NPC bark / callout subtitles from P_Barks.<lang>\n"
            "; Requires UTF-8 P_Barks.<lang> in System\\Localization\\Barks\\.\n"
            "DialogShowBarks=1\n"
        )
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
        elif "[Localization]" in text or "[localization]" in text or "[UI]" in text or "[ui]" in text:
            text = text.rstrip() + "\n" + insert
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
    if "OverrideTextLanguage=" not in text:
        block = LOCALIZATION_BLOCK.strip("\n")
        text = block + "\n\n" + text.lstrip()
        if not text.endswith("\n"):
            text += "\n"
    text = _relocate_section_to_top(text, "Localization")
    if "OverrideDialogsLanguage=" not in text:
        needle = "DisplayDialogs="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl >= 0:
                text = text[: nl + 1] + (
                    "; Leave empty value to synchronize language with [Localization] OverrideTextLanguage settings from Remaster.ini\n"
                    "; Can override language of dialogs with value of: cze, deu, esp, fra, hun, int, ita, pol, prt, rus, svk\n"
                    "; If given text translation does not exist then int (english) will be used.\n"
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
    if "DisableFishEyeVision=" not in text:
        needle = "DisableCargoShipSway="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            insert = (
                "; 0 = keep fish-eye warp on goggles (original)\n"
                "; 1 = disable barrel warp; night / thermal / optic / cam colour stays\n"
                "DisableFishEyeVision=0\n"
            )
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
    if "PistolReticleSettleMult=" not in text:
        insert = (
            "\n[Weapons]\n"
            "; Focused reticle spread floor (the game multiplies it by 0.6 while crouched)\n"
            "; Lower = tighter when fully focused and more accurate.\n"
            "; Game defaults: pistol 0.8, rifle 0.5\n"
            "PistolAccuracyBase=0.4\n"
            "RifleAccuracyBase=0.35\n"
            "; Reticle spread multiplier while moving\n"
            "; 0 = no movement penalty, 0.5 = half, 1 = original, 2 = double\n"
            "PistolMoveSpeedPenaltyMult=0.75\n"
            "RifleMoveSpeedPenaltyMult=0.75\n"
            "; Settle-time multiplier for the aiming reticle\n"
            "; 0 = always focused (ready to shoot, no settle wait)\n"
            "; 0.5 = half of original settle time, 0.75 = 75%, etc.\n"
            "; 1 = original settle time\n"
            "PistolReticleSettleMult=0.75\n"
            "RifleReticleSettleMult=0.75\n"
        )
        needle = "DisableFishEyeVision="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
        else:
            text = text + insert
    # Weapons keys added by later versions go above the existing ones (INI order:
    # AccuracyBase, MoveSpeedPenaltyMult, ReticleSettleMult).
    def _insert_before(text: str, block: str, anchors) -> str:
        for a in anchors:
            i = text.find(a)
            if i >= 0:
                ls = text.rfind("\n", 0, i) + 1
                return text[:ls] + block + text[ls:]
        # default: first entry of the [Weapons] section
        i = text.find("[Weapons]")
        if i >= 0:
            nl = text.find("\n", i)
            if nl >= 0:
                return text[: nl + 1] + block + text[nl + 1 :]
        return text
    if "PistolMoveSpeedPenaltyMult=" not in text:
        text = _insert_before(text, (
            "; Reticle spread multiplier while moving\n"
            "; 0 = no movement penalty, 0.5 = half, 1 = original, 2 = double\n"
            "PistolMoveSpeedPenaltyMult=0.75\n"
            "RifleMoveSpeedPenaltyMult=0.75\n"
        ), ("; Settle-time multiplier for the aiming reticle", "PistolReticleSettleMult="))
    if "PistolAccuracyBase=" not in text:
        text = _insert_before(text, (
            "; Focused reticle spread floor (the game multiplies it by 0.6 while crouched)\n"
            "; Lower = tighter when fully focused and more accurate.\n"
            "; Game defaults: pistol 0.8, rifle 0.5\n"
            "PistolAccuracyBase=0.4\n"
            "RifleAccuracyBase=0.35\n"
        ), ())
    if "EnableCustomLoadouts=" not in text:
        block = (
            "[Gameplay]\n"
            "; 1 = add CUSTOM to the mission briefing loadouts (Redding's recommendation, Stealth,\n"
            ";     Assault). With CUSTOM picked, the equipped-gear list edits the kit in two\n"
            ";     tabs, WEAPONS and GADGETS (the last row switches tabs): left click an item\n"
            ";     = one more, right click = one less; the rifle row above the list changes\n"
            ";     SC-20K ammo (keys: PgUp / PgDn = pick item, Left / Right = change amount).\n"
            ";     Ammo is the total carried, by full magazines. CUSTOM always starts as a\n"
            ";     copy of the mission's Stealth loadout and changes are not saved.\n"
            "; 0 = original loadouts only\n"
            "EnableCustomLoadouts=1\n"
            "; Highest custom amounts. Ammo is in rounds\n"
            "; (per clip: SC-20K 30, pistol 20, shotgun 7, sniper 6).\n"
            "; Attachments are ON / OFF; the rifle carries two. Switching on a third switches\n"
            "; off the foregrip first, then sniper / shotgun; the launcher only goes by hand.\n"
            "; Without the launcher its rounds (sticky camera / shocker, airfoil, gas) are OFF.\n"
            "CustomLoadoutMaxRifleAmmoAmount=300\n"
            "CustomLoadoutMaxPistolAmmoAmount=100\n"
            "CustomLoadoutMaxShotgunAmmoAmount=35\n"
            "CustomLoadoutMaxSniperAmmoAmount=30\n"
            "CustomLoadoutMaxGadgetAmount=10\n"
            "CustomLoadoutMaxGrenadeAmount=5\n"
            "CustomLoadoutMaxMineAmount=5\n"
        )
        i = text.find("[Physics]")
        if i >= 0:
            text = text[:i] + block + "\n\n" + text[i:]
        else:
            text = text.rstrip("\n") + "\n\n" + block
    if "EnableCustomLoadouts=" in text and "CustomLoadoutMaxRifleAmmoAmount=" not in text:
        i = text.find("[Gameplay]")
        if i >= 0:
            j = text.find("\n[", i + 1)
            j = len(text) if j < 0 else j + 1
            body = text[i:j]
            enabled = "1"
            for line in body.splitlines():
                if line.strip().startswith("EnableCustomLoadouts="):
                    enabled = line.split("=", 1)[1].strip() or "1"
            block = (
            "[Gameplay]\n"
            "; 1 = add CUSTOM to the mission briefing loadouts (Redding's recommendation, Stealth,\n"
            ";     Assault). With CUSTOM picked, the equipped-gear list edits the kit in two\n"
            ";     tabs, WEAPONS and GADGETS (the last row switches tabs): left click an item\n"
            ";     = one more, right click = one less; the rifle row above the list changes\n"
            ";     SC-20K ammo (keys: PgUp / PgDn = pick item, Left / Right = change amount).\n"
            ";     Ammo is the total carried, by full magazines. CUSTOM always starts as a\n"
            ";     copy of the mission's Stealth loadout and changes are not saved.\n"
            "; 0 = original loadouts only\n"
                f"EnableCustomLoadouts={enabled}\n"
            "; Highest custom amounts. Ammo is in rounds\n"
            "; (per clip: SC-20K 30, pistol 20, shotgun 7, sniper 6).\n"
            "; Attachments are ON / OFF; the rifle carries two. Switching on a third switches\n"
            "; off the foregrip first, then sniper / shotgun; the launcher only goes by hand.\n"
            "; Without the launcher its rounds (sticky camera / shocker, airfoil, gas) are OFF.\n"
            "CustomLoadoutMaxRifleAmmoAmount=300\n"
            "CustomLoadoutMaxPistolAmmoAmount=100\n"
            "CustomLoadoutMaxShotgunAmmoAmount=35\n"
            "CustomLoadoutMaxSniperAmmoAmount=30\n"
            "CustomLoadoutMaxGadgetAmount=10\n"
            "CustomLoadoutMaxGrenadeAmount=5\n"
            "CustomLoadoutMaxMineAmount=5\n"
            )
            text = text[:i] + block + "\n\n" + text[j:].lstrip("\n")
    # Earlier CUSTOM help text, and saved custom amounts (no longer stored)
    help_text = (
        "; 1 = add CUSTOM to the mission briefing loadouts (Redding's recommendation, Stealth,\n"
        ";     Assault). With CUSTOM picked, the equipped-gear list edits the kit in two\n"
        ";     tabs, WEAPONS and GADGETS (the last row switches tabs): left click an item\n"
        ";     = one more, right click = one less; the rifle row above the list changes\n"
        ";     SC-20K ammo (keys: PgUp / PgDn = pick item, Left / Right = change amount).\n"
        ";     Ammo is the total carried, by full magazines. CUSTOM always starts as a\n"
        ";     copy of the mission's Stealth loadout and changes are not saved.\n"
    )
    text = re.sub(
        r"(?m)^; 1 = add CUSTOM to the mission briefing loadouts.*\n(?:;     .*\n)*",
        lambda _m: help_text,
        text,
        count=1,
    )
    while m := re.search(r"(?im)^\[CustomLoadout\][ \t]*(?:\n|$)", text):
        nxt = re.search(r"(?m)^\[", text[m.end():])
        end = m.end() + nxt.start() if nxt else len(text)
        text = text[: m.start()] + text[end:]
        text = text.rstrip("\n") + "\n"
    if "EnableOpsatStatistics=" not in text:
        stats_block = (
            "; 1 = add MISSION STATISTICS to the OPSAT menu: the end-of-mission statistics page\n"
            ";     (time, alarms, knockouts...) with the counters of the mission being played.\n"
            ";     OPSAT / EXIT buttons as on the other OPSAT pages (Esc = back to the OPSAT);\n"
            ";     the mission time keeps running while it is open.\n"
            "; 0 = original OPSAT menu\n"
            "EnableOpsatStatistics=1\n"
        )
        m = re.search(r"(?m)^CustomLoadoutMaxMineAmount=.*\n", text)
        if m is None:
            m = re.search(r"(?m)^EnableCustomLoadouts=.*\n", text)
        if m is not None:
            text = text[: m.end()] + stats_block + text[m.end():]
    if "EnableOpsatCentering=" not in text:
        m = re.search(r"(?m)^EnableOpsatStatistics=.*\n", text)
        if m is not None:
            text = (text[: m.end()]
                    + "; 1 = center the OPSAT main menu in its panel (with or without MISSION STATISTICS)\n"
                    + "; 0 = original position (a bit low)\n"
                    + "EnableOpsatCentering=1\n"
                    + text[m.end():])
    if "FixBathhouseVentEnemySpotted=" not in text:
        m = re.search(r"(?m)^EnableOpsatCentering=.*\n", text)
        if m is not None:
            text = (text[: m.end()]
                    + "; 1 = Bathhouse: the night vision guard below the vent room seeing Sam in the vent does\n"
                    + ";     not count as TIMES IDENTIFIED AS INTRUDER (the guard still reacts)\n"
                    + "; 0 = original\n"
                    + "FixBathhouseVentEnemySpotted=1\n"
                    + text[m.end():])
    if "DialogVideoFontSize=" not in text:
        insert = (
            "\n; Dialog font settings for video subtitles\n"
            "DialogVideoFontSize=38\n"
            "DialogVideoBold=1\n"
            "; 0 = invisible, 1-255 = text alpha\n"
            "DialogVideoAlpha=255\n"
            "DialogVideoColor=FFFFFF\n"
            "; Width of outline in pixels, 0 = disable outline\n"
            "DialogVideoOutlinePixels=1\n"
            "; Color of outline\n"
            "DialogVideoOutlineColor=000000\n"
            "; 0 = no backdrop, 1-255 = backdrop alpha\n"
            "DialogVideoBackgroundAlpha=128\n"
            "DialogVideoBackgroundColor=000000\n"
        )
        needle = "DialogBackgroundColor="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
    if "DialogOutlinePixels=" not in text:
        insert = (
            "; Width of outline in pixels, 0 = disable outline\n"
            "DialogOutlinePixels=1\n"
            "; Color of outline\n"
            "DialogOutlineColor=000000\n"
        )
        needle = "DialogColor="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
            if nl < 0:
                text = text + "\n" + insert
            else:
                text = text[: nl + 1] + insert + text[nl + 1 :]
    if "DialogVideoOutlinePixels=" not in text and "DialogVideoColor=" in text:
        insert = (
            "; Width of outline in pixels, 0 = disable outline\n"
            "DialogVideoOutlinePixels=1\n"
            "; Color of outline\n"
            "DialogVideoOutlineColor=000000\n"
        )
        needle = "DialogVideoColor="
        i = text.find(needle)
        if i >= 0:
            nl = text.find("\n", i)
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
    """Add missing [Localization] (top) / [Camera] / [Physics] / [UI] / [LocalizationKeys]. None if present."""
    lower = text.lower()
    extra = ""
    if "[camera]" not in lower or "[physics]" not in lower:
        extra += CAMERA_PHYSICS_BLOCK
    if "[ui]" not in lower and "displaydialogs=" not in lower:
        extra += UI_BLOCK
    if "[localizationkeys]" not in lower:
        extra += "\n" + LOCALIZATION_KEYS_BLOCK
    out = text
    changed = False
    if "[localization]" not in lower:
        block = LOCALIZATION_BLOCK.strip("\n")
        out = block + "\n\n" + out.lstrip()
        changed = True
    if extra:
        out = out.rstrip() + "\n" + extra
        changed = True
    if not changed:
        return None
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
