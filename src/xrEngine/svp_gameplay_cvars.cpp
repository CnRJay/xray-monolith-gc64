#include "stdafx.h"
#include "svp_gameplay_cvars.h"
#include "xr_ioconsole.h"
#include "xr_ioc_cmd.h"
#include "device.h"
#include "svp_console_policy.h"

float g_zoom_smooth = 12.f; // pip dynamic-scope zoom smoothing rate (lerp per second), 0 = instant stepped feel
float g_zoom_analog = 0.f; // pip dynamic-scope analog zoom, 0 = discrete config steps, >0 = fine steps across the range
int g_zoom_clicks = 1; // pip a scope authoring zoom_step_count 1 clicks between its two detents, ignoring analog and smoothing, 0 = off
int g_svp_zoom_base = 1; // pip true svp scopes derive the bottom detent in the authored 75 base so it renders 1x, 0 = legacy fov derivation
int g_svp_authored_mags = 1; // pip svp scopes read authored magnifications from the scope section, 0 = legacy scope_zoom_factor derivation
static int s_svp_optic_api = 1;
float g_svp_zero = 100.f; // pip auto-zero cap in meters, shots converge on the aimed surface up to this range, 0 = raw fire axis
int g_svp_unify_cam_fx = 1; // pip camera-only effector kicks also rotate the weapon while a PiP scope is aimed, 0 = stock split
int g_svp_world_cam_fx = 1; // pip cam effectors keep driving the main view while a PiP scope is aimed, 0 = frozen surround
int g_svp_hud_true_fov = 0; // pip the hud weapon renders at the true scene fov while a PiP scope is aimed (0 = stock viewmodel fov)
int g_svp_zoom_sync = 1; // pip the svp renders the dialed magnification through the raise, 0 = track the live zoom factor
int g_svp_crescent = 0; // pip swing crescent master switch, 0 hides the parallax crescent under a pip scope
float g_svp_sens = 1.f; // pip scoped mouse sensitivity multiplier, applies only while the svp renders
float g_svp_sens_curve = 1.f; // pip zoom sens response exponent, 1 = proportional to magnification, 0 = flat

bool svp_optic_api_active()
{
	return Device.m_SecondViewport.IsOpticApiEnabled();
}

static LPCSTR svp_optic_source(const CSecondVPParams::OpticConfig& config,
	CSecondVPParams::EOpticConfigValue field)
{
	return config.source[field][0] ? config.source[field] : "engine_default";
}

static void svp_dump_optic_record(LPCSTR name, const CSecondVPParams::OpticConfig& config)
{
	Msg("[SVP-CONFIG] %s valid=%d typed=%d token=%u gen=%u route=%u frame=%u session=%u",
		name, config.valid, config.typed_route, config.context_token, config.generation,
		config.route_epoch, config.frame, config.session);
	Msg("[SVP-CONFIG] %s context=%s weapon=%s weapon_id=%u scope=%s diagnostic_scope=%s zoom=%u reticle=%u@%s hybrid=%d/%d@%s identity=%s",
		name, config.context, config.weapon, config.weapon_id, config.scope,
		config.diagnostic_scope, config.zoom_type, config.reticle_type,
		svp_optic_source(config, CSecondVPParams::optic_reticle_type),
		config.has_hybrid_reflex, config.hybrid_reflex,
		svp_optic_source(config, CSecondVPParams::optic_hybrid_reflex),
		config.identity_source);
	Msg("[SVP-CONFIG] %s profile_id=%s spec_section=%s model=%s binding=%s binding_section=%s",
		name, config.profile_id, config.spec_section, config.model, config.binding, config.binding_section);
	Msg("[SVP-CONFIG] %s objective=%d %.4f,%.4f,%.4f,%.4f@%s mm=%d %.3f@%s",
		name, config.has_objective_offset, config.objective_offset.x, config.objective_offset.y,
		config.objective_offset.z, config.objective_offset.w,
		svp_optic_source(config, CSecondVPParams::optic_objective_offset),
		config.has_objective_mm, config.objective_mm,
		svp_optic_source(config, CSecondVPParams::optic_objective_mm));
	Msg("[SVP-CONFIG] %s middle_grey=%.4f@%s adapt=%.4f@%s convergence_limit_m=%.3f@%s",
		name, config.middle_grey, svp_optic_source(config, CSecondVPParams::optic_middle_grey),
		config.adapt_speed, svp_optic_source(config, CSecondVPParams::optic_adapt_speed),
		config.convergence_limit_m,
		svp_optic_source(config, CSecondVPParams::optic_convergence_limit_m));
	Msg("[SVP-CONFIG] %s tunnel=%.4f@%s %.4f@%s %.4f@%s",
		name, config.tunneling_parallax,
		svp_optic_source(config, CSecondVPParams::optic_tunneling_parallax),
		config.tunneling_min, svp_optic_source(config, CSecondVPParams::optic_tunneling_min),
		config.tunneling_max, svp_optic_source(config, CSecondVPParams::optic_tunneling_max));
	Msg("[SVP-CONFIG] %s tracking=%.3f@%s %.3f@%s %.3f@%s",
		name, config.tracking_speed, svp_optic_source(config, CSecondVPParams::optic_tracking_speed),
		config.tracking_accel_mm_s2, svp_optic_source(config, CSecondVPParams::optic_tracking_accel),
		config.tracking_limit_mm, svp_optic_source(config, CSecondVPParams::optic_tracking_limit));
	Msg("[SVP-CONFIG] %s eye_relief=%.3f@%s %.3f@%s exit_pupil=%.3f@%s %.3f@%s",
		name, config.eye_relief_low_mm,
		svp_optic_source(config, CSecondVPParams::optic_eye_relief_low),
		config.eye_relief_high_mm, svp_optic_source(config, CSecondVPParams::optic_eye_relief_high),
		config.exit_pupil_low_mm, svp_optic_source(config, CSecondVPParams::optic_exit_pupil_low),
		config.exit_pupil_high_mm, svp_optic_source(config, CSecondVPParams::optic_exit_pupil_high));
	Msg("[SVP-CONFIG] %s pupil=%.3f@%s %.3f@%s %.3f@%s transmission=%.3f@%s twilight=%.3f@%s",
		name, config.pupil_parity, svp_optic_source(config, CSecondVPParams::optic_pupil_parity),
		config.pupil_field_low, svp_optic_source(config, CSecondVPParams::optic_pupil_field_low),
		config.pupil_field_high, svp_optic_source(config, CSecondVPParams::optic_pupil_field_high),
		config.transmission, svp_optic_source(config, CSecondVPParams::optic_transmission),
		config.twilight_strength, svp_optic_source(config, CSecondVPParams::optic_twilight_strength));
	Msg("[SVP-CONFIG] %s physical=%d %.3f@%s %.3f@%s eye_coupling=%d@%s reticle_illum=%.3f@%s",
		name, config.has_physical_range, config.physical_min,
		svp_optic_source(config, CSecondVPParams::optic_physical_min),
		config.physical_max, svp_optic_source(config, CSecondVPParams::optic_physical_max),
		config.eye_coupling, svp_optic_source(config, CSecondVPParams::optic_eye_coupling),
		config.reticle_illum, svp_optic_source(config, CSecondVPParams::optic_reticle_illum));
	string256 magnifications = {};
	for (u32 i = 0; i < config.magnifications.count &&
		i < _countof(config.magnifications.values); ++i)
	{
		string32 value = {};
		xr_sprintf(value, "%s%.3f", i ? "," : "", config.magnifications.values[i]);
		xr_strcat(magnifications, value);
	}
	Msg("[SVP-CONFIG] %s magnification_mode=%u@%s count=%u values=%s@%s mod_lane=%d %.3f,%.3f,%.3f,%.3f@%s",
		name, static_cast<u32>(config.magnifications.mode),
		svp_optic_source(config, CSecondVPParams::optic_magnification_mode),
		static_cast<u32>(config.magnifications.count), magnifications,
		svp_optic_source(config, CSecondVPParams::optic_magnifications),
		config.has_mod_lane, config.mod_lane.x, config.mod_lane.y,
		config.mod_lane.z, config.mod_lane.w,
		svp_optic_source(config, CSecondVPParams::optic_mod_lane));
}

