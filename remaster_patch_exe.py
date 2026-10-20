#!/usr/bin/env python3

"""
remaster_patch_exe
~~~~~~~~~~~~~~~~~~
Patch splintercell3.exe for Magma bloom quality (TLS + remaster.ini).

This is separate from ``patch_exe`` (UMD 4/8 GiB LAA/SC8G). Run ``patch_exe``
first if you need large UMD3, then this tool for bloom.

Writes ``remaster.ini`` next to the EXE::

    ; Default value: 256
    ; Qualities: [256, 512, 1024, 2048, 4096]
    [Bloom]
    Size=2048

At launch the game maps Size → MagmaBloom Size/Fine64/32/16.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from scct import __version__
from scct.common import pause_if_frozen
from scct.remaster_patch import patch_remaster_exe, restore_remaster_exe


def _find_default_exe() -> Path | None:
    here = Path.cwd()
    for candidate in (
        here / "splintercell3.exe",
        here / "System" / "splintercell3.exe",
    ):
        if candidate.is_file():
            return candidate
    return None


def _get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="remaster_patch_exe",
        description=(
            "Patch splintercell3.exe (PE32, ImageBase 0x10900000) for Magma "
            "bloom quality via TLS remaster.ini [Bloom] Size. "
            "Separate from patch_exe (UMD large-address / UMD3)."
        ),
    )
    parser.add_argument(
        "exe_path",
        type=Path,
        nargs="?",
        metavar="exe",
        help="Path to splintercell3.exe. Default: ./splintercell3.exe or ./System/splintercell3.exe.",
    )
    parser.add_argument(
        "--restore",
        action="store_true",
        help="Restore the EXE from .remaster.bak and remove remaster.ini.",
    )
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    return parser


def _main(argv: list[str] | None = None) -> int:
    parser = _get_parser()
    args = parser.parse_args(argv if argv is not None else sys.argv[1:])
    path = args.exe_path if args.exe_path is not None else _find_default_exe()
    if path is None:
        print(
            "error: no splintercell3.exe found (pass a path, or place it in .\\ or .\\System\\)",
            file=sys.stderr,
        )
        return 1
    path = path.resolve()
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 1
    try:
        if args.restore:
            for line in restore_remaster_exe(path):
                print(line)
        else:
            for line in patch_remaster_exe(path):
                print(line)
    except (OSError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    _code = 1
    try:
        _code = _main()
    finally:
        pause_if_frozen()
    raise SystemExit(_code)
