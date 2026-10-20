"""One-shot generator for embedded Magma bloom site tables."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(r"D:\Games\SCCT Remix\ANALYZE\Remaster\FIXES")))
from patch_fx_complete_2x import SITES  # noqa: E402

SKIP_P = ("hdr_", "draw_975", "draw_9766", "draw_984", "draw_98e", "draw_990")
SKIP_S = ("_64", "_32", "1_64", "1_32", "1p_64", "1p_32")


def skip(tag: str) -> bool:
    t = tag.lower()
    return any(t.startswith(p) for p in SKIP_P) or any(s in t for s in SKIP_S)


lines = [
    "# Auto-generated Magma bloom coarse sites (stock values). Do not edit by hand.",
    "COARSE_SITES: list[tuple[int, int, str]] = [",
]
for va, old, _new, kind, tag in SITES:
    if skip(tag):
        continue
    lines.append(f"    (0x{va:08X}, {old}, {kind!r}),  # {tag}")
lines.append("]")
out = Path(__file__).resolve().parent / "scct" / "remaster_sites.py"
out.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"wrote {out} ({sum(1 for L in lines if L.startswith('    ('))} sites)")
