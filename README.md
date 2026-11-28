<div align="center">

<br />

# X-Ray Monolith: Next Gen

### High-FPS Multithreaded DirectX 11 Engine for S.T.A.L.K.E.R. Anomaly 1.5.3

<br />

<p>
  <a href="https://github.com/CnRJay/xray-monolith-gc64/releases/latest">
    <img src="https://img.shields.io/github/v/release/CnRJay/xray-monolith-gc64?include_prereleases&style=for-the-badge&logo=github&color=2ea043&label=Latest%20Release" alt="Latest Release" />
  </a>
  <a href="https://github.com/CnRJay/xray-monolith-gc64/releases">
    <img src="https://img.shields.io/github/downloads/CnRJay/xray-monolith-gc64/total?style=for-the-badge&color=0969da&label=Downloads" alt="Downloads" />
  </a>
  <img src="https://img.shields.io/badge/DirectX%2011-x64-0078D4?style=for-the-badge" alt="DirectX 11 x64" />
  <img src="https://img.shields.io/badge/LuaJIT%202.1-GC64-4B32C3?style=for-the-badge" alt="LuaJIT 2.1 GC64" />
  <a href="./License.txt">
    <img src="https://img.shields.io/badge/License-Non--Commercial-d73a49?style=for-the-badge" alt="License" />
  </a>
</p>

<p>
  <a href="https://github.com/CnRJay/xray-monolith-gc64/releases/latest"><b>Download</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#highlights"><b>Features</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#benchmarks-rough-reference"><b>Benchmarks</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#installation"><b>Install</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#console-settings"><b>Settings</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#faq"><b>FAQ</b></a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="./CHANGELOG.md"><b>Changelog</b></a>
</p>

<br />

Built for heavily modded Anomaly setups like **G.A.M.M.A.**<br />
Higher FPS, smoother combat, faster loading, 64-bit script memory, 3D volumetric fog, and True PIP scopes.

<br />

</div>

> [!TIP]
> **G.A.M.M.A. Users:** This build is plug-and-play. Everything it needs (including **3DSS** for PIP scopes) is already included in G.A.M.M.A. Extract the release zip into your Anomaly folder, delete `appdata/shaders_cache`, and launch.

---

## Highlights

<table>
  <tr>
    <td width="50%" valign="top">
      <h3>Dedicated Render Thread</h3>
      Game logic and DirectX 11 rendering run on separate threads at the same time (<code>r__mt_decouple</code>). Your CPU prepares the next frame while the GPU draws the current one, raising average FPS and smoothing out frame-time drops.
    </td>
    <td width="50%" valign="top">
      <h3>64-Bit LuaJIT &amp; Stutter Fixes</h3>
      Upgraded to 64-bit LuaJIT 2.1 (<code>GC64</code>) built straight into <code>AnomalyDX11.exe</code>. Removes the old ~2 GB script memory crash limit in big modpacks and reworks background script cleanup to cut out periodic hitches.
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>Much Faster Startup &amp; Loading</h3>
      Scans mod files, archives, terrain, and models across multiple CPU cores. Cuts startup times significantly, especially when launching large modlists through Mod Organizer 2.
    </td>
    <td width="50%" valign="top">
      <h3>Smoother Gunfights</h3>
      Optimized sound occlusion and capped per-frame AI scheduler spikes so heavy firefights with multiple squads stay responsive instead of tanking your framerate.
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>3D Volumetric Fog &amp; Lighting</h3>
      Replaces the older volumetric pass with a GPU-accelerated 3D volumetric fog system (<code>r4_volumetric_froxels</code>) supporting sun god rays, shadowed spotlights, campfires, flashlights, and weather fog.
    </td>
    <td width="50%" valign="top">
      <h3>True PIP Scopes</h3>
      Includes physical objective-camera dual-render scopes (from <code>TheLostInPlace</code>) with proper eye relief, continuous barrel/handguard rendering, hybrid optics support, and an in-game optic workbench.
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>Modern CPU Acceleration</h3>
      Automatically detects and uses <b>AVX2</b> and <b>AVX-512</b> on supported CPUs for faster visibility culling and character animations, while still running cleanly on older 64-bit CPUs.
    </td>
    <td width="50%" valign="top">
      <h3>Modpack Ready &amp; Auto-Configured</h3>
      Full <b>DLTX</b>, <b>DXML</b>, <b>3DSS</b>, <b>SSS</b>, <b>Beef's NVG</b>, <b>Atmospherics</b>, and <b>MO2</b> compatibility. Automatically applies recommended engine defaults once on upgrade without overwriting your personal tweaks.
    </td>
  </tr>
