<div align="center">

# X-Ray Monolith: Next Gen

### Multithreaded DirectX 11 Engine for S.T.A.L.K.E.R. Anomaly 1.5.3

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
  <img src="https://img.shields.io/badge/CPU-AVX2%20%2F%20AVX--512%20Auto-0071C5?style=flat-square" alt="AVX2 / AVX-512" />
</p>

<p>
  <a href="#overview">Overview</a> |
  <a href="#key-features">Features</a> |
  <a href="#performance-benchmarks-rough-reference">Benchmarks</a> |
  <a href="#console-variables">Settings</a> |
  <a href="#installation">Installation</a> |
  <a href="#building-from-source">Building</a> |
  <a href="#troubleshooting--faq">FAQ</a> |
  <a href="#credits">Credits</a>
</p>

</div>

---

## Overview

**X-Ray Monolith: Next Gen** is a custom **DirectX 11 (64-bit)** engine build for S.T.A.L.K.E.R. Anomaly 1.5.3, built with large modpacks like **G.A.M.M.A.** in mind.

It focuses on the biggest pain points in modded Anomaly: low CPU-bound framerates, stutters during combat and exploration, out-of-memory script crashes in long saves, and slow startup times under Mod Organizer 2. For full technical patch notes, see [CHANGELOG.md](./CHANGELOG.md).

> [!TIP]
> **G.A.M.M.A. Users**: This build is plug-and-play. Everything it needs (including **3DSS** for PIP scopes) is already in G.A.M.M.A. Extract the release zip into your Anomaly folder, delete `appdata/shaders_cache`, and play.

---

## Key Features

### Performance, Stability & Loading
- **Higher FPS and Stronger Lows**: Separates game logic and DirectX 11 rendering onto dedicated threads (`r__mt_decouple`) so your CPU can prepare the next frame while the GPU draws the current one.
- **No More 2 GB Script Memory Crashes**: Upgraded to 64-bit LuaJIT 2.1 (`GC64`), removing the old ~2 GB script memory ceiling that causes out-of-memory crashes in massive modpacks. Built directly into `AnomalyDX11.exe` with no extra `lua51.dll` needed.
- **Far Fewer Stutters**: Reworked how script garbage collection runs in the background so it spreads work evenly across frames instead of freezing the game every few seconds.
- **Much Faster Game Startup & Loading**: Overhauled how the engine scans mod files, archives, terrain, and models across multiple CPU cores. Cuts startup times dramatically, especially when launching through Mod Organizer 2.
- **Smoother Gunfights**: Optimized sound occlusion and capped per-frame AI scheduler spikes so large firefights with multiple squads stay responsive instead of tanking FPS.
- **Modern CPU Acceleration**: Automatically detects and uses **AVX2** and **AVX-512** on supported CPUs for faster visibility culling and character animation processing, while remaining fully compatible with older 64-bit CPUs.
- **Smarter Multithreading**: Runs shadow culling, grass visibility, particles, and character bones across multiple CPU cores, and uses Intel's TBB memory allocator to reduce allocation hitches.
- **Automatic Default Updates**: Safely applies recommended engine setting updates once when you upgrade (`engine_cfg_ver`) without overwriting any custom console tweaks you make afterward.

### Graphics & Scopes
- **3D Volumetric Fog & Lighting**: Replaces the older volumetric system with a new GPU-accelerated 3D volumetric fog pipeline (`r4_volumetric_froxels`) supporting sun god rays, shadowed spotlights, dynamic lights (like campfires and flashlights), and weather-driven fog.
- **True Picture-in-Picture (PIP) Scopes**: Includes physical objective-camera dual-render scopes (from `TheLostInPlace`) with realistic eye relief, continuous barrel/handguard rendering, hybrid optics support, and an in-game optic workbench.
- **DX11 Rendering Optimizations**: Faster grass instancing, smarter shadow caching for distant sun shadows, reduced unnecessary GPU texture copies, and optimized tree geometry rendering.

### Mod Compatibility & Tools
- **Modpack Friendly**: Includes full **DLTX** and **DXML** support and works with **3DSS**, **Screen Space Shaders (SSS)**, **Beef's NVG**, **Atmospherics**, and **Mod Organizer 2**.
- **Quality of Life Fixes**: Animation smoothing, collision fixes, and removal of the old 1,000-save limit.
- **Optional Profiling & Stats**: Built-in live PIP/render stats overlay (`r__svp_stats`) and a separate Optick-enabled release build (`ProfiledDX11`) for anyone who wants to capture CPU/GPU frame traces.

---

## Performance Benchmarks (Rough Reference)

