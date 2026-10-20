#!/usr/bin/env python3

"""
remaster_patch
~~~~~~~~~~~~~~
Patch splintercell3.exe for Magma bloom quality via TLS + remaster.ini.

Installs a .rmst TLS callback that reads:

    [Bloom]
    Size=2048

and applies matched MagmaBloom create/draw/UV + fine-mip caves:

- Size from ``[Bloom] Size=``
- Fine64/32/16 = fixed 2048/1024/512 when Size > 256, else stock 64/32/16

Separate from the UMD 4/8 GiB ``patch_exe`` (LAA/SC8G). Safe to stack:
run ``patch_exe`` first for large UMD3, then ``remaster_patch_exe``.

:copyright: (c) 2026 by Dominik Wojtasik.
:license: MIT, see LICENSE for more details.
"""

from __future__ import annotations

import shutil
import struct
from pathlib import Path

from scct.remaster_sites import COARSE_SITES

IMGBASE = 0x10900000
RMST_NAME = b".rmst\x00\x00\x00"
RMST_SIZE = 0x10000
SCRM_SIG = b"SCRM"  # remaster bloom marker at start of .rmst payload
REMASTER_BAK_SUFFIX = ".remaster.bak"

IAT_GetModuleFileNameW = 0x110B7114
IAT_GetPrivateProfileIntW = 0x110B72F0
IAT_VirtualProtect = 0x110B71A8

# Stock Magma bloom create markers
STOCK_4BA8_VA = 0x10C8B3AF
STOCK_4BFC_W_VA = 0x10C8B37C
STOCK_4BFC_H_VA = 0x10C8B377

FINE_PAIR = [
    (0x10C8B3FD, 64, "create"),
    (0x10C8B41E, 32, "create"),
    (0x10C8B43F, 16, "create"),
    (0x10C8B4AE, 64, "create"),
    (0x10C8B4CF, 32, "create"),
    (0x10C90162, 64, "draw"),
    (0x10C901BB, 32, "draw"),
    (0x10C90209, 16, "draw"),
]
FINE_INTS = [
    (0x10C9BE05, 64),
    (0x10C9BE5E, 32),
    (0x10C9BEB2, 16),
    (0x10C9C4E6, 32),
    (0x10C9C4FD, 64),
]
FINE_SIZE_F = [
    (0x10C9BE69, 0x42800000, 64),
    (0x10C9BE74, 0x42800000, 64),
    (0x10C9BEBD, 0x42000000, 32),
    (0x10C9BEC8, 0x42000000, 32),
    (0x10C9C5E3, 0x41800000, 16),
    (0x10C9C638, 0x42000000, 32),
    (0x10C9C690, 0x42800000, 64),
]
FINE_UV_F = [
    (0x10C96CEE, 0x3C800000, 64),
    (0x10C96D0E, 0x3D000000, 32),
    (0x10C9C601, 0x3C800000, 64),
    (0x10C9DB6F, 0x3C800000, 64),
    (0x10C9DB9B, 0x3D000000, 32),
    (0x10CA29A2, 0x3C800000, 64),
    (0x111C2E78, 0x3D800000, 16),
    (0x111C2E7C, 0x3D800000, 16),
    (0x111C2E88, 0xBD800000, 16),
    (0x111C2E8C, 0x3D800000, 16),
    (0x111C2E98, 0xBD800000, 16),
    (0x111C2E9C, 0x3D800000, 16),
    (0x111C2EA8, 0x3D000000, 32),
    (0x111C2EAC, 0x3D000000, 32),
    (0x111C2EB8, 0xBD000000, 32),
    (0x111C2EBC, 0x3D000000, 32),
    (0x111C2EC8, 0xBD000000, 32),
    (0x111C2ECC, 0x3D000000, 32),
    (0x111C2ED8, 0x3C800000, 64),
    (0x111C2EDC, 0x3C800000, 64),
    (0x111C2EE8, 0xBC800000, 64),
    (0x111C2EEC, 0x3C800000, 64),
    (0x111C2EF8, 0xBC800000, 64),
    (0x111C2EFC, 0x3C800000, 64),
]
FINE_1P_F = [
    (0x10C96CF6, 0x3F820000, 64),
    (0x10C96D16, 0x3F840000, 32),
    (0x10C9DB7A, 0x3F820000, 64),
    (0x10C9DBA6, 0x3F840000, 32),
    (0x10CA29AD, 0x3F820000, 64),
]
EXTRA_FLOATS = [
    (0x10C96632, 0x3B800000),
    (0x10C96CAE, 0x3B800000),
    (0x10C96CB6, 0x3F808000),
    (0x10C96CCE, 0x3C000000),
    (0x10C96CD6, 0x3F810000),
    (0x10C97D57, 0x3B800000),
    (0x10C992CA, 0x3B800000),
    (0x10C96110, 0x3C000000),
    (0x10C96138, 0x3F810000),
    (0x10C96150, 0x3C000000),
    (0x10C96158, 0x3F810000),
    (0x111C2FA8, 0xBB800000),
    (0x111C2FAC, 0x3B800000),
]