</table>

<p align="right">
  <sub>Want the full low-level breakdown? Read <a href="./CHANGELOG.md">CHANGELOG.md</a>.</sub>
</p>

---

## Benchmarks (Rough Reference)

<div align="center">

| Average FPS | 1% Lows | Heavy Stutters (> 50 ms) | MO2 Startup Scan |
| :---: | :---: | :---: | :---: |
| **~+35-40%** | **~3x Higher** | **~28 -> 0** | **~3x Faster** |
| `~97 -> ~136-154 FPS` | `~23 -> ~68 FPS` | `3,000-frame capture` | `~17.5 s -> ~5.2-6.3 s` |

</div>

> [!NOTE]
> **Disclaimer:** These numbers are rough, non-exact reference figures from a single heavily modded **G.A.M.M.A.** test install (Ryzen 9 9950X3D, 1440p DX11, Rookie Village / Cordon AI combat, 3,000-frame Optick captures on `ProfiledDX11`). Actual performance **will vary depending on your CPU, GPU, resolution, settings, and modlist**. The standard `DX11` release build (without profiling) has slightly lower CPU overhead than `ProfiledDX11`.

<details>
<summary><b>View Detailed Reference Tables</b></summary>
<br />

#### In-Game Frame Pacing (3,000-Frame Test Runs)
| Metric | Earlier Baseline | Current Build (`ProfiledDX11`) | Approximate Change |
| :--- | :---: | :---: | :---: |
| **Average FPS** | ~97 FPS | **~136-154 FPS** | ~+35-40% FPS |
| **Median FPS** | ~105 FPS | **~145 FPS** | ~+38% FPS |
| **5% Lows** | ~56 FPS | **~99 FPS** | ~+40 FPS |
| **1% Lows** | ~23 FPS | **~68 FPS** | ~3x smoother lows |
| **Heavy Stutters (> 50 ms)** | ~28 per 3,000 frames | **0** | Removed in test runs |
| **Combat Audio Overhead** | ~3.6 ms (up to ~14.5 ms) | **~0.05 ms** | ~70x faster in gunfights |

#### Startup & Level Loading (G.A.M.M.A. via Mod Organizer 2, ~149k Files)
| Load Stage | Before | After | Approximate Change |
| :--- | :---: | :---: | :---: |
| **Initial File/Mod Scan (`Init FileSystem`)** | ~17.5 s | **~5.2-6.3 s** | ~3x faster (~11-12 s saved) |
| **Tree Geometry Prep (`TREE-IB`)** | ~73 ms | **~28-35 ms** | ~2.3x faster |
| **Terrain Occlusion Build (`HOM`)** | ~119 ms | **~9 ms** | ~13x faster |

</details>

---

## Installation

