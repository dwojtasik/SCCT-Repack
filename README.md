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

## Splinter Cell Chaos Theory Remastered

With use of SCCT-Repack I was able to prepare some quality improvements for Splinter Cell Chaos Theory:

- Support for UMD archives >8GB instead of roughly 2GB.
- AI 4x upscaled albedo textures with various models (Remacri, HDCube, UltraSharp, AoMR).
- New high resolution (4x) normal (PBRnxt) and specular (FrankenMap MMG) maps mixed with original ones for a look faithful to the original.
- New high resolution bloom and pyramid mips for high quality vision modes without any shimmering (requires to disable high quality soft shadows).
  - This includes remaster.ini with different qualities from default 256 up to 4096. You can also use value in between e.g. 320.
  - **NOTE**: Upscaled mips results in [overall reduced blur](https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_b.jpg&l_label=4x%20AI%20upscale&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_a.jpg&r_label=4x%20AI%20upscale%20+%20Remaster%20EXE) e.g. in background of UI activities like PC browsing or hacking.

Modification was tested on Steam version in 4K and 1440p resolution but should work with any version and resolution.

### Installation of remastered version

- Patch your `splintercell3.exe` with `patch_exe.exe` to allow read of >8GB UMD archives.
  - To install patch simply copy it into game system directory and run next to `splintercell3.exe`.
- [Optional] Patch your `splintercell3.exe` with `remaster_patch_exe.exe` and modify generated `remaster.ini` config to get high quality bloom and vision modes.
  - Go into game display settings -> advanced -> shader options and disable high quality soft shadows.
    Currently improvements of bloom & pyramid mips are not compatible with this effect.
- Download and replace `dynamic-pc.umd` with HQ textures from [here](https://drive.google.com/file/d/19QDV_Q9gmzjbWZbPPT-kQKCeH4WJVzZ-).

### Remastered visuals **(click in table to open slider view)**

<p align="center">
  <img width="600" height="600" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/comparison.gif?raw=true"/>
  <br>
  <b>Textures comparison</b>
</p>

<p align="center">
  <img width="600" height="600" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/vision.gif?raw=true"/>
  <br>
  <b>Vision qualities comparison</b>
</p>

<p align="center">
  <img width="100%" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/shimmer.gif?raw=true"/>
  <br>
  <b>Bloom shimmering reduction</b>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/offplace_bloom.jpg" width="100%">
  <br>
  <b>Bloom placement correction</b>
</p>

<p align="center">
  <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_b.jpg&l_label=4x AI upscale&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_a.jpg&r_label=4x AI upscale + Remaster EXE">
    <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/clean_text.jpg" width="100%">
  </a>
  <br>
  <b>Text clarity (click to open slider view)</b>
</p>

<table>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_1_a.jpg" width="100%">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_2_a.jpg" width="100%">
      </a>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_3_a.jpg" width="100%">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_a.jpg&r_label=4x AI upscale">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_4_a.jpg" width="100%">
      </a>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_b.jpg&l_label=4x AI upscale&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_a.jpg" width="100%">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_b.jpg&l_label=4x AI upscale&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_a.jpg" width="100%">
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

Unmodified UMD files pack back at their original offsets, including unlisted padding the TOC does not name (`_scct_gaps/`). That padding is required for a byte-identical `dynamic-pc.umd` (the stock archive stores extra localization and Magma bytes between files). If any payload size changes, the UMD is rebuilt tightly and that padding is dropped. A tight rebuild past 4 GiB uses **UMD3** (TOC at the start so Magma can still parse it, payloads may sit past 4 GiB).

When packing packages, Texture mip `TLazyArray` skip offsets are rewritten so the engine still seeks to the correct bytes after serial sizes change.

## Patch the game EXE

Unpatched `splintercell3.exe` cannot reliably read UMDs larger than **~2 GiB**. `patch_exe` sets Large Address Aware, switches file seeks / size compares from signed to unsigned, and adds Magma 64-bit I/O for **UMD3** archives **past 4 GiB, including files larger than 8 GiB**. Stock and remaster v1/v2 UMDs keep the original Magma path.

```text
python3 patch_exe.py [path\to\splintercell3.exe]
python3 patch_exe.py --restore [path\to\splintercell3.exe]
```

If no path is given, `.\splintercell3.exe` or `.\System\splintercell3.exe` is used. A `.bak` copy of the original EXE is written on the first run (`--restore` copies it back). You can also drag-and-drop the EXE onto `patch_exe.exe`.

This patch is **only** for the PE32 `splintercell3.exe` with `ImageBase 0x10900000`. Other EXEs are rejected.

## Size limits


| Limit      | What hits it                                                                                                                                                                               |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **~2 GiB** | Unpatched PE32: ~2 GiB user virtual address space, and `Seek` sign-extends file offsets. A UMD past 2 GiB will fail to open or look already at EOF.                                        |
| **~4 GiB** | UMD **v1/v2** table of contents uses **uint32** offsets, sizes, and the 16-byte trailer file-size field. After the unsigned-seek half of `patch_exe`, the game can read these up to 4 GiB. |
| **>8 GiB** | **UMD3** (written automatically when a rebuilt UMD would exceed 4 GiB): 32-byte trailer, TOC kept below 4 GiB, payload offsets past 4 GiB stored in the TOC flags slot as `offset >> 32`. Magma 64-bit I/O from `patch_exe` (SC8G) has **no 8 GiB cap** (tested past 8 GiB). Remaining limits: ~4 GiB **per contained file** (uint32 size), TOC itself below 4 GiB, NTFS. |


`unpack.py` reads v1/v2 and UMD3. `pack.py` writes v1/v2 when the archive still fits; it upgrades to UMD3 when it would not, or when the source was already UMD3.

## Build Windows EXEs

```text
setup_venv.bat
build.bat
```

Produces `dist\unpack.exe`, `dist\pack.exe`, `dist\patch_exe.exe`, and `dist\remaster_patch_exe.exe` (PyInstaller onefile, console).

`remaster_patch_exe` is separate from the UMD 4/8 GiB `patch_exe`: it installs Magma bloom TLS + writes `remaster.ini` (`[Bloom] Size=`). Stack after `patch_exe` when both are needed.

## Layout

```text
unpack.py              CLI
pack.py                CLI
patch_exe.py           CLI (UMD LAA / SC8G)
remaster_patch_exe.py  CLI (Magma bloom remaster.ini TLS)
scct/                  UMD + Unreal package + DARE audio + EXE patch internals
setup_venv.bat         create .venv and install build deps
build.bat              PyInstaller onefile builds
```

