# Changelog

All notable changes to **X-Ray Monolith (DX11 MT + True PIP + LuaJIT 2.1 GC64)** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added
- **True 2-Stage Decoupled Render Thread (`r__mt_decouple`)**:
  - Added a dedicated persistent Render Thread (`CRenderThread`) that overlaps Game Thread Frame N+1 (`FrameMove`, `xrSheduler`, `seqFrameMT`, `LuaGC`, `CalculateBonesThread`) with Render Thread Frame N DX11 submission and `Present()`, controlled via `r__mt_decouple` (`0–1`, default `1`).
  - Split DX11 rendering into `CRender::ExtractFrame()` (Safe Sync Window on Main Thread + TBB for `g_SpatialSpace`, `CObject::renderable_Render`, sun cascade/light culling, HUD/SVP optics, and bone matrix snapshotting) and `CRender::RenderSubmit()` (pure DX11 submission on Render Thread).
  - Added `SRenderViewSnapshot` and `SRenderEnvSnapshot` double-buffered frame snapshots (`Device.m_rt_view` / `Device.m_rt_env`), `SFrameBoneArena` frozen skeleton bone matrices, deferred `model_Delete` queueing while the Render Thread is busy, 4-bucket `CUICommandBuffer` 2D/3D UI recording (`dxUIRender` + `dxFontRender`), and `ImDrawData` deep-copy capture (`dxImGuiRender`) to eliminate data races across gameplay, UI, Lua, and rendering.
- **Physical Objective-Camera True PIP (`TheLostInPlace/xray-monolith-pip` Integration)**:
  - Integrated the physical objective-camera True PIP architecture (physical aperture/eyebox model, off-axis deferred lighting reconstruction, continuous barrel/handguard rendering, TAA/DLSS lens exclusion, hybrid reflex/magnifier support, typed Lua optic API, and in-game ImGui optic workbench).
  - Added the live per-viewport render performance stats overlay (`r__svp_stats`) and dedicated `pip.log` diagnostics.
- **LuaJIT 2.1 GC64 (Native VS2026 Integration)**:
  - Upgraded scripting runtime to LuaJIT 2.1 with `LUAJIT_ENABLE_GC64` enabled across `luajit`, `lua_extensions`, `luabind`, and `xrGame`, removing the legacy 2 GB LuaJIT memory ceiling for heavy modpacks.
  - Ported upstream's `luaJIT_gc_udhint` GC userdata prefetch optimization into `luajit.h` and `lj_gc.c`.
- **DX11 GPU Pipeline Optimizations**:
  - Added `r4_cbuffer_skip_unchanged` (`0–1`, default `1`): dirty-checks DX11 constant buffer writes before `Map(WRITE_DISCARD)` + `CopyMemory` + `Unmap` to skip redundant uploads.
  - Added `r4_post_pingpong` (`0–1`, default `1`): ping-pongs `rt_Generic_0` <-> `rt_Generic` across post-process passes with SRV hazard unbinding instead of performing full-screen `CopyResource` blits.
  - Added `r4_sun_cascade_cache` (`0–8`, default `1`): caches the far sun shadow cascade (cascade 2) in a dedicated `rt_smap_depth_far` target across frames, automatically invalidating on camera movement, sun movement, level loads, and device resets.

### Changed
- **Upstream Multithreading Baseline**:
  - Merged all commits from `upstream/all-in-one-vs2022-wpo-mt` (`707ca7680`), adopting upstream's `CDSGraphManager`, Optick GPU/CPU profiler scopes, UI 3D icon atlas, and MT fixes.
- **GitHub Actions CI & Tag-Gated Releases (DX11 Only)**:
  - Streamlined `.github/workflows/msbuild.yml` to build only `DX11|x64` on `all-in-one-vs2022-wpo`, `feat/**`, and PRs (removed legacy DX8/DX9/DX10 builds, unused `-AVX` variants, duplicate non-MT matrix, and auto-releases on every commit).
  - Gated GitHub Releases strictly on version tags (`v*`) or manual `workflow_dispatch` with a `release_tag` input.
  - Packaged release archives in drag-and-drop Anomaly root layout (`bin/AnomalyDX11.exe` + unpacked `gamedata/`) with automated `CHANGELOG.md` release notes extraction and a separate PDB archive.

