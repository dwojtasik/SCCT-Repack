#!/usr/bin/env python3

"""
unpack
~~~~~~
Extract SCCT UMD archives, Unreal packages (UTX / USX / UAX / UKX), and DARE audio.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from scct import __version__
from scct.common import ARCHIVE_SUFFIXES, MANIFEST_NAME, kind_from_path, pause_if_frozen
from scct.dare import extract_dare, peek_dare
from scct.package import extract_package, peek_package
from scct.umd import extract_umd, peek_umd


def _detect(path: Path) -> str:
    """Return 'umd', 'package', or 'dare' for a supported input file.

    Args:
        path (Path): File to inspect.

    Returns:
        str: Detected format.

    Raises:
        ValueError: If the file is not a supported archive.
    """
    if peek_umd(path):
        return "umd"
    if peek_package(path):
        return "package"
    if peek_dare(path):
        return "dare"
    raise ValueError(f"{path.name}: not a UMD, Unreal package, or DARE audio file")


def _default_output(src: Path) -> Path:
    """Extract next to the input file, into a folder named after its stem."""
    return src.parent / src.stem


def _already_unpacked(path: Path) -> bool:
    return (path.parent / path.stem / MANIFEST_NAME).is_file()


def _collect_archives(root: Path) -> list[Path]:
    """Group archives so maps/UMDs (directory extracts) run before packages."""
    files = [path for path in root.rglob("*") if path.is_file() and path.suffix.lower() in ARCHIVE_SUFFIXES]
    files = [path for path in files if not _already_unpacked(path)]
    maps = sorted((path for path in files if path.suffix.lower() in {".sm0", ".lm0"}), key=lambda p: str(p).lower())
    umds = sorted((path for path in files if path.suffix.lower() == ".umd"), key=lambda p: str(p).lower())
    packages = sorted(
        (path for path in files if path.suffix.lower() in {".utx", ".usx", ".uax", ".ukx"}),
        key=lambda p: str(p).lower(),
    )
    map_dirs = {path.parent for path in maps}
    streams = sorted(
        (
            path
            for path in files
            if path.suffix.lower() in {".ss0", ".ls0"} and path.parent not in map_dirs
        ),
        key=lambda p: str(p).lower(),
    )
    return maps + umds + packages + streams


def _get_parser() -> argparse.ArgumentParser:
    """Return argument parser for unpack.

    Returns:
        argparse.ArgumentParser: The argument parser.
    """
    parser = argparse.ArgumentParser(
        prog="unpack",
        description="Extract a SCCT UMD, Unreal package, or DARE audio file.",
    )
    parser.add_argument(
        "input_path",
        type=Path,
        nargs="?",
        metavar="input",
        help="Path to a .umd / .utx / .usx / .uax / .ukx / .sm0 / .lm0 / .ss0 / .ls0 file, or a directory with -r.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        dest="output_dir",
        default=None,
        help="Output directory. Default: <input_dir>/<filename_without_extension>.",
    )
    parser.add_argument(
        "-r",
        "--recursive",
        action="store_true",
        help="Extract container archives (UMD / SM0 / LM0) first, then unpack packages and leftover streams inside those directories.",
    )
    parser.add_argument(
        "--map",
        type=Path,
        dest="map_file",
        default=None,
        help="Path to MAPS.SM0 or MAPS.LM0 when it is not next to the SS0/LS0. Asked interactively if omitted.",
    )
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    return parser


def unpack_file(
    src: Path,
    dest: Path | None = None,
    recursive: bool = False,
    map_file: Path | None = None,
) -> Path:
    """Unpack `src` into `dest` (created if needed).

    Args:
        src (Path): Archive or package path.
        dest (Path | None): Output directory, or None for the default.
        recursive (bool): Unpack nested archives found in `dest`.
        map_file (Path | None): MAPS.SM0 / MAPS.LM0 when it is not next to an SS0/LS0.

    Returns:
        Path: Directory that received the files.
    """
    src = src.resolve()
    if not src.is_file():
        raise FileNotFoundError(f"input file not found: {src}")
    dest = (dest if dest is not None else _default_output(src)).resolve()
    kind = _detect(src)
    if kind == "umd":
        label = "umd"
    elif kind == "dare":
        label = peek_dare(src) or "dare"
    else:
        label = kind_from_path(src) or "package"
    print(f"unpack {src.name} ({label}) -> {dest}")
    if kind == "umd":
        count = extract_umd(src, dest)
        print(f"extracted {count} files")
    elif kind == "dare":
        count = extract_dare(src, dest, map_file)
        print(f"extracted {count} streams")
    else:
        count = extract_package(src, dest)
        print(f"extracted {count} exports")
    if recursive:
        unpack_tree(dest, recursive=True, map_file=map_file)
    return dest


def unpack_tree(root: Path, recursive: bool = True, map_file: Path | None = None) -> int:
    """Unpack archives under `root`: maps and UMDs first, then packages, then leftover streams.

    Each container extract is allowed to produce new nested archives; those are
    unpacked on the next pass so inner directories are decoded before any later
    sibling file is treated as a final payload.
    """
    root = root.resolve()
    total = 0
    while True:
        archives = _collect_archives(root)
        if not archives:
            return total
        maps = [path for path in archives if path.suffix.lower() in {".sm0", ".lm0"}]
        umds = [path for path in archives if path.suffix.lower() == ".umd"]
        if maps:
            batch = maps
        elif umds:
            batch = umds
        else:
            batch = archives
        for path in batch:
            unpack_file(path, path.parent / path.stem, recursive=False, map_file=map_file)
            total += 1
        if not recursive:
            return total


def _main(argv: list[str] | None = None) -> int:
    """Program entry.

    Args:
        argv (list[str] | None): Argument list without the program name.

    Returns:
        int: Process exit code.
    """
    parser = _get_parser()
    args = parser.parse_args(argv if argv is not None else sys.argv[1:])
    if args.input_path is None:
        parser.print_help()
        return 1
    src = args.input_path
    try:
        if src.is_dir():
            if not args.recursive:
                raise ValueError("a directory requires -r / --recursive")
            if args.output_dir is not None:
                raise ValueError("--output cannot be used with a directory input")
            count = unpack_tree(src, recursive=True, map_file=args.map_file)
            print(f"unpacked {count} archives")
        else:
            unpack_file(src, args.output_dir, recursive=args.recursive, map_file=args.map_file)
    except (OSError, ValueError, RuntimeError) as exc:
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