BLOOM_QUALITIES = (256, 512, 1024, 2048, 4096)
DEFAULT_BLOOM_SIZE = 2048

REMASTER_INI = """; Default value: 256
; Qualities: [256, 512, 1024, 2048, 4096]
[Bloom]
Size={size}
"""


def _rva(va: int) -> int:
    return va - IMGBASE


def _align(v: int, a: int) -> int:
    return (v + a - 1) & ~(a - 1)


def _u16(s: str) -> bytes:
    return s.encode("utf-16-le") + b"\x00\x00"


def _f_from_u32(u: int) -> float:
    return struct.unpack("<f", struct.pack("<I", u))[0]


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
    coff = e_lfanew + 4
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    imgbase = struct.unpack_from("<I", data, opt + 28)[0]
    checksum_off = opt + 64
    nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    soh = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    return {
        "e_lfanew": e_lfanew,
        "coff": coff,
        "opt": opt,
        "magic": magic,
        "imgbase": imgbase,
        "checksum_off": checksum_off,
        "nsec": nsec,
        "sec0": e_lfanew + 24 + soh,
        "size_image_off": opt + 56,
        "tls_dir_off": opt + 96 + 9 * 8,
    }


def _find_rmst(data: bytes, pe: dict) -> tuple[int, int, int] | None:
    """Return (hdr_off, va, raw_off) for .rmst or None."""
    for i in range(pe["nsec"]):
        off = pe["sec0"] + i * 40
        if data[off : off + 8] == RMST_NAME:
            va = struct.unpack_from("<I", data, off + 12)[0]
            raw = struct.unpack_from("<I", data, off + 20)[0]
            return off, va, raw
    return None


def _stock_bloom_ok(data: bytes) -> bool:
    """True if coarse bloom creates still look Magma-stock (or remaster-restorable)."""
    def push_imm(va: int) -> int | None:
        o = _rva(va)
        if data[o] != 0x68:
            return None
        return struct.unpack_from("<I", data, o + 1)[0]

    return (
        push_imm(STOCK_4BA8_VA) == 256
        and push_imm(STOCK_4BFC_W_VA) == 256
        and push_imm(STOCK_4BFC_H_VA) == 600
    )


def _fine_stock_or_jmp(data: bytes) -> bool:
    for va, stock, kind in FINE_PAIR:
        o = _rva(va)
        if data[o] == 0xE9:
            continue
        if kind == "create":
            want = bytes([0x6A, stock, 0x6A, stock, 0x50])
        else:
            want = bytes([0x6A, stock, 0x6A, stock, 0x51, 0x50])
        if data[o : o + len(want)] != want:
            return False
    return True


