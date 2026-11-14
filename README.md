# SCCT-Repack

THIS IS EXPERIMENTAL TOOL ONLY!

Current version can reliably replace textures and sounds.</br>
Static meshes can be edited, including topology changes (subdivision, added/removed faces); test edited meshes in game.

Unpack and pack Splinter Cell Chaos Theory archives from the command line:

- **UMD** — `dynamic-pc.umd` (game data archive)
- **UTX** — textures (PNG)
- **USX** — static meshes (OBJ, BIN)
- **UAX** — sound cues (JSON; WAV / OGG on stock UE2)
- **UKX** — skeletal meshes / animations (JSON, BIN)
- **SM0 / LM0 + SS0 / LS0** — Dare Audio banks (PCM / UBI IMA / OGG)

Decoded exports:

- **UTX Texture** → **PNG** plus a `.bin` template. Unmodified PNGs pack back as the original serial bytes (fonts/UI stay intact). Edited PNGs are recooked (DXT1/DXT3/DXT5/RGBA8/…)
- **USX StaticMesh** → **OBJ** (v2: positions, UVs, normals, vertex colors, one group per material section) plus a `.bin` template. Pack rebuilds the vertex / index / wireframe / section streams from the OBJ (any topology, up to 65535 vertices); collision stays on the template. See [StaticMesh OBJ](#staticmesh-obj)
- **UAX Sound** → **JSON** Dare cue records (`cue_id`, `bank_id`, optional streamed `.bin` name, extra bytes). These are play/stop events, not WAV samples
- **UKX MeshAnimation** → **JSON** (bones, sequence names/frames/rate/notifies, motion-chunk headers; compressed analog keys stay as `analog_hex`). Skeletal meshes and notifies stay as engine serial `.bin`
- **SM0 / SS0** → **WAV** (PCM16 and UBI IMA) or **OGG**. Keep `MAPS.SM0` next to the `*.SS0` files (or `MAPS.LM0` next to `*.LS0`). If the map is not in that folder, unpack/pack asks for the path (or takes `--map`). Unpacking a map writes streams from every companion SS0. Unmodified WAV/OGG files pack back as the original stream bytes; edited PCM/IMA/OGG is recooked (internal SM0 PCM must keep the same byte size)

Keep each StaticMesh `.bin` next to its `.obj`; packing reads both.

Requires Python 3.10+ and Pillow, or a compiled EXE.

For compiling remaster DLL MinGW-w64 i686 g++ and windres are needed.

## Splinter Cell Chaos Theory Remastered

With use of SCCT-Repack I was able to prepare some quality improvements for Splinter Cell Chaos Theory:

- Support for UMD archives >8GB instead of roughly 2GB.
- AI 4x upscaled albedo textures with various models (Remacri, HDCube, UltraSharp, AoMR).
- New high resolution (4x) normal (PBRnxt) and specular (FrankenMap MMG) maps mixed with original ones for a look faithful to the original.
- New static meshes in all levels that are using either CC0/CC4 free models or game ones but decimated, smoothed and with baked normals for best quality. Includes also meshes built-in into level geometry like arched windows, cables or ropes (currently only in **Lighthouse**, **Cargo ship** and **Bank** level).
- Remastered bloom (progressive mip-chain bloom) without blocky artifacts or shimmering when the camera moves, at any resolution.
- New high resolution pyramid mips for high quality vision modes.
- Bink videos support up to 4K (stock is 640×480).
  - Links to remastered 4K videos are in [installation details](#installation-of-remastered-version-and-best-settings).
- High FPS physics fix (`HighFpsForcedTickrate=35` in `Remaster.ini`).
  - Fixes ropes / hanging objects that are swaying too fast due to too high tickrate.
  - Possibly also solves issues with NPC pathfinding on high FPS.
- Customizable dialog subtitles overlay for all supported languages (`DisplayDialogs=1` in `Remaster.ini`).
  - Conversation lines are displayed at the bottom of the screen, synced to the spoken audio
  - Configurable speaker name display, text language, color, size, position and backdrop.
  - Echelon comms are disabled by default `DialogShowEchelonComms=0` as they are displayed in comms box anyway.
  - NPC bark / callout subtitles: `DialogShowBarks=1` in `Remaster.ini`.
    - Requires UTF-8 `P_Barks.<lang>` in `Data\Sounds\localization` (shipped with the remastered UMD pack; not installed by `remaster_patch_exe`).
    - Supported lang codes match text localization: `cze`, `deu`, `esp`, `fra`, `hun`, `int`, `ita`, `pol`, `prt`, `rus`, `svk`.
    - The Dare `header_id` → `Play_*` map is compiled into the DLL. Magma `Play_random_*` parents are never shown — only the leaf that Dare actually plays.

https://github.com/user-attachments/assets/85f0b06d-1161-422d-8518-6895ae9627e9
- Option to disable cargo ship camera sway (to do that set `DisableCargoShipSway=1` in `Remaster.ini`).
- Option to disable the fish-eye warp on night / thermal / optic / sticky-cam goggles (`DisableFishEyeVision=1` in `Remaster.ini`).

Modification was tested on Steam and Ubisoft Connect version but should work with any DRM version.

### Installation of remastered version and best settings

- [Optional] Steps for overall **best quality**.
  - Download [Widescreen Fix](https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/scct).
    - Modify settings in `System\scripts\SplinterCellChaosTheory.WidescreenFix.ini`:
      ```ini
      ForceWindowedMode = 1         // Enable to fix flickering or missing videos on high FPS
                                    // as well as shader issues (broken vision / light trails etc.)
      ShadowMapResolution = 8192    // Keep at 8K, as 16K breaks some shadows
      EnableShadowFiltering = 1     // Enable for better, more smooth shadows
      FPSLimit = 240                // Set to your screen refresh rate
      ```
  - Download 4K BINK Videos:
    - Open this [Steam tutorial](https://steamcommunity.com/sharedfiles/filedetails/?id=728093866) and go to section **[SP] AI Enhanced Videos**.
    - Download selected **AI Enhanced** video packs and copy *.BIK videos. **DO NOT** copy modified EXE as it's not needed.
    - You can also get [-> HERE <-](https://drive.google.com/drive/folders/1Z_31Bc5gHr9XmB0dShSrhNoI4dM8S354) 4K widescreen videos for menu and trainings (EN / PL / RO) updated by me. Download and unpack into game directory (not **System**).
    <p align="center">
      <img src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/4k_bink_support.gif?raw=true" width="80%">
      <br>
      <b>Menu background videos comparison</b>
    </p>
  - Modify game **Display** settings -> **Advanced**:
    - Set **Shader Model Type** to **Shader Model 3.0**.
    - In **Shader Options** enable:
      - **Parallax Mapping** to get better relief from new textures.
      - **High Quality Soft Shadows** to get better soft shadows that benefits from new bloom implementation.
    - Enable **High Quality Textures**.
    - Enable **Hardware Shadow Mapping** to get rid of triangular non-aliased shadows of thin objects near walls like ladders or staircase handrails.
- Patch your `splintercell3.exe` with `patch_exe.exe` to allow read of >8GB UMD archives.
  - To install patch simply copy it into game system directory and run next to `splintercell3.exe`.
- Run `remaster_patch_exe.exe` next to `splintercell3.exe` for full Remaster features. Edit `Remaster.ini` as needed.
  - It writes `AVIFIL32.dll` (the remaster DLL) and `Remaster.ini` next to `splintercell3.exe`.
  - The game loads `AVIFIL32.dll` from its own directory first, so no EXE change is needed and the signed Ubisoft Connect EXE keeps working. All calls to the Windows `AVIFIL32.dll` are forwarded to the original one, and it does not conflict with ReShade, dgVoodoo, DXVK or ASI loaders (including the widescreen fix installed as `msvfw32.dll` / `msacm32.dll`, which the remaster loads just like the Windows `AVIFIL32.dll` does).
- Download `UMD_Remastered.zip` from [-> HERE <-](https://drive.google.com/drive/folders/1tRK5hTebmlgY8VI8wcMHmy6QMYRr1DjU) and unpack into game directory (not **System**). It contains updated static meshes, textures and synced vertex streams in *.SCL levels.
  - You can also pick older **4K textures only** version from the same link above: `UMD_4KTexturesOnly.zip`.

### Remastered visuals **(click in table to open slider view)**

<p align="center">
  <img width="600" height="600" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/comparison.gif?raw=true"/>
  <br>
  <b>Textures comparison</b>
</p>

<p align="center">
  <img width="600" height="600" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/mesh_compare.gif?raw=true"/>
  <br>
  <b>Static meshes comparison</b>
</p>

<p align="center">
  <img width="600" height="600" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/vision.gif?raw=true"/>
  <br>
  <b>Vision qualities comparison</b>
</p>

<p align="center">
  <img width="100%" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/shimmer.gif?raw=true"/>
  <br>
  <b>Bloom shimmering fix</b>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/offplace_bloom.jpg" width="100%">
  <br>
  <b>Bloom placement correction</b>
</p>

<p align="center">
  <img width="100%" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/physics1.gif?raw=true"/>
  <img width="100%" src="https://github.com/dwojtasik/SliderCompareHtml/blob/main/assets/SCCT-Repack/physics2.gif?raw=true"/>
  <br>
  <b>Physics fix on high FPS</b>
</p>

<p align="center">
  <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_a.jpg&r_label=4x AI upscale + Remaster EXE">
    <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/shadows.jpg" width="100%">
  </a>
  <br>
  <b>Soft shadows comparison</b>
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
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_5_a.jpg" width="100%">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_6_a.jpg" width="100%">
      </a>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_7_a.jpg" width="100%">
      </a>
    </td>
    <td width="50%" align="center">
      <a href="https://dwojtasik.github.io/SliderCompareHtml/?l_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_8_b.jpg&l_label=Vanilla&r_img=https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_8_a.jpg&r_label=4x AI upscale + Remaster EXE">
        <img src="https://raw.githubusercontent.com/dwojtasik/SliderCompareHtml/refs/heads/main/assets/SCCT-Repack/scct_8_a.jpg" width="100%">
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
                 [--sync-level <level.scl> ...] [--level-out <dir>]
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

### Levels and rebuilt StaticMeshes (`--sync-level`)

A level (`.scl`) can store a per-vertex colour stream for a placed mesh: the actor's `StaticMeshInstance` holds one colour per vertex of the mesh. When an edited OBJ changes a mesh's vertex count, that stream no longer matches and the level crashes while loading. Moving vertices only (same topology) needs nothing.

Pass every level that places meshes of the package you pack:

```text
python3 pack.py Data\StaticMeshes\07_UNhq_OBJ --sync-level Data\Maps\07_UNhq.scl
# -> Data\StaticMeshes\07_UNhq_OBJ.usx
# -> Data\StaticMeshes\Maps\07_UNhq.scl   (only if an instance had to be changed)
```

Mismatched streams get the new vertex count; all-zero streams (the stock content of nearly all of them) stay all zero. Do not empty such a stream instead: an actor with an empty instance is relit from the level's static lights when the level loads, and coloured lights then show as rainbow gradients on the mesh. The resized instances are placed right after the export data, and the import table, export table and the Havok data behind it follow in their original order, moved back by the inserted size (no other export moves). Keeping the tables in place and moving the import table behind the Havok data crashes the game while loading. Sync from the **stock** level: a stream that was emptied earlier cannot be restored from that level.

A level is written only when something changed, to `--level-out` (default: `Maps` next to the packed file); the source level is never overwritten. Packing a UMD with `-r` syncs against every USX packed into it. Shared packages (`Gameobjects_Obj`, `Ingredients_Obj`, …) are placed by many levels: pass all of them. An instance whose colours are not all zero is not changed; pack then stops with an error naming it (keep that mesh's vertex count).

## StaticMesh OBJ

`unpack` writes StaticMesh exports as OBJ v2 (first lines contain `# scct-obj 2`):

- `v x y z [r g b]` - Unreal Z-up positions as stored; vertex color only for meshes with a color stream
- `vt u v` - engine UVs (signed 16-bit / 2048, so tiling and negative UVs survive), V flipped so textures map like in game
- `vn x y z` - authored vertex normal
- `g section_N` + `usemtl <material>` - one group per render section (material slot). Keep the groups; every section needs at least one triangle
- faces keep the engine winding (clockwise front faces)

Packing an edited OBJ:

- unchanged OBJ -> original bytes
- same vertices / triangles, only positions moved -> positions patched in place, authored tangent frames kept
- anything else (subdivision, new or removed faces, moved vertices) -> rebuilt **append-only**. The original vertex stream and index buffer stay byte-identical at the start of the new streams, because the collision model and the second section table at the end of the serial (its own copy of the index list) reference them. The new render geometry is appended, and the render sections point at it. The largest section goes last (Section.FirstIndex / NumTriangles are u16). The render geometry never shares an original vertex, and a corner where triangles with differently running UVs meet (mirrored / rotated islands) gets one vertex per UV direction, so every vertex has a tangent frame that fits all of its triangles. A new vertex keeps the tangent frame interpolated from the template triangle it lies on (UV-space barycentrics) only where it agrees with the frame computed from the new geometry and UVs (within 8 degrees, `scct.mesh.STOCK_TANGENT_AGREE_DEG`); otherwise the computed frame is used, and every frame is made orthonormal (UV-less triangles get a frame from the normal). Mismatched frames light as bright diagonal "metallic" streaks in game, and faceted stock tangents on a smoothed mesh as flat bands. Vertex colors are interpolated from the template. Bounding box / sphere are recomputed; everything after the sections is kept verbatim. Keep the whole index buffer within 65535 entries to stay clear of other u16 counters

Every cooked StaticMesh holds one absolute file offset (an empty TLazyArray at the end of its kDOP collision data, framed as `01 01000000 <offset> 00 09000000`). When a mesh changes size, every export after it moves, so `pack` rewrites that offset in every StaticMesh (like the texture mip skips). Packages built without this fix crash while loading the level as soon as one mesh grows or shrinks.

Render sections stored as triangle strips (common in shared packages such as `Gameobjects_Obj`: bottles, sprinklers) are exported as plain triangles (odd strip triangles flipped back, stitching triangles dropped); an edited strip mesh is packed with triangle-list sections. Unedited strip meshes still pack byte-identical. Older versions read strips as lists (broken OBJ geometry): re-unpack such packages.

OBJs written by older versions (no `# scct-obj 2` line) still pack, but topology changes are only accepted for single-section meshes; re-unpack with this version to edit them.

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

Each EXE gets a Windows version resource (`make_version_files.py`) and is built without UPX.

`remaster_patch_exe` is separate from the UMD 4/8 GiB `patch_exe`: it writes `Remaster.dll` as `AVIFIL32.dll` + `Remaster.ini` next to the EXE and does not modify the EXE. EXEs patched by older versions (`.rmst` section with a `Remaster.dll` import) are restored from their `.remaster.bak`.

`Remaster.dll` is prebuilt in `scct\data\` and bundled into `remaster_patch_exe.exe`. To rebuild it from `remaster\` (MinGW-w64 i686 g++ and windres, e.g. MSYS2 `mingw-w64-i686-gcc`):

```text
build_dll.bat
```

## Layout

```text
unpack.py              CLI
pack.py                CLI
patch_exe.py           CLI (UMD LAA / SC8G)
remaster_patch_exe.py  CLI (Remaster.dll as AVIFIL32.dll + Remaster.ini)
scct/                  UMD + Unreal package + DARE audio + EXE patch internals
scct/level.py          level sync for rebuilt StaticMeshes (pack --sync-level)
scct/data/             prebuilt Remaster.dll
remaster/              Remaster.dll C++ source
setup_venv.bat         create .venv and install build deps
build.bat              PyInstaller onefile builds
make_version_files.py  version resources for the PyInstaller EXEs
build_dll.bat          Remaster.dll build (MinGW-w64 i686)
```

