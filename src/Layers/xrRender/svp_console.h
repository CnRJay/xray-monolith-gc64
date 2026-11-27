#ifndef svp_consoleH
#define svp_consoleH
#pragma once

// true PiP second viewport (SVP) console vars, the complete extern set
// definitions and registration live in svp_console.cpp

extern ECORE_API int scope_svp_enabled; // true PiP scope mode with zero off and positive inputs objective
extern ECORE_API int scope_debug; // scope debug overlay level
extern ECORE_API int ps_r__3db_debug; // ballistics overlay level

// render / perf
extern ECORE_API float ps_r__svp_render_scale;
extern ECORE_API float ps_r__svp_supersample;
extern ECORE_API int ps_r__svp_diag;
extern ECORE_API int ps_r__svp_cop_diag;
extern ECORE_API int ps_r__svp_report;
extern ECORE_API int ps_r__svp_stats; // per-viewport render stats overlay (0 off, 1 compact, 2 breakdown, 3 lean main view)
extern ECORE_API u32 svp_stats_ssa_culled; // svp small-object cull tally read by the overlay, incremented in r__dsgraph_render
extern ECORE_API u32 svp_stats_cull_reject; // svp off-cone frustum-reject tally read by the overlay, incremented in r__dsgraph_render
extern ECORE_API u32 svp_stats_cull_reject_ident; // svp identity-matrix sorted world statics the cone rejects, incremented in svp_cull_reject
extern ECORE_API u32 svp_stats_lights_mirrored; // svp lights the cone cull mirrors into the scope, incremented in lights_render
extern ECORE_API u32 svp_stats_lights_skipped; // svp lights the cone cull drops, incremented in lights_render
extern ECORE_API u32 svp_stats_taa_stamp; // successful raw taa skip mask draws read by the overlay
extern ECORE_API u32 svp_stats_nvg_split; // svp nvg tube split fires read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_lod_scale; // svp lod scale armed frames read by the overlay, incremented in svp_set_lod_scale
extern ECORE_API u32 svp_stats_hud_cull_reject; // svp hud drain cone rejects read by the overlay, incremented in svp_hud_latch
extern ECORE_API u32 svp_stats_reflex_capture; // svp hybrid reflex draws read by the overlay
extern ECORE_API u32 svp_stats_distort_guard; // svp distort guard stamps read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_nvg_sky; // svp nvg sky lum remaps read by the overlay, incremented in phase_combine
extern ECORE_API u32 svp_stats_disc_latch; // svp adaptive-res disc latch moves read by the overlay, incremented in phase_3DSSReticle
extern ECORE_API u32 svp_stats_fwd_keep; // svp forward-list keeps read by the overlay, incremented in render_forward
// session-lifetime ever-fired ledger latches, set at the gated call sites, swept once per session
extern ECORE_API u32 svp_ledger_cull_reject;
extern ECORE_API u32 svp_ledger_ssa_culled;
extern ECORE_API u32 svp_ledger_cull_reject_ident;
extern ECORE_API u32 svp_ledger_lights_mirrored;
extern ECORE_API u32 svp_ledger_lights_skipped;
extern ECORE_API u32 svp_ledger_lod_scale;
extern ECORE_API u32 svp_ledger_distort_guard;
extern ECORE_API u32 svp_ledger_disc_latch;
extern ECORE_API u32 svp_ledger_fwd_keep;
extern ECORE_API u32 svp_ledger_grass_cull;
extern ECORE_API float ps_r__svp_adaptive_res;
extern ECORE_API float ps_r__svp_lod;
extern ECORE_API float ps_r__svp_cull_ssa;
extern ECORE_API int ps_r__svp_dlss;
extern ECORE_API int ps_r__svp_adaptive_grow;
extern ECORE_API int ps_r__svp_cull;
extern ECORE_API int ps_r__svp_light_cull;
extern ECORE_API int ps_r__svp_corner_mask;
extern ECORE_API int ps_r__svp_skip_motionblur;
extern ECORE_API int ps_r__svp_skip_hud_distort;
extern ECORE_API int ps_r__svp_skip_dof;
extern ECORE_API int ps_r__svp_wpn_dof;
extern ECORE_API int ps_r__svp_skip_lut;
extern ECORE_API int ps_r__svp_emissive;
extern ECORE_API int ps_r__svp_skip_ssr;
extern ECORE_API int ps_r__svp_skip_volumetric;
extern ECORE_API int ps_r__svp_skip_grass;
extern ECORE_API int ps_r__svp_grass_cull;
extern ECORE_API int ps_r__svp_sss_sun;
extern ECORE_API int ps_r__svp_skip_pos_copy;
extern ECORE_API int ps_r__svp_smap_alloc;

