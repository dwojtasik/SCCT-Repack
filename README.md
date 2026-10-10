# SCCT-Repack

THIS IS EXPERIMENTAL TOOL ONLY!

Current version can reliably replace textures and sounds. Static meshes still in-progress.

Unpack and pack Splinter Cell Chaos Theory archives from the command line:

- **UMD** — `dynamic-pc.umd` (game data archive)
- **UTX** — textures (PNG)
- **USX** — static meshes (OBJ, BIN)
- **UAX** — sound cues (JSON; WAV / OGG on stock UE2)
- **UKX** — skeletal meshes / animations (JSON, BIN)
- **SM0 / LM0 + SS0 / LS0** — Dare Audio banks (PCM / UBI IMA / OGG)

Decoded exports:

- **UTX Texture** → **PNG** plus a `.bin` template. Unmodified PNGs pack back as the original serial bytes (fonts/UI stay intact). Edited PNGs are recooked (DXT1/DXT3/DXT5/RGBA8/…)
- **USX StaticMesh** → **OBJ** plus a `.bin` template (pack patches vertices/UVs/indices from the OBJ; collision stays on the template)
- **UAX Sound** → **JSON** Dare cue records (`cue_id`, `bank_id`, optional streamed `.bin` name, extra bytes). These are play/stop events, not WAV samples
- **UKX MeshAnimation** → **JSON** (bones, sequence names/frames/rate/notifies, motion-chunk headers; compressed analog keys stay as `analog_hex`). Skeletal meshes and notifies stay as engine serial `.bin`
- **SM0 / SS0** → **WAV** (PCM16 and UBI IMA) or **OGG**. Keep `MAPS.SM0` next to the `*.SS0` files (or `MAPS.LM0` next to `*.LS0`). If the map is not in that folder, unpack/pack asks for the path (or takes `--map`). Unpacking a map writes streams from every companion SS0. Unmodified WAV/OGG files pack back as the original stream bytes; edited PCM/IMA/OGG is recooked (internal SM0 PCM must keep the same byte size)

Keep each StaticMesh `.bin` next to its `.obj`; packing reads both.

Requires Python 3.10+ and Pillow, or a compiled EXE.

## Results **(click to open slider view)**

