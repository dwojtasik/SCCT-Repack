#!/usr/bin/env python3

"""
pack
~~~~
Rebuild SCCT UMD archives, Unreal packages, and DARE audio from unpack directories.
:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from scct import __version__
from scct.common import ALL_MODES, MANIFEST_NAME, kind_from_manifest, load_json, pause_if_frozen
from scct.dare import pack_dare
from scct.package import pack_package
from scct.umd import pack_umd

_KNOWN_EXT = {f".{mode}" for mode in ALL_MODES}


def _output_path(src_dir: Path, kind: str, output: str | None) -> Path:
    """Resolve the packed file path.

    Args:
        src_dir (Path): Directory being packed.
        kind (str): Lowercase kind (`umd`, `utx`, ...).
        output (str | None): Optional filename or path without requiring an extension.

    Returns:
        Path: Destination file including the kind extension.
    """
    ext = f".{kind}"
    if output is None:
        return src_dir.parent / f"{src_dir.name}{ext}"
    given = Path(output)
    stem = given.stem if given.suffix.lower() in _KNOWN_EXT else given.name
    if given.is_absolute() or given.parent != Path("."):
        return given.parent / f"{stem}{ext}"
    return src_dir.parent / f"{stem}{ext}"


def _manifest_dirs(root: Path) -> list[Path]:
    """Return unpack directories under `root`, deepest first."""
    root = root.resolve()
    found = {path.parent.resolve() for path in root.rglob(MANIFEST_NAME)}
    if (root / MANIFEST_NAME).is_file():
        found.add(root)

    def _depth(path: Path) -> tuple[int, str]:
        try:
            rel = path.relative_to(root)
        except ValueError:
            return (0, str(path).lower())
        return (-len(rel.parts), str(path).lower())

    return sorted(found, key=_depth)


def _get_parser() -> argparse.ArgumentParser:
    """Return argument parser for pack.

    Returns:
        argparse.ArgumentParser: The argument parser.
    """
    parser = argparse.ArgumentParser(
        prog="pack",
        description="Rebuild a SCCT UMD, Unreal package, or DARE audio file from an unpack.py directory.",
    )
    parser.add_argument(
        "input_dir",
        type=Path,
        metavar="input",
        help="Directory produced by unpack.py.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        dest="output_name",
        default=None,
        help="Output filename without extension. Default: name of the input directory.",
    )
    parser.add_argument(
        "-r",
        "--recursive",
        action="store_true",
        help="Encode nested unpack directories into packages first (deepest first), then write the top-level archive.",
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


def pack_dir(
    src_dir: Path,
    output_name: str | None = None,
    recursive: bool = False,
    map_file: Path | None = None,
) -> Path:
    """Pack `src_dir` using the kind stored in `_scct.json`.

    Args:
        src_dir (Path): Unpack directory.
        output_name (str | None): Optional output name/path without extension.
        recursive (bool): Encode nested unpack directories into packages first
            (deepest first), then this directory. A folder without `_scct.json`
            still packs every nested unpack tree.
        map_file (Path | None): MAPS.SM0 / MAPS.LM0 when it is not next to an SS0/LS0.

    Returns:
        Path: Written archive/package path.
    """
    src_dir = src_dir.resolve()
    if not src_dir.is_dir():
        raise FileNotFoundError(f"input directory not found: {src_dir}")
    last: Path | None = None
    if recursive:
        inner = [path for path in _manifest_dirs(src_dir) if path != src_dir]
        for path in inner:
            last = pack_dir(path, None, recursive=False, map_file=map_file)
        if not (src_dir / MANIFEST_NAME).is_file():
            if last is None:
                raise FileNotFoundError(
                    f"missing {MANIFEST_NAME} in {src_dir} "
                    "(this directory was not created by unpack.py)"
                )
            if output_name is not None:
                raise ValueError("--output requires a top-level unpack directory")
            return last
    kind = kind_from_manifest(load_json(src_dir / MANIFEST_NAME))
    dest = _output_path(src_dir, kind, output_name)
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"pack {kind} {src_dir.name} -> {dest}")
    if kind == "umd":
        count = pack_umd(src_dir, dest)
        print(f"packed {count} files ({dest.stat().st_size} bytes)")
    elif kind in {"sm0", "lm0", "ss0", "ls0"}:
        count = pack_dare(src_dir, dest, map_file)
        print(f"packed {count} streams ({dest.stat().st_size} bytes)")
    else:
        count = pack_package(src_dir, dest)
        print(f"packed {count} exports ({dest.stat().st_size} bytes)")
    return dest


def _main(argv: list[str] | None = None) -> int:
    """Program entry.

    Args:
        argv (list[str] | None): Argument list without the program name.

    Returns:
        int: Process exit code.
    """
    parser = _get_parser()
    args = parser.parse_args(argv if argv is not None else sys.argv[1:])
    try:
        pack_dir(args.input_dir, args.output_name, recursive=args.recursive, map_file=args.map_file)
    except (OSError, ValueError, RuntimeError, OverflowError) as exc:
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
