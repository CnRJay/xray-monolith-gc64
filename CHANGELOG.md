# Changelog

All notable changes to **X-Ray Monolith (DX11 MT + True PIP + LuaJIT 2.1 GC64)** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added
- **Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`r4_volumetric_froxels`)**:
  - Replaced legacy `.peak` volumetric lights and low-resolution sliced/screen-space volumetrics with a native Frostbite/UE5-style DX11 Compute 3D Froxel volumetric pipeline (`r4_volumetric_froxels.h/.cpp` + 7 dedicated HLSL 5.0 compute/apply shaders in `gamedata/shaders/r3/`).
  - Evaluates multi-cascade hardware-PCF sun shadow visibility (`froxel_sun_shadow.cs`) across cascades 0–2, live shadow-atlas spotlight cones (`froxel_spot_shadow.cs`), up to 64 dynamic point/spot lights via GPU `StructuredBuffer`, weather-driven height fog with animated 3D wind noise (`froxel_inject.cs`), dual-lobe Henyey-Greenstein phase + temporal reprojection (`froxel_light.cs`), and energy-conserving analytical Beer-Lambert transmittance integration (`froxel_integrate.cs`).
  - Composites into the lit scene via a single hardware-trilinear 3D lookup with eye-adaptation exposure matching (`froxel_apply.vs/.ps`) and Beer-Lambert hardware blending, avoiding conflicts with mod-overridden `combine_1.ps` / `combine_volumetric.ps` shaders.
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
- **Removed Legacy `.peak` Volumetric System**:
  - Removed LVutner's `.peak` volumetric code (`ps_pfx_volumetric_mode` / `pfx_volumetric_mode`, `CRenderTarget::accum_volumetric_lv`, `s_combine->E[5]` `pfx_volumetric_light`, and its MCM/XML options) across R2/R3/R4 in favor of the native Compute 3D Froxel volumetric pipeline.
- **Upstream Multithreading Baseline**:
  - Merged all commits from `upstream/all-in-one-vs2022-wpo-mt` (`707ca7680`), adopting upstream's `CDSGraphManager`, Optick GPU/CPU profiler scopes, UI 3D icon atlas, and MT fixes.
- **GitHub Actions CI & Tag-Gated Releases (DX11 Only)**:
  - Streamlined `.github/workflows/msbuild.yml` to build only `DX11|x64` on `all-in-one-vs2022-wpo`, `feat/**`, and PRs (removed legacy DX8/DX9/DX10 builds, unused `-AVX` variants, duplicate non-MT matrix, and auto-releases on every commit).
  - Gated GitHub Releases strictly on version tags (`v*`) or manual `workflow_dispatch` with a `release_tag` input.
  - Packaged release archives in drag-and-drop Anomaly root layout (`bin/AnomalyDX11.exe` + unpacked `gamedata/`) with automated `CHANGELOG.md` release notes extraction and a separate PDB archive.

### Performance
- **Runtime AVX2 Bone Palette Upload & Optick CPU Hotspot Wins**:
  - Added runtime CPUID-dispatched AVX2 bulk 4×4→3×4 matrix transpose (`BonePaletteSIMD.h/.cpp`, `BonePaletteSIMD_AVX2.cpp` with per-file `/arch:AVX2` and SSE2 fallback) for `CSkeletonX::_Render` `SEND_MATRICES` (both current `sbones_array` and `sbones_array_prev` motion-vector palettes), replacing ~108,900 scalar `RCache.set_ca` calls/frame with single direct CB writes.
  - Inlined `R_constants::GetCBuffer` into `dx10R_Backend_Runtime.h` with a switch dispatch and removed its per-call `PROF_EVENT` macro (~69,000 calls/frame on Render Thread).
  - Cached per-viewport interactive grass bender arrays once per frame in `FTreeVisual::Render` and uploaded via `RCache.set_c_bytes` instead of rebuilding and dirtying constant buffers on every tree draw (~3,860 draws/frame).
  - Overlapped `CRender::extract_sun_cascades()` (disjoint `GMCascade` instances, no HOM dependency) ahead of `HOM.MT_RENDER()` inside `CRender::ExtractFrame()` so sun shadow culling runs concurrently with `Secondary Task 1.1` HOM rasterization instead of blocking Main Thread on `m_mt_render_guard`.
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
- **True PIP Scope Raise Crash & Render Thread Resource Safety**:
  - Fixed a crash when raising weapons with a PIP scope under `r__mt_decouple 1` caused by `TargetMain->EnsureScopeShaders()` (on hipfire lens capture) and `TargetSVP->EnsureScopeShaders()` (in `svp_nearblur_pass`) invoking `CResourceManager::_CreateShader` / Lua shader compilation on the Render Thread concurrently with Main Thread Frame N+1 Lua execution.
  - Pre-initialized scope shaders on both `TargetMain` and `TargetSVP` during `ExtractFrame()` / `EnsureTargetSVP()`, added a `CRenderThread::IsOnRenderThread()` guard inside `EnsureScopeShaders()`, and pre-created all SVP alias `ref_texture` handles (`$user$svp_nearblur_src`, `$user$svp_nearblur_pos`, `r2_RT_secondVP`, `r2_RT_generic2`, `r2_RT_heat`, `$user$svp_tonemap`) on the Main Thread to eliminate per-frame Render Thread `CResourceManager::_CreateTexture` calls.
  - Switched `RCache.Invalidate()` to `RCache.Invalidate(false)` in `phase_3DSSReticle()`, `phase_scope_debug()`, and `share_main_smaps()` so `r4_post_pingpong 1` texture overrides stay intact while aiming through PIP scopes, and switched SVP Render Thread reads of `Device.dwFrame` / `Device.matrices[1]` / `Device.matrices_previous[1]` to `Device.m_rt_view`.