### Performance
- **Intel Masked Software Occlusion Culling (Runtime AVX2 / AVX-512)**:
  - Integrated Intel Masked Occlusion Culling (`moc.vcxproj`) with runtime CPUID dispatch for AVX2 and AVX-512 alongside the legacy HOM occluder.
- **DX11 GPU & Geometry Optimizations**:
  - Adopted 32-byte GPU grass instancing with culled slot-range skipping and front-to-back sorting, combined with TBB parallel `CDetailManager::UpdateVisibleM()` (`r__mt_details`).
  - Adopted non-sun dynamic light volume scissoring, null pixel shader sun shadow passes, DX11 input-layout/packet/constant-buffer submission memos, and `meshoptimizer` tree vertex-cache/overdraw index reordering.
- **Multithreading Worker Parallelization (`feat/true-mt`)**:
  - Parallelized `CalculateBonesThread` across TBB workers (`mt_ParallelBones`) and replaced global `::Random` jitter in `UCalc_Visibox` with a thread-safe hash to eliminate cross-thread RNG contention.
  - Parallelized `CParticlesAsync` simulation across TBB workers and replaced `xrSpinWait` busy-spinning with `std::condition_variable` to eliminate wasted CPU cycles while waiting on the particle worker.
  - Parallelized `CDetailManager::UpdateVisibleM()` across `cache_level1` rows (`r__mt_details`, default `1`) with lock-free per-row buckets and deterministic hashing.
  - Parallelized `CDSGraphManager` static scene-graph culling (`r4_parallel_cull`, default `1`) across uncached sun shadow cascades, shadowed dynamic lights, and overlapped main/SVP static culling with dynamic capture and scene preparation.
- **Visual Studio Compile Times**:
  - Replaced `/ZI` (Edit & Continue, which silently disabled `/MP`) with `/Zi` across all projects.
  - Enabled `UseMultiToolTask` and `EnforceProcessCountAcrossBuilds` in `src/Common.props`.
  - Removed unconditional LuaJIT pre-build regeneration and duplicate `lj_vm.obj` link input (`LNK4042`).
  - Disabled unused renderers in single-renderer configurations in `src/engine-vs2026.sln`.

### Fixed
- **Multithreading & Renderer Thread-Safety Fixes**:
  - Gave `CalculateBonesThread` its own `vCameraPosition_bones` / `mFullTransform_bones` camera snapshot so same-frame SVP `SetActive` viewport flips cannot race with bone visibility queries.
  - Replaced global `::Random.randI` inside `CHOM::visible(vis_data&)` with a deterministic hash delay so parallel grass and static culling workers never contend on the global RNG state.
  - Synchronized `r4_post_pingpong` `RCache.override_Texture` bindings inside `CRenderTarget::SetActive()` so Main and SVP viewports never cross-bind `$user$generic0` or `$user$generic`.
- **LuaJIT 2.1 Compatibility**:
  - Updated `luaL_reg` references to `luaL_Reg` in `base.cpp`, `lfs.c`, and `lmarshal.c`, guarded `luaL_pushmodule` in `luasocket.c` with `LUAJIT_VERSION_NUM`, and made `xr_alloc.c` a no-op under `LJ_GC64`.

### Console Variables Added
| CVar | Range | Default | Description |
| :--- | :---: | :---: | :--- |
| `r4_cbuffer_skip_unchanged` | `0–1` | `1` | Skip `Map(WRITE_DISCARD)` on unchanged constant buffers |
| `r4_post_pingpong` | `0–1` | `1` | Ping-pong `rt_Generic_0`/`rt_Generic` across post-process passes |
| `r4_sun_cascade_cache` | `0–8` | `1` | Cache far sun shadow cascade (`1` = 4-frame cadence; `0` = off) |
| `r__mt_details` | `0–1` | `1` | Parallelize grass/detail visibility culling across TBB workers |
| `r4_parallel_cull` | `0–1` | `1` | Parallelize sun cascade, dynamic light, and main/SVP static culling |
| `r__mt_decouple` | `0–1` | `1` | Overlap Game Thread Frame N+1 with dedicated Render Thread Frame N submission |
