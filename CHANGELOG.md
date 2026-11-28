# Changelog

All notable changes to **X-Ray Monolith (DX11 MT + True PIP + LuaJIT 2.1 GC64)** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

---

## [1.0.0-beta] - 2026-11-27

> **Beta Pre-Release**: Public beta testing build ahead of the final `1.0.0` release. Use `STALKER-Anomaly-DX11_v1.0.0-beta.zip` for normal play (profiling disabled), or `STALKER-Anomaly-ProfiledDX11_v1.0.0-beta.zip` if you want to capture Optick profiles (`prof_capture`) for bug reports. One executable works on all 64-bit CPUs (AVX2 and AVX-512 are detected automatically at runtime). Always delete `appdata/shaders_cache` after extracting.

### Added
- **Versioned `user.ltx` Config Migration (`engine_cfg_ver`)**:
  - Added automatic one-time `user.ltx` config migration (`engine_cfg_ver`, currently `v1`) right after `user.ltx` loads in `execUserScript()` so users upgrading from vanilla Anomaly, G.A.M.M.A., or older builds automatically receive updated engine defaults once while keeping any manual tweaks afterward.
- **Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`r4_volumetric_froxels`)**:
  - Replaced legacy `.peak` volumetric lights and low-resolution sliced/screen-space volumetrics with a DX11 Compute 3D Froxel volumetric pipeline (`r4_volumetric_froxels.h/.cpp` + 7 dedicated HLSL 5.0 compute/apply shaders in `gamedata/shaders/r3/`).
  - Evaluates multi-cascade hardware-PCF sun shadow visibility (`froxel_sun_shadow.cs`) across cascades 0-2, live shadow-atlas spotlight cones (`froxel_spot_shadow.cs`), up to 64 dynamic point/spot lights via GPU `StructuredBuffer`, weather-driven height fog with animated 3D wind noise (`froxel_inject.cs`), dual-lobe Henyey-Greenstein phase + temporal reprojection (`froxel_light.cs`), and analytical Beer-Lambert transmittance integration (`froxel_integrate.cs`).
  - Composites into the lit scene via a single hardware-trilinear 3D lookup with eye-adaptation exposure matching (`froxel_apply.vs/.ps`) and Beer-Lambert hardware blending, avoiding conflicts with mod-overridden `combine_1.ps` / `combine_volumetric.ps` shaders.
- **2-Stage Decoupled Render Thread (`r__mt_decouple`)**:
  - Added a dedicated persistent Render Thread (`CRenderThread`) that overlaps Game Thread Frame N+1 (`FrameMove`, `xrSheduler`, `seqFrameMT`, `LuaGC`, `CalculateBonesThread`) with Render Thread Frame N DX11 submission and `Present()`, controlled via `r__mt_decouple` (`0-1`, default `1`).
  - Split DX11 rendering into `CRender::ExtractFrame()` (safe sync window on Main Thread + TBB for `g_SpatialSpace`, `CObject::renderable_Render`, sun cascade/light culling, HUD/SVP optics, and bone matrix snapshotting) and `CRender::RenderSubmit()` (pure DX11 submission on Render Thread).
  - Added `SRenderViewSnapshot` and `SRenderEnvSnapshot` double-buffered frame snapshots (`Device.m_rt_view` / `Device.m_rt_env`), `SFrameBoneArena` frozen skeleton bone matrices, deferred `model_Delete` queueing while the Render Thread is busy, 4-bucket `CUICommandBuffer` 2D/3D UI recording (`dxUIRender` + `dxFontRender`), and `ImDrawData` deep-copy capture (`dxImGuiRender`).
- **Physical Objective-Camera True PIP (`TheLostInPlace/xray-monolith-pip` Integration)**:
  - Integrated the physical objective-camera True PIP architecture (physical aperture/eyebox model, off-axis deferred lighting reconstruction, continuous barrel/handguard rendering, TAA/DLSS lens exclusion, hybrid reflex/magnifier support, typed Lua optic API, and in-game ImGui optic workbench).
  - Added the live per-viewport render performance stats overlay (`r__svp_stats`) and dedicated `pip.log` diagnostics.