def _coarse_tables() -> tuple[
    list[tuple[int, int]],
    list[tuple[int, int]],
    list[tuple[int, int]],
    list[tuple[int, int]],
]:
    ints: list[tuple[int, int]] = []
    size_f: list[tuple[int, int]] = []
    uv_f: list[tuple[int, int]] = []
    one_p: list[tuple[int, int]] = []
    seen: set[int] = set()

    def add_float(addr: int, stock: int) -> None:
        if addr in seen:
            return
        seen.add(addr)
        f = _f_from_u32(stock)
        af = abs(f)
        if af >= 8.0:
            size_f.append((addr, stock))
        elif af > 0.5:
            one_p.append((addr, stock))
        else:
            uv_f.append((addr, stock))

    for va, old, kind in COARSE_SITES:
        imm = va + 1 if kind == "push" else va
        if old <= 2400:
            if old in (256, 0x100):
                cls = 0
            elif old in (128, 0x80):
                cls = 1
            else:
                # Height (600) and other ints stay Magma stock — do not patch
                continue
            if imm not in seen:
                seen.add(imm)
                ints.append((imm, cls))
        else:
            add_float(imm, old)
    for va, stock in EXTRA_FLOATS:
        add_float(va, stock)
    return ints, size_f, uv_f, one_p


