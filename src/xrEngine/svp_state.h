#ifndef svp_stateH
#define svp_stateH
#pragma once

#include <functional>
#include <atomic>
#include "../xrcore/xrSyncronize.h"

// the SVP (PiP second viewport) cross-thread data bus, logic publishes and render consumes by
// field name, device.h includes this just before CRenderDevice and aliases it back inside
class ENGINE_API CSecondVPParams //--#SM+#-- +SecondVP+
{
	std::atomic<bool> isActive{ false };
	std::atomic<u32> m_svp_session{ 0 };
	u8 frameDelay;  // Ia eaeii eaa?a n iiiaioa i?ioeiai ?aiaa?a ai aoi?ie au?ii?o iu ia?i?i iiaue
					  //(ia ii?ao auou iaiuoa 2 - ea?aue aoi?ie eaa?, ?ai aieuoa oai aieaa ieceee FPS ai aoi?ii au?ii?oa)

public:
	bool isCamReady; // Oeaa aioiaiinoe eaia?u (FOV, iiceoey, e o.i) e ?aiaa?o aoi?iai au?ii?oa

	IC bool IsSVPActive() const { return isActive.load(std::memory_order_acquire); }
	IC u32 GetSVPSession() const { return m_svp_session.load(std::memory_order_acquire); }
	IC bool SnapshotExact(u32 frame, u32 session, u32 current) const
	{
		return frame == current && session == GetSVPSession();
	}
	IC bool SnapshotRecent(u32 frame, u32 session, u32 current) const
	{
		const bool fresh = frame != u32(-1) && (frame == current || frame + 1 == current);
		return fresh && session == GetSVPSession();
	}
	void SetSVPActive(bool bState);
	bool    IsSVPFrame();

	IC u8 GetSVPFrameDelay() { return frameDelay; }
	void  SetSVPFrameDelay(u8 iDelay)
	{
		frameDelay = iDelay;
		clamp<u8>(frameDelay, 2, u8(-1));
	}

	// true PiP additions
	struct Lens { Fmatrix m_W; float radius; };
	enum ECameraDomain : u8
	{
		camera_main_eye,
		camera_objective
	};
	Lens eyepiece;
	Lens objective;
	Fvector3 w_ffp;
	Fvector3 w_sfp;
	// pip held lens radii for the debug overlays, cleared when the svp deactivates
	float dbg_eyepiece_r = 0.f;
	float dbg_objective_r = 0.f;

	// pip DLSS-SR scaffolding, all inert at gate 0. cached SVP scene constants refreshed at the
	// svpCamera tail (render thread, written then read same frame) for the eval inputs
	float svp_near = 0.f, svp_far = 0.f, svp_fov = 0.f, svp_aspect = 1.f;
	Fvector svp_cam_pos = {}, svp_up = {}, svp_right = {}, svp_fwd = {};
	float svp_front_use_m = 0.f; // signed eyepiece to objective distance used by true PiP
	ECameraDomain svp_camera_domain = camera_main_eye;
	u32 svp_camera_frame = u32(-1); // render frame that published matrices[1]
	u32 svp_camera_session = 0; // SVP session that published matrices[1]
	const void* svp_lens_root = nullptr;
	const void* svp_lens_visual = nullptr;
	const void* svp_lens_owner = nullptr;
	u32 svp_lens_frame = u32(-1);
	Fvector2 svp_jitter_px = {}; // raw sub-pixel jitter baked into matrices[1].mProject, {0,0} at gate 0
	bool m_lens_prev_valid = false; // render-thread edge state for the lens-appears reset trigger

	float svp_disc_px = 0.f; // pip on-screen eyepiece disc diameter (px), learned in the lens composite
	float svp_disc_applied = 0.f; // pip disc px the SVP target is sized to, locked at ADS-in so it never resizes mid-ADS
	float svp_panel_aspect = 1.f; // pip flat-panel lens W:H from the lens AABB (1 = round/square scope)