// main-view post gate, lossless skips for post passes with nothing to do
extern ECORE_API int ps_r__pp_lean;
extern ECORE_API int ps_r__cp_scale_region;
extern ECORE_API int ps_r__ssfx_ssr_enable;
extern ECORE_API int ps_r__ssfx_bloom_hud;
extern ECORE_API u32 svp_stats_lean_flags; // bit per lean skip that fired this frame, decoded by the breakdown panel

// pipeline-waste tallies for the third overlay box, all reset per frame like the counters above
extern ECORE_API u32 svp_stats_copies;
extern ECORE_API u32 svp_stats_copy_kb;
extern ECORE_API u32 svp_stats_tiny;
extern ECORE_API u32 svp_stats_shadow;
// tracked copy categories, hist is the closed temporal publishes, tail the back-copies, scene the alias
// publishes, pass the volumetric sss and scope position publishes the other three never counted
enum svp_copy_cat_e { SVP_CP_HIST = 0, SVP_CP_TAIL, SVP_CP_SCENE, SVP_CP_PASS, SVP_CP_COUNT };
extern ECORE_API u32 svp_stats_copy_kb_cat[SVP_CP_COUNT];
// brackets one tracked full-frame copy, self-gated so it costs an int compare when the overlay is off
extern ECORE_API void svp_copy_begin(u32 cat, u32 bytes);
extern ECORE_API void svp_copy_end(u32 cat);
// gpu timer hook, the r4 stats module installs it, null on the renderers without a query pool
extern ECORE_API void (*svp_copy_timer_hook)(u32 cat, bool begin);

// engine-wide render cost tallies for the fps audit overlay, reset per frame like the counters above
extern ECORE_API u32 svp_stats_state_apply;   // dx10State::Apply calls this frame
extern ECORE_API u32 svp_stats_sampler_set;   // XXSetSamplers issues this frame, every shader stage combined
extern ECORE_API u32 svp_stats_cb_flush;      // constant buffer Flush calls this frame
extern ECORE_API u32 svp_stats_cb_flush_map;  // of those, how many actually mapped and copied
extern ECORE_API float svp_stats_join_ms;         // secondary task join wait, ms, this frame
extern ECORE_API float svp_stats_capture_base_ms; // main graph traverse plus capture, cpu ms, this frame
extern ECORE_API float svp_stats_capture_cascade_ms[3]; // per sun cascade traverse plus capture, cpu ms
// sun sub timer ids, order matches SEC_SUN_SMAP through SEC_SUN_SVP
enum { SUN_SUB_SMAP = 0, SUN_SUB_GRASS, SUN_SUB_MINMAX, SUN_SUB_ACCUM, SUN_SUB_VOL, SUN_SUB_SVP };
extern void svp_stats_sun_sub_begin(u32 sub);
extern void svp_stats_sun_sub_end(u32 sub);
extern ECORE_API float svp_stats_present_ms;  // swapchain Present wall clock, ms, this frame
extern ECORE_API u32 svp_stats_sort_calls;    // render queue sort invocations this frame
extern ECORE_API u32 svp_stats_sort_packets;  // total packets sorted across those invocations
extern ECORE_API u32 svp_stats_layout_hit;    // ApplyVertexLayout calls the layout memo can serve
extern ECORE_API u32 svp_stats_layout_miss;   // of those, the ones that still walk the declaration map
// grass slot spheres measured against the sun cascade frustums, summed over the cascades of one frame
extern ECORE_API u32 svp_stats_grass_slots;   // slots tested
extern ECORE_API u32 svp_stats_grass_keep;    // of those, the ones the frustum keeps
extern ECORE_API u32 svp_stats_grass_runs;    // stretches of consecutive kept slots
extern ECORE_API u32 svp_stats_grass_run_max; // longest single stretch
extern ECORE_API u32 svp_stats_grass_drop;    // instances the cull left unsubmitted
extern ECORE_API u32 svp_stats_grass_draws;   // sub range draws the cull issued
// the same measurements taken on the scope gbuffer pass against the scope cone
extern ECORE_API u32 svp_stats_grass_svp_slots;
extern ECORE_API u32 svp_stats_grass_svp_keep;
extern ECORE_API u32 svp_stats_grass_svp_runs;
extern ECORE_API u32 svp_stats_grass_svp_run_max;
extern ECORE_API u32 svp_stats_grass_svp_drop;
extern ECORE_API u32 svp_stats_grass_svp_draws;
// session-lifetime running totals, never reset, read by the level-load audit summary
extern ECORE_API u32 svp_stats_detail_main_thread; // grass MT_CALC runs that fell back to the calling thread
extern ECORE_API u32 svp_stats_hom_main_thread;    // HOM MT_RENDER runs that fell back to the calling thread
extern ECORE_API u32 svp_stats_hom_tested;         // detail slots tested against HOM
extern ECORE_API u32 svp_stats_hom_rejected;       // of those, how many HOM rejected