def _build_loader(lab: dict[str, int], loader_va: int) -> bytearray:
    code = bytearray()
    patches: list[tuple[int, str, str]] = []
    labels: dict[str, int] = {}

    def at() -> int:
        return loader_va + len(code)

    def emit(b: bytes | bytearray) -> None:
        code.extend(b)

    def i32(x: int) -> bytes:
        return struct.pack("<I", x & 0xFFFFFFFF)

    def call_iat(iat: int) -> None:
        emit(b"\xff\x15" + i32(iat))

    def L(name: str) -> None:
        labels[name] = at()

    def jmp8(n: str) -> None:
        patches.append((len(code), "jmp8", n))
        emit(b"\xeb\x00")

    def je8(n: str) -> None:
        patches.append((len(code), "je8", n))
        emit(b"\x74\x00")

    def jne8(n: str) -> None:
        patches.append((len(code), "jne8", n))
        emit(b"\x75\x00")

    def jg8(n: str) -> None:
        patches.append((len(code), "jg8", n))
        emit(b"\x7f\x00")

    def jz32(n: str) -> None:
        patches.append((len(code), "jz32", n))
        emit(b"\x0f\x84" + i32(0))

    def jnz32(n: str) -> None:
        patches.append((len(code), "jnz32", n))
        emit(b"\x0f\x85" + i32(0))

    def jmp32(n: str) -> None:
        patches.append((len(code), "jmp32", n))
        emit(b"\xe9" + i32(0))

    # TLS reason == 1
    emit(b"\x8b\x44\x24\x08")
    emit(b"\x83\xf8\x01")
    jz32("run")
    emit(b"\xc2\x0c\x00")
    L("run")
    emit(b"\xa1" + i32(lab["once"]))
    emit(b"\x85\xc0")
    jz32("do_it")
    emit(b"\xc2\x0c\x00")
    L("do_it")
    emit(b"\xc7\x05" + i32(lab["once"]) + i32(1))
    emit(b"\x60\x9c\xfc")

    # Build remaster.ini path next to EXE
    emit(b"\x68" + i32(260))
    emit(b"\x68" + i32(lab["ini_path"]))
    emit(b"\x6a\x00")
    call_iat(IAT_GetModuleFileNameW)
    emit(b"\xbb" + i32(lab["ini_path"]))
    emit(b"\x31\xc9")
    L("scan")
    emit(b"\x66\x8b\x03")
    emit(b"\x66\x85\xc0")
    je8("scan_done")
    emit(b"\x66\x3d\x5c\x00")
    je8("mark")
    emit(b"\x66\x3d\x2f\x00")
    jne8("scannext")
    L("mark")
    emit(b"\x89\xd9")
    L("scannext")
    emit(b"\x83\xc3\x02")
    jmp8("scan")
    L("scan_done")
    emit(b"\x85\xc9")
    jne8("has_slash")
    emit(b"\xb9" + i32(lab["ini_path"] - 2))
    L("has_slash")
    emit(b"\x8d\x79\x02")
    emit(b"\xbe" + i32(lab["name_ini"]))
    L("cpy")
    emit(b"\x66\xa5")
    emit(b"\x66\x83\x7e\xfe\x00")
    jne8("cpy")

    # Read [Bloom] Size (default 256)
    emit(b"\x68" + i32(lab["ini_path"]))
    emit(b"\x68" + i32(256))
    emit(b"\x68" + i32(lab["k_Size"]))
    emit(b"\x68" + i32(lab["s_Bloom"]))
    call_iat(IAT_GetPrivateProfileIntW)
    emit(b"\xa3" + i32(lab["cfg_size"]))

    # Clamp Size >= 16
    emit(b"\x83\xf8\x10")
    jg8("size_ok")
    emit(b"\xc7\x05" + i32(lab["cfg_size"]) + i32(256))
    L("size_ok")

    # Fine*: Size>256 → fixed 2048/1024/512; else stock 64/32/16
    # Height stays Magma default (600) — never written.
    emit(b"\xa1" + i32(lab["cfg_size"]))
    emit(b"\x3d" + i32(256))
    jg8("fine_hi")
    emit(b"\xc7\x05" + i32(lab["cfg_f64"]) + i32(64))
    emit(b"\xc7\x05" + i32(lab["cfg_f32"]) + i32(32))
    emit(b"\xc7\x05" + i32(lab["cfg_f16"]) + i32(16))
    jmp32("fine_done")
    L("fine_hi")
    emit(b"\xc7\x05" + i32(lab["cfg_f64"]) + i32(2048))
    emit(b"\xc7\x05" + i32(lab["cfg_f32"]) + i32(1024))
    emit(b"\xc7\x05" + i32(lab["cfg_f16"]) + i32(512))
    L("fine_done")

    # Unlock .text
    emit(b"\x68" + i32(lab["old_prot"]))
    emit(b"\x6a\x40")
    emit(b"\x68" + i32(0x700000))
    emit(b"\x68" + i32(0x10A00000))
    call_iat(IAT_VirtualProtect)

    # --- Coarse RT scale (always; Size=256 is identity) ---
    emit(b"\xdb\x05" + i32(lab["cfg_size"]))
    emit(b"\xdb\x05" + i32(lab["c_256"]))
    emit(b"\xde\xf9")
    emit(b"\xd9\x1d" + i32(lab["f_scale"]))

    emit(b"\xbe" + i32(lab["tbl_mb_int"]))
    L("mb_int")
    emit(b"\x8b\x1e")
    emit(b"\x85\xdb")
    je8("mb_int_done")
    emit(b"\x8b\x4e\x04")
    emit(b"\x85\xc9")
    je8("mb_wr_size")
    # cls 1 = half Size (128 stock)
    emit(b"\xa1" + i32(lab["cfg_size"]))
    emit(b"\xd1\xe8")
    emit(b"\x89\x03")
    jmp8("mb_int_next")
    L("mb_wr_size")
    emit(b"\xa1" + i32(lab["cfg_size"]))
    emit(b"\x89\x03")
    L("mb_int_next")
    emit(b"\x83\xc6\x08")
    jmp8("mb_int")
    L("mb_int_done")

    def float_loop(tbl: str, mode: str, done: str) -> None:
        emit(b"\xbe" + i32(lab[tbl]))
        L(tbl + "_lp")
        emit(b"\x8b\x1e")
        emit(b"\x85\xdb")
        je8(done)
        emit(b"\x8b\x46\x04")
        emit(b"\xa3" + i32(lab["f_tmp"]))
        emit(b"\xd9\x05" + i32(lab["f_tmp"]))
        if mode == "mul":
            emit(b"\xd8\x0d" + i32(lab["f_scale"]))
        elif mode == "div":
            emit(b"\xd8\x35" + i32(lab["f_scale"]))
        else:
            emit(b"\xd8\x25" + i32(lab["c_1"]))
            emit(b"\xd8\x35" + i32(lab["f_scale"]))
            emit(b"\xd8\x05" + i32(lab["c_1"]))
        emit(b"\xd9\x1b")
        emit(b"\x83\xc6\x08")
        jmp8(tbl + "_lp")

    float_loop("tbl_mb_szf", "mul", "mb_szf_done")
    L("mb_szf_done")
    float_loop("tbl_mb_uvf", "div", "mb_uvf_done")
    L("mb_uvf_done")
    float_loop("tbl_mb_1pf", "1p", "mb_1pf_done")
    L("mb_1pf_done")

    # --- Fine cave IMM + ints ---
    emit(b"\xbe" + i32(lab["tbl_fine_imm"]))
    L("mb_fimm")
    emit(b"\x8b\x1e")
    emit(b"\x85\xdb")
    je8("mb_fimm_done")
    emit(b"\x8b\x4e\x04")
    emit(b"\x83\xf9\x40")
    je8("mb_fi64")
    emit(b"\x83\xf9\x20")
    je8("mb_fi32")
    emit(b"\xa1" + i32(lab["cfg_f16"]))
    emit(b"\x89\x03")
    jmp8("mb_fi_next")
    L("mb_fi64")
    emit(b"\xa1" + i32(lab["cfg_f64"]))
    emit(b"\x89\x03")
    jmp8("mb_fi_next")
    L("mb_fi32")
    emit(b"\xa1" + i32(lab["cfg_f32"]))
    emit(b"\x89\x03")
    L("mb_fi_next")
    emit(b"\x83\xc6\x08")
    jmp8("mb_fimm")
    L("mb_fimm_done")

    emit(b"\xbe" + i32(lab["tbl_fine_int"]))
    L("mb_fint")
    emit(b"\x8b\x1e")
    emit(b"\x85\xdb")
    je8("mb_fint_done")
    emit(b"\x8b\x4e\x04")
    emit(b"\x83\xf9\x40")
    je8("mb_fint64")
    emit(b"\x83\xf9\x20")
    je8("mb_fint32")
    emit(b"\xa1" + i32(lab["cfg_f16"]))
    emit(b"\x89\x03")
    jmp8("mb_fint_next")
    L("mb_fint64")
    emit(b"\xa1" + i32(lab["cfg_f64"]))
    emit(b"\x89\x03")
    jmp8("mb_fint_next")
    L("mb_fint32")
    emit(b"\xa1" + i32(lab["cfg_f32"]))
    emit(b"\x89\x03")
    L("mb_fint_next")
    emit(b"\x83\xc6\x08")
    jmp8("mb_fint")
    L("mb_fint_done")

    def fine_float_loop(tbl: str, mode: str, done: str) -> None:
        emit(b"\xbe" + i32(lab[tbl]))
        L(tbl + "_lp")
        emit(b"\x8b\x1e")
        emit(b"\x85\xdb")
        je8(done)
        emit(b"\x8b\x4e\x08")
        emit(b"\x89\x0d" + i32(lab["f_tmp"]))
        emit(b"\x83\xf9\x40")
        je8(tbl + "_s64")
        emit(b"\x83\xf9\x20")
        je8(tbl + "_s32")
        emit(b"\xdb\x05" + i32(lab["cfg_f16"]))
        jmp8(tbl + "_div")
        L(tbl + "_s64")
        emit(b"\xdb\x05" + i32(lab["cfg_f64"]))
        jmp8(tbl + "_div")
        L(tbl + "_s32")
        emit(b"\xdb\x05" + i32(lab["cfg_f32"]))
        L(tbl + "_div")
        emit(b"\xdb\x05" + i32(lab["f_tmp"]))
        emit(b"\xde\xf9")
        emit(b"\xd9\x1d" + i32(lab["f_scale"]))
        emit(b"\x8b\x46\x04")
        emit(b"\xa3" + i32(lab["f_tmp"]))
        emit(b"\xd9\x05" + i32(lab["f_tmp"]))
        if mode == "mul":
            emit(b"\xd8\x0d" + i32(lab["f_scale"]))
        elif mode == "div":
            emit(b"\xd8\x35" + i32(lab["f_scale"]))
        else:
            emit(b"\xd8\x25" + i32(lab["c_1"]))
            emit(b"\xd8\x35" + i32(lab["f_scale"]))
            emit(b"\xd8\x05" + i32(lab["c_1"]))
        emit(b"\xd9\x1b")
        emit(b"\x83\xc6\x0c")
        jmp8(tbl + "_lp")

    fine_float_loop("tbl_fine_szf", "mul", "mb_fszf_done")
    L("mb_fszf_done")
    fine_float_loop("tbl_fine_uvf", "div", "mb_fuvf_done")
    L("mb_fuvf_done")
    fine_float_loop("tbl_fine_1pf", "1p", "mb_f1pf_done")
    L("mb_f1pf_done")

    L("fini")
    emit(b"\x9d\x61")
    emit(b"\xc2\x0c\x00")

    for off, kind, name in patches:
        target = labels[name]
        src = loader_va + off
        if kind.endswith("8"):
            rel = target - (src + 2)
            if not -128 <= rel <= 127:
                raise RuntimeError(f"rel8 {name}={rel}")
            code[off + 1] = rel & 0xFF
        elif kind in ("jz32", "jnz32"):
            struct.pack_into("<i", code, off + 2, target - (src + 6))
        elif kind == "jmp32":
            struct.pack_into("<i", code, off + 1, target - (src + 5))
        else:
            raise RuntimeError(kind)
    return code