- **LuaJIT 2.1 GC64 (Native VS2026 Integration)**:
  - Upgraded the scripting runtime to LuaJIT 2.1 with `LUAJIT_ENABLE_GC64` enabled across `luajit`, `lua_extensions`, `luabind`, and `xrGame`, removing the legacy 2 GB LuaJIT memory ceiling for heavy modpacks.
  - Ported upstream's `luaJIT_gc_udhint` GC userdata prefetch optimization into `luajit.h` and `lj_gc.c`.
- **DX11 GPU Pipeline Optimizations**:
  - Added `r4_cbuffer_skip_unchanged` (`0-1`, default `1`): dirty-checks DX11 constant buffer writes before `Map(WRITE_DISCARD)` + `CopyMemory` + `Unmap` to skip redundant uploads.
  - Added `r4_post_pingpong` (`0-1`, default `1`): ping-pongs `rt_Generic_0` and `rt_Generic` across post-process passes with SRV hazard unbinding instead of full-screen `CopyResource` copies.
  - Added `r4_sun_cascade_cache` (`0-8`, default `1`): caches the far sun shadow cascade (cascade 2) in a dedicated `rt_smap_depth_far` target across frames, automatically invalidating on camera movement, sun movement, level loads, and device resets.

### Changed
- **Removed Legacy `.peak` Volumetric System**:
  - Removed LVutner's `.peak` volumetric code (`ps_pfx_volumetric_mode` / `pfx_volumetric_mode`, `CRenderTarget::accum_volumetric_lv`, `s_combine->E[5]` `pfx_volumetric_light`, and its MCM/XML options) across R2/R3/R4 in favor of the native Compute 3D Froxel volumetric pipeline.
- **Upstream Multithreading Baseline**:
  - Merged all commits from `upstream/all-in-one-vs2022-wpo-mt` (`707ca7680`), adopting upstream's `CDSGraphManager`, Optick GPU/CPU profiler scopes, UI 3D icon atlas, and MT fixes.
- **GitHub Actions CI & Tag-Gated Releases (DX11 Only)**:
  - Streamlined `.github/workflows/msbuild.yml` to build only `DX11|x64` on `all-in-one-vs2022-wpo`, `feat/**`, and PRs (removed legacy DX8/DX9/DX10 builds, unused `-AVX` variants, duplicate non-MT matrix, and auto-releases on every commit).
  - Gated GitHub Releases strictly on version tags (`v*`) or manual `workflow_dispatch` with a `release_tag` input.
  - Packaged release archives in drag-and-drop Anomaly root layout (`bin/AnomalyDX11.exe` + unpacked `gamedata/`) with automated `CHANGELOG.md` release notes extraction, a separate Optick-enabled `ProfiledDX11` release archive (keeping profiling disabled in the main player release), and separate PDB archives.

### Performance
> **Rough Reference Benchmarks**: On a heavily modded G.A.M.M.A. test install (Ryzen 9 9950X3D, 1440p DX11, 3,000-frame `ProfiledDX11` captures), these changes roughly moved average FPS from ~97 FPS (~10.3 ms) to ~136-154 FPS (~6.5-7.3 ms), 1% lows (`p99`) from ~23 FPS (~43 ms) to ~68 FPS (~14.8 ms), reduced > 50 ms stutters from ~28 to 0, cut combat audio updates from ~3.6 ms/f (14.5 ms max) to ~0.05 ms/f, and shortened MO2 USVFS `Init FileSystem` boot time from ~17.5 s to ~5.2-6.3 s. Numbers are approximate and will vary per CPU, GPU, resolution, and modlist.