1. **Back up** your existing `bin/` and `gamedata/` folders in your Anomaly root directory.
2. **Download** a release `.zip` from the [Releases page](https://github.com/CnRJay/xray-monolith-gc64/releases):
   - **`STALKER-Anomaly-DX11_<tag>.zip`** (Recommended): Standard release build with profiling turned off for best FPS.
   - **`STALKER-Anomaly-ProfiledDX11_<tag>.zip`** (Optional): Includes Optick CPU/GPU profiling (`prof_capture`) for benchmarking and bug reports.
3. **Extract** the archive directly into your `ANOMALY` root folder so `bin/AnomalyDX11.exe` and `gamedata/` overwrite cleanly.
   - *One executable works on all 64-bit CPUs (AVX2 and AVX-512 are detected automatically at runtime; no separate AVX2 download needed), and LuaJIT 2.1 GC64 is built directly into the executable.*
4. **Clear your shader cache** by deleting `appdata/shaders_cache` (and your MO2 overwrite folder if it contains one).
5. **Launch** with **Anomaly (DX11)** (`bin/AnomalyDX11.exe`).

> [!IMPORTANT]
> Always delete `appdata/shaders_cache` after updating the engine. Old cached shaders are the #1 cause of black screens, broken scopes, or crashes when loading a save.

---

## Console Settings

Everything works out of the box with recommended defaults. To tweak visuals or test settings live, press `~` in-game or edit `user.ltx`:

| Command | Range | Default | What It Controls |
| :--- | :---: | :---: | :--- |
| `r__mt_decouple` | `0-1` | `1` | Dedicated render thread (FPS and frame-pacing boost) |
| `r4_volumetric_froxels` | `0-1` | `1` | 3D volumetric fog & lighting (`0` = fallback) |
| `r4_volumetric_quality` | `0-2` | `1` | Volumetric fog quality (`0` = low, `1` = medium, `2` = high) |
| `r4_volumetric_range` | `30-250` | `120.0` | Volumetric fog render distance in meters |
| `r4_volumetric_density` | `0.0-4.0` | `1.0` | Overall fog and light-beam density |
| `r4_volumetric_anisotropy` | `-0.5-0.92` | `0.65` | Strength of forward-scattering god rays and light shafts |
| `r__svp_stats` | `0-2` | `0` | On-screen PIP scope & render performance stats overlay |

<details>
<summary><b>Advanced Engine &amp; Tuning Commands</b></summary>
<br />

| Command | Range | Default | What It Controls |
| :--- | :---: | :---: | :--- |
| `r4_volumetric_temporal` | `0.0-0.96` | `0.90` | Temporal smoothing for volumetric fog (`0` = off) |
| `r4_sun_cascade_cache` | `0-8` | `1` | Caches distant sun shadows across frames (`0` = off) |
| `r4_parallel_cull` | `0-1` | `1` | Multithreaded shadow and scene culling |
| `r__mt_details` | `0-1` | `1` | Multithreaded grass visibility |
| `r4_post_pingpong` | `0-1` | `1` | Faster post-processing without extra texture copies |
| `r4_cbuffer_skip_unchanged` | `0-1` | `1` | Skips redundant shader constant uploads |
| `lua_parallel_gc_trigger_mb` | `8-512` | `16` | Script memory growth (MB) before a background cleanup cycle |
| `lua_parallel_gc_budget_us` | `100-5000` | `1500` | Max time per frame (microseconds) spent on background script cleanup |

</details>

---

## System Requirements

| Component | Minimum | Recommended |
| :--- | :--- | :--- |
| **OS** | Windows 10 (1903+) 64-bit | Windows 10 / 11 64-bit |
| **CPU** | 64-bit x86 (SSE2) | Multi-core CPU with **AVX2** or **AVX-512** |
| **RAM** | 8 GB | 16-32 GB (recommended for G.A.M.M.A.) |
| **GPU** | DirectX 11 (Shader Model 5.0) | DX11 GPU with 6 GB+ VRAM |
| **Runtime** | [Visual C++ 2022 Redistributable (x64)](https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/) | S.T.A.L.K.E.R. Anomaly 1.5.3 + **3DSS** (included in G.A.M.M.A.) |

---

## FAQ

<details>
<summary><b>My RAM usage is high (6-12 GB+). Is this a memory leak?</b></summary>
<br />
<b>No.</b> This is normal. The TBB memory allocator keeps pooled memory ready to avoid stutters, and 64-bit LuaJIT lets large modpacks grow past the old 2 GB cap instead of crashing with Out-Of-Memory errors.
</details>

<details>
<summary><b>My screen is black when loading a save, but the HUD is visible.</b></summary>
<br />
Delete <code>appdata/shaders_cache</code> and relaunch. Old compiled shaders from a previous engine build are the most common cause of black screens.
</details>

<details>
<summary><b>How do I isolate a graphics or stability issue?</b></summary>
<br />
You can toggle major features off live in the console (<code>~</code>) without restarting:
<ul>
  <li><code>r__mt_decouple 0</code> - Turn off the decoupled render thread</li>
  <li><code>r4_volumetric_froxels 0</code> - Turn off 3D volumetric fog</li>
  <li><code>r4_post_pingpong 0</code> - Turn off post-process target ping-ponging</li>
  <li><code>r4_sun_cascade_cache 0</code> - Turn off distant sun shadow caching</li>
</ul>
</details>

<details>
<summary><b>How do I record a performance profile for a bug report?</b></summary>
<br />
Download the <code>ProfiledDX11</code> build, open the console (<code>~</code>), and run <code>prof_capture 600</code>. The trace saves to <code>appdata/logs/optick.opt</code> and can be opened in Optick.
</details>

---

## Building from Source

<details>
<summary><b>Visual Studio / MSBuild Instructions &amp; Contributing Guidelines</b></summary>
<br />

### Prerequisites
- **Visual Studio 2026** (or VS 2022) with the **Desktop development with C++** workload
- **Git**

### Visual Studio IDE
1. Open `src/engine-vs2026.sln` (or `src/engine.sln`).
2. Set Platform to **`x64`**.
3. Pick a Configuration:
   - **`DX11`**: Standard release build with profiling disabled (`AnomalyDX11.exe`)
   - **`ProfiledDX11`**: Release build with Optick CPU + DX11 GPU profiling (`AnomalyDX11.exe`)
4. Build (`Ctrl+Shift+B`). Outputs go to `_build/_game/bin_dbg/`.

### Command Line (MSBuild)
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "src\engine-vs2026.sln" /p:Configuration=DX11 /p:Platform=x64 /m /nologo /v:minimal
```

### Contributing Guidelines
1. Fork and create a feature/fix branch (`git checkout -b feat/my-improvement`).
2. Keep thread safety intact: never call Lua, `CResourceManager::_CreateShader`, or `CResourceManager::_CreateTexture` from the Render Thread or TBB worker loops, and guard DX11-only code in shared `src/Layers/xrRender/` files with `#if RENDER == R_R4` or `#if defined(USE_DX11)`.
3. Verify `DX11|x64` builds cleanly and test in-game before opening a PR.

</details>

---

## Credits & License

- **X-Ray Monolith**: [TheMrDemonized](https://github.com/themrdemonized/xray-monolith)
- **Physical Objective-Camera True PIP & Intel MOC Integration**: [TheLostInPlace](https://github.com/TheLostInPlace/xray-monolith-pip)
- **Original True PIP Concept**: [m22spencer](https://github.com/m22spencer/xray-monolith)
- **OpenXRay Team**: [OpenXRay](https://github.com/openxray)
- **Libraries**: [Intel oneTBB](https://github.com/oneapi-src/oneTBB), [Intel Masked Occlusion Culling](https://github.com/GameTechDev/MaskedOcclusionCulling), [LuaJIT](https://luajit.org/), [meshoptimizer](https://github.com/zeux/meshoptimizer), [Optick](https://github.com/bombomby/optick)

Contents of this repository are licensed under a custom GSC Game World proprietary license for non-commercial use. See [License.txt](./License.txt) for full terms.

<div align="center">
  <br />
  <a href="#x-ray-monolith-next-gen">Back to top</a>
</div>
