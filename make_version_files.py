#!/usr/bin/env python3

"""
make_version_files
~~~~~~~~~~~~~~~~~~
Write PyInstaller version resource files (``--version-file``) for the CLI EXEs.

Every EXE gets a proper Windows version resource (company, description,
version from ``scct.__version__``) - unversioned onefile EXEs are a common
trigger for antivirus false positives.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import sys
from pathlib import Path

from scct import __version__

TOOLS = {
    "unpack": "SCCT-Repack unpacker for Splinter Cell: Chaos Theory archives",
    "pack": "SCCT-Repack packer for Splinter Cell: Chaos Theory archives",
    "patch_exe": "SCCT-Repack large UMD support patch for splintercell3.exe",
    "remaster_patch_exe": "SCCT-Repack remaster installer for Splinter Cell: Chaos Theory",
}

TEMPLATE = """VSVersionInfo(
  ffi=FixedFileInfo(filevers={vt}, prodvers={vt}, mask=0x3F, flags=0x0, OS=0x40004, fileType=0x1, subtype=0x0, date=(0, 0)),
  kids=[
    StringFileInfo([StringTable('040904B0', [
      StringStruct('CompanyName', 'Dominik Wojtasik'),
      StringStruct('FileDescription', '{desc}'),
      StringStruct('FileVersion', '{ver}'),
      StringStruct('InternalName', '{name}'),
      StringStruct('LegalCopyright', '(c) 2026 Dominik Wojtasik, MIT License'),
      StringStruct('OriginalFilename', '{name}.exe'),
      StringStruct('ProductName', 'SCCT-Repack'),
      StringStruct('ProductVersion', '{ver}')])]),
    VarFileInfo([VarStruct('Translation', [1033, 1200])])
  ]
)
"""


def write_version_files(out_dir: Path) -> list[Path]:
    """Write ``version_<tool>.txt`` for every tool.

    Args:
        out_dir (Path): Output directory (created if missing).

    Returns:
        list[Path]: Written files.
    """
    parts = [int(p) for p in __version__.split(".")]
    vt = tuple((parts + [0, 0, 0, 0])[:4])
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name, desc in TOOLS.items():
        path = out_dir / f"version_{name}.txt"
        path.write_text(TEMPLATE.format(vt=vt, ver=__version__, name=name, desc=desc), encoding="utf-8")
        written.append(path)
    return written


if __name__ == "__main__":
    for p in write_version_files(Path(sys.argv[1] if len(sys.argv) > 1 else "build")):
        print(f"wrote {p}")
