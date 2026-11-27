#include "stdafx.h"
#include "../xrRender/FBasicVisual.h" // pip dxRender_Visual (GetTexture/Render) for draw_scope
#include "../xrRender/SkeletonX.h" // pip lens bone latch compensation for the skinned lens draws
#include "../../xrEngine/igame_persistent.h" // pip env-driven eye pupil for the exit-pupil twilight dimming
#include "../../xrEngine/environment.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_* (kept in sync with the shader)
#include "svp_physical_optics.h" // pip physical aperture math (exit pupil, virtual eye follower)
#include "svp_optics.h"
#endif

#if defined(USE_DX11)	//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
struct SSvpObjectiveHudState
{
	u32 frame = u32(-1);
	u32 session = 0;
	u32 items = 0;
	u32 skinned = 0;
	u32 drawn = 0;
	u32 roots_missing = 0;
	u32 bones_missing = 0;
	bool active = false;
};

static SSvpObjectiveHudState s_svp_objective_hud;

static bool svp_objective_hud_role(u8 role)
{
	return role == IDSGraphManager::hud_hands
		|| role == IDSGraphManager::hud_primary_item
		|| role == IDSGraphManager::hud_offhand_item
		|| role == IDSGraphManager::hud_optic;
}

bool svp_objective_hud_current()
{
	auto& vp = Device.m_SecondViewport;
	return s_svp_objective_hud.active
		&& s_svp_objective_hud.frame == Device.dwFrame
		&& s_svp_objective_hud.session == vp.GetSVPSession()
		&& vp.svp_camera_frame == Device.dwFrame
		&& vp.svp_camera_session == s_svp_objective_hud.session;
}

void svp_objective_hud_note_draw(u8 role)
{
	if (svp_objective_hud_current() && svp_objective_hud_role(role))
		++s_svp_objective_hud.drawn;
}

static void svp_objective_hud_bypass(LPCSTR reason)
{
	s_svp_objective_hud.active = false;
	s_svp_objective_hud.frame = u32(-1);
	extern int ps_r__svp_cop_diag;
	static u32 s_bypass_diag_ms = 0;
	if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_bypass_diag_ms > 1000)
	{
		s_bypass_diag_ms = Device.dwTimeGlobal;
		PipMsg("[SVP-CONT] frame=%u path=native-objective pose=live pupil=centered reason=%s",
			Device.dwFrame, reason);
	}
}

// per-scope objective diameter in mm, resolved by the optics bus (spec cvar then authored w), 0 = none
static float svp_objective_mm()
{
	return Device.m_SecondViewport.svp_opt_obj_mm;
}

// pip electronic overlay actually on screen, NV shows only with its overlay on (markswitch 0),
// thermal shows until the overlay is dropped (markswitch < 2)
static bool svp_overlay_active(float param3x, int markswitch)
{
	return (param3x >= 0.5f) && ((param3x < 1.5f) ? (markswitch == 0) : (markswitch < 2));
}
// pip thermal-typed optic with its overlay on, near-blur skips these so they keep full DoF
static bool svp_thermal_active(float param3x, int markswitch)
{
	return (param3x >= 1.5f) && svp_overlay_active(param3x, markswitch);
}

// pip the thermal overlay state for callers outside this file
bool svp_thermal_overlay_active()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	return svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current);
}

// pip a fresh forward body ahead of the objective, published by the hud drain
bool svp_clipon_resolved()
{
	extern int ps_r__svp_clipon;
	auto& vp = Device.m_SecondViewport;
	return ps_r__svp_clipon && _valid(vp.svp_clipon_axial) && vp.svp_clipon_axial > 0.f
		&& vp.svp_hud_min_frame != u32(-1)
		&& Device.dwFrame >= vp.svp_hud_min_frame
		&& Device.dwFrame - vp.svp_hud_min_frame <= 8
		&& vp.svp_hud_min_session == vp.GetSVPSession()
		&& vp.svp_hud_min_epoch == vp.svp_optic_epoch;
}

// pip eye coupling from the typed profile, the legacy thermal read covers an untyped route
bool svp_optic_eye_coupled()
{
	// a clip-on presents through the host eyepiece so the display exemption never applies
	if (svp_clipon_resolved())
		return true;
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	if (config.typed_route)
		return config.eye_coupling;
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	return !svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current);
}

// pip physical aperture cvars, registered in svp_console.cpp, drive the exit-pupil model at r__svp_aperture 1
extern int ps_r__svp_aperture;
extern int ps_r__svp_photo_model;
extern int ps_r__svp_authored_optics;
extern int ps_r__svp_diag;
extern float ps_r__svp_eyebox;
extern float ps_r__svp_twilight;
extern float ps_s3ds_transmission;
extern float ps_s3ds_twilight_strength;
extern float ps_s3ds_eye_relief_low_mm, ps_s3ds_eye_relief_high_mm;
extern float ps_s3ds_exit_pupil_low_mm, ps_s3ds_exit_pupil_high_mm;
extern float ps_s3ds_pupil_field_low, ps_s3ds_pupil_field_high;
extern float ps_s3ds_eye_tracking_speed, ps_s3ds_eye_tracking_accel_mm_s2, ps_s3ds_eye_tracking_limit_mm;
extern float ps_s3ds_tunneling_parallax, ps_s3ds_tunneling_min, ps_s3ds_tunneling_max;
extern float ps_svp_exit_scale, ps_svp_exit_offset, ps_svp_tunnel_scale, ps_svp_tunnel_offset, ps_svp_dim_scale, ps_svp_dim_offset;
extern float g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_max_mag;
extern Fvector4 ps_s3ds_param_1;
extern Fvector4 ps_svp_exit_curve_low, ps_svp_exit_curve_high;
extern Fvector4 ps_svp_tunnel_curve_low, ps_svp_tunnel_curve_high;
extern Fvector4 ps_svp_dim_curve_low, ps_svp_dim_curve_high;

// pip physical optics helpers, re-housed from the aperture model, the math matches the source lib
static SvpPhysicalOptics::MagnificationResponse svp_make_response(const Fvector4& low, const Fvector4& high)
{
	SvpPhysicalOptics::MagnificationResponse response;
	response.value[0] = low.x;
	response.value[1] = low.y;
	response.value[2] = low.z;
	response.value[3] = low.w;
	response.value[4] = high.x;
	response.value[5] = high.y;
	response.value[6] = high.z;
	response.value[7] = high.w;
	return response;
}

// interpolate a low/high profile endpoint across the optic's zoom range
static float svp_interp_profile(float low, float high)
{
	return SvpPhysicalOptics::InterpolateMagnification(low, high, g_pip_scope_magnification,
		g_pip_scope_min_mag, g_pip_scope_max_mag);
}

static float svp_eye_relief_mm()
{
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float low = config.typed_route ? config.eye_relief_low_mm : ps_s3ds_eye_relief_low_mm;
	const float high = config.typed_route ? config.eye_relief_high_mm : ps_s3ds_eye_relief_high_mm;
	return svp_interp_profile(low, high);
}

static float svp_pupil_field_scale()
{
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float low = config.typed_route ? config.pupil_field_low : ps_s3ds_pupil_field_low;
	const float high = config.typed_route ? config.pupil_field_high : ps_s3ds_pupil_field_high;
	return svp_interp_profile(low, high);
}

// exit pupil mm, authored low/high reciprocal-mag interp then objective/mag then the ocular-ratio proxy
static float svp_calc_exit_pupil_mm(float objective_mm)
{
	if (g_pip_scope_magnification <= 0.01f)
		return 0.f;

	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	float low = config.typed_route ? config.exit_pupil_low_mm : ps_s3ds_exit_pupil_low_mm;
	float high = config.typed_route ? config.exit_pupil_high_mm : ps_s3ds_exit_pupil_high_mm;
	if (objective_mm > 0.01f)
	{
		if (low <= 0.01f && g_pip_scope_min_mag > 0.01f)
			low = objective_mm / g_pip_scope_min_mag;
		if (high <= 0.01f && g_pip_scope_max_mag > 0.01f)
			high = objective_mm / g_pip_scope_max_mag;
	}
	if (low > 0.01f || high > 0.01f)
	{
		if (low <= 0.01f)
			low = high;
		if (high <= 0.01f)
			high = low;
		return SvpPhysicalOptics::InterpolateReciprocalMagnification(low, high, g_pip_scope_magnification,
			g_pip_scope_min_mag, g_pip_scope_max_mag);
	}

	const float authored_exit = ps_s3ds_param_1.z > 0.01f ? ps_s3ds_param_1.z : 0.5f;
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : g_pip_scope_magnification;
	return authored_exit * Device.m_SecondViewport.eyepiece.radius * 2000.f *
		(minimum_mag / g_pip_scope_magnification);
}

