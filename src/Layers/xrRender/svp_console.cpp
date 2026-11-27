#include	"stdafx.h"
#pragma		hdrstop

#include	"svp_console.h"

#include	"../../xrEngine/xr_ioconsole.h"
#include	"../../xrEngine/xr_ioc_cmd.h"
#include	"../../xrEngine/device.h"
#include	"../../xrEngine/svp_console_policy.h"

int scope_svp_enabled = 0; // true PiP scope mode with zero off and positive inputs objective
float ps_r__svp_render_scale = 1.0f; // SVP render scale, 1.0 keeps the dwHeight/2 per side
float ps_r__svp_supersample = 1.0f; // SVP supersample: render the SVP square larger so the eyepiece downsamples it (SSAA), 1.0 = off, 2.0 = 4x SVP pixels
int ps_r__svp_diag = 0; // SVP perf diagnostics: throttled log of [SVP-RES] over-render ratio + [SVP-ALLOC] target size while scoped, 0 = off
int ps_r__svp_cop_diag = 0; // svp optics diagnostics, 1 = throttled [SVPCOP]/[SVP-HUD]/[SVP-BARREL], 2 = adds the per-frame [SVP-AIM] reticle-vs-screen-center delta
int ps_r__svp_report = 0; // svp one-shot report, 1 re-dumps [SVP-CFG] + [SVP-FILES] on the next scoped frame then clears itself
int ps_r__svp_stats = 0; // per-viewport render stats overlay, 0 off, 1 compact table, 2 adds the per-section breakdown, 3 lean main view
u32 svp_stats_ssa_culled = 0; // svp small-object cull tally the overlay reads, incremented in r__dsgraph_render while the overlay is on
u32 svp_stats_cull_reject = 0; // svp off-cone frustum-reject tally the overlay reads, incremented in r__dsgraph_render
u32 svp_stats_cull_reject_ident = 0; // svp identity-matrix sorted world statics the cone rejects, incremented in svp_cull_reject
u32 svp_stats_lights_mirrored = 0; // svp lights the cone cull mirrors into the scope, incremented in lights_render
u32 svp_stats_lights_skipped = 0; // svp lights the cone cull drops, incremented in lights_render
u32 svp_stats_taa_stamp = 0; // successful raw taa skip mask draws read by the overlay
u32 svp_stats_nvg_split = 0; // svp nvg tube split fires the overlay reads, incremented in phase_combine
u32 svp_stats_lod_scale = 0; // svp lod scale armed frames the overlay reads, incremented in svp_set_lod_scale
u32 svp_stats_hud_cull_reject = 0; // svp hud drain cone rejects the overlay reads, incremented in svp_hud_latch
u32 svp_stats_reflex_capture = 0; // svp hybrid reflex draws the overlay reads
u32 svp_stats_distort_guard = 0; // svp distort guard stamps the overlay reads, incremented in phase_combine
u32 svp_stats_nvg_sky = 0; // svp nvg sky lum remaps the overlay reads, incremented in phase_combine
u32 svp_stats_disc_latch = 0; // svp adaptive-res disc latch moves the overlay reads, incremented in phase_3DSSReticle
u32 svp_stats_fwd_keep = 0; // svp forward-list keeps the overlay reads, incremented in render_forward
u32 svp_stats_optic_resolve = 0; // svp optics bus resolve fires the overlay reads, incremented in svp_optics_resolve
// session-lifetime ever-fired ledger latches, one per instrumented gated path, never per-frame
// reset, the [SVP-LEDGER] sweep flags any still zero while its gate cvar is on
u32 svp_ledger_cull_reject = 0;
u32 svp_ledger_ssa_culled = 0;
u32 svp_ledger_cull_reject_ident = 0;
u32 svp_ledger_lights_mirrored = 0;
u32 svp_ledger_lights_skipped = 0;
u32 svp_ledger_lod_scale = 0;
u32 svp_ledger_distort_guard = 0;
u32 svp_ledger_disc_latch = 0;
u32 svp_ledger_fwd_keep = 0;
u32 svp_ledger_grass_cull = 0;
int ps_r__3db_debug = 0; // 3db ballistics overlay, 1 = bone markers + fire axis + crosshair ray + sight line + mrad log, 2 = adds the zeroed ray, 3 = adds fading shot tracers
float ps_r__svp_adaptive_res = 1.2f; // adaptive SVP resolution: size the SVP render to the on-screen eyepiece disc * this margin. 0 = off (full svp_height), 1.0 = render exactly at the disc (sharpest, mild pan shimmer), 1.2 = keep ~1.2x SSAA. big oculars clamp to no-op
float ps_r__svp_lod = 1.0f; // SVP LOD reduction strength [0..1]: scale the scope's LOD selection to its true pixel coverage (coarser at low mag, capped at the main view so zoomed detail is never worse). 0 = off
float ps_r__svp_cull_ssa = 4.0f; // SVP small-object cull strength: skip scope geometry below this * the LOD-out ssa, scaled by magnification (tiny distant clutter at low mag). 0 = off, higher = more aggressive
int ps_r__svp_dlss = 0; // SVP DLSS-SR master gate, 0 = stock (render_scale inert), nonzero = scaffolding active
// svpscope 2 geometric objective (single-lens scopes have no distinct front lens to capture, derive it
// along the optical axis from the eyepiece), lengths are in eyepiece radii (the only mesh-scale-robust
// unit), refined per scope from real objective_mm later
float ps_r__svp_obj_dist = 1.0f;     // svpscope 2 objective: scale on the AUTO geomscan front distance (1.0 = raw auto, fixed 14r fallback when geomscan finds nothing)
float ps_r__svp_obj_size = 1.0f;     // svpscope 2 objective radius = eyepiece_radius * this (authored-set median, mod_system_3dss_objective_lenses n=67, 0.65 = the old tuned value)
float ps_r__svp_near = 0.0f;         // objective near plane, 0 = auto from the nearest drawn weapon, positive meters = manual override
int ps_r__svp_focal_derive = 1;      // svpscope 2 focal anchors derive from eye relief + tube length (0 = the original 0.4/0.6 split)
int ps_r__svp_glare_model = 1;       // veiling glare falloff: 1 = Stiles-Holladay 1/theta^2 vs the scope's half fov, 0 = legacy pow6 cone
int ps_r__svp_photo_model = 1;       // eye photometrics: 1 = Moon-Spencer pupil + squared relative brightness, 0 = legacy linear models
int ps_r__svp_adaptive_grow = 0;     // 1 sizes the SVP up to the disc past the half-height base, costs up to 4x the pixels on large oculars (0 = shrink-only, stock cost)
// Retired commands remain registered so old user configs load cleanly
static int s_svp_compat_drain_anchor = 1;
static int s_svp_compat_settle_derive = 1;
static int s_svp_compat_ratio_derive = 1;
static int s_svp_compat_recoil_hold = 1;
int ps_r__svp_roll_stabilize = 0; // svp scope tilt stabilize reserve, the world image is always level (real optics never roll the image with mount cant)
int ps_r__svp_clean_optics = 1; // svp strip the 3DSS fake cosmetics (parallax shadow, chromatism, nvg blur, fisheye) for a clean scope (1 = stripped, default; 0 = full 3DSS look)
int ps_r__svp_distort_guard = 1; // svp stamp the distort mask neutral over the composited lens so the combine warp is identity there (0 = let the lens warp with the main view)
int ps_r__svp_jitterfix = 1; // svp lens edge jitter pre-pass, likely superseded by the sentinel jitter, 0 skips it for the a/b
int ps_r__svp_taa_mask = 1; // svp stamp the taa mask over the composited lens so the main resolve keeps the svp viewport taa there (0 = let the main taa re-resolve the lens)
static int s_svp_compat_hud_fov_match = 2;
int ps_r__svp_bloom = 1; // svp bloom on the scope pass so magnified bright sources flare (0 = off)
int ps_r__svp_local_exposure = 1; // svp scope-local eye adaptation, the image grades by the scope's own measured exposure (0 = main exposure)
float ps_r__svp_exposure_bias = 0.0f; // svp local-exposure bias in stops
int ps_r__svp_light_capture = 1; // svp capture lights for the scope cone past the main frustum/fog cull (0 = main-view lights only)
int ps_r__svp_npc_detail = 1; // svp keep dynamic parts the main view discards as subpixel (distant NPC heads), the scope resolves them (0 = stock discard)
int ps_r__svp_thermal_sim = 1; // svp digital-sensor sim on thermal displays: sensor cell grid + per-cell noise (0 = clean optical image)
float ps_r__svp_twilight = 1.0f; // svp exit-pupil twilight dimming: zooming shrinks the exit pupil below the dark-adapted eye and the image dims, day scenes unaffected (0 = off)
float ps_r__svp_parallax = 0.0f; // svp reticle parallax, 0 = pinned center (default), 1 = the real eye deflection response
int ps_r__svp_reticle_fit = 1; // svp reticle field slope follows the stock zoom projection so authored sizes fill the rim (0 = legacy wide-view slope)
float ps_r__svp_near_blur = 1.0f; // svp near-field defocus strength on the scope image (svpscope 2, 0 = off)
int ps_r__svp_nearblur_scatter = 0; // svp near-blur composite, 0 = gather default look, 1 = scatter accumulator
float ps_r__svp_focus_m = 100.0f; // svp parallax focus distance in meters, objects off this plane defocus by the thin lens law
int ps_r__svp_authored_optics = 1; // svp use the authored per-scope scope_objective_lens_offset for the front plane + defocus aperture (0 = measured geometry only)
int ps_r__svp_measured_optics = 1; // svp fill the objective offset + mm from mesh detection when authored ltx is absent
int ps_r__svp_lens_reject = 1; // svp drop a detected objective offset whose eye disc sits off the runtime eyepiece (0 = apply every detected offset)
float ps_r__svp_eyebox = 1.0f; // svp eyebox strength, console unregistered, 1.0 keeps the anchor bound publish alive
int ps_r__svp_reflex_capture = 1; // svp hybrid reflex through the objective camera, 0 keeps the 1x fallback
float ps_s3ds_objective_mm = 0.f; // per-scope objective clear aperture mm from spec sheets, pushed by zzz_extra_scope_features (0 = mesh-relative fallback)
float ps_s3ds_middle_grey = 0.f; // per-scope SVP tonemap middle-grey override, pushed by zzz_extra_scope_features (0 = inherit main)
float ps_s3ds_adapt_speed = 0.f; // per-scope SVP tonemap adaptation speed override (0 = inherit main)
int ps_r__svp_chroma = 1; // svp keep the authored per-scope chromatic aberration on glass under true PiP, scaled by zoom (0 = stripped with clean optics)
float ps_r__svp_field_curve = 1.0f; // svp field curvature edge softness, outer field blurs like a real non-flat-field scope (0 = flat)
int ps_r__svp_field_stop = 1; // svp ocular field stop rim vignette from the capped pupil penumbra (0 = off)
int ps_r__svp_aperture = 1; // stateless physical exit-pupil transmission for true PiP
float ps_s3ds_tunneling_parallax = 0.035f; // maximum inner-tube shift in lens UV
float ps_s3ds_tunneling_min = 0.04f; // tube visibility at the optic's minimum magnification
float ps_s3ds_tunneling_max = 0.06f; // tube visibility at the optic's maximum magnification
float ps_s3ds_eye_tracking_speed = 5.0f; // target-closing response while the cheek weld is intact
float ps_s3ds_eye_tracking_accel_mm_s2 = 80.0f; // maximum virtual-eye acceleration in millimeters per second squared
float ps_s3ds_eye_tracking_limit_mm = 7.0f; // maximum eye travel; larger scope displacement remains visible
float ps_s3ds_eye_relief_low_mm = 80.0f;
float ps_s3ds_eye_relief_high_mm = 80.0f;
float ps_s3ds_exit_pupil_low_mm = 0.0f; // zero derives the endpoint from objective diameter
float ps_s3ds_exit_pupil_high_mm = 0.0f;
float ps_s3ds_pupil_parity = -1.0f; // signed exit to entrance pupil mapping
float ps_s3ds_pupil_field_low = 0.55f;
float ps_s3ds_pupil_field_high = 0.55f;
float ps_s3ds_transmission = 1.0f;
float ps_s3ds_twilight_strength = 0.35f; // per-optic twilight-loss strength
Fvector4 ps_svp_exit_curve_low = {1.f, 1.f, 1.f, 1.f}; // response at 1x, 2x, 3x, 4x
Fvector4 ps_svp_exit_curve_high = {1.f, 1.f, 1.f, 1.f}; // response at 6x, 8x, 12x, 20x
Fvector4 ps_svp_tunnel_curve_low = {1.f, 1.f, 1.f, 1.f};
Fvector4 ps_svp_tunnel_curve_high = {1.f, 1.f, 1.f, 1.f};
Fvector4 ps_svp_dim_curve_low = {1.f, 1.f, 1.f, 1.f};
Fvector4 ps_svp_dim_curve_high = {1.f, 1.f, 1.f, 1.f};
float ps_svp_exit_scale = 1.f;
float ps_svp_exit_offset = 0.f;
float ps_svp_tunnel_scale = 1.f;
float ps_svp_tunnel_offset = 0.f;
float ps_svp_dim_scale = 1.f;
float ps_svp_dim_offset = 0.f;
float ps_r__svp_veiling_glare = 0.0f; // svp veiling glare strength, off-axis sun scatter washes the image near the sun (0 = off)
float ps_r__svp_rain_optic = 1.0f; // svp rain droplets on the objective glass, scaled by rain density (0 = off)
float ps_r__svp_rain_debug = 0.0f; // svp forces the scope rain regardless of weather, the value stands in for rain density (0 = live weather)
int ps_r__svp_uv_debug = 0; // svp sample coordinate display 0 off 1 V 2 U
int ps_r__svp_autoflip = 1; // svp auto-correct a mesh-inverted lens (world upside down) by detecting the flipped sample-coord V (1 = on)
int ps_r__svp_autoflip_reticle = 1; // svp auto-correct a mesh-inverted reticle to match the auto-flipped world (1 = on)
float ps_r__svp_coating = 0.0f; // svp lens coating, typical multi-coated glass transmission loss + faint warm tint (0 = off, 1 = physical)
float ps_r__svp_mirage = 0.0f; // svp heat mirage on distant ground, grows with sun heat and magnification (0 = off)
int ps_r__svp_flat_window = 1; // svp flat-screen optics render the panel's see-through window subtense so magnification tracks the stock look (1 = on)
float ps_r__svp_sharpen = 0.35f; // svp contrast-adaptive sharpen on the scope image (0 = off, 1 = max)
float ps_r__svp_sharpen_falloff = 0.0f; // svp sharpen radial falloff toward the disc rim, crisp center soft edge (0 = uniform)
float ps_r__svp_sharpen_inner = 0.0f; // svp sharpen inner crisp-zone radius before the falloff starts (0 = from center)
float ps_r__svp_nvg_bleach = 0.0f; // svp NVG highlight bleach roll-off, replaces the hard clamp so bright sources compress not clip (0 = off, stock)
float ps_r__svp_nvg_sensitivity = 1.0f; // svp NVG bleach onset sensitivity, higher rolls off dimmer sources
int ps_r__svp_nvg_objective = 1; // svp lens draws the centered tube class so wearer mask furniture stays off the glass
static int s_svp_compat_hud_full = 2;
int ps_r__svp_weapon_continuity = 1; // svp same frame weapon pose and entrance pupil camera
int ps_r__svp_clipon = 1; // svp a clip-on ahead of the objective owns the entrance (0 = lens pair front)
static int s_svp_compat_ray_transfer = 2;
int ps_r__svp_optic_body_suppress = 1; // svp omit the housing that contains the objective plane
static int s_svp_compat_near_pupil = 0;
static int s_svp_compat_drain_clip = 0;
int ps_r__svp_cull = 1; // svp cull the scope geometry to the scope frustum, the SVP re-submits the whole main-frustum world otherwise (1 = on)
int ps_r__svp_skip_motionblur = 0; // svp skip motion blur on the scope pass, magnified blur is an artifact and a small cost (0 = keep)
int ps_r__svp_skip_hud_distort = 1; // svp skip hud-layer distortion on the scope pass, the muzzle heat billboard sits on the entrance pupil and warps the image (1 = skip)
// note r__svp_distort_guard owns the lens footprint in the main view, 0 here shows warp there only with the guard also 0
int ps_r__svp_skip_dof = 0; // svp skip the scope-internal dof pass, main dof still covers the composited lens (0 = current doubled behavior)
int ps_r__svp_wpn_dof = 1; // svp pass the sss weapon dof constants through while scoped, 0 zeroes them like before
int ps_r__svp_skip_lut = 0; // svp skip the scope-internal lut grade, main grade still covers the composited lens (0 = current doubled behavior)
int ps_r__svp_emissive = 1; // svp replay self-illum geometry into the scope image so lamps glow through the scope (0 = main-only emissive)
int ps_r__svp_skip_ssr = 1; // svp scope reflections, 0 reflective water + SSR, 1 flat water + SSR (default), 2 flat water + no SSR
int ps_r__svp_skip_volumetric = 0; // svp skip volumetric lights on the scope pass, subtle at magnification (0 = keep)
int ps_r__svp_skip_grass = 0; // svp skip grass/details on the scope pass, near grass is mostly off a zoomed cone (0 = keep)
int ps_r__svp_grass_cull = 1; // svp draw only the grass slot ranges the scope cone keeps (0 = draw the whole main-view set)
int ps_r__svp_sss_sun = 0; // svp compute the scope SSS pass and keep the sun contact shadow term on the scope, expensive (0 = off)
int ps_r__svp_skip_pos_copy = 1; // svp skip the main position copy into generic2 on the scope path, the lens composite remaps that name to the scope surface (0 = copy)
int ps_r__svp_smap_alloc = 1; // svp target allocates its own shadow atlas it never renders into, 0 aliases the main maps and takes effect on the next svp target build
int ps_r__svp_light_cull = 1; // svp cone-cull the mirrored light blends, skip a light whose sphere never meets the scope cone (1 = on, 0 = mirror everything)
int ps_r__svp_corner_mask = 1; // svp stencil the dead corners outside the eyepiece disc so the lighting + combine passes skip them (1 = on)
int ps_r__pp_lean = 1; // master gate for the idle post-pass skips in phase_combine (0 = stock, every pass runs)
int ps_r__cp_scale_region = 1; // history copies move only the scaled box the pass wrote plus a filter margin (0 = whole surface)
int ps_r__ssfx_ssr_enable = 1; // ssfx screen space reflections master switch, the shader-presence flag has no off (1 = on)
int ps_r__ssfx_bloom_hud = 0; // hud sorted glass in the bloom emissive buffer (0 = excluded, 1 = the old double count)
u32 svp_stats_lean_flags = 0; // bit per lean skip that fired this frame, decoded by the breakdown panel
u32 svp_stats_copies = 0; // tracked full-frame CopyResource calls this frame, tallied by svp_copy_begin
u32 svp_stats_copy_kb = 0; // destination kilobytes those tracked copies moved
u32 svp_stats_tiny = 0; // main-view sorted draws below the lod-out ssa, counted in r__dsgraph_render
u32 svp_stats_shadow = 0; // sun cascade shadow-map renders this frame, counted in render_sun_cascade