- **Game Boot, Save Load & Level Load Overhaul**:
  - Overhauled `CLocatorAPI` VFS startup scanning (`LocatorAPI.cpp`): deduplicated already-scanned recursive `fsgame.ltx` subtrees, replaced serial CRT `_findfirst`/`_findnext` + per-directory `stat(".xrignore")` with TBB-parallel Win32 `FindFirstFileExA(..., FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)` and inline `.xrignore` detection, added TBB-parallel `.db`/`.xdb` archive header and TOC LZ decompression while preserving exact deterministic lexicographical mod override order, and early-exited `Register()` parent-folder walks on the first already-registered ancestor.
  - Paused LuaJIT GC (`LUA_GCSTOP`) across `net_Start` through `net_start6` to prevent mid-load GC pauses, and capped USVFS-facing parallel directory/model I/O concurrency to avoid MO2 USVFS lock contention on high-core CPUs.
  - Eliminated the second `level.geom` disk pass in `TREE-IB` reorder by capturing tree vertex positions directly from RAM during `LoadBuffers()` and parallelized `meshoptimizer` cache/overdraw reordering across disjoint ranges via TBB.
  - Parallelized `CHOM::terrain_load()` (multi-million CFORM face scan) and `CModelPool::Prefetch()` `.ogf` disk reads via TBB.
- **LuaJIT GC64 Hysteresis & Time-Budgeted Parallel GC**:
  - Fixed a 100x Windows timing bug in LuaJIT's `LUA_GCTIMEOUT` (`gc_step_timeout` in `lj_api.c`) that compared raw `QueryPerformanceCounter` ticks against `timeout_usec * 1000` without scaling by `QueryPerformanceFrequency` (causing 500 us budgets to run for 50.0 ms), made `gc_step_timeout` yield immediately after `GCSatomic` transitions, added 2-pass incremental `grayagain` draining before `GCSatomic`, and relocated finalized/no-`__gc` `GCudata` off `mainthread->nextgc` onto `g->gc.root` (`lj_gc.c`) so `lj_gc_separateudata` never re-walks finalized userdata.
  - Replaced the legacy 25x/frame `LUA_GCSTEP` loop in `seqLuaGC` (which forced ~2 full GC cycles/sec and caused 15-19 ms `GCSatomic` stutters in both `secondary_tasks.wait` and `CActor::shedule_Update`) with GC64-aware heap-growth hysteresis (`lua_parallel_gc_trigger_mb`, default `16` MB) and time-budgeted incremental slices (`LUA_GCTIMEOUT` via `lua_parallel_gc_budget_us`, default `1500` us) that keep Main Thread auto-GC paused between cycles.
  - Updated LuaJIT `case GCSatomic` to yield the incremental step immediately after `atomic()` instead of piling string sweeps onto the same frame.
- **Combat Audio Occlusion & Deferred Scheduler Cap**:
  - Switched `CSoundRender_Core::get_occlusion()` from `CDB::OPT_ONLYNEAREST` to `CDB::OPT_ONLYFIRST` (early-out on first blocking triangle) and cached per-emitter sound occlusion at 20 Hz (`CSoundRender_Emitter::update_culling()`), eliminating up to 14.5 ms single-thread audio raycast stalls during heavy AI gunfights.
  - Capped `CSheduler` deferred AI time budget (`psShedulerTarget`) at `6.0 ms` (down from `66.0 ms`) with exact fractional QPC limits and per-item QPC checks so multi-squad combat spreads evenly across frames instead of stalling `secondary_tasks.wait()`.
  - Removed per-draw `PROF_EVENT` instrumentation from `R_constants::flush_cache()` (~6,500 calls/f), `CBackend::set_Element()`, and `CBackend::set_States()`, and force-inlined `flush_cb_stage`.
