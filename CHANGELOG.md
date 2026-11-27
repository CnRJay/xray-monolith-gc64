# Changelog

All notable changes to **X-Ray Monolith (DX11 MT + True PIP + LuaJIT 2.1 GC64)** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added
- **LuaJIT 2.1 GC64 (Native VS2026 Integration)**:
  - Upgraded scripting runtime to LuaJIT 2.1 with `LUAJIT_ENABLE_GC64` enabled across `luajit`, `lua_extensions`, `luabind`, and `xrGame`, removing the legacy 2 GB LuaJIT memory ceiling for heavy modpacks.
  - Ported upstream's `luaJIT_gc_udhint` GC userdata prefetch optimization into `luajit.h` and `lj_gc.c`.
- **True PIP (SVP) Quality & Resolution Controls**:
  - Added `r__svp_scale` (`0.25–1.0`, default `0.5`) to configure the square SVP render target resolution (8-pixel aligned).
  - Added `r__svp_quality` (`0–1`, default `0`) to skip expensive screen-space passes (SSR, IL, motion blur, `SSS_ext`, and non-NVG blur) inside the scope viewport by default while allowing full quality on demand.
- **DX11 GPU Pipeline Optimizations**:
  - Added `r4_cbuffer_skip_unchanged` (`0–1`, default `1`): dirty-checks DX11 constant buffer writes before `Map(WRITE_DISCARD)` + `CopyMemory` + `Unmap` to skip redundant uploads.
  - Added `r4_post_pingpong` (`0–1`, default `1`): ping-pongs `rt_Generic_0` <-> `rt_Generic` across 12 post-process passes with SRV hazard unbinding instead of performing full-screen `CopyResource` blits.
  - Added `r4_sun_cascade_cache` (`0–8`, default `1`): caches the far sun shadow cascade (cascade 2) in a dedicated `rt_smap_depth_far` target across frames, automatically invalidating on camera movement, sun movement, level loads, and device resets.

### Changed
- **Upstream Multithreading Baseline**:
  - Merged all commits from `upstream/all-in-one-vs2022-wpo-mt` (`707ca7680`), adopting upstream's `CDSGraphManager`, Optick GPU/CPU profiler scopes, and MT fixes while preserving True PIP Scopes (SVP 0.0.15).
- **GitHub Actions CI & Tag-Gated Releases (DX11 Only)**:
  - Streamlined `.github/workflows/msbuild.yml` to build only the `DX11|x64` configuration on `all-in-one-vs2022-wpo`, `feat/**`, and PRs (removed legacy DX8/DX9/DX10 builds, unused `-AVX` variants, duplicate non-MT matrix, upstream `retarget-pr.yml`, and auto-releases on every commit).
  - Gated GitHub Releases strictly on version tags (`v*`) or manual `workflow_dispatch` with a `release_tag` input.
  - Packaged release archives in drag-and-drop Anomaly root layout (`bin/AnomalyDX11.exe` + unpacked `gamedata/`) with automated `CHANGELOG.md` release notes extraction and a separate PDB archive.

### Performance
- **Multithreading Worker Parallelization (`feat/true-mt`)**:
  - Parallelized `CalculateBonesThread` across TBB workers (`mt_ParallelBones`) and replaced global `::Random` jitter in `UCalc_Visibox` with a thread-safe hash to eliminate cross-thread RNG contention.
  - Parallelized `CParticlesAsync` simulation across TBB workers and replaced `xrSpinWait` busy-spinning with `std::condition_variable` to eliminate wasted CPU cycles while waiting on the particle worker.
  - Parallelized `CDetailManager::UpdateVisibleM()` across `cache_level1` rows (`r__mt_details`, default `1`) with lock-free per-row buckets and deterministic hashing.
  - Parallelized `CDSGraphManager` static scene-graph culling (`r4_parallel_cull`, default `1`) across uncached sun shadow cascades, shadowed dynamic lights, and overlapped main/SVP static culling with dynamic capture and scene preparation.