	// pip flat-panel on-screen quad for the binocular target brackets, plane half-extent world vectors
	// (logic projects them through the hud transform), the shader V-crop, and the active-panel flag
	Fvector svp_panel_ax_w = {}; // panel center -> width edge (svp ndc +x)
	Fvector svp_panel_ax_h = {}; // panel center -> height edge (svp ndc -y)
	float svp_panel_vcrop = 1.f; // svp_glass2.w flat-panel V-crop (1 = svp matches the panel)
	bool svp_panel_flat = false; // a reticle_type 8 flat window drives the svp this frame

	// pip nearest drawn weapon extent ahead of the objective (m), published by the svp hud drain and
	// read by the next frame's objective camera, -1 = nothing ahead, 0 = geometry reaches the plane
	float svp_hud_min_axial = -1.f;
	float svp_clipon_axial = -1.f; // far extent of a body ahead of the objective (m), -1 = none
	u32 svp_hud_min_bones = 0; // bones the derive actually measured, 0 = whole-visual box path
	u32 svp_hud_axis_skip = 0; // bones excluded because the sight axis passes through their box
	u32 svp_hud_min_frame = u32(-1);
	u32 svp_hud_min_session = 0;

	// pip near extent of the hybrid capture reflex (m), published by the objective reflex draw and
	// read by the next frame's near derive, -1 = no capture candidate this frame
	float svp_hybrid_front = -1.f;
	u32 svp_hybrid_front_frame = u32(-1);
	u32 svp_hybrid_front_session = 0;
	u32 svp_hybrid_front_epoch = 0;
	u32 svp_hud_min_epoch = 0;

	u32 svp_optic_epoch = 0; // pip optic identity counter, bumps on a lens visual or radius change, subscribers reseed
	u32 svp_camera_epoch = 0; // pip camera input counter, leaves target and disc sizing untouched
	// pip resolved per-optic optics inputs, the bus fills these once at the lens derive so one
	// precedence and one eps gate govern every consumer instead of each re-reading the raw cvars
	Fvector4 svp_opt_offset = { 0.f, 0.f, 0.f, 0.f }; // xy lateral zw front/radius (eyepiece radii), authored_optics gated, 0 = none
	float svp_opt_obj_mm = 0.f; // objective clear aperture mm, the spec cvar then the authored w fallback, 0 = none
	bool svp_alt_sight = false; // aimed on a non scope sight, the fit latch freezes on the wrong pose
	float svp_recoil_relax_s = 0.f; // authored one shot recovery, dispersion over relax speed
	float svp_zoom_pub = 0.f; // raise transient free zoom for the svp camera, 0 = unset falls back to hud_params.y
	float svp_eyebox_rad = 0.f; // eyebox half angle (rad) from the real exit pupil and eye relief
	float svp_shadow_gain = 0.f; // swing envelope 0..1, hard weapon motion sweeps the crescent in
	u32 svp_lever_ms = 0; // pip lever throw stamp, a click flip pulses the transition shadow for the throw
	float svp_swing_x = 0.f, svp_swing_y = 0.f; // latched swing side unit vector for the crescent
	float svp_mag = 0.f; // current scope magnification for the zoom scaled trigger, 0 unknown
	float svp_fov_scale = 1.f; // config zoom factors ride the 75 base, this rescales them to the live fov, 1 for script authored
	float svp_aim_fov = 0.f; // steady wide main view fov (deg) through a pip scope, the mag reads it so recoil fov punches never wobble it, 0 unset
	bool svp_authored_mag = false; // pip flat optic carries authored mags, keep the clean optical mag not the panel subtense ratio
	bool svp_min_75base = false; // pip the min zoom bound was computed in the 75 base so it rescales to the aim fov

	// logic flags a broken cheek weld while render owns the eye follower
	std::atomic<bool> svp_eye_tracking_suspended{ false };