> [!NOTE]
> **Disclaimer**: These numbers are rough, non-exact reference figures from a single heavily modded **G.A.M.M.A.** setup (Ryzen 9 9950X3D, 1440p DX11, Rookie Village / Cordon combat, 3,000-frame Optick captures on `ProfiledDX11`). Real results **will vary per hardware, resolution, settings, and modlist**. The standard `DX11` release build (without profiling) has slightly lower CPU overhead than `ProfiledDX11`.

### In-Game Performance (3,000-Frame Test Runs)
| Metric | Earlier Baseline | Current Build (`ProfiledDX11`) | Approximate Change |
| :--- | :---: | :---: | :---: |
| **Average FPS** | ~97 FPS | **~136-154 FPS** | **~+35-40% FPS** |
| **Median FPS** | ~105 FPS | **~145 FPS** | **~+38% FPS** |
| **5% Lows** | ~56 FPS | **~99 FPS** | **~+40 FPS** |
| **1% Lows** | ~23 FPS | **~68 FPS** | **~3x smoother lows** |
| **Heavy Stutters (> 50 ms)** | ~28 per 3,000 frames | **0** | **Eliminated in test runs** |
| **Combat Audio Overhead** | ~3.6 ms (up to ~14.5 ms) | **~0.05 ms** | **~70x faster in gunfights** |

### Startup & Level Loading (G.A.M.M.A. via Mod Organizer 2, ~149k Files)
| Load Stage | Before | After | Approximate Change |
| :--- | :---: | :---: | :---: |
| **Initial File/Mod Scan (`Init FileSystem`)** | ~17.5 s | **~5.2-6.3 s** | **~3x faster (~11-12 s saved)** |
| **Tree Geometry Prep (`TREE-IB`)** | ~73 ms | **~28-35 ms** | **~2.3x faster** |
| **Terrain Occlusion Build (`HOM`)** | ~119 ms | **~9 ms** | **~13x faster** |

---

## Console Variables

Everything works out of the box with sensible defaults. If you want to tweak or test settings, open the in-game console (`~`) or edit `user.ltx`:

| Command | Range | Default | What It Controls |
| :--- | :---: | :---: | :--- |
| `r__mt_decouple` | `0-1` | `1` | Dedicated render thread (big FPS and smoothness boost) |
| `r4_volumetric_froxels` | `0-1` | `1` | 3D volumetric fog & lighting (`0` = fallback) |
| `r4_volumetric_quality` | `0-2` | `1` | Volumetric fog quality (`0` = low, `1` = medium, `2` = high) |
| `r4_volumetric_range` | `30-250` | `120.0` | Volumetric fog render distance in meters |
| `r4_volumetric_density` | `0.0-4.0` | `1.0` | Overall fog and light-beam density |
| `r4_volumetric_anisotropy` | `-0.5-0.92` | `0.65` | Strength of forward-scattering god rays and light shafts |
| `r4_volumetric_temporal` | `0.0-0.96` | `0.90` | Temporal smoothing for volumetric fog (`0` = off) |
| `r4_sun_cascade_cache` | `0-8` | `1` | Caches distant sun shadows across frames for better FPS (`0` = off) |
| `r4_parallel_cull` | `0-1` | `1` | Multithreaded shadow and scene culling |
| `r__mt_details` | `0-1` | `1` | Multithreaded grass visibility |
| `r4_post_pingpong` | `0-1` | `1` | Faster post-processing without extra texture copies |
| `r4_cbuffer_skip_unchanged` | `0-1` | `1` | Skips redundant shader constant uploads |
| `lua_parallel_gc_trigger_mb` | `8-512` | `16` | Script memory growth (MB) before running a background cleanup cycle |
| `lua_parallel_gc_budget_us` | `100-5000` | `1500` | Max time per frame (microseconds) spent on background script cleanup |
| `r__svp_stats` | `0-2` | `0` | On-screen PIP scope & render performance stats overlay |

---

## System Requirements

| Component | Minimum | Recommended |
| :--- | :--- | :--- |
| **OS** | Windows 10 (1903+) 64-bit | Windows 10 / 11 64-bit |
| **CPU** | 64-bit x86 with SSE2 | Modern multi-core CPU with **AVX2** or **AVX-512** |
| **RAM** | 8 GB | 16-32 GB (for heavy modpacks like G.A.M.M.A.) |
| **GPU** | DirectX 11 (Shader Model 5.0) | DX11 GPU with 6 GB+ VRAM |
| **Runtime** | [Visual C++ 2022 Redistributable (x64)](https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/) | S.T.A.L.K.E.R. Anomaly 1.5.3 + **3DSS** (included in G.A.M.M.A.) |