def _build_rmst(section_va: int) -> tuple[bytearray, int, list[tuple[int, int, int, str]]]:
    data = bytearray()
    lab: dict[str, int] = {}
    mb_int, mb_szf, mb_uvf, mb_1pf = _coarse_tables()
    fine_jmps: list[tuple[int, int, int, str]] = []
    fine_imm: list[tuple[int, int]] = []

    def here() -> int:
        return section_va + len(data)

    def al(n: int = 4) -> None:
        while len(data) % n:
            data.append(0)

    def label(name: str) -> None:
        lab[name] = here()

    def put(b: bytes | bytearray) -> None:
        data.extend(b)

    def put_dd(v: int = 0) -> None:
        al(4)
        put(struct.pack("<I", v & 0xFFFFFFFF))

    def put_pairs(rows: list[tuple[int, int]]) -> None:
        for a, b in rows:
            put(struct.pack("<II", a & 0xFFFFFFFF, b & 0xFFFFFFFF))
        put(struct.pack("<II", 0, 0))

    def put_triples(rows: list[tuple[int, int, int]]) -> None:
        for a, b, c in rows:
            put(struct.pack("<III", a & 0xFFFFFFFF, b & 0xFFFFFFFF, c & 0xFFFFFFFF))
        put(struct.pack("<III", 0, 0, 0))

    # Signature
    put(SCRM_SIG)
    al(4)

    label("tls_index")
    put_dd(0)
    label("tls_callbacks")
    put_dd(0)
    put_dd(0)
    label("tls_dir")
    put(struct.pack("<IIIIII", 0, 0, 0, 0, 0, 0))

    label("once")
    put_dd(0)
    label("old_prot")
    put_dd(0)
    label("cfg_size")
    put_dd(256)
    label("cfg_f64")
    put_dd(64)
    label("cfg_f32")
    put_dd(32)
    label("cfg_f16")
    put_dd(16)
    label("f_scale")
    put_dd(0)
    label("f_tmp")
    put_dd(0)
    label("c_256")
    put_dd(256)
    label("c_1")
    put(struct.pack("<f", 1.0))

    label("ini_path")
    put(b"\x00" * 520)
    label("s_Bloom")
    put(_u16("Bloom"))
    label("k_Size")
    put(_u16("Size"))
    label("name_ini")
    put(_u16("remaster.ini"))

    al(4)
    label("tbl_mb_int")
    put_pairs(mb_int)
    label("tbl_mb_szf")
    put_pairs(mb_szf)
    label("tbl_mb_uvf")
    put_pairs(mb_uvf)
    label("tbl_mb_1pf")
    put_pairs(mb_1pf)
    label("tbl_fine_int")
    put_pairs(FINE_INTS)
    label("tbl_fine_szf")
    put_triples(FINE_SIZE_F)
    label("tbl_fine_uvf")
    put_triples(FINE_UV_F)
    label("tbl_fine_1pf")
    put_triples(FINE_1P_F)

    al(16)
    for site_va, stock, kind in FINE_PAIR:
        entry = here()
        stolen = 5 if kind == "create" else 6
        mid = b"\x50" if kind == "create" else b"\x51\x50"
        resume = site_va + stolen
        put(b"\x68")
        fine_imm.append((here(), stock))
        put(struct.pack("<I", stock))
        put(b"\x68")
        fine_imm.append((here(), stock))
        put(struct.pack("<I", stock))
        put(mid)
        put(b"\xe9")
        put(struct.pack("<i", resume - (here() + 4)))
        fine_jmps.append((site_va, entry, stock, kind))

    al(4)
    label("tbl_fine_imm")
    put_pairs(fine_imm)

    al(16)
    code_off = len(data)
    code_va = section_va + code_off
    put(_build_loader(lab, code_va))

    struct.pack_into("<I", data, lab["tls_callbacks"] - section_va, code_va)
    struct.pack_into(
        "<IIIIII",
        data,
        lab["tls_dir"] - section_va,
        0,
        0,
        lab["tls_index"],
        lab["tls_callbacks"],
        0,
        0,
    )
    if len(data) > RMST_SIZE:
        raise RuntimeError(f".rmst overflow {len(data):#x} > {RMST_SIZE:#x}")
    put(bytes(RMST_SIZE - len(data)))
    return data, lab["tls_dir"] - IMGBASE, fine_jmps


