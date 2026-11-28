<div align="center">

# X-Ray Monolith: Next Gen

### Modernized DX11 Multithreaded X-Ray Engine for S.T.A.L.K.E.R. Anomaly 1.5.3

<p>
  <a href="./License.txt">
    <img src="https://img.shields.io/badge/License-Non--commercial-red.svg?style=flat-square" alt="License" />
  </a>
  <a href="https://github.com/CnRJay/xray-monolith-gc64/releases/latest">
    <img src="https://img.shields.io/github/v/release/CnRJay/xray-monolith-gc64?include_prereleases&label=Release&style=flat-square" alt="Latest release" />
  </a>
  <a href="https://github.com/CnRJay/xray-monolith-gc64/releases">
    <img src="https://img.shields.io/github/downloads/CnRJay/xray-monolith-gc64/total?label=Downloads&style=flat-square" alt="All downloads" />
  </a>
  <img src="https://img.shields.io/badge/Renderer-DirectX%2011%20(x64)-0078D4?style=flat-square" alt="DirectX 11 x64" />
  <img src="https://img.shields.io/badge/Scripting-LuaJIT%202.1%20GC64-2C2D72?style=flat-square" alt="LuaJIT 2.1 GC64" />
  <img src="https://img.shields.io/badge/SIMD-AVX2%20%2F%20AVX--512%20(Runtime)-0071C5?style=flat-square" alt="AVX2 / AVX-512" />
</p>

<p>
  <a href="#overview">Overview</a> •
  <a href="#key-features">Features</a> •
  <a href="#console-variables">CVars</a> •
  <a href="#installation">Installation</a> •
  <a href="#building-from-source">Building</a> •
  <a href="#troubleshooting--faq">FAQ</a> •
  <a href="#credits">Credits</a>
</p>

</div>

---

## Overview

**X-Ray Monolith: Next Gen** is a modernized **DirectX 11 (x64)** fork of the **X-Ray Monolith Engine**, engineered from the ground up for heavy S.T.A.L.K.E.R. Anomaly 1.5.3 modpacks (such as **G.A.M.M.A.**) running hundreds of Lua scripts and high-density scenes.

Built on top of upstream X-Ray Monolith, this fork breaks through the engine's legacy single-threaded rendering bottleneck, 2 GB LuaJIT memory ceiling, and dated volumetric/occlusion pipelines — delivering higher framerates, stronger 1% lows, modern GPU compute graphics, and rock-solid long-session stability.

> [!TIP]
> **G.A.M.M.A. Users**: This engine is **Plug and Play**. All required dependencies (including **3DSS** for True PIP scopes) are already present in G.A.M.M.A. — simply extract the release archive into your Anomaly root folder, clear `appdata/shaders_cache`, and play.

---

## Key Features

### Multithreading & Engine Architecture
| System | What It Does |
| :--- | :--- |
| **True 2-Stage Decoupled Render Thread** | Overlaps **Game Thread Frame N+1** (`FrameMove`, AI scheduler, physics, LuaGC, parallel bone kinematics) with **Render Thread Frame N** DX11 submission and `Present()` via `r__mt_decouple`, using double-buffered view/environment snapshots, frozen per-frame bone arenas, and 4-bucket UI command buffers. |
| **LuaJIT 2.1 GC64** | Enables 64-bit GC pointers natively across `luajit`, `luabind`, and `xrGame`, eliminating the legacy ~2 GB Lua memory crash ceiling in massive modpacks. Statically linked directly into `AnomalyDX11.exe` (no mismatched `lua51.dll` headaches). |
| **TBB Parallel Scene Pipeline** | Parallelizes sun cascade & shadowed dynamic light culling (`r4_parallel_cull`), grass/detail visibility (`r__mt_details`), skeleton bone updates (`mt_ParallelBones`), and async particle simulation with condition-variable waits (zero busy-spinning). |
| **Intel TBB Allocator** | Replaces legacy allocation bottlenecks to cut heap fragmentation, speed up saves/level transitions, and smooth out micro-stutters. |
| **Runtime AVX2 / AVX-512 SIMD** | Automatic CPUID dispatch (safe on all SSE2+ x64 CPUs) accelerating **Intel Masked Software Occlusion Culling** (AVX2/AVX-512) and bulk **skinning bone-palette matrix transposes** (AVX2 with SSE fallback). |

### Next-Gen DX11 Rendering & Graphics
- **Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`r4_volumetric_froxels`)**:
  - Replaces legacy `.peak` and low-res screen-space volumetrics with a Frostbite/UE5-style 3D froxel compute pipeline (7 dedicated HLSL 5.0 compute/apply shaders).
  - Evaluates multi-cascade hardware-PCF sun shadows, live shadow-atlas spotlight cones, up to 64 dynamic point/spot lights, weather-driven height fog with animated 3D wind noise, dual-lobe Henyey-Greenstein phase scattering, temporal reprojection, and analytical Beer-Lambert transmittance integration.