	// pip logic publishes the complete weapon pose as one record
	struct WeaponPoseSnapshot
	{
		Fvector fire_ray_pos = {};
		Fvector fire_ray_dir = {};
		float fire_ray_zero = 0.f;
		bool optic_typed = false;
		bool optic_config_valid = false;
		u32 optic_context_token = 0;
		u32 optic_config_generation = 0;
		u32 optic_route_epoch = 0;
		u32 frame = u32(-1);
		u32 session = 0;
		Fvector muzzle_pos = {};
		Fvector eye_ray_pos = {};
		Fvector eye_ray_dir = {};
	};

	// pip render publishes the complete sight line as one record
	struct SightSnapshot
	{
		Fvector position = {};
		Fvector direction = {};
		float lens_radius = 0.f;
		bool optic_typed = false;
		bool optic_config_valid = false;
		u32 optic_context_token = 0;
		u32 optic_config_generation = 0;
		u32 optic_route_epoch = 0;
		u32 frame = u32(-1);
		u32 session = 0;
		u32 optic_epoch = 0;
	};

	struct FireTrace { Fvector pos; Fvector dir; u32 time_ms; };

	enum EOpticFieldType : u8
	{
		optic_type_integer,
		optic_type_number,
		optic_type_boolean,
		optic_type_string,
		optic_type_objective,
		optic_type_mode,
		optic_type_magnifications,
		optic_type_lane,
		optic_type_sources
	};

	enum EOpticFieldId : u8
	{
		optic_field_schema_version,
		optic_field_context_token,
		optic_field_context,
		optic_field_weapon,
		optic_field_weapon_id,
		optic_field_scope,
		optic_field_diagnostic_scope,
		optic_field_identity_source,
		optic_field_zoom_type,
		optic_field_profile_id,
		optic_field_spec_section,
		optic_field_model,
		optic_field_binding,
		optic_field_binding_section,
		optic_field_reticle_type,
		optic_field_hybrid_reflex,
		optic_field_objective_offset,
		optic_field_objective_mm,
		optic_field_middle_grey,
		optic_field_adapt_speed,
		optic_field_convergence_limit_m,
		optic_field_tunneling_parallax,
		optic_field_tunneling_min,
		optic_field_tunneling_max,
		optic_field_tracking_speed,
		optic_field_tracking_accel,
		optic_field_tracking_limit,
		optic_field_eye_relief_low,
		optic_field_eye_relief_high,
		optic_field_exit_pupil_low,
		optic_field_exit_pupil_high,
		optic_field_pupil_parity,
		optic_field_pupil_field_low,
		optic_field_pupil_field_high,
		optic_field_transmission,
		optic_field_twilight_strength,
		optic_field_physical_min,
		optic_field_physical_max,
		optic_field_eye_coupling,
		optic_field_reticle_illum,
		optic_field_magnification_mode,
		optic_field_magnifications,
		optic_field_mod_lane,
		optic_field_sources,
		optic_field_count
	};

	struct OpticObjectMemberDescriptor
	{
		LPCSTR name;
		bool finite;
		bool has_range;
		double minimum;
		double maximum;
		bool minimum_exclusive;
		bool maximum_exclusive;
	};

	struct OpticFieldDescriptor
	{
		EOpticFieldId id;
		LPCSTR name;
		EOpticFieldType type;
		bool required;
		bool registrable;
		bool source_required;
		double minimum;
		double maximum;
		u16 string_capacity;
		u8 array_min;
		u8 array_max;
		bool finite;
		bool ordered;
		bool allow_zero;
		bool minimum_exclusive;
		bool maximum_exclusive;
		bool non_empty;
		const OpticObjectMemberDescriptor* members;
		u8 member_count;
		const LPCSTR* enum_values;
		u8 enum_value_count;
		LPCSTR element_type;
		bool element_non_empty;
		u16 element_string_capacity;
		LPCSTR constraint;
	};