def _append_or_reuse_rmst(data: bytearray, pe: dict) -> tuple[int, int, int]:
    """Return (section_va, raw_off, hdr_off)."""
    found = _find_rmst(data, pe)
    if found is not None:
        hdr, va, raw = found
        # Resize virtual/raw to RMST_SIZE if needed
        struct.pack_into("<I", data, hdr + 8, RMST_SIZE)
        struct.pack_into("<I", data, hdr + 16, RMST_SIZE)
        if raw + RMST_SIZE > len(data):
            data.extend(bytes(raw + RMST_SIZE - len(data)))
        return IMGBASE + va, raw, hdr

    e = pe["e_lfanew"]
    nsec = pe["nsec"]
    last = pe["sec0"] + (nsec - 1) * 40
    last_vsz, last_va, last_rsz = struct.unpack_from("<III", data, last + 8)
    new_va = _align(last_va + max(last_vsz, last_rsz), 0x1000)
    new_raw = _align(len(data), 0x1000)
    if len(data) < new_raw:
        data.extend(bytes(new_raw - len(data)))
    data.extend(bytes(RMST_SIZE))
    hdr = pe["sec0"] + nsec * 40
    # Ensure PE header has room for another section (usually does)
    data[hdr : hdr + 8] = RMST_NAME
    struct.pack_into("<IIIIII", data, hdr + 8, RMST_SIZE, new_va, RMST_SIZE, new_raw, 0, 0)
    struct.pack_into("<HH", data, hdr + 32, 0, 0)
    struct.pack_into("<I", data, hdr + 36, 0xE0000020)
    struct.pack_into("<H", data, e + 6, nsec + 1)
    struct.pack_into("<I", data, pe["size_image_off"], _align(new_va + RMST_SIZE, 0x1000))
    return IMGBASE + new_va, new_raw, hdr


