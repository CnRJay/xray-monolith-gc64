#ifndef svp_gameplay_cvarsH
#define svp_gameplay_cvarsH
#pragma once

// true PiP gameplay cvars, xrEngine resident so xrGame reads them without linking xrRender

extern float g_zoom_smooth;
extern float g_zoom_analog;
extern int g_zoom_clicks;
extern int g_svp_zoom_base;
extern int g_svp_authored_mags;
extern float g_svp_zero;
extern int g_svp_unify_cam_fx;
extern int g_svp_world_cam_fx;
extern int g_svp_hud_true_fov;
extern int g_svp_zoom_sync;
extern int g_svp_crescent;
extern float g_svp_sens;
extern float g_svp_sens_curve;
extern bool svp_optic_api_active();

// authored zoom factor convention base, a factor f renders mag (SVP_ZOOM_BASE_FOV / 0.75) / f
constexpr float SVP_ZOOM_BASE_FOV = 75.f;
// highest supported magnification, the ladder validation and the camera fov floor share it
constexpr float SVP_MAG_LIMIT = 200.f;
// THE fov seam, every scope fov derives here, scale = g_fov / SVP_ZOOM_BASE_FOV or 1 scripted
constexpr float svp_factor_to_fov(float factor, float fov_scale)
{
	return factor * 0.75f * fov_scale;
}

// registers the svp gameplay console commands, called once from CCC_Register
extern void svp_gameplay_cvars_init();

#endif