u32 svp_stats_copy_kb_cat[SVP_CP_COUNT] = { 0, 0, 0, 0 }; // tracked copy kilobytes split by category
void (*svp_copy_timer_hook)(u32 cat, bool begin) = nullptr;

void svp_copy_begin(u32 cat, u32 bytes)
{
	if (ps_r__svp_stats == 0)
		return;
	++svp_stats_copies;
	const u32 kb = bytes >> 10;
	svp_stats_copy_kb += kb;
	if (cat < SVP_CP_COUNT)
		svp_stats_copy_kb_cat[cat] += kb;
	if (svp_copy_timer_hook)
		svp_copy_timer_hook(cat, true);
}

void svp_copy_end(u32 cat)
{
	if (ps_r__svp_stats == 0 || !svp_copy_timer_hook)
		return;
	svp_copy_timer_hook(cat, false);
}

// engine-wide render cost tallies for the fps audit overlay, defined here per the cross-module counter style
u32 svp_stats_state_apply = 0;
u32 svp_stats_sampler_set = 0;
u32 svp_stats_cb_flush = 0;
u32 svp_stats_cb_flush_map = 0;
float svp_stats_join_ms = 0.f;
float svp_stats_capture_base_ms = 0.f;
float svp_stats_capture_cascade_ms[3] = { 0.f, 0.f, 0.f };
float svp_stats_present_ms = 0.f;
u32 svp_stats_sort_calls = 0;
u32 svp_stats_sort_packets = 0;
u32 svp_stats_layout_hit = 0;
u32 svp_stats_layout_miss = 0;
u32 svp_stats_grass_slots = 0;
u32 svp_stats_grass_keep = 0;
u32 svp_stats_grass_runs = 0;
u32 svp_stats_grass_run_max = 0;
u32 svp_stats_grass_drop = 0;
u32 svp_stats_grass_draws = 0;
u32 svp_stats_grass_svp_slots = 0;
u32 svp_stats_grass_svp_keep = 0;
u32 svp_stats_grass_svp_runs = 0;
u32 svp_stats_grass_svp_run_max = 0;
u32 svp_stats_grass_svp_drop = 0;
u32 svp_stats_grass_svp_draws = 0;
u32 svp_stats_detail_main_thread = 0;
u32 svp_stats_hom_main_thread = 0;
u32 svp_stats_hom_tested = 0;
u32 svp_stats_hom_rejected = 0;