- **3DSS / True PIP Scope Lens Capture & TAA Motion Vectors**:
  - Removed a stale pre-`tlip-pip` `bCaptureScopeLens` early-return block in `CDSGraphManager::r_dsgraph_insert_dynamic()` that discarded all `iScopeLense > 0` visuals before `tlip-pip`'s lens/reflex capture could run (which caused scopes to render as empty hollow tubes).
  - Removed duplicate `Device.matrices_previous[0..1] = Device.matrices[0..1]` overwrites in `CRenderDevice::on_idle()` that were zeroing out previous-frame camera view/projection matrices (`m_v_prev`/`m_p_prev`) and breaking SSS TAA camera motion vectors.
- **Froxel Volumetrics & Post-Process Ping-Pong Interaction**:
  - Preserved `RCache.textureOverrides` across `CVolumetricFroxels::ApplyToScene()` (`RCache.Invalidate(false)`) so `r4_post_pingpong 1` no longer alternates to the wrong swapped render target (eliminating the half-rate black-frame flickering when `r4_volumetric_froxels 1`).
  - Enabled `D3DCOMPILE_IEEE_STRICTNESS` for froxel shaders so `isnan`/`isinf` guards are preserved by the HLSL optimizer, and unbound the scene RTV after `ApplyToScene()`.
- **Tree Wind Motion Vector Frame Latch (`FTreeVisual`)**:
  - Fixed `FTreeVisual_setup::calculate()` storing `dwFrame` while `FTreeVisual::Render()` compared against `dwViewport`, which caused `prev_wave`/`prev_wind` to overwrite with current-frame wind on every tree after the first and degrade TAA motion vectors on swaying foliage.
- **Froxel Volumetrics Matrix Inversion & NaN Protection**:
  - Replaced 4x3 affine `Fmatrix::invert()` on the perspective `viewProj` matrix in `CVolumetricFroxels::BeginFrame()` with full 4x4 `D3DXMatrixInverse`, cleared newly allocated 3D UAV textures on creation, and added `isnan`/`isinf` sanitization across `froxel_light.cs`, `froxel_integrate.cs`, and `froxel_apply.ps` to eliminate full-screen black-frame flickering.
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
| `r4_cbuffer_skip_unchanged` | `0–1` | `1` | Skip `Map(WRITE_DISCARD)` on unchanged constant buffers |
| `r4_post_pingpong` | `0–1` | `1` | Ping-pong `rt_Generic_0`/`rt_Generic` across post-process passes |
| `r4_sun_cascade_cache` | `0–8` | `1` | Cache far sun shadow cascade (`1` = 4-frame cadence; `0` = off) |
| `r__mt_details` | `0–1` | `1` | Parallelize grass/detail visibility culling across TBB workers |
| `r4_parallel_cull` | `0–1` | `1` | Parallelize sun cascade, dynamic light, and main/SVP static culling |
| `r__mt_decouple` | `0–1` | `1` | Overlap Game Thread Frame N+1 with dedicated Render Thread Frame N submission |
| `r4_volumetric_froxels` | `0–1` | `1` | Enable Native DX11 Compute 3D Froxel Volumetric Fog & Lighting (`0` = legacy fallback) |
| `r4_volumetric_quality` | `0–2` | `1` | Froxel 3D grid resolution (`0` = `128×72×48`, `1` = `160×90×64`, `2` = `240×135×96`) |
| `r4_volumetric_range` | `30–250` | `120.0` | Maximum froxel volumetric fog & lighting distance (meters) |
| `r4_volumetric_density` | `0.0–4.0` | `1.0` | Global atmospheric scattering & volumetric fog density multiplier |
| `r4_volumetric_anisotropy` | `-0.5–0.92` | `0.65` | Henyey-Greenstein phase anisotropy controlling forward-scattering sun/spot beams |
| `r4_volumetric_temporal` | `0.0–0.96` | `0.90` | Temporal reprojection history blend factor (`0` disables temporal filtering) |