	using OpticFieldDescriptorArray = OpticFieldDescriptor[optic_field_count];
	static const OpticFieldDescriptorArray& OpticFieldDescriptors();
	static LPCSTR OpticFieldTypeName(EOpticFieldType type);
	static LPCSTR OpticSchemaHash();

	enum EOpticConfigValue : u8
	{
		optic_reticle_type,
		optic_hybrid_reflex,
		optic_objective_offset,
		optic_objective_mm,
		optic_middle_grey,
		optic_adapt_speed,
		optic_convergence_limit_m,
		optic_tunneling_parallax,
		optic_tunneling_min,
		optic_tunneling_max,
		optic_tracking_speed,
		optic_tracking_accel,
		optic_tracking_limit,
		optic_eye_relief_low,
		optic_eye_relief_high,
		optic_exit_pupil_low,
		optic_exit_pupil_high,
		optic_pupil_parity,
		optic_pupil_field_low,
		optic_pupil_field_high,
		optic_transmission,
		optic_twilight_strength,
		optic_physical_min,
		optic_physical_max,
		optic_eye_coupling,
		optic_reticle_illum,
		optic_magnification_mode,
		optic_magnifications,
		optic_mod_lane,
		optic_value_count
	};

	enum EOpticMagnificationMode : u8
	{
		optic_magnification_none,
		optic_magnification_fixed,
		optic_magnification_continuous,
		optic_magnification_detent
	};

	struct OpticMagnifications
	{
		EOpticMagnificationMode mode = optic_magnification_none;
		u8 count = 0;
		float values[16] = {};
	};

	struct OpticConfig
	{
		bool valid = false;
		bool typed_route = false;
		bool has_objective_offset = false;
		bool has_objective_mm = false;
		bool has_hybrid_reflex = false;
		bool hybrid_reflex = false;
		bool has_physical_range = false;
		bool has_mod_lane = false;
		u8 zoom_type = 0;
		u8 reticle_type = 0;
		u32 weapon_id = 0;
		u32 context_token = 0;
		u32 generation = 0;
		u32 route_epoch = 0;
		u32 frame = u32(-1);
		u32 session = 0;
		u64 fingerprint = 0;
		Fvector4 objective_offset = { 0.f, 0.f, 0.f, 0.f };
		float objective_mm = 0.f;
		float middle_grey = 0.f;
		float adapt_speed = 0.f;
		float convergence_limit_m = 100.f;
		float tunneling_parallax = 0.035f;
		float tunneling_min = 0.04f;
		float tunneling_max = 0.06f;
		float tracking_speed = 5.f;
		float tracking_accel_mm_s2 = 80.f;
		float tracking_limit_mm = 7.f;
		float eye_relief_low_mm = 80.f;
		float eye_relief_high_mm = 80.f;
		float exit_pupil_low_mm = 0.f;
		float exit_pupil_high_mm = 0.f;
		float pupil_parity = -1.f;
		float pupil_field_low = 0.55f;
		float pupil_field_high = 0.55f;
		float transmission = 1.f;
		float twilight_strength = 0.35f;
		float physical_min = 0.f;
		float physical_max = 0.f;
		bool eye_coupling = true;
		float reticle_illum = 1.f;
		Fvector4 mod_lane = { 0.f, 0.f, 0.f, 0.f };
		OpticMagnifications magnifications;
		string256 context = {};
		string128 weapon = {};
		string128 scope = {};
		string128 diagnostic_scope = {};
		string64 identity_source = {};
		string128 profile_id = {};
		string128 spec_section = {};
		string32 model = {};
		string32 binding = {};
		string128 binding_section = {};
		string256 source[optic_value_count] = {};
	};

	struct OpticPublication
	{
		OpticConfig accepted;
		u32 base_generation = 0;
		u64 base_fingerprint = 0;
	};