int ps_r__hom_engine = 1; // occlusion engine, 0 legacy raster, 1 masked rasterizer, 2 both with legacy answering
int ps_r__hom_moc_res = 3; // masked buffer preset, 0 256x144, 1 384x216, 2 512x288, 3 768x432
u32 svp_stats_hom_engine = 0;
u32 svp_stats_hom_res_w = 0;
u32 svp_stats_hom_res_h = 0;
u32 svp_stats_hom_tris_in = 0;
u32 svp_stats_hom_tris_emitted = 0;
u32 svp_stats_hom_render_us = 0;
u64 svp_stats_hom_test_ticks = 0;
u32 svp_stats_hom_disagree = 0;
u32 svp_stats_hom_shadow_queries = 0;
u32 svp_stats_hom_dis_keep = 0;
u32 svp_stats_moc_ret = 0;
u32 svp_stats_moc_fill_pct = 0;

int ps_r__hom_terrain = 1; // cform ground occluders, 0 skips the load scan and every per frame cost
u32 svp_stats_hom_terr_cells = 0;
u32 svp_stats_hom_terr_emitted = 0;
u32 svp_stats_hom_terr_capped = 0;

int scope_debug = 0;

class CCC_SvpScopeMode final : public CCC_Integer
{
public:
	CCC_SvpScopeMode(LPCSTR name, int* value, int minimum, int maximum)
		: CCC_Integer(name, value, minimum, maximum)
	{
	}