- **Runtime AVX2 Bone Palette Upload & CPU Hotspot Fixes**:
  - Added runtime CPUID-dispatched AVX2 bulk 4x4->3x4 matrix transpose (`BonePaletteSIMD.h/.cpp`, `BonePaletteSIMD_AVX2.cpp` with per-file `/arch:AVX2` and SSE2 fallback) for `CSkeletonX::_Render` `SEND_MATRICES` (both current `sbones_array` and `sbones_array_prev` motion-vector palettes), replacing ~108,900 scalar `RCache.set_ca` calls/frame with single direct CB writes.
  - Inlined `R_constants::GetCBuffer` into `dx10R_Backend_Runtime.h` with a switch dispatch and removed its per-call `PROF_EVENT` macro (~69,000 calls/frame on Render Thread).
  - Cached per-viewport interactive grass bender arrays once per frame in `FTreeVisual::Render` and uploaded via `RCache.set_c_bytes` instead of rebuilding and dirtying constant buffers on every tree draw (~3,860 draws/frame).
  - Overlapped `CRender::extract_sun_cascades()` (disjoint `GMCascade` instances, no HOM dependency) ahead of `HOM.MT_RENDER()` inside `CRender::ExtractFrame()` so sun shadow culling runs concurrently with `Secondary Task 1.1` HOM rasterization instead of blocking Main Thread on `m_mt_render_guard`.
- **Intel Masked Software Occlusion Culling (Runtime AVX2 / AVX-512)**:
  - Integrated Intel Masked Occlusion Culling (`moc.vcxproj`) with runtime CPUID dispatch for AVX2 and AVX-512 alongside the legacy HOM occluder.
- **DX11 GPU & Geometry Optimizations**:
  - Adopted 32-byte GPU grass instancing with culled slot-range skipping and front-to-back sorting, combined with TBB parallel `CDetailManager::UpdateVisibleM()` (`r__mt_details`).
  - Adopted non-sun dynamic light volume scissoring, null pixel shader sun shadow passes, DX11 input-layout/packet/constant-buffer submission memos, and `meshoptimizer` tree vertex-cache/overdraw index reordering.
- **Multithreading Worker Parallelization**:
  - Parallelized `CalculateBonesThread` across TBB workers (`mt_ParallelBones`) and replaced global `::Random` jitter in `UCalc_Visibox` with a thread-safe hash to eliminate cross-thread RNG contention.
  - Parallelized `CParticlesAsync` simulation across TBB workers and replaced `xrSpinWait` busy-spinning with `std::condition_variable` to avoid wasted CPU cycles while waiting on the particle worker.
  - Parallelized `CDetailManager::UpdateVisibleM()` across `cache_level1` rows (`r__mt_details`, default `1`) with lock-free per-row buckets and deterministic hashing.
  - Parallelized `CDSGraphManager` static scene-graph culling (`r4_parallel_cull`, default `1`) across uncached sun shadow cascades, shadowed dynamic lights, and overlapped main/SVP static culling with dynamic capture and scene preparation.
- **Visual Studio Compile Times**:
  - Replaced `/ZI` (Edit & Continue, which silently disabled `/MP`) with `/Zi` across all projects.
  - Enabled `UseMultiToolTask` and `EnforceProcessCountAcrossBuilds` in `src/Common.props`.
  - Removed unconditional LuaJIT pre-build regeneration and duplicate `lj_vm.obj` link input (`LNK4042`).
  - Disabled unused renderers in single-renderer configurations in `src/engine-vs2026.sln`.

### Fixed
- **Thread-Safe `LzHuf` Decompression (`xrCore`)**:
  - Converted mutable `LzHuf.cpp` codec state (`LZfs`, Huffman/LZSS tables, and sliding-window buffer) to `static thread_local` and reset output pointers in `Init_Input()`, fixing heap corruption when multiple `.db`/`.xdb` archive TOCs decompress concurrently across TBB worker threads.
- **Optick `prof_capture` / GPU Timeline Decoupled Render Thread Synchronization**:
  - Added `FlushRenderThread()` before `PROF_START_CAPTURE()`, `PROF_STOP_CAPTURE()`, and inside `dx10GpuTimeline::prof_state_changed()`, fixing an `nvwgf2umx.dll` D3D11 immediate-context crash when `prof_capture` finished while the dedicated Render Thread was actively submitting Frame N-1.
  - Updated `dx10GpuTimeline` to re-latch the active submission thread on `frame_start()` and read `Device.m_rt_view.dwFrame` on the Render Thread so GPU timeline capture stays intact across console/menu and decoupled gameplay transitions.