// aperture twilight dimming, exit-pupil transmission scaled by the per-mag dim curve, outer gate assumed
static float svp_calc_twilight_dim(float pupil_mm, float environment_brightness)
{
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	const float pupil_ratio = _min(exit_pupil_mm / pupil_mm, 1.f);
	const float relative_brightness = ps_r__svp_photo_model ? pupil_ratio * pupil_ratio : pupil_ratio;
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float authored_twilight = config.typed_route
		? config.twilight_strength : ps_s3ds_twilight_strength;
	const float authored_transmission = config.typed_route
		? config.transmission : ps_s3ds_transmission;
	const float twilight_strength = _min(ps_r__svp_twilight, 1.f) * clampr(authored_twilight, 0.f, 1.f);
	float dimming = clampr(authored_transmission, 0.f, 1.f) *
		(1.f + (_max(relative_brightness, 0.6f) - 1.f) * twilight_strength);
	const float response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_dim_curve_low, ps_svp_dim_curve_high), g_pip_scope_magnification,
		ps_svp_dim_scale, ps_svp_dim_offset);
	dimming = 1.f - clampr((1.f - dimming) * response, 0.f, 1.f);

	if (ps_r__svp_diag)
	{
		static u32 s_twl_ms = 0;
		if (Device.dwTimeGlobal - s_twl_ms > 1000)
		{
			s_twl_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", exit_pupil_mm,
				pupil_mm, environment_brightness, dimming);
		}
	}
	return dimming;
}

// aperture eyebox half angle, exit-pupil radius plus eye pupil over the interpolated eye relief
static void svp_update_eyebox_limit(float pupil_mm)
{
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && exit_pupil_mm > 0.01f &&
		g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
	{
		const float exit_radius = exit_pupil_mm * 0.0005f;
		const float pupil_radius = pupil_mm * 0.0005f;
		const float eye_relief = _max(svp_eye_relief_mm() * 0.001f, 0.05f);
		Device.m_SecondViewport.svp_eyebox_rad = atanf((exit_radius + pupil_radius) / eye_relief);
		return;
	}

	Device.m_SecondViewport.svp_eyebox_rad = 0.f;
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
	{
		static u32 s_ebg_ms = 0;
		if (Device.dwTimeGlobal - s_ebg_ms > 1000)
		{
			s_ebg_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f",
				ps_r__svp_authored_optics, Device.m_SecondViewport.svp_opt_offset.w,
				Device.m_SecondViewport.svp_opt_obj_mm,
				g_pip_scope_magnification, pupil_mm);
		}
	}
}

SSvpEyeSample svp_update_eye_sample(const Fmatrix& eye_view)
{
	auto& viewport = Device.m_SecondViewport;
	static SvpPhysicalOptics::EyeTrackingState tracking;
	static SSvpEyeSample cached;
	static u32 cached_frame = u32(-1);
	static u32 cached_session = 0;

	if (scope_svp_enabled < 2 || !viewport.IsSVPActive())
		return {};

	const u32 session = viewport.GetSVPSession();
	if (cached_session != session)
	{
		tracking = {};
		cached = {};
		cached_frame = u32(-1);
		cached_session = session;
	}
	if (cached_frame == Device.dwFrame)
		return cached;

	cached = {};
	cached_frame = Device.dwFrame;
	SSvpEyeSample& sample = cached;
	const auto& eyepiece = viewport.eyepiece;
	const auto& objective = viewport.objective;
	if (eyepiece.radius <= EPS)
		return sample;

	// the exit pupil rides the ocular presentation axis, the gaze axis in view space, so a
	// folded prism objective chord must not tilt the rest zero
	Fvector lens_center_view;
	eye_view.transform_tiny(lens_center_view, eyepiece.m_W.c);

	Fvector eye_ray = lens_center_view;
	eye_ray.normalize_safe();
	const float forward = eye_ray.z;
	if (_abs(forward) <= 0.001f)
		return sample;

	const float eye_relief_mm = svp_eye_relief_mm();
	const float inverse_forward = 1.f / forward;
	sample.raw_mm.set(-eye_ray.x * inverse_forward * eye_relief_mm,
		-eye_ray.y * inverse_forward * eye_relief_mm);
	sample.eye_relief_mm = eye_relief_mm;
	const SvpPhysicalOptics::Vec2 raw = { sample.raw_mm.x, sample.raw_mm.y };
	const auto& config = viewport.RenderOpticConfig();
	const float tracking_limit = config.typed_route
		? config.tracking_limit_mm : ps_s3ds_eye_tracking_limit_mm;
	const float tracking_speed = config.typed_route
		? config.tracking_speed : ps_s3ds_eye_tracking_speed;
	const float tracking_accel = config.typed_route
		? config.tracking_accel_mm_s2 : ps_s3ds_eye_tracking_accel_mm_s2;
	const SvpPhysicalOptics::Vec2 target =
		SvpPhysicalOptics::LimitEyeOffset(raw, tracking_limit);
	SvpPhysicalOptics::UpdateEyeTracking(tracking, target,
		viewport.svp_eye_tracking_suspended.load(std::memory_order_acquire),
		viewport.svp_camera_epoch, Device.dwFrame, Device.fTimeDelta,
		tracking_speed, tracking_accel);
	sample.residual_mm.set(sample.raw_mm.x - tracking.offset.x, sample.raw_mm.y - tracking.offset.y);

	const float objective_mm = svp_objective_mm();
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(objective_mm);
	if (objective_mm > 0.01f && exit_pupil_mm > 0.01f)
		sample.entrance_scale = objective_mm / exit_pupil_mm;
	else
		sample.entrance_scale = _max(g_pip_scope_magnification, 1.f);
	sample.valid = _valid(sample.raw_mm.x) && _valid(sample.raw_mm.y)
		&& _valid(sample.residual_mm.x) && _valid(sample.residual_mm.y)
		&& _valid(sample.eye_relief_mm) && sample.eye_relief_mm > 0.01f
		&& _valid(sample.entrance_scale) && sample.entrance_scale > 0.f;
	return sample;
}

// physical aperture bind, always binds the aperture constants, x = 0 when the cvar is off
static void svp_bind_aperture(float pupil_mm)
{
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : g_pip_scope_magnification;
	const float maximum_mag = g_pip_scope_max_mag > minimum_mag ? g_pip_scope_max_mag : minimum_mag;
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	auto& viewport = Device.m_SecondViewport;
	const auto& eyepiece = viewport.eyepiece;
	// published lens axes are the view basis, the screen consumes lens space and neither a
	// rolled lens bone nor a folded prism chord may skew it
	Fvector lens_right, lens_up;
	lens_right.set(Device.vCameraRight);
	lens_up.set(Device.vCameraTop);
	lens_right.normalize_safe();
	lens_up.normalize_safe();

	const SSvpEyeSample eye = svp_update_eye_sample(Device.matrices[0].mView);
	const Fvector2 raw_eye_offset_mm = eye.raw_mm;
	// the objective camera consumes the raw offset, the aperture loss rides the tracked eye residual
	const Fvector2 eye_offset_mm = eye.residual_mm;
	const float inverse_lens_diameter = eyepiece.radius > EPS ? 0.5f / eyepiece.radius : 0.f;

	// a rigid display panel carries no eyepiece, its image ignores where the eye sits
	const bool digital_display = !svp_optic_eye_coupled();
	RCache.set_c("svp_aperture", ps_r__svp_aperture ? 1.f : 0.f, g_pip_scope_magnification, minimum_mag, maximum_mag);
	RCache.set_c("svp_eyebox", digital_display ? 0.f : eye_offset_mm.x,
		digital_display ? 0.f : eye_offset_mm.y, exit_pupil_mm * 0.5f, pupil_mm * 0.5f);
	const float exit_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_exit_curve_low, ps_svp_exit_curve_high), g_pip_scope_magnification,
		ps_svp_exit_scale, ps_svp_exit_offset);
	const float tunnel_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_tunnel_curve_low, ps_svp_tunnel_curve_high), g_pip_scope_magnification,
		ps_svp_tunnel_scale, 0.f);
	const auto& config = viewport.RenderOpticConfig();
	const float tunnel_parallax = digital_display ? 0.f
		: (config.typed_route ? config.tunneling_parallax : ps_s3ds_tunneling_parallax);
	const float tunnel_min = digital_display ? 0.f
		: (config.typed_route ? config.tunneling_min : ps_s3ds_tunneling_min);
	const float tunnel_max = digital_display ? 0.f
		: (config.typed_route ? config.tunneling_max : ps_s3ds_tunneling_max);
	RCache.set_c("svp_optic_profile", tunnel_parallax, tunnel_min, tunnel_max, tunnel_response);
	RCache.set_c("svp_pupil_model", svp_pupil_field_scale(), exit_response,
		digital_display ? 0.f : ps_svp_tunnel_offset, 0.f);
	RCache.set_c("svp_lens_center", eyepiece.m_W.c.x, eyepiece.m_W.c.y, eyepiece.m_W.c.z, inverse_lens_diameter);
	RCache.set_c("svp_lens_right", lens_right.x, lens_right.y, lens_right.z, 0.f);
	RCache.set_c("svp_lens_up", lens_up.x, lens_up.y, lens_up.z, 0.f);

	if (ps_r__svp_diag)
	{
		static u32 s_apert_ms = 0;
		if (Device.dwTimeGlobal - s_apert_ms > 1000)
		{
			s_apert_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-APERT] raw %.2f,%.2fmm applied %.2f,%.2fmm exit_r %.2fmm pupil_r %.2fmm",
				raw_eye_offset_mm.x, raw_eye_offset_mm.y, eye_offset_mm.x, eye_offset_mm.y,
				exit_pupil_mm * 0.5f, pupil_mm * 0.5f);
		}
	}
}

void CRenderTarget::EnsureScopeShaders()
{
	if (m_scope_shaders_ready)
		return;
	s_scope_color_write.create("scope_color_write");
	s_scope_depth_write.create("scope_depth_write");
	s_scope_debug.create("scope_debug");
	s_svp_nearblur.create("svp_nearblur");
	s_svp_distort_stamp.create("svp_distort_stamp");
	s_svp_taa_stamp.create("svp_taa_stamp");
	m_scope_shaders_ready = true;
}