class CCC_SvpDumpOptic final : public IConsole_Command
{
public:
	CCC_SvpDumpOptic(LPCSTR name) : IConsole_Command(name)
	{
	}

	void Execute(LPCSTR) override
	{
		CSecondVPParams::OpticConfig accepted;
		CSecondVPParams::OpticConfig active;
		u32 route_epoch = 0;
		Device.m_SecondViewport.ReadOpticConfigState(accepted, active, route_epoch);
		Msg("[SVP-CONFIG] api=%d connected=%d active=%d route_epoch=%u",
			s_svp_optic_api, Device.m_SecondViewport.IsOpticApiConnected(),
			svp_optic_api_active(), route_epoch);
		svp_dump_optic_record("accepted", accepted);
		svp_dump_optic_record("active", active);
	}
};

void svp_gameplay_cvars_init()
{
	CMD4(CCC_Float, "g_zoom_smooth", &g_zoom_smooth, 0.f, 60.f);
	CMD4(CCC_Float, "g_zoom_analog", &g_zoom_analog, 0.f, 200.f);
	CMD4(CCC_Integer, "g_zoom_clicks", &g_zoom_clicks, 0, 1);
	CMD4(CCC_Integer, "g_svp_zoom_base", &g_svp_zoom_base, 0, 1);
	CMD4(CCC_Integer, "g_svp_authored_mags", &g_svp_authored_mags, 0, 1);
	CMD4(CCC_SvpFixedInteger, "g_svp_optic_api", &s_svp_optic_api, 0, 1);
	CMD4(CCC_SvpProfileFloat, "g_svp_zero", &g_svp_zero, 0.f, 1000.f);
	CMD4(CCC_SvpFixedInteger, "g_svp_unify_cam_fx", &g_svp_unify_cam_fx, 0, 1);
	CMD4(CCC_SvpFixedInteger, "g_svp_world_cam_fx", &g_svp_world_cam_fx, 0, 1);
	CMD4(CCC_SvpInternalInteger, "g_svp_hud_true_fov", &g_svp_hud_true_fov, 0, 1);
	CMD4(CCC_SvpFixedInteger, "g_svp_zoom_sync", &g_svp_zoom_sync, 0, 1);
	CMD4(CCC_SvpFixedInteger, "g_svp_crescent", &g_svp_crescent, 0, 1);
	CMD4(CCC_Float, "g_svp_sens", &g_svp_sens, 0.1f, 3.f);
	CMD4(CCC_Float, "g_svp_sens_curve", &g_svp_sens_curve, 0.f, 2.f);
	CMD1(CCC_SvpDumpOptic, "svp_dump_optic");
}