// occlusion engine selection, latched once per frame at the top of CHOM::Render
extern ECORE_API int ps_r__hom_engine;   // 0 legacy raster, 1 masked rasterizer, 2 both with legacy answering
extern ECORE_API int ps_r__hom_moc_res;  // masked buffer preset, 0 256x144, 1 384x216, 2 512x288
extern ECORE_API u32 svp_stats_hom_engine;         // engine id the last latch picked
extern ECORE_API u32 svp_stats_hom_res_w;          // masked buffer width, zero while the engine is off
extern ECORE_API u32 svp_stats_hom_res_h;
extern ECORE_API u32 svp_stats_hom_tris_in;        // occluders reaching the emit loop this frame
extern ECORE_API u32 svp_stats_hom_tris_emitted;   // of those, the ones that passed the facing test
extern ECORE_API u32 svp_stats_hom_render_us;      // occluder render span, both engines under shadow compare
extern ECORE_API u64 svp_stats_hom_test_ticks;     // query time, sampled only while the breakdown panel is up
extern ECORE_API u32 svp_stats_hom_disagree;       // session total of shadow compare mismatches
extern ECORE_API u32 svp_stats_hom_shadow_queries; // session total of shadow compare queries
extern ECORE_API u32 svp_stats_hom_dis_keep;       // mismatches where legacy culls and the masked engine keeps
extern ECORE_API u32 svp_stats_moc_ret;            // last masked RenderTriangles result plus one, 0 = never ran
extern ECORE_API u32 svp_stats_moc_fill_pct;       // masked buffer occupancy percent from the throttled probe

// cform ground fed to the masked buffer, read at level load and once per occlusion frame
extern ECORE_API int ps_r__hom_terrain;            // 0 skips the load scan and every per frame cost
extern ECORE_API u32 svp_stats_hom_terr_cells;     // terrain grid cells the frustum kept this frame
extern ECORE_API u32 svp_stats_hom_terr_emitted;   // terrain tris pushed into the masked buffer
extern ECORE_API u32 svp_stats_hom_terr_capped;    // 1 when the emit cap stopped the walk

// optics derivation
extern ECORE_API float ps_r__svp_obj_dist;
extern ECORE_API float ps_r__svp_obj_size;
extern ECORE_API float ps_r__svp_near;
extern ECORE_API int ps_r__svp_focal_derive;
extern ECORE_API int ps_r__svp_glare_model;
extern ECORE_API int ps_r__svp_photo_model;
extern ECORE_API int ps_r__svp_roll_stabilize;
extern ECORE_API int ps_r__svp_clean_optics;
extern ECORE_API int ps_r__svp_distort_guard;
extern ECORE_API int ps_r__svp_jitterfix;
extern ECORE_API int ps_r__svp_taa_mask;
extern ECORE_API int ps_r__svp_bloom;
extern ECORE_API int ps_r__svp_local_exposure;
extern ECORE_API float ps_r__svp_exposure_bias;
extern ECORE_API int ps_r__svp_light_capture;
extern ECORE_API int ps_r__svp_npc_detail;
extern ECORE_API int ps_r__svp_thermal_sim;
extern ECORE_API float ps_r__svp_twilight;
extern ECORE_API float ps_r__svp_parallax;
extern ECORE_API float ps_r__svp_near_blur;
extern ECORE_API float ps_r__svp_focus_m;
extern ECORE_API int ps_r__svp_authored_optics;
extern ECORE_API float ps_r__svp_eyebox;
extern ECORE_API int ps_r__svp_reflex_capture;
extern ECORE_API int ps_r__svp_measured_optics; // measured lens geometry fills unauthored optics, registered in svp_console.cpp
extern ECORE_API int ps_r__svp_lens_reject; // detected objective offsets measured off the runtime eyepiece are dropped