---

## Installation

1. **Backup**: Back up your existing `bin/` and `gamedata/` folders inside your Anomaly root directory.
2. **Download**: Grab the latest DX11 release `.zip` from the [Releases page](https://github.com/CnRJay/xray-monolith-gc64/releases):
   - **`STALKER-Anomaly-DX11_<tag>.zip`**: Recommended for normal play (clean release build with profiling turned off for best FPS).
   - **`STALKER-Anomaly-ProfiledDX11_<tag>.zip`**: Optional build with Optick CPU/GPU profiling enabled (`prof_capture`) for benchmarking and bug reports.
3. **Extract (Drag & Drop)**: Extract the zip directly into your `ANOMALY` root folder so `bin/AnomalyDX11.exe` and `gamedata/` overwrite cleanly.
   - *LuaJIT 2.1 GC64 is built directly into `AnomalyDX11.exe`, so no separate `lua51.dll` replacement is needed.*
4. **Clear Shader Cache (Required)**: Delete the `appdata/shaders_cache` folder inside your Anomaly directory (and your MO2 overwrite folder if present).
5. **Launch**: Start the game using **Anomaly (DX11)** (`bin/AnomalyDX11.exe`).

> [!IMPORTANT]
> Always delete `appdata/shaders_cache` after updating the engine. Old cached shaders can cause black screens, broken scopes, or crashes when loading a save.

---

## Building from Source

### Prerequisites
- **Visual Studio 2026** (or VS 2022) with the **Desktop development with C++** workload
- **Git**

### Visual Studio IDE
1. Open `src/engine-vs2026.sln` (or `src/engine.sln`).
2. Select Platform: **`x64`**.
3. Select Configuration:
   - **`DX11`**: Standard release build (`AnomalyDX11.exe`)
   - **`VerifiedDX11`**: Release build with extra `VERIFY` safety checks and PDB symbols (`VerifiedDX11.exe`)
   - **`ProfiledDX11`**: Release build with **Optick** CPU + DX11 GPU profiling enabled (`AnomalyDX11.exe`)
4. Build the solution (`Ctrl+Shift+B`).

### Command Line (MSBuild)
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "src\engine-vs2026.sln" /p:Configuration=DX11 /p:Platform=x64 /m /nologo /v:minimal
```

Built executables and PDBs are output to `_build/_game/bin_dbg/`.

---

## Troubleshooting & FAQ

<details>
<summary><b>My RAM usage is high (6-12 GB+). Is this a memory leak?</b></summary>

**No.** This is normal. The TBB memory allocator keeps pooled memory ready to avoid stutters, and 64-bit LuaJIT lets large modpacks use more than the old 2 GB cap instead of crashing with Out-Of-Memory errors.
</details>

<details>
<summary><b>The screen is black when loading a save, but the HUD is visible.</b></summary>

Delete `appdata/shaders_cache` and relaunch. Old compiled shaders from a previous engine build are the most common cause of black screens.
</details>

<details>
<summary><b>How do I test or isolate a graphics issue?</b></summary>

You can turn major features off live in the console (`~`) without restarting:
- `r__mt_decouple 0`: Turn off the decoupled render thread
- `r4_volumetric_froxels 0`: Turn off 3D volumetric fog
- `r4_post_pingpong 0`: Turn off post-process target ping-ponging
- `r4_sun_cascade_cache 0`: Turn off distant sun shadow caching
</details>

<details>
<summary><b>How do I record a performance profile for a bug report?</b></summary>

Use the `ProfiledDX11` build, open the console (`~`), and run `prof_capture 600`. The trace saves to `appdata/logs/optick.opt` and can be opened in Optick.
</details>

---

## Contributing

Pull requests focused on performance, stability, and DX11/DX12 rendering improvements are welcome. See [CHANGELOG.md](./CHANGELOG.md) for detailed technical changes.

### Guidelines
1. **Fork & Branch**: Create a descriptive branch (`git checkout -b feat/my-improvement`).
2. **Thread Safety**:
   - Never run Lua callbacks, `CResourceManager::_CreateShader`, or `CResourceManager::_CreateTexture` on the Render Thread or inside TBB worker loops.
   - Never mutate global physics, sound, or environment state from worker threads without snapshotting or synchronization.
   - Guard DX11-only additions in shared `src/Layers/xrRender/` files with `#if RENDER == R_R4` or `#if defined(USE_DX11)`.
3. **Verify**: Make sure `DX11|x64` and `VerifiedDX11|x64` compile cleanly and test in-game before opening a PR.

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