// pip r__scope_debug overlay, a top-left grid of the main + SVP views, their ssfx buffers (prev-frame,
// prev-pos, motion vectors) and the shadow map, main viewport only, binds each $main/$svp RT by name
void CRenderTarget::phase_scope_debug()
{
	if (!scope_debug || Device.m_SecondViewport.IsSVPFrame())
		return;

	EnsureScopeShaders();
	if (!s_scope_debug)
		return;

	// snapshot the finished main view so the overlay can sample it without reading the RT it draws into
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);

	auto M = RImplementation.TargetMain;
	auto S = RImplementation.TargetSVP;
	auto bind = [M](LPCSTR name, ref_rt& rt)
	{
		// fall back to a valid RT when the source is absent (no SSS = no ssfx buffers) so the debug
		// technique bind always resolves to a real texture instead of an unregistered name
		ref_rt& src = rt ? rt : M->rt_Generic_0;
		if (!src)
			return;
		ref_texture t;
		t.create(name);
		// raw pSurface, surface_get would AddRef a reference nobody releases (per-frame leak)
		t->surface_set(src->pSurface);
	};
	bind("$user$viewport2$main", M->rt_secondVP);
	bind("$user$ssfx_prev_p$main", M->rt_Position); // no MT prev-pos buffer, show the gbuffer position
	bind("$user$ssfx_motion_vectors$main", M->rt_ssfx_motion_vectors);
	bind("$user$ssfx_prev_frame$main", M->rt_ssfx_prev_frame);
	bind("$user$smap_depth", M->rt_smap_depth);
	if (S)
	{
		bind("$user$viewport2$svp", S->rt_secondVP);
		bind("$user$ssfx_prev_p$svp", S->rt_Position);
		bind("$user$ssfx_motion_vectors$svp", S->rt_ssfx_motion_vectors);
		bind("$user$ssfx_prev_frame$svp", S->rt_ssfx_prev_frame);
	}
	// the bind cache keys on CTexture identity so the remaps above are invisible to it
	RCache.Invalidate();

	// draw onto the CURRENT target phase_combine left bound (the final LDR image), not a fresh RT or the
	// following HUD/UI passes would render offscreen
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);
	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	// fullscreen triangle, the shader discards everything outside the top-left quarter grid
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(3, g_combine->vb_stride, Offset);
	pv->set(0, h * 2, d_Z, d_W, C, 0.f, 2.f); pv++;
	pv->set(0, 0, d_Z, d_W, C, 0.f, 0.f); pv++;
	pv->set(w * 2, 0, d_Z, d_W, C, 2.f, 0.f); pv++;
	RCache.Vertex.Unlock(3, g_combine->vb_stride);

	RCache.set_Geometry(g_combine);
	RCache.set_Element(s_scope_debug->E[1]);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 3, 0, 1);
}

// pip zero the geometry stencil in the four dead corners outside the inscribed eyepiece disc so
// every downstream >=1 test (lights, combine, wallmarks) skips them, no shader or accum edits
void CRenderTarget::stamp_svp_corner_mask()
{
	// square rt only, a flat panel fills the whole rect and has no dead corner
	extern int ps_r__svp_flat_window;
	extern Fvector4 ps_s3ds_param_3;
	if (Width != Height || (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8))
		return;

	// corner-triangle leg, a quarter of the side stays clear of the inscribed disc edge
	const float side = float(Width);
	const float leg = side * 0.25f;
	const float z = EPS_S;
	const u32 C = color_rgba(255, 255, 255, 255);

	// each corner is one triangle packed as a quad, the duplicated vertex collapses the second tri
	u32 Offset;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(16, g_combine->vb_stride, Offset);
	auto corner = [&](float cx, float cy, float ax, float ay, float bx, float by)
	{
		pv->set(cx, cy, z, 1.f, C, 0, 0); pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0); pv++;
		pv->set(bx, by, z, 1.f, C, 0, 0); pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0); pv++;
	};
	corner(0.f,  0.f,  leg,        0.f,  0.f,   leg);
	corner(side, 0.f,  side - leg, 0.f,  side,  leg);
	corner(0.f,  side, leg,        side, 0.f,   side - leg);
	corner(side, side, side - leg, side, side,  side - leg);
	RCache.Vertex.Unlock(16, g_combine->vb_stride);

	u_setrt(NULL, NULL, NULL, baseZB); // only the svp depth-stencil, no color target
	RCache.set_Element(s_occq->E[1]);
	RCache.set_Geometry(g_combine);
	// replace the whole stencil byte with 0 in the corners, clearing the bit0 geometry marker
	StateManager.SetStencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0xff,
		D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	StateManager.SetColorWriteEnable(0);
	StateManager.SetDepthFunc(D3DCMP_ALWAYS);
	StateManager.SetDepthEnable(FALSE);
	StateManager.SetCullMode(D3DCULL_NONE);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 16, 0, 8);
	StateManager.SetColorWriteEnable(D3D_COLOR_WRITE_ENABLE_ALL);
}