// per-scope overrides pushed by zzz_extra_scope_features.script
extern ECORE_API float ps_s3ds_objective_mm;
extern ECORE_API float ps_s3ds_middle_grey;
extern ECORE_API float ps_s3ds_adapt_speed;

// glass / reticle cosmetics
extern ECORE_API int ps_r__svp_chroma;
extern ECORE_API float ps_r__svp_field_curve;
extern ECORE_API int ps_r__svp_field_stop;
extern ECORE_API int ps_r__svp_aperture;
extern ECORE_API float ps_s3ds_tunneling_parallax;
extern ECORE_API float ps_s3ds_tunneling_min;
extern ECORE_API float ps_s3ds_tunneling_max;
extern ECORE_API float ps_s3ds_eye_tracking_speed;
extern ECORE_API float ps_s3ds_eye_tracking_accel_mm_s2;
extern ECORE_API float ps_s3ds_eye_tracking_limit_mm;
extern ECORE_API float ps_s3ds_eye_relief_low_mm;
extern ECORE_API float ps_s3ds_eye_relief_high_mm;
extern ECORE_API float ps_s3ds_exit_pupil_low_mm;
extern ECORE_API float ps_s3ds_exit_pupil_high_mm;
extern ECORE_API float ps_s3ds_pupil_parity;
extern ECORE_API float ps_s3ds_pupil_field_low;
extern ECORE_API float ps_s3ds_pupil_field_high;
extern ECORE_API float ps_s3ds_transmission;
extern ECORE_API float ps_s3ds_twilight_strength;
extern ECORE_API Fvector4 ps_svp_exit_curve_low;
extern ECORE_API Fvector4 ps_svp_exit_curve_high;
extern ECORE_API Fvector4 ps_svp_tunnel_curve_low;
extern ECORE_API Fvector4 ps_svp_tunnel_curve_high;
extern ECORE_API Fvector4 ps_svp_dim_curve_low;
extern ECORE_API Fvector4 ps_svp_dim_curve_high;
extern ECORE_API float ps_svp_exit_scale;
extern ECORE_API float ps_svp_exit_offset;
extern ECORE_API float ps_svp_tunnel_scale;
extern ECORE_API float ps_svp_tunnel_offset;
extern ECORE_API float ps_svp_dim_scale;
extern ECORE_API float ps_svp_dim_offset;
extern ECORE_API float ps_r__svp_veiling_glare;
extern ECORE_API float ps_r__svp_rain_optic;
extern ECORE_API float ps_r__svp_rain_debug;
extern ECORE_API int ps_r__svp_uv_debug;
extern ECORE_API int ps_r__svp_autoflip;
extern ECORE_API int ps_r__svp_autoflip_reticle;
extern ECORE_API float ps_r__svp_coating;
extern ECORE_API float ps_r__svp_mirage;
extern ECORE_API int ps_r__svp_flat_window;
extern ECORE_API float ps_r__svp_sharpen;
extern ECORE_API float ps_r__svp_sharpen_falloff;
extern ECORE_API float ps_r__svp_sharpen_inner;
extern ECORE_API float ps_r__svp_nvg_bleach;
extern ECORE_API float ps_r__svp_nvg_sensitivity;
extern ECORE_API int ps_r__svp_nvg_objective;
extern ECORE_API int ps_r__svp_weapon_continuity;
extern ECORE_API int ps_r__svp_optic_body_suppress;
extern ECORE_API int ps_r__svp_nearblur_scatter;

// registers every svp console command, called once from xrRender_initconsole
extern void svp_console_init();

// prints the [SVP-LEDGER] inert-path sweep, called once per session from svpCamera
extern void svp_ledger_sweep();

#endif