- **True PIP (SVP) Pipeline Overhaul**:
  - Skipped allocating Main-only render targets and LUTs on `TargetSVP` (13 `rt_ssfx_bloom*`, `rt_Bloom_1/2`, `rt_pp_bloom`, `rt_dof`, `rt_fakescope`, `rt_secondVP`, `rt_ui_pda`, `rt_HDR10_HalfRes`, `t_ss_async`, 3D material LUT, noise LUTs) and eliminated the `phase_svp_capture()` `CopyResource` by binding `TargetSVP->rt_Generic_0` directly as `r2_RT_secondVP`.
  - Merged scope `IMAGE`, `RETICLE`, `SHADOW`, and `LENS` passes into a single draw call (`scope_color_write.ps`) and early-returned `phase_3DSSReticle()` when no scope/reflex mesh is present, eliminating two full-resolution copies on every non-scoped frame.
  - Added a circular Early-Z corner mask (`depth = 0.0` outside `r = 0.55`) on `TargetSVP` so the GPU skips geometry and deferred shading in the four invisible corners of the square scope target.
  - Restored full-resolution multi-batch dynamic light shadowmap pooling during hip-fire, reserving shared single-atlas shrinking strictly for active SVP ADS frames.
- **Visual Studio Compile Times**:
  - Replaced `/ZI` (Edit & Continue, which silently disabled `/MP`) with `/Zi` across all 26 `.vcxproj` projects.
  - Enabled `UseMultiToolTask` and `EnforceProcessCountAcrossBuilds` in `src/Common.props`.
  - Removed unconditional LuaJIT pre-build regeneration and duplicate `lj_vm.obj` link input (`LNK4042`).
  - Disabled unused renderers in single-renderer configurations in `src/engine-vs2026.sln`.

### Fixed
- **Multithreading Thread-Safety Fixes**:
  - Gave `CalculateBonesThread` its own `vCameraPosition_bones` / `mFullTransform_bones` camera snapshot so same-frame SVP `SetActive` viewport flips cannot race with bone visibility queries.
  - Replaced global `::Random.randI` inside `CHOM::visible(vis_data&)` with a deterministic hash delay so parallel grass and static culling workers never contend on the global RNG state.
- **True PIP Scope Fixes**:
  - Fixed distant grass popping out inside magnified scopes by adding SVP frustum visibility and magnification SSA scaling to `CDetailManager::UpdateVisibleM()`.
  - Fixed fake-3DSS (`r__svpscope 0`) reticle warping when merging scope color passes by tracking per-stage `scope_active_phase`, moved `JITTERFIX` early-out before `new_Scope(I)`, and fixed aspect correction to use `output_res`.
  - Replaced per-frame `ref_texture::create()` allocations and mid-frame `TargetMain->SetActive(true)` invalidation in `phase_3DSSReticle()` with cached texture overrides, and gated `SCOPE_PHASE_CUSTOM_DEPTH` on `R2FLAG_DOF`.
- **LuaJIT 2.1 Compatibility**:
  - Updated `luaL_reg` references to `luaL_Reg` in `base.cpp`, `lfs.c`, and `lmarshal.c`, guarded `luaL_pushmodule` in `luasocket.c` with `LUAJIT_VERSION_NUM`, and made `xr_alloc.c` a no-op under `LJ_GC64`.

### Console Variables Added
| CVar | Range | Default | Description |
| :--- | :---: | :---: | :--- |
| `r__svp_scale` | `0.25–1.0` | `0.5` | Square SVP render target resolution scale (8-px aligned) |
| `r__svp_quality` | `0–1` | `0` | `0` skips heavy screen-space FX in SVP; `1` full quality |
| `r4_cbuffer_skip_unchanged` | `0–1` | `1` | Skip `Map(WRITE_DISCARD)` on unchanged constant buffers |
| `r4_post_pingpong` | `0–1` | `1` | Ping-pong `rt_Generic_0`/`rt_Generic` across post-process passes |
| `r4_sun_cascade_cache` | `0–8` | `1` | Cache far sun shadow cascade (`1` = 4-frame cadence; `0` = off) |
| `r__mt_details` | `0–1` | `1` | Parallelize grass/detail visibility culling across TBB workers |
| `r4_parallel_cull` | `0–1` | `1` | Parallelize sun cascade, dynamic light, and main/SVP static culling |