// pip stash the SVP combined color in rt_secondVP so the scope lens can sample it
void CRenderTarget::phase_svp_capture()
{
	PIX_EVENT(PHASE_SCOPE_SVP_CAPTURE);
	if (ps_r__svp_dlss != 0)
	{
		// pip DLSS seam: assemble SvpDlssInputs from the SVP target + cached consts, then EvalSVP_DLSS (stub for now)
		SvpDlssInputs in;
		auto& vp = Device.m_SecondViewport;
		in.viewport_id = 1; // stable SVP handle for DLSS history (main = 0), NOT the per-frame dwViewport
		in.color_srv = rt_Generic_0->pTexture->get_SRView();
		in.render_extent = { (u32)Width, (u32)Height };
		in.depth_srv = rt_baseZB ? rt_baseZB->pTexture->get_SRView() : nullptr;
		in.mvec_srv = rt_ssfx_motion_vectors->pTexture->get_SRView();
		in.out_rtv = rt_secondVP->pRT;
		in.out_uav = rt_secondVP->pUAView;
		in.display_extent = { (u32)Width, (u32)Height }; // stub rt_secondVP follows the render extent, Ascii makes it display-res
		in.view = Device.matrices[1].mView;
		in.proj = Device.matrices[1].mProject;
		in.view_proj.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		in.prev_view = Device.matrices_previous[1].mView;
		in.prev_proj = Device.matrices_previous[1].mProject;
		in.prev_view_proj.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
		in.jitter_px = vp.svp_jitter_px;
		in.near_plane = vp.svp_near; in.far_plane = vp.svp_far; in.fov = vp.svp_fov; in.aspect = vp.svp_aspect;
		in.cam_pos = vp.svp_cam_pos; in.up = vp.svp_up; in.right = vp.svp_right; in.fwd = vp.svp_fwd;
		in.reset = vp.dlss_reset_next.exchange(false);
		EvalSVP_DLSS(in);
		return;
	}
	// pip near-field defocus, thermal falls through to the plain copy
	if (svp_nearblur_pass()) return;
	// rt_secondVP alpha is garbage (nothing writes it) and must stay UNREAD, the scope shaders sample
	// .rgb only and the lens composite blends srcalpha with its OWN forced o.a, never the source alpha
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

// pip DLSS-SR eval, a passthrough stub. TODO Ascii replaces the body with the Streamline eval,
// the SvpDlssInputs signature and the seam call are frozen, no sl::/NGX symbols here
void CRenderTarget::EvalSVP_DLSS(const SvpDlssInputs& in)
{
	// a real eval restores via Target->SetActive(true) then unbinds the CS stage, the copy needs none
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

void CRenderTarget::svp_objective_hud_prepare(bool svp_follows)
{
	s_svp_objective_hud = SSvpObjectiveHudState();

	extern int ps_r__svp_weapon_continuity;
	extern int scope_svp_enabled;
	auto& vp = Device.m_SecondViewport;
	if (!svp_follows || scope_svp_enabled < 2 || !Device.true_pip_on || !vp.IsSVPActive())
		return;
	if (!ps_r__svp_weapon_continuity)
	{
		svp_objective_hud_bypass("continuity-off");
		return;
	}
	if (this != RImplementation.TargetMain)
	{
		svp_objective_hud_bypass("target-owner");
		return;
	}
	if (vp.svp_camera_frame != Device.dwFrame)
	{
		svp_objective_hud_bypass("camera-frame");
		return;
	}
	const u32 session = vp.GetSVPSession();
	if (vp.svp_camera_session != session)
	{
		svp_objective_hud_bypass("camera-session");
		return;
	}
	if (vp.eyepiece.radius <= EPS)
	{
		svp_objective_hud_bypass("eyepiece");
		return;
	}
	if (vp.svp_front_use_m <= EPS)
	{
		svp_objective_hud_bypass("objective-camera");
		return;
	}

	u32 queued = 0;
	auto inspect = [&](auto& graph)
	{
		queued += (u32)graph.size();
		for (auto& item : graph)
		{
			if (!svp_objective_hud_role(item.hud_role))
				continue;
			if (!item.pVisual || !item.pMatrix || !item.pSE)
			{
				++s_svp_objective_hud.roots_missing;
				continue;
			}
			if (RImplementation.GMBase.svp_pose_of(item.pMatrix) == item.pMatrix)
				++s_svp_objective_hud.roots_missing;
			++s_svp_objective_hud.items;
			CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(item.pVisual);
			if (skeleton)
			{
				++s_svp_objective_hud.skinned;
				if (!skeleton->SVP_BoneSnapshotReady())
					++s_svp_objective_hud.bones_missing;
			}
		}
	};
	auto& graph = RImplementation.GMBase.RGraph;
	inspect(graph.mapHUD);
	inspect(graph.mapHUDSorted.Sorted);
	inspect(graph.mapHUDSorted.Wmark);
	inspect(graph.mapHUDSorted.Emissive);
	inspect(graph.mapHUDSorted.Distort);
	if (!queued)
	{
		svp_objective_hud_bypass("hud-empty");
		return;
	}
	if (!s_svp_objective_hud.items)
	{
		svp_objective_hud_bypass("weapon-empty");
		return;
	}

	s_svp_objective_hud.frame = Device.dwFrame;
	s_svp_objective_hud.session = session;
	s_svp_objective_hud.active = true;
}

void CRenderTarget::svp_objective_hud_report()
{
	if (!svp_objective_hud_current())
		return;
	extern int ps_r__svp_cop_diag;
	static u32 s_diag_ms = 0;
	if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_diag_ms > 1000)
	{
		s_diag_ms = Device.dwTimeGlobal;
		const bool exact = !s_svp_objective_hud.roots_missing
			&& !s_svp_objective_hud.bones_missing;
		PipMsg("[SVP-CONT] frame=%u path=native-objective pose=%s capture=entrance-pupil mapping=geometry depth=shared items=%u skinned=%u drawn=%u missing=%u/%u near=%.2fcm",
			Device.dwFrame, exact ? "same-frame" : "partial",
			s_svp_objective_hud.items, s_svp_objective_hud.skinned,
			s_svp_objective_hud.drawn, s_svp_objective_hud.roots_missing,
			s_svp_objective_hud.bones_missing,
			Device.m_SecondViewport.svp_near * 100.f);
	}
}

// pip render the captured lens meshes with shader se, the bind callback sets the scope_phase
// (IMAGE/RETICLE/SHADOW/LENS) that scope_color_write composites into the lens
void CRenderTarget::draw_scope(ref_shader se, std::function<void()> bind)
{
	auto elem = se ? se->E[0] : nullptr;
	if (!elem)
		return;

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);
	RImplementation.rmNear();

	// per-lens dump, which texture each lens draw samples as s_reticle + the optic type/geometry
	extern int ps_r__svp_diag;
	static u32 s_lens_ms = 0;
	const bool lens_diag = ps_r__svp_diag && Device.dwTimeGlobal - s_lens_ms > 1000;
	if (lens_diag)
		s_lens_ms = Device.dwTimeGlobal;

	u32 lens_idx = 0;
	for (auto& N : RImplementation.GMBase.RGraph.mapScopeHUDSorted)
	{
		dxRender_Visual* V = N.pVisual;
		if (!V || !N.pMatrix)
			continue;

		CTexture* tex = V->GetTexture();
		// per-lens marker, names the reticle source texture so a capture shows which texture each
		// scope_color_write draw samples as s_reticle (gated on r__gpu_markers)
		PIX_EVENT_F("scope_lens tex=%s", tex ? tex->cName.c_str() : "none");
		if (lens_diag)
		{
			extern Fvector4 ps_s3ds_param_3;
			extern float g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio;
			PipMsg("[SVP-LENS] %u tex %s rtype %d itype %d eyep_r %.4f mag %.2f min %.2f ratio %.2f",
				lens_idx, tex ? tex->cName.c_str() : "none", (int)ps_s3ds_param_3.y, (int)ps_s3ds_param_3.x,
				Device.m_SecondViewport.eyepiece.radius, g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio);
		}
		++lens_idx;
		if (tex)
		{
			// surface_get AddRefs (and services staging), release the ref once the alias holds its own
			ID3DBaseTexture* s = tex->surface_get();
			t_reticle->surface_set(s);
			_RELEASE(s);
		}

		RCache.set_Element(elem);
		// reuse the main HUD root for every late lens phase
		Fmatrix lensW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(V);
		const bool frozen_lens = svp_objective_hud_current() && sk && sk->SVP_BoneSnapshotReady();
		// the quad skins from the live bone palette, folding latched bone x live inverse into the
		// world cancels any bone step since the housing draw (the lens glass rides one bone)
		if (!frozen_lens)
		{
			Fmatrix bL, bNow;
			if (sk && RImplementation.GMBase.svp_lens_bone_of(V, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				lensW.mulB_43(bL);
				lensW.mulB_43(inv);
			}
		}
		RCache.set_xform_world(lensW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c("scope_svp", (int)Device.m_SecondViewport.IsSVPActive());
		RCache.set_c("scope_debug", (int)scope_debug);
		Fvector pt = {0, 0, 0};
		Device.m_SecondViewport.eyepiece.m_W.transform(pt);
		RCache.set_c("scope_w_eyepiece", pt.x, pt.y, pt.z, 1.0f);
		const Fvector& w_ffp = Device.m_SecondViewport.w_ffp;
		const Fvector& w_sfp = Device.m_SecondViewport.w_sfp;
		RCache.set_c("scope_w_ffp", w_ffp.x, w_ffp.y, w_ffp.z, 1.0f);
		RCache.set_c("scope_w_sfp", w_sfp.x, w_sfp.y, w_sfp.z, 1.0f);
		// pip reticle collimator geometry 2*ocular_radius/eye_distance, the shader rebuilds the
		// authored reticle magnification as a centered field under true PiP
		{
			Fvector ed; ed.sub(Device.m_SecondViewport.eyepiece.m_W.c, Device.vCameraPosition);
			const float dist = _max(ed.magnitude(), 0.02f);
			float kg = 2.f * Device.m_SecondViewport.eyepiece.radius / dist;
			clamp(kg, 0.02f, 3.f);
			// y = true-scale parallax, the real reticle shift is ~0.15 mrad at full eye deflection
			extern float ps_r__svp_parallax;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_ratio;
			float par = 0.f;
			if (ps_r__svp_parallax > 0.f && g_pip_scope_magnification > 0.01f)
			{
				const float eff_mag = _max(g_pip_scope_ratio * g_pip_scope_magnification, 1.f);
				const float hfov = deg2rad(_max(Device.fFOV, 1.f));
				par = ps_r__svp_parallax * 0.00075f * kg * eff_mag / hfov;
			}
			// z is the sine exact rim slope 2R/sqrt(RR+LL) on the axial depth, the stock
			// per pixel tangent saturates at the rim where kg in x stays the linear tan
			extern int ps_r__svp_reticle_fit;
			float zslope = 0.f;
			if (ps_r__svp_reticle_fit)
			{
				const float R = Device.m_SecondViewport.eyepiece.radius;
				const float L = _max(ed.dotproduct(Device.vCameraDirection), 0.02f);
				zslope = 2.f * R / sqrtf(R * R + L * L);
				clamp(zslope, 0.02f, 3.f);
			}
			// runtime proof the slope publish executes with these exact values
			{
				extern int ps_r__svp_diag;
				static u32 s_retz_ms = 0;
				if (ps_r__svp_diag && Device.dwTimeGlobal - s_retz_ms > 1000)
				{
					s_retz_ms = Device.dwTimeGlobal;
					PipMsg("[SVP-RETZ] z=%.4f kg=%.4f R=%.4fcm L=%.4fcm fit=%d",
						zslope, kg, Device.m_SecondViewport.eyepiece.radius * 100.f,
						_max(ed.dotproduct(Device.vCameraDirection), 0.02f) * 100.f,
						ps_r__svp_reticle_fit);
				}
			}
			RCache.set_c("svp_optics", kg, par, zslope, _max(g_pip_scope_ratio, 1.f));
		}
		// pip scope-local exposure, x = 0 off else 2^bias
		{
			extern int ps_r__svp_local_exposure;
			extern float ps_r__svp_exposure_bias;
			// y = exit-pupil twilight dimming, exit pupil (ocular*ratio shrunk by zoom) vs the
			// env-adapted eye pupil squared, electronic sights exempt
			extern float ps_r__svp_twilight;
			extern Fvector4 ps_s3ds_param_1;
			extern Fvector4 ps_s3ds_param_3;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_min_mag;
			// nvg on = the tube gain owns the scope brightness, our exposure lifts stand down
			extern Fvector4 ps_dev_param_8;
			const bool nvg_on = ps_dev_param_8.x >= 1.f;
			// env-adapted eye pupil (mm), shared by the twilight dimming and the eyebox
			float envb = 0.f, pupil_mm = 0.f;
			extern int ps_r__svp_photo_model;
			if (g_pGamePersistent)
			{
				CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
				envb = 0.299f * E.sun_color.x + 0.587f * E.sun_color.y + 0.114f * E.sun_color.z
					+ 0.5f * (0.299f * E.hemi_color.x + 0.587f * E.hemi_color.y + 0.114f * E.hemi_color.z);
				if (ps_r__svp_photo_model)
				{
					// Moon-Spencer pupil response, luminance anchored so envb 1 reads as an
					// overcast day (~2500 cd/m2) and envb 0.01 as moonlight (~0.25)
					const float L = 2500.f * envb * envb;
					pupil_mm = 4.9f - 3.f * tanhf(0.4f * log10f(_max(L, 1e-4f)));
					clamp(pupil_mm, 2.f, 8.f);
				}
				else
					pupil_mm = 6.f - 3.5f * _min(envb / 0.25f, 1.f);
			}
			float dim = 0.f;
			extern int ps_markswitch_current;
			// twilight dims passive optics, NV/thermal are exempt only while their overlay is actually
			// active (markswitch 0 for NV, < 2 for thermal), else the scope shows a plain image
			const bool overlay_active = svp_overlay_active(ps_s3ds_param_3.x, ps_markswitch_current);
			if (ps_r__svp_twilight > 0.f && !overlay_active && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
			{
				// aperture uses the exit-pupil transmission + dim curve, else our current twilight
				if (ps_r__svp_aperture)
					dim = svp_calc_twilight_dim(pupil_mm, envb);
				else
				{
				const float mn = (g_pip_scope_min_mag > 0.01f) ? g_pip_scope_min_mag : g_pip_scope_magnification;
				// exit pupil = objective diameter / magnification, real per-scope objective when
				// known, else the ocular-ratio proxy
				const float omm = svp_objective_mm();
				float ep_mm;
				if (omm > 0.01f)
					ep_mm = omm / g_pip_scope_magnification;
				else
				{
					const float xp = (ps_s3ds_param_1.z > 0.01f) ? ps_s3ds_param_1.z : 0.5f;
					ep_mm = xp * Device.m_SecondViewport.eyepiece.radius * 2000.f * (mn / g_pip_scope_magnification);
				}
				// relative brightness is the pupil ratio squared, the legacy model used it linearly
				const float dr = _min(ep_mm / pupil_mm, 1.f);
				const float d = ps_r__svp_photo_model ? dr * dr : dr;
				dim = 1.f + (_max(d, 0.6f) - 1.f) * _min(ps_r__svp_twilight, 1.f);
				extern int ps_r__svp_diag;
				if (ps_r__svp_diag)
				{
					static u32 s_twl_ms = 0;
					if (Device.dwTimeGlobal - s_twl_ms > 1000)
					{
						s_twl_ms = Device.dwTimeGlobal;
						PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", ep_mm, pupil_mm, envb, dim);
					}
				}
				}
			}
			// pip eyebox half angle from the real exit pupil, the sight anchor bound reads it
			{
				// aperture uses the exit-pupil radius + interpolated eye relief, else our current eyebox
				if (ps_r__svp_aperture)
					svp_update_eyebox_limit(pupil_mm);
				else
				{
				extern float ps_r__svp_eyebox;
				extern int ps_r__svp_authored_optics;
				extern float g_pip_scope_magnification;
				extern int ps_r__svp_diag;
				const float eb_omm = svp_objective_mm();
				if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && eb_omm > 0.01f && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
				{
					const float ep_r = eb_omm * 0.0005f / g_pip_scope_magnification;
					const float p_r = pupil_mm * 0.0005f;
					// full-blackout half angle for the sight anchor bound, arm = authored eye relief
					const float eb_arm = _max(ps_s3ds_param_1.y * 0.01f, 0.05f);
					Device.m_SecondViewport.svp_eyebox_rad = atanf((ep_r + p_r) / eb_arm);
				}
				else
				{
					// no authored bound on this optic, drop the previous scope's so the anchor
					// falls back to its own default instead of a stale narrow eyebox
					Device.m_SecondViewport.svp_eyebox_rad = 0.f;
					if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
					{
						static u32 s_ebg_ms = 0;
						if (Device.dwTimeGlobal - s_ebg_ms > 1000)
						{
							s_ebg_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f",
								ps_r__svp_authored_optics, Device.m_SecondViewport.svp_opt_offset.w,
								Device.m_SecondViewport.svp_opt_obj_mm,
								g_pip_scope_magnification, pupil_mm);
						}
					}
				}
				}
			}
			// crescent drive, exposure zw carries the latched swing side, the tangent offset is
			// scaled by pupil over eye relief so the bite depth reads the same on every scope
			float st = 0.f;
			float sw_k = 0.f;
			const float shadow_g = Device.m_SecondViewport.svp_shadow_gain;
			{
				extern float g_pip_scope_magnification;
				extern Fvector4 ps_s3ds_param_1;
				st = (g_pip_scope_magnification > 0.01f) ? (g_pip_scope_magnification - 1.f) / 7.f : 0.f;
				clamp(st, 0.f, 1.f);
				Device.m_SecondViewport.svp_mag = _max(g_pip_scope_magnification, 0.f);
				const float snug = 1.3f + (0.55f - 1.3f) * st;
				float gp = shadow_g * 4.f;
				clamp(gp, 0.f, 1.f);
				float z_eff = ps_s3ds_param_1.z * (3.0f + (snug - 3.0f) * gp);
				if (z_eff < 0.08f) z_eff = 0.08f;
				sw_k = 0.4f * z_eff / _max(ps_s3ds_param_1.y, 0.05f);
			}
			RCache.set_c("svp_exposure", (ps_r__svp_local_exposure && !nvg_on) ? powf(2.f, ps_r__svp_exposure_bias) : 0.f,
				nvg_on ? 0.f : dim,
				Device.m_SecondViewport.svp_swing_x * shadow_g * sw_k,
				Device.m_SecondViewport.svp_swing_y * shadow_g * sw_k);
			// pip physical aperture, exit-pupil transmission + virtual-eye eyebox, x = 0 when disabled
			svp_bind_aperture(pupil_mm);
			// pip glass2: x = lens coating strength, y = heat mirage (sun elevation + magnification)
			{
				extern float ps_r__svp_coating;
				extern float ps_r__svp_mirage;
				extern float g_pip_scope_magnification;
				float mirage = 0.f;
				if (ps_r__svp_mirage > 0.f && g_pip_scope_magnification > 0.01f && g_pGamePersistent)
				{
					CEnvDescriptor& Em = *g_pGamePersistent->Environment().CurrentEnv;
					const float heat = _max(-Em.sun_dir.y, 0.f);       // high overhead sun heats the ground
					const float clear = 1.f - _min(Em.rain_density, 1.f);
					const float magf = _min(_max((g_pip_scope_magnification - 2.f) / 6.f, 0.f), 1.f);
					mirage = ps_r__svp_mirage * heat * clear * magf;
					extern int ps_r__svp_diag;
					if (ps_r__svp_diag)
					{
						static u32 s_mir_ms = 0;
						if (Device.dwTimeGlobal - s_mir_ms > 1000)
						{
							s_mir_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-MIRAGE] heat %.2f clear %.2f magf %.2f -> %.3f", heat, clear, magf, mirage);
						}
					}
				}
				// w = flat-panel V-crop: (svp W/H) / panel aspect. 1 when the SVP already matches the
				// panel (non-square path, no crop needed); 1/aspect when the SVP is square (fallback)
				float vcrop = 1.f;
				if (RImplementation.TargetSVP && RImplementation.TargetSVP->Height > 0
					&& Device.m_SecondViewport.svp_panel_aspect > 0.01f)
					vcrop = ((float)RImplementation.TargetSVP->Width / (float)RImplementation.TargetSVP->Height)
						/ Device.m_SecondViewport.svp_panel_aspect;
				RCache.set_c("svp_glass2", ps_r__svp_coating, mirage, 0.f, vcrop);
				Device.m_SecondViewport.svp_panel_vcrop = vcrop; // pip binocular bracket mapping reads it
				// pip glass3: x = sharpen amount, y = field-stop onset, z = sharpen radial falloff, w = sharpen inner crisp radius
				extern float ps_r__svp_sharpen, ps_r__svp_sharpen_falloff, ps_r__svp_sharpen_inner;
				// pip field-stop onset, a stop at the field edge blurred by the viewing aperture
				// which is the exit pupil capped by the eye pupil, 1 = off
				float fs_onset = 1.f;
				{
					extern int ps_r__svp_field_stop;
					extern float g_pip_scope_magnification;
					extern Fvector4 ps_s3ds_param_1;
					const float fs_omm = svp_objective_mm();
					const float fs_fov = Device.m_SecondViewport.svp_fov;
					if (ps_r__svp_field_stop && fs_omm > 0.01f && g_pip_scope_magnification > 0.01f && fs_fov > 0.01f && pupil_mm > EPS)
					{
						const float ep_r = fs_omm * 0.0005f / g_pip_scope_magnification;
						const float ap_r = _min(ep_r, pupil_mm * 0.0005f);
						const float er = _max(ps_s3ds_param_1.y * 0.01f, 0.05f);
						const float app_half = g_pip_scope_magnification * fs_fov * 0.5f;
						const float penumbra = _min(atanf(ap_r / er) / app_half, 1.f);
						fs_onset = 1.f - 0.5f * penumbra;
						extern int ps_r__svp_diag;
						if (ps_r__svp_diag)
						{
							static u32 s_fs_ms = 0;
							if (Device.dwTimeGlobal - s_fs_ms > 1000)
							{
								s_fs_ms = Device.dwTimeGlobal;
								PipMsg("[SVP-FSTOP] ep %.1fmm pupil %.1fmm onset %.3f", ep_r * 2000.f, pupil_mm, fs_onset);
							}
						}
					}
				}
				RCache.set_c("svp_glass3", ps_r__svp_sharpen, fs_onset, ps_r__svp_sharpen_falloff, ps_r__svp_sharpen_inner);
				// pip glass4: x = nvg bleach roll-off, y = nvg auto-gain, w = shadow swing envelope
				extern float ps_r__svp_nvg_bleach, ps_r__svp_nvg_sensitivity;
				RCache.set_c("svp_glass4", ps_r__svp_nvg_bleach, ps_r__svp_nvg_sensitivity, 0.f,
					shadow_g);
			}
		}

		bind();
		const bool frozen_was = g_svp_hud_frozen_pass;
		const bool history_was = g_svp_hud_history_write;
		if (frozen_lens)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = false;
		}
		V->Render(0);
		g_svp_hud_frozen_pass = frozen_was;
		g_svp_hud_history_write = history_was;
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

// Draw captured reflex materials with their own shaders
u32 CRenderTarget::draw_reflex(bool svp)
{
	PIX_EVENT_F("RENDER_REFLEX_SIGHTS x%u", (u32)RImplementation.GMBase.RGraph.mapReflexHUDSorted.size());

	Fmatrix FTold = Device.mFullTransform;
	if (svp)
	{
		// Match the objective world and weapon projection
		Device.mFullTransform.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		RCache.set_xform_view(Device.matrices[1].mView);
		RCache.set_xform_project(Device.matrices[1].mProject);
	}
	else
	{
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);
	}
	RImplementation.rmNear();

	extern int ps_r__svp_diag;
	static u32 s_node_diag_ms = 0;
	const bool node_diag = svp && ps_r__svp_diag
		&& Device.dwTimeGlobal - s_node_diag_ms > 1000;
	if (node_diag)
		s_node_diag_ms = Device.dwTimeGlobal;

	auto& nodes = RImplementation.GMBase.RGraph.mapReflexHUDSorted;
	u32 selected_index = u32(-1);
	Fmatrix selected_world = {};
	bool selected_frozen = false;
	bool selected_straddle = false;
	Fvector selected_shift = {};
	float selected_score = flt_max;
	float hybrid_front = -1.f;
	if (svp)
	{
		auto& vp = Device.m_SecondViewport;
		Fvector objective_axis = vp.objective.m_W.k;
		if (_valid(objective_axis) && objective_axis.square_magnitude() > EPS)
			objective_axis.normalize();
		else
			objective_axis.set(Device.matrices[1].mView._13,
				Device.matrices[1].mView._23, Device.matrices[1].mView._33);

		for (u32 index = 0; index < nodes.size(); ++index)
		{
			auto& N = nodes[index];
			if (!N.pVisual || !N.pSE || !N.pMatrix)
				continue;

			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			BOOL snapshot_visible = TRUE;
			const bool bone_visible = !sk
				|| (sk->SVP_BoneSnapshotVisible(snapshot_visible)
					? !!snapshot_visible : sk->SVP_LensBoneVisible());
			const void* owner = sk ? sk->SVP_SkeletonOwner() : nullptr;
			const bool root_match = !vp.svp_lens_root || N.pMatrix == vp.svp_lens_root;
			const bool owner_match = vp.svp_lens_owner && owner
				&& owner == vp.svp_lens_owner;
			const bool related = root_match || owner_match;

			Fmatrix refW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
			Fmatrix boundsW = refW;
			Fmatrix latched_bone;
			const bool has_latched_bone = sk
				&& RImplementation.GMBase.svp_lens_bone_of(N.pVisual, latched_bone);
			if (has_latched_bone)
				boundsW.mulB_43(latched_bone);
			const bool frozen_reflex = svp_objective_hud_current()
				&& sk && sk->SVP_BoneSnapshotReady();
			if (!frozen_reflex)
			{
				Fmatrix live_bone;
				if (has_latched_bone && sk->SVP_LensBoneXform(live_bone))
				{
					Fmatrix inv;
					inv.invert(live_bone);
					refW.mulB_43(latched_bone);
					refW.mulB_43(inv);
				}
			}

			const auto& vis = N.pVisual->getVisData();
			Fvector world_center;
			boundsW.transform_tiny(world_center, vis.sphere.P);
			Fvector view_center;
			Device.matrices[1].mView.transform_tiny(view_center, world_center);
			const float world_scale = _max(boundsW.i.magnitude(),
				_max(boundsW.j.magnitude(), boundsW.k.magnitude()));
			const float world_radius = vis.sphere.R * world_scale;
			// capture content ahead of the objective, its near extent bounds the near derive
			if (bone_visible && related && _valid(view_center) && _valid(world_radius)
				&& world_radius > EPS)
			{
				const float front = _max(view_center.z - world_radius, 0.f);
				hybrid_front = (hybrid_front < 0.f) ? front : _min(hybrid_front, front);
			}
			const float near_plane = _max(vp.svp_near, EPS);
			const float far_plane = _max(vp.svp_far, near_plane + EPS);

			Fvector axis_delta;
			axis_delta.sub(world_center, vp.objective.m_W.c);
			const float axis_depth = axis_delta.dotproduct(objective_axis);
			Fvector radial;
			radial.mad(axis_delta, objective_axis, -axis_depth);
			const float axis_offset = radial.magnitude();

			bool visible = bone_visible && related
				&& _valid(view_center) && _valid(world_radius) && world_radius > EPS
				&& _valid(axis_depth) && _valid(axis_offset)
				&& axis_depth + world_radius > -EPS
				&& view_center.z + world_radius > near_plane
				&& view_center.z - world_radius < far_plane;
			float ndc_x = 0.f;
			float ndc_y = 0.f;
			// a sphere straddling the camera plane projects to infinity, the depth test above
			// already admits it so the screen bounds refine only a fully forward candidate
			const bool straddle = view_center.z - world_radius <= near_plane;
			if (visible && !straddle)
			{
				const Fmatrix& P = Device.matrices[1].mProject;
				const float clip_x = view_center.x * P._11 + view_center.y * P._21
					+ view_center.z * P._31 + P._41;
				const float clip_y = view_center.x * P._12 + view_center.y * P._22
					+ view_center.z * P._32 + P._42;
				const float clip_w = view_center.x * P._14 + view_center.y * P._24
					+ view_center.z * P._34 + P._44;
				const float inv_w = _abs(clip_w) > EPS_S ? 1.f / clip_w : 0.f;
				ndc_x = clip_x * inv_w;
				ndc_y = clip_y * inv_w;
				const float depth = _max(view_center.z - world_radius, near_plane);
				const float radius_x = _abs(P._11) * world_radius / depth;
				const float radius_y = _abs(P._22) * world_radius / depth;
				visible = clip_w > EPS
					&& ndc_x + radius_x > -1.f && ndc_x - radius_x < 1.f
					&& ndc_y + radius_y > -1.f && ndc_y - radius_y < 1.f;
			}

			const float score = axis_offset
				/ _max(_max(axis_depth, world_radius), EPS);
			if (node_diag)
			{
				auto tx = N.pVisual->GetTexture();
				PipMsg("[SVP-HYBRID] candidate=%u tex=%s view=(%.4f %.4f %.4f) ndc=(%.3f %.3f) r=%.2fcm axis=(%.2f %.2f) score=%.4f visible=%d bone=%d related=%d role=%u frozen=%d owner=%p",
					index, tx ? tx->cName.c_str() : "?", view_center.x, view_center.y,
					view_center.z, ndc_x, ndc_y, world_radius * 100.f,
					axis_depth * 100.f, axis_offset * 100.f, score,
					visible ? 1 : 0, bone_visible ? 1 : 0, related ? 1 : 0,
					N.hud_role, frozen_reflex ? 1 : 0, owner);
			}
			if (visible && score < selected_score)
			{
				selected_index = index;
				selected_world = refW;
				selected_frozen = frozen_reflex;
				selected_straddle = straddle;
				selected_score = score;
				selected_shift.set(0.f, 0.f, 0.f);
				if (straddle)
				{
					// minimum axial push that clears the near plane with one near depth spare
					const float push = near_plane * 2.f + world_radius - view_center.z;
					selected_shift.mul(objective_axis, push);
				}
			}
		}

		// published for the next camera build so the derived near never clips the capture
		vp.svp_hybrid_front = hybrid_front;
		vp.svp_hybrid_front_frame = Device.dwFrame;
		vp.svp_hybrid_front_session = vp.GetSVPSession();
		vp.svp_hybrid_front_epoch = vp.svp_optic_epoch;
	}

	u32 drawn = 0;
	u32 node_index = 0;
	for (auto& N : nodes)
	{
		if (svp && node_index != selected_index)
		{
			node_index++;
			continue;
		}
		if (!N.pVisual || !N.pSE || !N.pMatrix)
		{
			node_index++;
			continue;
		}
		RCache.set_Element(N.pSE);
		Fmatrix refW = svp ? selected_world
			: *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		// a mesh straddling the camera plane cannot rasterize, the minimum axial push
		// clears the near plane and the window keeps filling the view
		if (svp && selected_straddle)
			refW.c.add(selected_shift);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		const bool frozen_reflex = svp ? selected_frozen
			: svp_objective_hud_current() && sk && sk->SVP_BoneSnapshotReady();
		// Match the objective pass bone pose
		if (!svp && !frozen_reflex)
		{
			Fmatrix bL, bNow;
			if (sk && RImplementation.GMBase.svp_lens_bone_of(N.pVisual, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				refW.mulB_43(bL);
				refW.mulB_43(inv);
			}
		}
		if (node_diag)
		{
			auto tx = N.pVisual->GetTexture();
			Fvector draw_view;
			Device.matrices[1].mView.transform_tiny(draw_view, refW.c);
			PipMsg("[SVP-HYBRID] selected=%u tex=%s score=%.4f frozen=%d straddle=%d drawView=(%.3f %.3f %.3f) vp=%ux%u owner=%p",
				node_index, tx ? tx->cName.c_str() : "?", selected_score,
				frozen_reflex ? 1 : 0, selected_straddle ? 1 : 0,
				draw_view.x, draw_view.y, draw_view.z,
				Device.dwWidth, Device.dwHeight,
				sk ? sk->SVP_SkeletonOwner() : nullptr);
		}
		RCache.set_xform_world(refW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();
		const bool frozen_was = g_svp_hud_frozen_pass;
		const bool history_was = g_svp_hud_history_write;
		if (frozen_reflex)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = false;
		}
		N.pVisual->Render(0);
		g_svp_hud_frozen_pass = frozen_was;
		g_svp_hud_history_write = history_was;
		drawn++;
		node_index++;
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	if (svp)
		RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
	return drawn;
}

// Render an engaged hybrid through the objective camera
bool CRenderTarget::draw_hybrid_reflex()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	auto& G = RImplementation.GMBase.RGraph;
	auto& vp = Device.m_SecondViewport;
	constexpr int mark_magnifier_type = 12;
	const float reticle_value = ps_s3ds_param_3.y;
	const bool reticle_valid = _valid(reticle_value)
		&& reticle_value >= 0.f && reticle_value <= float(u8(-1))
		&& floorf(reticle_value) == reticle_value;
	const int reticle_type = reticle_valid ? static_cast<int>(reticle_value) : -1;
	const bool camera_current = vp.svp_camera_frame == Device.dwFrame
		&& vp.svp_camera_session == vp.GetSVPSession();
	const bool lens_current = vp.svp_lens_frame == Device.dwFrame
		&& vp.svp_lens_root != nullptr && vp.svp_lens_visual != nullptr
		&& vp.eyepiece.radius > EPS;
	const bool target_current = RImplementation.TargetSVP == this
		&& RImplementation.Target == this;
	const auto& optic = vp.RenderOpticConfig();
	const bool identity_current = optic.typed_route && optic.valid
		&& optic.frame == Device.dwFrame
			&& optic.session == vp.GetSVPSession()
			&& (optic.scope[0] || optic.diagnostic_scope[0]);
	const bool type_current = identity_current
		&& optic.reticle_type == reticle_type;
	const bool legacy_hybrid = !optic.has_hybrid_reflex
		&& optic.reticle_type == mark_magnifier_type;
	const bool hybrid_eligible = type_current
		&& (optic.hybrid_reflex || legacy_hybrid);

	enum class HybridState : u32
	{
		Inactive,
		WrongDomain,
		NotHybrid,
		Thermal,
		StaleCamera,
		StaleLens,
		StaleIdentity,
		StaleType,
		Empty,
		NoDraw,
		Drawn
	};

	HybridState state = HybridState::Inactive;
	u32 drawn = 0;
	if (!Device.true_pip_on || !vp.IsSVPActive())
		state = HybridState::Inactive;
	else if (vp.svp_camera_domain != CSecondVPParams::camera_objective
		|| vp.svp_front_use_m <= EPS || vp.objective.radius <= EPS)
		state = HybridState::WrongDomain;
	else if (!reticle_valid)
		state = HybridState::StaleType;
	else if (svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current))
		state = HybridState::Thermal;
	else if (!camera_current || !target_current)
		state = HybridState::StaleCamera;
	else if (!lens_current)
		state = HybridState::StaleLens;
	else if (!identity_current)
		state = HybridState::StaleIdentity;
	else if (!type_current)
		state = HybridState::StaleType;
	else if (!hybrid_eligible)
		state = HybridState::NotHybrid;
	else if (G.mapScopeHUDSorted.empty() || G.mapReflexHUDSorted.empty())
		state = HybridState::Empty;
	else
	{
		// Post processed color stays intact while the optical reticle remains sharp
		u_setrt(Width, Height, rt_secondVP->pRT, nullptr, nullptr, nullptr);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();
		drawn = draw_reflex(true);
		state = drawn ? HybridState::Drawn : HybridState::NoDraw;
	}

	extern int ps_r__svp_diag;
	static u32 s_diag_ms = 0;
	static u32 s_last_state = u32(-1);
	static int s_last_type = -2;
	static int s_last_mark = -2;
	static u32 s_last_epoch = u32(-1);
	const u32 state_value = static_cast<u32>(state);
	const bool changed = state_value != s_last_state || reticle_type != s_last_type
		|| ps_markswitch_current != s_last_mark || vp.svp_optic_epoch != s_last_epoch;
	if (ps_r__svp_diag && (changed || Device.dwTimeGlobal - s_diag_ms > 1000))
	{
		s_diag_ms = Device.dwTimeGlobal;
		s_last_state = state_value;
		s_last_type = reticle_type;
		s_last_mark = ps_markswitch_current;
		s_last_epoch = vp.svp_optic_epoch;
		const char* state_name = state == HybridState::Drawn ? "drawn"
			: state == HybridState::NoDraw ? "no_draw"
			: state == HybridState::Empty ? "empty"
			: state == HybridState::StaleType ? "stale_type"
			: state == HybridState::StaleIdentity ? "stale_identity"
			: state == HybridState::StaleLens ? "stale_lens"
			: state == HybridState::StaleCamera ? "stale_camera"
			: state == HybridState::Thermal ? "thermal"
			: state == HybridState::NotHybrid ? "not_hybrid"
			: state == HybridState::WrongDomain ? "wrong_domain"
			: "inactive";
		LPCSTR optic_name = optic.scope[0] ? optic.scope
			: (optic.diagnostic_scope[0] ? optic.diagnostic_scope : "legacy");
		PipMsg("[SVP-HYBRID] state=%s path=objective-mesh target=secondvp projection=scene rtype=%d config_type=%u authored=%d hybrid=%d legacy=%d eligible=%d image=%.0f mark=%d mag=%.2f scope=%u reflex=%u camera=%d lens=%d target_ok=%d typed=%d identity=%d type_ok=%d optic=%s spec=%s gen=%u drawn=%u session=%u epoch=%u frame=%u",
			state_name, reticle_type, optic.reticle_type,
			optic.has_hybrid_reflex ? 1 : 0, optic.hybrid_reflex ? 1 : 0,
			legacy_hybrid ? 1 : 0,
			hybrid_eligible ? 1 : 0, ps_s3ds_param_3.x, ps_markswitch_current,
			vp.svp_mag, (u32)G.mapScopeHUDSorted.size(), (u32)G.mapReflexHUDSorted.size(),
			camera_current ? 1 : 0, lens_current ? 1 : 0, target_current ? 1 : 0,
			optic.typed_route ? 1 : 0, identity_current ? 1 : 0,
			type_current ? 1 : 0,
			optic_name, optic.spec_section[0] ? optic.spec_section : "none",
			optic.generation, drawn,
			vp.GetSVPSession(), vp.svp_optic_epoch, Device.dwFrame);
	}

	if (state == HybridState::Drawn)
	{
		if (ps_r__svp_stats)
			++svp_stats_reflex_capture;
		return true;
	}
	return false;
}

void CRenderTarget::phase_3DSSReticle()
{
	PIX_EVENT(PHASE_SCOPE_RETICLE);

	// pip take the PiP path only when the active optic can drive the SVP, an optic that
	// captures nothing falls through to the stock render_Reticle
	const bool svp = Device.m_SecondViewport.IsSVPActive() && RImplementation.TargetSVP;
	const bool has_lens = !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS;

	// pip [SVP-RET] reticle pipeline diag, dumps the capture maps before the draws consume them
	{
		extern int ps_r__svp_diag;
		static u32 s_ret_ms = 0;
		if (ps_r__svp_diag && Device.dwTimeGlobal - s_ret_ms > 1000)
		{
			s_ret_ms = Device.dwTimeGlobal;
			auto& G = RImplementation.GMBase.RGraph;
			extern Fvector4 ps_s3ds_param_1;
			extern Fvector4 ps_s3ds_param_3;
			extern Fvector4 ps_shader_scope_params;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_min_mag;
			extern float g_pip_scope_max_mag;
			extern float g_pip_scope_ratio;
			// the lua triplet stays zero under pip, the pip fields carry the live engine mags
			PipMsg("[SVP-RET] path=%s svp=%d lens=%d scope=%u reflex=%u obj=%u rsize=%.2f rtype=%.0f lua=%.2f/%.2f/%.2f w=%.1f pip=%.2f/%.2f/%.2f ratio=%.2f hudy=%.1f",
				(Device.true_pip_on && (svp || has_lens)) ? "pip" : "stock",
				(int)svp, (int)has_lens,
				(u32)G.mapScopeHUDSorted.size(), (u32)G.mapReflexHUDSorted.size(), (u32)G.mapScopeHUDObjective.size(),
				ps_s3ds_param_1.x, ps_s3ds_param_3.y,
				ps_shader_scope_params.x, ps_shader_scope_params.y, ps_shader_scope_params.z, ps_shader_scope_params.w,
				g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_max_mag, g_pip_scope_ratio,
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.y : 0.f);
			auto dump = [&](const char* tag, auto& map) {
				u32 i = 0;
				for (auto& N : map)
				{
					if (!N.pVisual || !N.pMatrix) { i++; continue; }
					Fmatrix W = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
					Fvector c; N.pVisual->getVisData().box.getcenter(c);
					Fvector wc; W.transform_tiny(wc, c);
					auto tx = N.pVisual->GetTexture();
					PipMsg("[SVP-RET]  %s[%u] tex=%s pos=(%.2f %.2f %.2f) d=%.1fcm r=%.1fcm",
						tag, i, tx ? tx->cName.c_str() : "?", wc.x, wc.y, wc.z,
						wc.distance_to(Device.vCameraPosition) * 100.f,
						N.pVisual->getVisData().sphere.R * 100.f);
					i++;
				}
			};
			dump("scope", G.mapScopeHUDSorted);
			dump("reflex", G.mapReflexHUDSorted);
			dump("obj", G.mapScopeHUDObjective);
		}
	}

	if (Device.true_pip_on && (svp || has_lens))
	{
		EnsureScopeShaders(); // glue shaders (lazy)

		auto M = RImplementation.TargetMain;
		auto S = RImplementation.TargetSVP;

		// a hybrid magnifier drew the holo dot inside the svp already, skip the 1x main-view overlay
		extern int ps_r__svp_reflex_capture;
		// Suppress the fallback only for the captured optic identity
		const bool reflex_in_svp = ps_r__svp_reflex_capture && svp
			&& Device.m_SecondViewport.svp_reflex_capture_ok
			&& Device.m_SecondViewport.svp_reflex_capture_epoch
				== Device.m_SecondViewport.svp_optic_epoch
			&& Device.m_SecondViewport.svp_reflex_capture_session
				== Device.m_SecondViewport.GetSVPSession();

		// the scope shader reads generic2 as the gbuffer position for the holepunch/depth
		// the scope path repoints that name at the svp position surface below so the copy feeds nothing
		if (!(svp && ps_r__svp_skip_pos_copy))
		{
			svp_copy_begin(SVP_CP_PASS, rt_copy_bytes(rt_Generic_2));
			HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());
			svp_copy_end(SVP_CP_PASS);
		}

		u_setrt(RImplementation.Target->rt_Generic_0, nullptr, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();

		if (!reflex_in_svp)
			draw_reflex(); // reflex / red dot, both 1x and magnifier

		// composite the captured eyepiece, inactive true PiP keeps clear glass over the main scene
		if (svp || (!RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS))
		{
			// JITTERFIX, cancel the TAA jitter in the VS so the lens edge has no ring, cvar 0 skips for the a/b
			extern int ps_r__svp_jitterfix;
			if (svp && ps_r__svp_jitterfix)
			{ PIX_EVENT(SCOPE_PHASE_JITTERFIX); draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_JITTERFIX); }); }

			if (svp)
			{
				// point the stock named textures at the active SVP targets
				auto remap = [](LPCSTR name, ref_rt& target) {
					ref_texture t;
					t.create(name);
					t->surface_set(target->pSurface);
				};
				remap(r2_RT_secondVP, S->rt_secondVP);
				remap(r2_RT_generic2, S->rt_Position);
				remap(r2_RT_heat, S->rt_Heat);
				{
					ref_texture t;
					t.create("$user$svp_tonemap");
					ID3DBaseTexture* s = S->t_LUM_dest->surface_get();
					t->surface_set(s);
					_RELEASE(s);
				}
				RCache.Invalidate();

				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();

				{ PIX_EVENT(SCOPE_PHASE_IMAGE);
				draw_scope(s_scope_color_write, []() {
					RCache.set_c("scope_phase", SCOPE_PHASE_IMAGE);
					auto ts = RImplementation.TargetSVP;
					Fvector4 sr; sr.set((float)ts->Width, (float)ts->Height, 1.0f / (float)ts->Width, 1.0f / (float)ts->Height);
					RCache.set_c("screen_res", sr);
					auto tm = RImplementation.TargetMain;
					Fvector4 outr; outr.set((float)tm->Width, (float)tm->Height, 1.0f / (float)tm->Width, 1.0f / (float)tm->Height);
					RCache.set_c("output_res", outr);
					Fvector up = {0, 1, 0};
					Device.m_SecondViewport.objective.m_W.transform_dir(up);
					Device.mView.transform_dir(up);
					up.z = 0.0f;
					up.normalize();
					float angle = acosf(up.dotproduct({0, 1, 0})) * (up.x > 0 ? 1.0f : -1.0f);
					RCache.set_c("hack_tex_angle", angle);
				});
				}

				// latch the on-screen eyepiece disc px for adaptive SVP resolution, learn only the
				// settled aimed disc so a raise transient or quick peek never freezes a partial value
				if (RImplementation.TargetSVP && Device.m_SecondViewport.eyepiece.radius > EPS)
				{
					auto& vpd = Device.m_SecondViewport;
					const Fmatrix& MH = Device.mFullTransformHud;
					auto toPx = [&](const Fvector& wp, float& sx, float& sy) {
						const float x  = wp.x*MH._11 + wp.y*MH._21 + wp.z*MH._31 + MH._41;
						const float y  = wp.x*MH._12 + wp.y*MH._22 + wp.z*MH._32 + MH._42;
						const float w  = wp.x*MH._14 + wp.y*MH._24 + wp.z*MH._34 + MH._44;
						const float iw = (fabsf(w) > 1e-6f) ? 1.0f/w : 0.0f;
						sx = (x*iw*0.5f + 0.5f) * (float)M->Width;
						sy = (1.0f - (y*iw*0.5f + 0.5f)) * (float)M->Height;
					};
					Fvector ei, li; li.set(vpd.eyepiece.radius, 0.f, 0.f);
					vpd.eyepiece.m_W.transform_tiny(ei, li);
					float cx, cy, ix, iy; toPx(vpd.eyepiece.m_W.c, cx, cy); toPx(ei, ix, iy);
					const float disc = 2.0f * sqrtf((ix-cx)*(ix-cx) + (iy-cy)*(iy-cy)); // on-screen disc diameter px
					// weapon raise factor, 1 only when fully aimed, gates learning to the settled pose
					const float rot = (g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants)
						? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f;
					// subscribe to the optic epoch, a swap under the jump threshold still re-learns the
					// new optic's disc, the epoch is consumed only after a settled re-learn
					static u32 s_disc_px_epoch = 0;
					const bool disc_epoch_swap = (vpd.svp_optic_epoch != s_disc_px_epoch);
					if (disc > 1.f && disc == disc && rot > 0.999f) // settled, ignore NaN / degenerate
					{
						float& latched = vpd.svp_disc_px;
						const float prev_latched = latched;
						if (latched <= 0.f || disc > latched + 24.f || disc_epoch_swap) // first settle, a jump, or an optic swap
							latched = disc;
						else if (disc < latched * 0.85f)               // big drop, swapped to a smaller optic
							latched = disc;
						if (latched != prev_latched) { if (ps_r__svp_stats) ++svp_stats_disc_latch; svp_ledger_disc_latch = 1; } // overlay + ledger proof the latch moved
						s_disc_px_epoch = vpd.svp_optic_epoch;
					}
					extern int ps_r__svp_diag;
					static u32 s_svpres_t = 0;
					if (ps_r__svp_diag && disc > 1.f && Device.dwTimeGlobal - s_svpres_t > 700)
					{
						s_svpres_t = Device.dwTimeGlobal;
						const u32 sres = RImplementation.TargetSVP->Width;
						extern float g_pip_scope_magnification; extern float ps_r__svp_adaptive_res;
						const float lin = (float)sres / disc;
						PipMsg("[SVP-RES] mag=%.1f svp=%ux%u disc=%.0fpx latch=%.0f adapt=%.2f overrender=%.2fx_linear %.2fx_area",
							g_pip_scope_magnification, sres, sres, disc, vpd.svp_disc_px, ps_r__svp_adaptive_res, lin, lin*lin);
					}
				}
			}


			// restore the stock textures for the reticle/shadow/lens draws
			M->SetActive(true);
			u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();

			{ PIX_EVENT(SCOPE_PHASE_RETICLE); draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE); }); }
			{ PIX_EVENT(SCOPE_PHASE_SHADOW);  draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_SHADOW); }); }
			{ PIX_EVENT(SCOPE_PHASE_LENS);    draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_LENS); }); }

			// The focus depth belongs only to a real SVP image
			if (svp)
			{
				PIX_EVENT(SCOPE_PHASE_CUSTOM_DEPTH);
				u_setrt(RImplementation.Target->rt_Position, 0, 0, 0, RImplementation.Target->baseZB);
				draw_scope(s_scope_depth_write, []() {
					RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE | SCOPE_PHASE_CUSTOM_DEPTH);
					RCache.set_c("scope_depth_value", 1.0f);
				});
			}

			// re-draw the reflex over the composited lens at the main-view position, the hybrid
			// magnifier already has it in the svp image so skip the 1x overlay there
			if (!reflex_in_svp)
			{
				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				draw_reflex();
			}
		}

		// the capture maps clear at the main-pass combine tail now, past the nvg split and taa mask
		// stamp that read them (deriveScopeLens already read them this frame)
		u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
		return;
	}

	// legacy 3D-fake / fake-SVP reticle, the stock path when true_pip is off
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	RImplementation.render_Reticle();

	// pip reflexes captured into mapReflexHUDSorted draw here for the fallback optics,
	// the map is empty when true_pip is off
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	draw_reflex();

	// the capture maps clear at the main-pass combine tail now, past every consumer, the frame-start
	// clear + per-frame rebuild cover staleness on the fallback path too
};
#endif