def _install_fine_jmps(data: bytearray, fine_jmps: list[tuple[int, int, int, str]]) -> None:
    for site_va, cave_va, stock, kind in fine_jmps:
        off = _rva(site_va)
        if kind == "create":
            want = bytes([0x6A, stock, 0x6A, stock, 0x50])
            stolen = 5
        else:
            want = bytes([0x6A, stock, 0x6A, stock, 0x51, 0x50])
            stolen = 6
        cur = bytes(data[off : off + stolen])
        if cur[0] == 0xE9:
            # Already trampolined — retarget
            data[off] = 0xE9
            struct.pack_into("<i", data, off + 1, cave_va - (site_va + 5))
            if stolen == 6:
                data[off + 5] = 0x90
            continue
        if cur != want:
            raise RuntimeError(
                f"fine site @{site_va:#x}: unexpected {cur.hex()} (want {want.hex()})"
            )
        data[off] = 0xE9
        struct.pack_into("<i", data, off + 1, cave_va - (site_va + 5))
        if stolen == 6:
            data[off + 5] = 0x90


def remaster_bak_path(exe: Path) -> Path:
    return Path(str(exe) + ".remaster.bak")


def write_remaster_ini(ini_path: Path, size: int = DEFAULT_BLOOM_SIZE) -> None:
    ini_path.write_text(REMASTER_INI.format(size=size), encoding="utf-8")