- **Physical Objective-Camera True PIP**:
  - Integrates `TheLostInPlace`'s physical objective-camera True PIP system: physical aperture & eyebox model, off-axis deferred lighting reconstruction, continuous barrel/handguard rendering, TAA/DLSS lens exclusion, hybrid reflex/magnifier support, typed Lua optic API, and in-game ImGui optic workbench.
- **DX11 GPU & CPU Pipeline Optimizations**:
  - **Constant Buffer Dirty-Check (`r4_cbuffer_skip_unchanged`)** — Skips redundant `Map(WRITE_DISCARD)` uploads on unchanged buffers.
  - **Zero-Copy Post-Process Ping-Pong (`r4_post_pingpong`)** — Ping-pongs `rt_Generic_0` ↔ `rt_Generic` across post-process passes instead of full-screen `CopyResource` blits.
  - **Far Sun Cascade Caching (`r4_sun_cascade_cache`)** — Caches the distant sun shadow cascade across frames with automatic invalidation on camera/sun movement.
  - **Geometry & Culling Wins** — 32-byte GPU grass instancing with slot-range skipping, non-sun light volume scissoring, null-PS sun shadow passes, DX11 submit memos, and `meshoptimizer` tree vertex-cache/overdraw index reordering.

### Modding, Diagnostics & Monolith Ecosystem
- **DLTX & DXML** — Full expanded LTX overrides (`@[newsection]`, `>add`, `<remove`) and runtime XML UI modification.
- **Mod Compatibility** — Full support for **3D Shader Scopes (3DSS)**, **Screen Space Shaders (SSS)**, **Beef's NVG**, **Atmospherics**, and **Mod Organizer 2**.
- **Built-In Profiling & Debugging** — Integrated **Optick** CPU + DX11 GPU timeline capture (`prof_capture <frames>` in `ProfiledDX11`), live per-viewport SVP/CPU/GPU stats overlay (`r__svp_stats`), LuaPanda debugging, and ImGui extensions.
- **Engine Fixes** — Animation smoothing, collision fixes, and removal of the legacy 1000-save limit.

---

## Console Variables

All major engine systems can be toggled or tuned live via the in-game console (`~`) or `user.ltx`:

| CVar | Range | Default | Description |
| :--- | :---: | :---: | :--- |
| `r__mt_decouple` | `0–1` | `1` | Overlap Game Thread Frame N+1 with dedicated Render Thread Frame N submission |
| `r4_parallel_cull` | `0–1` | `1` | Parallelize sun cascade, dynamic light, and main/SVP static scene culling |
| `r__mt_details` | `0–1` | `1` | Parallelize grass/detail visibility culling across TBB workers |
| `r4_volumetric_froxels` | `0–1` | `1` | Enable Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`0` = legacy fallback) |
| `r4_volumetric_quality` | `0–2` | `1` | Froxel 3D grid resolution (`0`: `128×72×48`, `1`: `160×90×64`, `2`: `240×135×96`) |
| `r4_volumetric_range` | `30–250` | `120.0` | Maximum volumetric fog & lighting distance (meters) |
| `r4_volumetric_density` | `0.0–4.0` | `1.0` | Global atmospheric scattering & volumetric fog density multiplier |
| `r4_volumetric_anisotropy` | `-0.5–0.92` | `0.65` | Henyey-Greenstein phase anisotropy for forward-scattering god rays & beams |
| `r4_volumetric_temporal` | `0.0–0.96` | `0.90` | Temporal reprojection history blend factor (`0` = off) |
| `r4_cbuffer_skip_unchanged` | `0–1` | `1` | Skip `Map(WRITE_DISCARD)` on unchanged DX11 constant buffers |
| `r4_post_pingpong` | `0–1` | `1` | Ping-pong post-process render targets instead of full-screen copies |
| `r4_sun_cascade_cache` | `0–8` | `1` | Cache far sun shadow cascade (`1` = 4-frame cadence, `0` = off) |
| `r__svp_stats` | `0–2` | `0` | Live per-viewport True PIP CPU/GPU/draw-call/VRAM performance overlay |

---

## System Requirements

| Component | Minimum | Recommended |
| :--- | :--- | :--- |
| **OS** | Windows 10 (1903+) 64-bit | Windows 10 / 11 64-bit |
| **CPU** | 64-bit x86 with SSE2 | Modern multi-core CPU with **AVX2** or **AVX-512** |
| **RAM** | 8 GB | 16–32 GB (for heavy modpacks like G.A.M.M.A.) |
| **GPU** | DirectX 11 (Shader Model 5.0) | DX11 GPU with 6 GB+ VRAM |
| **Runtime** | [Visual C++ 2022 Redistributable (x64)](https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/) | S.T.A.L.K.E.R. Anomaly 1.5.3 + **3DSS** (included in G.A.M.M.A.) |

---

## Installation