	void Execute(LPCSTR args) override
	{
		const int requested = atoi(args);
		if (requested < min || requested > max)
		{
			InvalidSyntax();
			return;
		}

		const int canonical = requested ? 2 : 0;
		const int previous = *value;
		if (canonical == previous)
		{
			if (requested != canonical)
				Msg("[SVP-CONFIG] requested=%d mode=%d state=unchanged", requested, canonical);
			return;
		}

		*value = canonical;
		Device.m_SecondViewport.SetOpticScopeMode(static_cast<u8>(canonical));
		Msg("[SVP-CONFIG] requested=%d mode=%d state=invalidated", requested, canonical);
	}
};

void svp_console_init()
{
#if defined(USE_DX11)
	// Keep the shared default off for other renderers
	// DX11 starts in objective true PiP unless user config disables it
	scope_svp_enabled = 2;
	Device.m_SecondViewport.SetOpticScopeMode(2);
	CMD4(CCC_SvpScopeMode, "r__svpscope", &scope_svp_enabled, 0, 2);
	CMD4(CCC_SvpInternalFloat, "r__svp_render_scale", &ps_r__svp_render_scale, 0.5f, 1.0f);
	CMD4(CCC_Float, "r__svp_supersample", &ps_r__svp_supersample, 1.0f, 2.0f); // SSAA the magnified scope image, 1.0 = off (4x SVP cost at 2.0)
	CMD4(CCC_Integer, "r__svp_diag", &ps_r__svp_diag, 0, 1); // SVP perf diagnostics log (0 = off)
	CMD4(CCC_Integer, "r__svp_cop_diag", &ps_r__svp_cop_diag, 0, 2); // svp optics log (1 = throttled, 2 = + per-frame [SVP-AIM])
	CMD4(CCC_Integer, "r__svp_report", &ps_r__svp_report, 0, 1); // svp one-shot [SVP-CFG]+[SVP-FILES] re-dump, self-clearing
	CMD4(CCC_Integer, "r__svp_stats", &ps_r__svp_stats, 0, 3); // per-viewport render stats overlay (1 = compact, 2 = per-section breakdown, 3 = lean main view)
	CMD4(CCC_Integer, "r__3db_debug", &ps_r__3db_debug, 0, 3); // 3db overlay (1 = markers + axes, 2 = + zeroed ray, 3 = + tracers)
	CMD4(CCC_Float, "r__svp_adaptive_res", &ps_r__svp_adaptive_res, 0.0f, 2.0f); // size SVP render to the eyepiece disc * margin (0 = off, 1.2 recommended)
	CMD4(CCC_Float, "r__svp_lod", &ps_r__svp_lod, 0.0f, 1.0f); // SVP LOD reduction strength (0 = off)
	CMD4(CCC_Float, "r__svp_cull_ssa", &ps_r__svp_cull_ssa, 0.0f, 8.0f); // SVP small-object cull strength (0 = off)
	CMD4(CCC_SvpInternalInteger, "r__svp_dlss", &ps_r__svp_dlss, 0, 1);
	CMD4(CCC_SvpInternalFloat, "r__svp_obj_dist", &ps_r__svp_obj_dist, 0.0f, 3.0f);
	CMD4(CCC_SvpVolatileFloat, "r__svp_near", &ps_r__svp_near, 0.0f, 0.5f); // objective near plane (0 = auto), session only
	CMD4(CCC_SvpInternalFloat, "r__svp_obj_size", &ps_r__svp_obj_size, 0.1f, 6.0f);
	CMD4(CCC_SvpFixedInteger, "r__svp_focal_derive", &ps_r__svp_focal_derive, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_glare_model", &ps_r__svp_glare_model, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_photo_model", &ps_r__svp_photo_model, 0, 1);
	CMD4(CCC_Integer, "r__svp_adaptive_grow", &ps_r__svp_adaptive_grow, 0, 1); // adaptive res grows to the disc (0 = shrink-only)
	CMD4(CCC_SvpFixedInteger, "r__svp_drain_anchor", &s_svp_compat_drain_anchor, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_settle_derive", &s_svp_compat_settle_derive, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_ratio_derive", &s_svp_compat_ratio_derive, 0, 1);
	CMD4(CCC_Integer, "r__svp_lens_reject", &ps_r__svp_lens_reject, 0, 1); // svp drop off-frame detected objective offsets
	CMD4(CCC_SvpFixedInteger, "r__svp_recoil_hold", &s_svp_compat_recoil_hold, 0, 1);
	CMD4(CCC_Integer, "r__svp_roll_stabilize", &ps_r__svp_roll_stabilize, 0, 1); // svp keep the scope world level on lean/cant (0 = realistic image-tilts-with-cant)
	CMD4(CCC_SvpFixedInteger, "r__svp_clean_optics", &ps_r__svp_clean_optics, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_distort_guard", &ps_r__svp_distort_guard, 0, 1);
	CMD4(CCC_SvpInternalInteger, "r__svp_jitterfix", &ps_r__svp_jitterfix, 0, 1);
	CMD4(CCC_Integer, "r__svp_taa_mask", &ps_r__svp_taa_mask, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_hud_fov_match", &s_svp_compat_hud_fov_match, 0, 2);
	CMD4(CCC_Integer, "r__svp_bloom", &ps_r__svp_bloom, 0, 1); // svp bloom on the scope pass
	CMD4(CCC_Integer, "r__svp_local_exposure", &ps_r__svp_local_exposure, 0, 1); // svp scope-local eye adaptation
	CMD4(CCC_SvpInternalFloat, "r__svp_exposure_bias", &ps_r__svp_exposure_bias, -3.0f, 3.0f);
	CMD4(CCC_Integer, "r__svp_light_capture", &ps_r__svp_light_capture, 0, 1); // svp scope-cone light capture
	CMD4(CCC_Integer, "r__svp_npc_detail", &ps_r__svp_npc_detail, 0, 1);
	CMD4(CCC_Integer, "r__svp_thermal_sim", &ps_r__svp_thermal_sim, 0, 1); // svp thermal digital-sensor sim (0 = clean)
	CMD4(CCC_Float, "r__svp_twilight", &ps_r__svp_twilight, 0.0f, 1.0f); // svp exit-pupil twilight dimming strength (0 = off)
	CMD4(CCC_Float, "r__svp_parallax", &ps_r__svp_parallax, 0.0f, 10.0f);
	CMD4(CCC_Integer, "r__svp_reticle_fit", &ps_r__svp_reticle_fit, 0, 1); // svp reticle slope fix kill-switch
	CMD4(CCC_Float, "r__svp_near_blur", &ps_r__svp_near_blur, 0.0f, 3.0f); // svp near-field defocus strength (0 = off)
	CMD4(CCC_Integer, "r__svp_nearblur_scatter", &ps_r__svp_nearblur_scatter, 0, 1);
	CMD4(CCC_SvpInternalFloat, "r__svp_focus_m", &ps_r__svp_focus_m, 10.0f, 1000.0f);
	CMD4(CCC_Integer, "r__svp_authored_optics", &ps_r__svp_authored_optics, 0, 1);
	CMD4(CCC_Integer, "r__svp_measured_optics", &ps_r__svp_measured_optics, 0, 1);
	CMD4(CCC_Integer, "r__svp_reflex_capture", &ps_r__svp_reflex_capture, 0, 1); // svp hybrid reflex through the objective camera
	CMD4(CCC_SvpProfileFloat, "s3ds_objective_mm", &ps_s3ds_objective_mm, 0.0f, 200.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_middle_grey", &ps_s3ds_middle_grey, 0.0f, 2.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_adapt_speed", &ps_s3ds_adapt_speed, 0.0f, 20.0f);
	CMD4(CCC_Integer, "r__svp_chroma", &ps_r__svp_chroma, 0, 1); // svp keep authored chromatic aberration on glass, zoom scaled (0 = stripped)
	// user knob, the workbench global lens slider writes it live
	CMD4(CCC_Float, "r__svp_field_curve", &ps_r__svp_field_curve, 0.0f, 3.0f);
	CMD4(CCC_Integer, "r__svp_field_stop", &ps_r__svp_field_stop, 0, 1); // svp ocular field stop rim vignette (0 = off)
	CMD4(CCC_Integer, "r__svp_aperture", &ps_r__svp_aperture, 0, 1); // svp exit-pupil transmission, 0 = pre-port look
	CMD4(CCC_SvpProfileFloat, "s3ds_tunneling_parallax", &ps_s3ds_tunneling_parallax, 0.0f, 0.15f);
	CMD4(CCC_SvpProfileFloat, "s3ds_tunneling_min", &ps_s3ds_tunneling_min, 0.0f, 1.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_tunneling_max", &ps_s3ds_tunneling_max, 0.0f, 1.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_eye_tracking_speed", &ps_s3ds_eye_tracking_speed, 0.1f, 30.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_eye_tracking_accel_mm_s2", &ps_s3ds_eye_tracking_accel_mm_s2, 1.0f, 500.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_eye_tracking_limit_mm", &ps_s3ds_eye_tracking_limit_mm, 0.0f, 20.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_eye_relief_low_mm", &ps_s3ds_eye_relief_low_mm, 20.0f, 150.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_eye_relief_high_mm", &ps_s3ds_eye_relief_high_mm, 20.0f, 150.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_exit_pupil_low_mm", &ps_s3ds_exit_pupil_low_mm, 0.0f, 100.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_exit_pupil_high_mm", &ps_s3ds_exit_pupil_high_mm, 0.0f, 100.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_pupil_parity", &ps_s3ds_pupil_parity, -1.0f, 1.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_pupil_field_low", &ps_s3ds_pupil_field_low, 0.0f, 6.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_pupil_field_high", &ps_s3ds_pupil_field_high, 0.0f, 6.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_transmission", &ps_s3ds_transmission, 0.0f, 1.0f);
	CMD4(CCC_SvpProfileFloat, "s3ds_twilight_strength", &ps_s3ds_twilight_strength, 0.0f, 1.0f);
	const Fvector4 curve_min = {0.f, 0.f, 0.f, 0.f};
	const Fvector4 curve_max = {3.f, 3.f, 3.f, 3.f};
	CMD4(CCC_SvpInternalVector4, "r__svp_exit_curve_1_4", &ps_svp_exit_curve_low, curve_min, curve_max);
	CMD4(CCC_SvpInternalVector4, "r__svp_exit_curve_6_20", &ps_svp_exit_curve_high, curve_min, curve_max);
	CMD4(CCC_SvpInternalVector4, "r__svp_tunnel_curve_1_4", &ps_svp_tunnel_curve_low, curve_min, curve_max);
	CMD4(CCC_SvpInternalVector4, "r__svp_tunnel_curve_6_20", &ps_svp_tunnel_curve_high, curve_min, curve_max);
	CMD4(CCC_SvpInternalVector4, "r__svp_dim_curve_1_4", &ps_svp_dim_curve_low, curve_min, curve_max);
	CMD4(CCC_SvpInternalVector4, "r__svp_dim_curve_6_20", &ps_svp_dim_curve_high, curve_min, curve_max);
	CMD4(CCC_SvpInternalFloat, "r__svp_exit_scale", &ps_svp_exit_scale, 0.f, 3.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_exit_offset", &ps_svp_exit_offset, -1.f, 1.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_tunnel_scale", &ps_svp_tunnel_scale, 0.f, 3.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_tunnel_offset", &ps_svp_tunnel_offset, -1.f, 1.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_dim_scale", &ps_svp_dim_scale, 0.f, 3.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_dim_offset", &ps_svp_dim_offset, -1.f, 1.f);
	CMD4(CCC_SvpInternalFloat, "r__svp_veiling_glare", &ps_r__svp_veiling_glare, 0.0f, 3.0f);
	CMD4(CCC_Float, "r__svp_rain_optic", &ps_r__svp_rain_optic, 0.0f, 3.0f); // svp rain droplets on the objective (0 = off)
	CMD4(CCC_Float, "r__svp_rain_debug", &ps_r__svp_rain_debug, 0.0f, 3.0f); // svp force scope rain, value = density stand-in (0 = live weather)
	CMD4(CCC_Integer, "r__svp_uv_debug", &ps_r__svp_uv_debug, 0, 2); // svp sample coordinate display
	CMD4(CCC_SvpFixedInteger, "r__svp_autoflip", &ps_r__svp_autoflip, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_autoflip_reticle", &ps_r__svp_autoflip_reticle, 0, 1);
	CMD4(CCC_SvpInternalFloat, "r__svp_coating", &ps_r__svp_coating, 0.0f, 1.0f);
	CMD4(CCC_SvpInternalFloat, "r__svp_mirage", &ps_r__svp_mirage, 0.0f, 3.0f);
	CMD4(CCC_SvpFixedInteger, "r__svp_flat_window", &ps_r__svp_flat_window, 0, 1);
	CMD4(CCC_Float, "r__svp_sharpen", &ps_r__svp_sharpen, 0.0f, 1.0f); // svp contrast-adaptive sharpen (0 = off)
	CMD4(CCC_SvpInternalFloat, "r__svp_sharpen_falloff", &ps_r__svp_sharpen_falloff, 0.0f, 1.0f);
	CMD4(CCC_SvpInternalFloat, "r__svp_sharpen_inner", &ps_r__svp_sharpen_inner, 0.0f, 1.0f);
	CMD4(CCC_SvpInternalFloat, "r__svp_nvg_bleach", &ps_r__svp_nvg_bleach, 0.0f, 1.0f);
	CMD4(CCC_SvpInternalFloat, "r__svp_nvg_sensitivity", &ps_r__svp_nvg_sensitivity, 0.1f, 4.0f);
	CMD4(CCC_Integer, "r__svp_nvg_objective", &ps_r__svp_nvg_objective, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_hud_full", &s_svp_compat_hud_full, 0, 2);
	CMD4(CCC_Integer, "r__svp_weapon_continuity", &ps_r__svp_weapon_continuity, 0, 1);
	CMD4(CCC_Integer, "r__svp_clipon", &ps_r__svp_clipon, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_ray_transfer", &s_svp_compat_ray_transfer, 0, 2);
	CMD4(CCC_SvpFixedInteger, "r__svp_optic_body_suppress", &ps_r__svp_optic_body_suppress, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_near_pupil", &s_svp_compat_near_pupil, 0, 1);
	CMD4(CCC_SvpFixedInteger, "r__svp_drain_clip", &s_svp_compat_drain_clip, 0, 1);
	CMD4(CCC_Integer, "r__svp_cull", &ps_r__svp_cull, 0, 1); // svp frustum cull the scope geometry (1 = on)
	CMD4(CCC_Integer, "r__svp_skip_motionblur", &ps_r__svp_skip_motionblur, 0, 1); // svp skip motion blur on the scope
	CMD4(CCC_Integer, "r__svp_skip_hud_distort", &ps_r__svp_skip_hud_distort, 0, 1); // svp skip hud-layer distortion on the scope
	CMD4(CCC_SvpInternalInteger, "r__svp_skip_dof", &ps_r__svp_skip_dof, 0, 1);
	CMD4(CCC_Integer, "r__svp_wpn_dof", &ps_r__svp_wpn_dof, 0, 1); // svp sss weapon dof while scoped
	CMD4(CCC_SvpInternalInteger, "r__svp_skip_lut", &ps_r__svp_skip_lut, 0, 1);
	CMD4(CCC_Integer, "r__svp_emissive", &ps_r__svp_emissive, 0, 1); // svp self-illum in the scope image
	CMD4(CCC_Integer, "r__svp_skip_ssr", &ps_r__svp_skip_ssr, 0, 2); // svp scope reflections level (0 expensive, 1 regular, 2 cheapest)
	CMD4(CCC_Integer, "r__svp_skip_volumetric", &ps_r__svp_skip_volumetric, 0, 1); // svp skip volumetric lights on the scope
	CMD4(CCC_Integer, "r__svp_skip_grass", &ps_r__svp_skip_grass, 0, 1); // svp skip grass/details on the scope
	CMD4(CCC_Integer, "r__svp_grass_cull", &ps_r__svp_grass_cull, 0, 1); // svp cone-cull the scope grass slot ranges
	CMD4(CCC_Integer, "r__svp_sss_sun", &ps_r__svp_sss_sun, 0, 1); // svp SSS sun contact shadows on the scope
	CMD4(CCC_Integer, "r__svp_skip_pos_copy", &ps_r__svp_skip_pos_copy, 0, 1); // svp skip the dead position copy on the scope lens composite (0 = copy)
	CMD4(CCC_Integer, "r__svp_smap_alloc", &ps_r__svp_smap_alloc, 0, 1); // svp owns a shadow atlas it never renders into (1 = allocate)
	CMD4(CCC_Integer, "r__svp_light_cull", &ps_r__svp_light_cull, 0, 1); // svp cone-cull the mirrored light blends (1 = on)
	CMD4(CCC_SvpFixedInteger, "r__svp_corner_mask", &ps_r__svp_corner_mask, 0, 1);
	CMD4(CCC_Integer, "r__pp_lean", &ps_r__pp_lean, 0, 1); // skip idle post passes (0 = stock)
	CMD4(CCC_Integer, "r__cp_scale_region", &ps_r__cp_scale_region, 0, 1); // history copies move the written box only (0 = whole surface)
	CMD4(CCC_Integer, "r__ssfx_ssr_enable", &ps_r__ssfx_ssr_enable, 0, 1); // ssfx ssr master switch (1 = on)
	CMD4(CCC_Integer, "r__ssfx_bloom_hud", &ps_r__ssfx_bloom_hud, 0, 1); // hud glass in the bloom emissive buffer (0 = fixed)
	CMD4(CCC_Integer, "r__hom_engine", &ps_r__hom_engine, 0, 2); // occlusion engine (0 legacy, 1 masked, 2 shadow compare)
	CMD4(CCC_Integer, "r__hom_moc_res", &ps_r__hom_moc_res, 0, 3); // masked buffer preset (0 256x144, 1 384x216, 2 512x288, 3 768x432)
	CMD4(CCC_Integer, "r__hom_terrain", &ps_r__hom_terrain, 0, 1); // cform ground as masked occluders, scans lazily when flipped on
	CMD4(CCC_Integer, "r__scope_debug", &scope_debug, 0, 4);
#endif
}

// Tracks paths expected during ordinary scoped play
namespace
{
	struct svp_ledger_row
	{
		const char*  name;
		const u32*   counter;  // session latch
		const int*   gate_i;   // int cvar gate, nonzero = on (null skips this test)
		const float* gate_f;   // float cvar gate, > thr = on (null skips this test)
		float        gate_thr;
	};
	static const svp_ledger_row s_svp_ledger[] = {
		{ "cull_reject",       &svp_ledger_cull_reject,       &ps_r__svp_cull,          nullptr,                 0.f },
		{ "ssa_culled",        &svp_ledger_ssa_culled,        nullptr,                  &ps_r__svp_cull_ssa,     0.f },
		{ "cull_reject_ident", &svp_ledger_cull_reject_ident, &ps_r__svp_cull,          nullptr,                 0.f },
		{ "lights_mirrored",   &svp_ledger_lights_mirrored,   &ps_r__svp_light_cull,    nullptr,                 0.f },
		{ "lights_skipped",    &svp_ledger_lights_skipped,    &ps_r__svp_light_cull,    nullptr,                 0.f },
		{ "lod_scale",         &svp_ledger_lod_scale,         nullptr,                  &ps_r__svp_lod,          0.f },
		{ "distort_guard",     &svp_ledger_distort_guard,     &ps_r__svp_distort_guard, nullptr,                 0.f },
		{ "disc_latch",        &svp_ledger_disc_latch,        nullptr,                  &ps_r__svp_adaptive_res, 0.f },
		{ "fwd_keep",          &svp_ledger_fwd_keep,          nullptr,                  nullptr,                 0.f },
		{ "grass_cull",        &svp_ledger_grass_cull,        &ps_r__svp_grass_cull,    nullptr,                 0.f },
	};
}

// prints [SVP-LEDGER] once per session, a gate on with its latch still zero is a maybe-inert path
void svp_ledger_sweep()
{
	u32 checked = 0, inert = 0;
	for (const svp_ledger_row& r : s_svp_ledger)
	{
		const bool on = (r.gate_i ? (*r.gate_i != 0) : true) && (r.gate_f ? (*r.gate_f > r.gate_thr) : true);
		if (!on)
			continue;
		++checked;
		if (*r.counter == 0)
		{
			PipMsg("[SVP-LEDGER] INERT? %s gate on counter zero", r.name);
			++inert;
		}
	}
	if (inert == 0)
		PipMsg("[SVP-LEDGER] ledger ok (%u checked)", checked);
	else
		PipMsg("[SVP-LEDGER] %u inert of %u checked", inert, checked);
}