def patch_remaster_exe(path: Path, bloom_size: int = DEFAULT_BLOOM_SIZE) -> list[str]:
    """Install Magma bloom remaster TLS + write remaster.ini next to the EXE.

    Args:
        path: Path to splintercell3.exe.
        bloom_size: Default [Bloom] Size written to remaster.ini.

    Returns:
        Human-readable log lines.
    """
    if bloom_size not in BLOOM_QUALITIES:
        raise RuntimeError(f"Bloom Size {bloom_size} invalid (use {list(BLOOM_QUALITIES)})")

    path = path.resolve()
    bak = remaster_bak_path(path)
    log: list[str] = []

    # Re-patch from remaster backup when present so fine sites are stock again
    if bak.is_file():
        data = bytearray(bak.read_bytes())
        log.append(f"base {bak}")
    else:
        data = bytearray(path.read_bytes())
        shutil.copy2(path, bak)
        log.append(f"backup {bak}")

    pe = _pe_fields(data)
    if pe["magic"] != 0x10B:
        raise RuntimeError(f"{path}: not PE32")
    if pe["imgbase"] != IMGBASE:
        raise RuntimeError(
            f"{path}: ImageBase {pe['imgbase']:#x} (expected {IMGBASE:#x}). "
            "This remaster patch is only for splintercell3.exe ImageBase 0x10900000."
        )
    if not _stock_bloom_ok(data):
        raise RuntimeError(
            f"{path}: Magma bloom creates are not stock (256/600). "
            "Restore a clean Magma EXE (or delete the remaster and UMD bak if wrong)."
        )
    if not _fine_stock_or_jmp(data):
        raise RuntimeError(f"{path}: fine mip create/draw sites unrecognized")

    section_va, raw_off, _hdr = _append_or_reuse_rmst(data, pe)
    # Refresh pe.nsec after possible section add
    pe = _pe_fields(data)
    payload, tls_rva, fine_jmps = _build_rmst(section_va)
    data[raw_off : raw_off + RMST_SIZE] = payload
    _install_fine_jmps(data, fine_jmps)

    struct.pack_into("<II", data, pe["tls_dir_off"], tls_rva, 24)
    csum = _pe_checksum(data, pe["checksum_off"])
    struct.pack_into("<I", data, pe["checksum_off"], csum)

    path.write_bytes(data)
    ini = path.parent / "remaster.ini"
    write_remaster_ini(ini, bloom_size)
    log.append(f"SCRM Magma bloom TLS @ {section_va:#x} ({RMST_SIZE:#x} bytes)")
    log.append(f"fine caves {len(fine_jmps)} sites")
    log.append(f"wrote {path} ({len(data)} bytes) CheckSum={csum:#x}")
    log.append(f"wrote {ini} [Bloom] Size={bloom_size}")
    return log


def restore_remaster_exe(path: Path) -> list[str]:
    """Restore EXE from ``.remaster.bak`` and remove remaster.ini."""
    path = path.resolve()
    bak = remaster_bak_path(path)
    if not bak.is_file():
        raise RuntimeError(f"backup not found: {bak}")
    shutil.copy2(bak, path)
    log = [f"restored {bak} -> {path}"]
    ini = path.parent / "remaster.ini"
    if ini.is_file():
        ini.unlink()
        log.append(f"removed {ini}")
    return log