- **True PIP Scope Raise Crash & Render Thread Resource Safety**:
  - Fixed a crash when raising weapons with a PIP scope under `r__mt_decouple 1` caused by `TargetMain->EnsureScopeShaders()` (on hipfire lens capture) and `TargetSVP->EnsureScopeShaders()` (in `svp_nearblur_pass`) invoking `CResourceManager::_CreateShader` / Lua shader compilation on the Render Thread concurrently with Main Thread Frame N+1 Lua execution.
  - Pre-initialized scope shaders on both `TargetMain` and `TargetSVP` during `ExtractFrame()` / `EnsureTargetSVP()`, added a `CRenderThread::IsOnRenderThread()` guard inside `EnsureScopeShaders()`, and pre-created all SVP alias `ref_texture` handles (`$user$svp_nearblur_src`, `$user$svp_nearblur_pos`, `r2_RT_secondVP`, `r2_RT_generic2`, `r2_RT_heat`, `$user$svp_tonemap`) on the Main Thread to eliminate per-frame Render Thread `CResourceManager::_CreateTexture` calls.
  - Switched `RCache.Invalidate()` to `RCache.Invalidate(false)` in `phase_3DSSReticle()`, `phase_scope_debug()`, and `share_main_smaps()` so `r4_post_pingpong 1` texture overrides stay intact while aiming through PIP scopes, and switched SVP Render Thread reads of `Device.dwFrame` / `Device.matrices[1]` / `Device.matrices_previous[1]` to `Device.m_rt_view`.
- **3DSS / True PIP Scope Lens Capture & TAA Motion Vectors**:
  - Removed a stale pre-`tlip-pip` `bCaptureScopeLens` early-return block in `CDSGraphManager::r_dsgraph_insert_dynamic()` that discarded all `iScopeLense > 0` visuals before `tlip-pip`'s lens/reflex capture could run (which caused scopes to render as empty hollow tubes).
  - Removed duplicate `Device.matrices_previous[0..1] = Device.matrices[0..1]` overwrites in `CRenderDevice::on_idle()` that were zeroing out previous-frame camera view/projection matrices (`m_v_prev`/`m_p_prev`) and breaking SSS TAA camera motion vectors.
- **Froxel Volumetrics & Post-Process Ping-Pong Interaction**:
  - Preserved `RCache.textureOverrides` across `CVolumetricFroxels::ApplyToScene()` (`RCache.Invalidate(false)`) so `r4_post_pingpong 1` no longer alternates to the wrong swapped render target (fixing half-rate black-frame flickering when `r4_volumetric_froxels 1`).
  - Enabled `D3DCOMPILE_IEEE_STRICTNESS` for froxel shaders so `isnan`/`isinf` guards are preserved by the HLSL optimizer, and unbound the scene RTV after `ApplyToScene()`.
- **Tree Wind Motion Vector Frame Latch (`FTreeVisual`)**:
  - Fixed `FTreeVisual_setup::calculate()` storing `dwFrame` while `FTreeVisual::Render()` compared against `dwViewport`, which caused `prev_wave`/`prev_wind` to overwrite with current-frame wind on every tree after the first and degrade TAA motion vectors on swaying foliage.
- **Froxel Volumetrics Matrix Inversion & NaN Protection**:
  - Replaced 4x3 affine `Fmatrix::invert()` on the perspective `viewProj` matrix in `CVolumetricFroxels::BeginFrame()` with full 4x4 `D3DXMatrixInverse`, cleared newly allocated 3D UAV textures on creation, and added `isnan`/`isinf` sanitization across `froxel_light.cs`, `froxel_integrate.cs`, and `froxel_apply.ps` to prevent full-screen black-frame flickering.
- **Decoupled Render Thread Environment & Skeleton Synchronization**:
  - Snapshotted `dxEnvDescriptorMixerRender` sky/envmap/cloud texture lists and `$user$sky0`/`$user$sky1` bindings during `ExtractFrame()` so `CEnvironment::OnFrame()` in Frame N+1 cannot mutate texture lists concurrently while the Render Thread executes `RenderSky`, `RenderClouds`, or `phase_combine`.
  - Synchronized `SFrameBoneArena::SnapshotBonesForRT()`, `CKinematics::CopyBonesFrom()` / `LL_SetBoneVisible()`, and `CActor::renderable_Render()` shadow-pass legs updates against parallel TBB sun-cascade extraction and concurrent Render Thread reads, fixing first-person legs deformation and bone palette corruption.