AI 4x upscaled textures (without coop). Modified `dynamic-pc.umd` can be downloaded [here](https://drive.google.com/file/d/1HAA6x32T-Ib4GUKgtLK3sR-42R7c9RnT).

<table>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_a.jpg" width="100%" alt="Bricks">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_a.jpg" width="100%" alt="Wall">
      </a>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_a.jpg" width="100%" alt="Bamboo">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_a.jpg" width="100%" alt="Tiles">
      </a>
    </td>
  </tr>
</table>

## Unpack

```text
python3 unpack.py <path_to_file.abc> [--output <output_dir>] [-r] [--map <MAPS.SM0|MAPS.LM0>]
```

`output_dir` is optional. By default files go next to the input, into a folder named after the file without the extension:

```text
python3 unpack.py Data\Textures\01_LightHouse_TEX.utx
# -> Data\Textures\01_LightHouse_TEX\
```

`-r` extracts container archives first (UMD / SM0 / LM0 → directories), then unpacks packages and leftover streams inside those directories. Pass a directory with `-r` to walk it the same way.

```text
python3 unpack.py Data\Sounds\Amb_01_Beach.SS0
# -> Data\Sounds\Amb_01_Beach\  (uses MAPS.SM0 in that folder)

python3 unpack.py TEST\Menu.SS0 --map Data\Sounds\MAPS.SM0
# or drop Menu.SS0 on unpack.exe and paste the MAPS.SM0 path when asked

python3 unpack.py extracted\dynamic-pc.umd -r
# extract UMD, then unpack every UTX/USX/UAX/UKX inside
```

Drag-and-drop a `.umd` / `.utx` / `.usx` / `.uax` / `.ukx` / `.sm0` / `.ss0` onto `unpack.exe` (same default output).

Each unpack directory contains `_scct.json` (required to pack again) plus the payloads. Replace `exports/*.png`, `exports/*.obj`, `exports/*.wav`, `exports/*.ogg`, or `exports/*.json` (Sound / MeshAnimation) before packing.

## Pack

```text
python3 pack.py <path_to_dir> [--output <filename>] [-r] [--map <MAPS.SM0|MAPS.LM0>]
```

Archive kind (UMD / UTX / USX / UAX / UKX / SM0 / SS0 / …) is read from `_scct.json` (`format`, written by unpack). The matching extension is added automatically.

`filename` is optional and should be **without** extension. By default the directory name is used, and the file is written next to that directory:

```text
python3 pack.py Data\Textures\01_LightHouse_TEX
# -> Data\Textures\01_LightHouse_TEX.utx

python3 pack.py extracted\dynamic-pc --output dynamic-pc
# -> extracted\dynamic-pc.umd

python3 pack.py Data\Sounds\Amb_01_Beach
# -> Data\Sounds\Amb_01_Beach.ss0
```

`-r` encodes nested unpack directories into packages first (deepest first), then writes the top-level archive. If the input folder has no `_scct.json` of its own, only the nested packages are written.

```text
python3 pack.py extracted\dynamic-pc -r
# pack every nested UTX/USX/UAX/UKX, then the UMD
```

If `MAPS.SM0` / `MAPS.LM0` is not next to the SS0/LS0 being packed, pack asks for that path (or use `--map`). Pack rebuilds from the unpacked WAV/OGG files; the original SS0/LS0 is optional (used only to copy unchanged encoded bytes). Edited audio is recooked; if a stream’s size changes, pack the SS0 back into the folder that contains the map so offsets are updated. Internal SM0 PCM must keep the same byte size.

Replace files that already exist in the unpack tree (PNG / OBJ / WAV / OGG / Sound JSON / MeshAnimation JSON). New TOC/export entries are **not** invented: only paths listed in `_scct.json` are packed. Extra files in the folder are ignored.

Unmodified UMD files pack back at their original offsets, including unlisted padding the TOC does not name (`_scct_gaps/`). That padding is required for a byte-identical `dynamic-pc.umd` (the stock archive stores extra localization and Magma bytes between files). If any payload size changes, the UMD is rebuilt tightly and that padding is dropped.

When packing packages, Texture mip `TLazyArray` skip offsets are rewritten so the engine still seeks to the correct bytes after serial sizes change.

## Patch the game EXE

Unpatched `splintercell3.exe` cannot reliably read UMDs larger than **~2 GiB**. `patch_exe` sets Large Address Aware and switches file seeks / size compares from signed to unsigned so the process can use archives up to the UMD format cap.

```text
python3 patch_exe.py [path\to\splintercell3.exe]
```

If no path is given, `.\splintercell3.exe` or `.\System\splintercell3.exe` is used. A `.bak` copy is written on the first run. You can also drag-and-drop the EXE onto `patch_exe.exe`.

This patch is **only** for the PE32 `splintercell3.exe` with `ImageBase 0x10900000`. Other EXEs are rejected.

## Size limits


| Limit      | What hits it                                                                                                                                                                               |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **~2 GiB** | Unpatched PE32: ~2 GiB user virtual address space, and `Seek` sign-extends file offsets. A UMD past 2 GiB will fail to open or look already at EOF.                                        |
| **~4 GiB** | UMD table of contents uses **uint32** offsets, sizes, and the trailer file-size field. Packing a UMD refuses to write past this. After `patch_exe`, the game can read UMDs up to this cap. |


The 4 GiB ceiling is a **format** limit, not something the EXE patch can raise.

## Build Windows EXEs

```text
setup_venv.bat
build.bat
```

Produces `dist\unpack.exe`, `dist\pack.exe`, and `dist\patch_exe.exe` (PyInstaller onefile, console).

## Layout

```text
unpack.py          CLI
pack.py            CLI
patch_exe.py       CLI
scct/              UMD + Unreal package + DARE audio + EXE patch internals
setup_venv.bat     create .venv and install build deps
build.bat          PyInstaller onefile builds
```