	static constexpr u32 optic_api_min = 3;
	static constexpr u32 optic_api_max = 3;
	static constexpr u32 optic_schema_min = 3;
	static constexpr u32 optic_schema_max = 3;
	static constexpr u32 optic_api_version = 3;
	static constexpr u32 optic_schema_version = 3;
	void PublishWeaponPose(const WeaponPoseSnapshot& pose);
	bool ReadWeaponPose(WeaponPoseSnapshot& pose) const;
	void ClearWeaponPose();
	void PublishSight(const SightSnapshot& sight);
	bool ReadSight(SightSnapshot& sight) const;
	void ClearSight();
	void AppendFireTrace(const FireTrace& trace);
	void ReadFireTraces(FireTrace (&traces)[16]) const;
	bool ConnectOpticApi(u32 api, u32 schema);
	void SetOpticScopeMode(u8 mode);
	IC bool IsOpticApiConnected() const
	{
		return m_optic_api_connected.load(std::memory_order_acquire);
	}
	IC bool IsOpticApiEnabled() const
	{
		return m_optic_api_connected.load(std::memory_order_acquire) &&
			m_optic_scope_mode.load(std::memory_order_acquire) > 0;
	}
	u32 BeginOpticContext(LPCSTR context, LPCSTR weapon, u32 weapon_id,
		LPCSTR scope, u8 zoom_type,
		LPCSTR identity_source, LPCSTR diagnostic_scope);
	bool PrepareOpticConfig(u32 context_token, const OpticConfig& config,
		OpticPublication& publication);
	bool PublishOpticConfig(u32 context_token, const OpticPublication& publication);
	bool ClearOpticConfig(u32 context_token);
	void InvalidateOpticConfig();
	bool ReadOpticConfig(OpticConfig& config) const;
	void LatchOpticConfig(u32 frame, u32 session);
	const OpticConfig& RenderOpticConfig() const;
	void ReadOpticConfigState(OpticConfig& accepted, OpticConfig& active, u32& route_epoch) const;
	IC u32 GetOpticRouteEpoch() const { return m_optic_route_epoch.load(std::memory_order_acquire); }

private:
	mutable xrCriticalSection m_snapshot_lock;
	WeaponPoseSnapshot m_weapon_pose;
	SightSnapshot m_sight;
	FireTrace m_fire_traces[16] = {};
	u32 m_fire_trace_head = 0;
	OpticConfig m_optic_accepted;
	OpticConfig m_optic_active;
	OpticConfig m_optic_neutral;
	std::atomic<bool> m_optic_api_connected{ false };
	std::atomic<u8> m_optic_scope_mode{ 0 };
	std::atomic<u32> m_optic_route_epoch{ 1 };
	u32 m_optic_token_counter = 0;
	u32 m_optic_generation_counter = 0;
	void ResetOpticConfigLocked();

public:

	// history reset for the eval, set by the triggers (logic + render threads), consumed render-side
	// at the seam via exchange, atomic because the logic-thread writers race the render-thread read
	std::atomic<bool> dlss_reset_next{ false };

	// set by the double-pass, read by the hybrid IsSVPFrame when true_pip is on
	bool m_render_pass_is_svp = false;

	// pip set only when the hybrid reflex drew into rt_secondVP this frame
	bool svp_reflex_capture_ok = false;
	u32 svp_reflex_capture_epoch = u32(-1);
	u32 svp_reflex_capture_session = 0;

	// pip set only around the SVP water surface draw, makes ssfx_issvp read 0 so the SSS water shader
	// uses the SVP reflection instead of its flat-scope fallback (ssfx_water.ps reflection = turbidity)
	bool force_water_reflect = false;

	// pip set around the SVP sun accum, makes ssfx_issvp read 0 so the sun keeps the SSS contact term
	bool force_svp_sss = false;

	// pip shared-shadow hook, called as accum(); if (dual_accum) dual_accum(accum), the lambda
	// re-accumulates the shadow unit into the SVP, null for R2/R3 and when PiP is off
	std::function<void(const std::function<void()>&)> dual_accum;

};

#endif