- **Decoupled Render Thread OCCQ Deferral**:
  - Deferred GPU light occlusion queries (`light::vis_prepare()` / `draw_occq_volume()`) out of `CRender::ExtractFrame()` (where `phase_occq()` and PART-0 depth are not yet bound) onto the Render Thread right after PART-0 depth fill, fixing a null vertex shader crash in `CBackend::ApplyVertexLayout` under `r__mt_decouple 1`.
- **Multithreading & Renderer Thread-Safety Fixes**:
  - Gave `CalculateBonesThread` its own `vCameraPosition_bones` / `mFullTransform_bones` camera snapshot so same-frame SVP `SetActive` viewport flips cannot race with bone visibility queries.
  - Replaced global `::Random.randI` inside `CHOM::visible(vis_data&)` with a deterministic hash delay so parallel grass and static culling workers never contend on the global RNG state.
  - Synchronized `r4_post_pingpong` `RCache.override_Texture` bindings inside `CRenderTarget::SetActive()` so Main and SVP viewports never cross-bind `$user$generic0` or `$user$generic`.
- **LuaJIT 2.1 Compatibility**:
  - Updated `luaL_reg` references to `luaL_Reg` in `base.cpp`, `lfs.c`, and `lmarshal.c`, guarded `luaL_pushmodule` in `luasocket.c` with `LUAJIT_VERSION_NUM`, and made `xr_alloc.c` a no-op under `LJ_GC64`.

### Console Variables Added
| CVar | Range | Default | Description |
| :--- | :---: | :---: | :--- |
| `r4_cbuffer_skip_unchanged` | `0-1` | `1` | Skip `Map(WRITE_DISCARD)` on unchanged constant buffers |
| `r4_post_pingpong` | `0-1` | `1` | Ping-pong `rt_Generic_0`/`rt_Generic` across post-process passes |
| `r4_sun_cascade_cache` | `0-8` | `1` | Cache far sun shadow cascade (`1` = 4-frame cadence; `0` = off) |
| `r__mt_details` | `0-1` | `1` | Parallelize grass/detail visibility culling across TBB workers |
| `r4_parallel_cull` | `0-1` | `1` | Parallelize sun cascade, dynamic light, and main/SVP static culling |
| `r__mt_decouple` | `0-1` | `1` | Overlap Game Thread Frame N+1 with dedicated Render Thread Frame N submission |
| `r4_volumetric_froxels` | `0-1` | `1` | Enable Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`0` = legacy fallback) |
| `r4_volumetric_quality` | `0-2` | `1` | Froxel 3D grid resolution (`0` = `128x72x48`, `1` = `160x90x64`, `2` = `240x135x96`) |
| `r4_volumetric_range` | `30-250` | `120.0` | Maximum froxel volumetric fog & lighting distance (meters) |
| `r4_volumetric_density` | `0.0-4.0` | `1.0` | Global atmospheric scattering & volumetric fog density multiplier |
| `r4_volumetric_anisotropy` | `-0.5-0.92` | `0.65` | Henyey-Greenstein phase anisotropy controlling forward-scattering sun/spot beams |
| `r4_volumetric_temporal` | `0.0-0.96` | `0.90` | Temporal reprojection history blend factor (`0` disables temporal filtering) |
| `lua_parallel_gc_trigger_mb` | `8-512` | `16` | LuaJIT GC64 heap growth (MB) before starting a parallel GC cycle |
| `lua_parallel_gc_budget_us` | `100-5000` | `1500` | Per-frame time budget (us) for incremental parallel LuaJIT GC slices |
| `engine_cfg_ver` | `0-10000` | `1` | Stored `user.ltx` config schema version for automatic one-time default migrations |
