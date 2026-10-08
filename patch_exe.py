#!/usr/bin/env python3

"""
patch_exe
~~~~~~~~~
Patch splintercell3.exe so UMD archives up to ~4 GiB can be read.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from scct import __version__
from scct.common import pause_if_frozen
from scct.patch import patch_exe


def _find_default_exe() -> Path | None:
    """Look for splintercell3.exe next to the tool or in ./System."""
    here = Path.cwd()
    for candidate in (
        here / "splintercell3.exe",
        here / "System" / "splintercell3.exe",
    ):
        if candidate.is_file():
            return candidate
    return None


def _get_parser() -> argparse.ArgumentParser:
    """Return argument parser for patch_exe.

    Returns:
        argparse.ArgumentParser: The argument parser.
    """
    parser = argparse.ArgumentParser(
        prog="patch_exe",
        description=(
            "Patch splintercell3.exe (PE32, ImageBase 0x10900000) "
            "for Large Address Aware + unsigned file seeks. "
            "UMD TOC is still uint32, so the archive cap stays ~4 GiB."
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
        "-v",
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    return parser


def _main(argv: list[str] | None = None) -> int:
    """Program entry.

    Args:
        argv (list[str] | None): Argument list without the program name.

    Returns:
        int: Process exit code.
    """
    parser = _get_parser()
    args = parser.parse_args(argv if argv is not None else sys.argv[1:])
    path = args.exe_path if args.exe_path is not None else _find_default_exe()
    if path is None:
        print("error: no splintercell3.exe found (pass a path, or place it in .\\ or .\\System\\)", file=sys.stderr)
        return 1
    path = path.resolve()
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 1
    try:
        for line in patch_exe(path):
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