1. **Backup**: Back up your existing `bin/` and `gamedata/` folders inside your Anomaly root directory.
2. **Download**: Grab the latest DX11 release `.zip` from the [Releases page](https://github.com/CnRJay/xray-monolith-gc64/releases).
3. **Extract (Drag & Drop)**: Extract the archive directly into your `ANOMALY` root folder so `bin/AnomalyDX11.exe` and `gamedata/` overwrite cleanly.
   - *LuaJIT 2.1 GC64 is statically linked into `AnomalyDX11.exe` — no separate `lua51.dll` replacement is required.*
4. **Clear Shader Cache (CRITICAL)**: Delete the `appdata/shaders_cache` folder inside your Anomaly directory (and MO2 overwrite if applicable).
5. **Launch**: Start the game using **Anomaly (DX11)** (`bin/AnomalyDX11.exe`).

> [!IMPORTANT]
> Always delete `appdata/shaders_cache` after updating the engine. Stale compiled shaders can cause black screens, broken scopes, or crashes on save load.

---

## Building from Source

### Prerequisites
- **Visual Studio 2026** (or VS 2022) with the **Desktop development with C++** workload
- **Git**

### Visual Studio IDE
1. Open `src/engine-vs2026.sln` (or `src/engine.sln`).
2. Select Platform: **`x64`**.
3. Select Configuration:
   - **`DX11`** — Optimized release build (`AnomalyDX11.exe`)
   - **`VerifiedDX11`** — Release build with `VERIFY` checks & PDB symbols (`VerifiedDX11.exe`)
   - **`ProfiledDX11`** — Release build with **Optick** CPU + DX11 GPU profiling enabled (`AnomalyDX11.exe`)
4. Build the solution (`Ctrl+Shift+B`).

### Command Line (MSBuild)
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "src\engine-vs2026.sln" /p:Configuration=DX11 /p:Platform=x64 /m /nologo /v:minimal
```

Built executables and PDBs are written to `_build/_game/bin_dbg/`.

---

## Troubleshooting & FAQ

<details>
<summary><b>My RAM usage is high (6–12 GB+). Is this a memory leak?</b></summary>

**No.** This is expected behavior. The Intel TBB allocator retains pooled arenas to eliminate allocation stalls, and LuaJIT 2.1 GC64 uses 64-bit tagged values so heavy modpacks can grow past the old 2 GB limit instead of crashing with Out-Of-Memory errors.
</details>

<details>
<summary><b>The screen is black when loading a save, but the HUD is visible.</b></summary>

Delete `appdata/shaders_cache` and relaunch. Stale shader bytecode from a previous engine version is the #1 cause of black screens.
</details>

<details>
<summary><b>How do I test or isolate a rendering issue?</b></summary>

You can toggle each major subsystem live from the console (`~`) without restarting:
- `r__mt_decouple 0` — Toggle the decoupled Render Thread
- `r4_volumetric_froxels 0` — Toggle Compute 3D Froxel Volumetrics
- `r4_post_pingpong 0` — Toggle zero-copy post-process ping-pong
- `r4_sun_cascade_cache 0` — Toggle far sun cascade caching
</details>

<details>
<summary><b>How do I capture a CPU + GPU performance profile?</b></summary>

Run a `ProfiledDX11` build, open the in-game console (`~`), and run `prof_capture 600`. The trace is saved to `appdata/logs/optick.opt` and can be opened directly in Optick.
</details>

---

## Contributing

Contributions focused on performance, stability, and modern DX11/DX12 graphics are welcome!

### Guidelines
1. **Fork & Branch**: Create a descriptive feature or fix branch (`git checkout -b feat/my-improvement`).
2. **Thread Safety (CRITICAL)**:
   - **Never** execute Lua callbacks, `CResourceManager::_CreateShader`, or `CResourceManager::_CreateTexture` on the Render Thread or inside TBB worker loops.
   - **Never** mutate global physics, sound, or environment state from worker threads without snapshotting or synchronization.
   - Guard DX11-only additions in shared `src/Layers/xrRender/` files with `#if RENDER == R_R4` or `#if defined(USE_DX11)`.
3. **Verify**: Ensure `DX11|x64` and `VerifiedDX11|x64` compile cleanly and test in-game before opening a PR.

---

## License

Contents of this repository are licensed under a custom GSC Game World proprietary license for non-commercial use. See [License.txt](./License.txt) for full terms.

---

## Credits

- **X-Ray Monolith**: [TheMrDemonized](https://github.com/themrdemonized/xray-monolith)
- **Physical Objective-Camera True PIP & Intel MOC Integration**: [TheLostInPlace](https://github.com/TheLostInPlace/xray-monolith-pip)
- **Original True PIP Concept**: [m22spencer](https://github.com/m22spencer/xray-monolith)
- **OpenXRay Team**: [OpenXRay](https://github.com/openxray)
- **Libraries**: [Intel oneTBB](https://github.com/oneapi-src/oneTBB), [Intel Masked Occlusion Culling](https://github.com/GameTechDev/MaskedOcclusionCulling), [LuaJIT](https://luajit.org/), [meshoptimizer](https://github.com/zeux/meshoptimizer), [Optick](https://github.com/bombomby/optick)
