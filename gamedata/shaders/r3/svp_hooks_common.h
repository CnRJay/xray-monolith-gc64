// svp_hooks_common 20260723 pupil
// shared SVP uniform + texture decls for the hook headers and monolith scopes, no shader_scope_params here
#ifndef SVP_HOOKS_COMMON_INCLUDED
#define SVP_HOOKS_COMMON_INCLUDED

Texture2D s_tonemap_svp;
Texture2D s_svp_rain; // objective-glass rain droplet atlas, x=Fade y=NormalY z=NormalX w=TimeOffset
#ifndef SVP_EXPOSURE_DECLARED
#define SVP_EXPOSURE_DECLARED
uniform float4 svp_exposure; // x = 0 off else 2^bias, y = twilight dim, zw = crescent swing side offset
#endif
#ifndef SVP_APERTURE_DECLARED
#define SVP_APERTURE_DECLARED
uniform float4 svp_aperture; // x enabled, y current magnification, z min, w max
#endif
#ifndef SVP_EYEBOX_DECLARED
#define SVP_EYEBOX_DECLARED
uniform float4 svp_eyebox; // xy live eye offset at the virtual pupil, zw exit/eye pupil radii mm
#endif
#ifndef SVP_OPTIC_PROFILE_DECLARED
#define SVP_OPTIC_PROFILE_DECLARED
uniform float4 svp_optic_profile; // x parallax, yz profile tunnel min/max, w global curve multiplier
uniform float4 svp_pupil_model; // x field decenter, y pupil sensitivity, z tunnel offset, w reserved
#endif

bool svp_physical_optics_active()
{
	return shader_scope_params.w < -1.5 && svp_aperture.x > 0.5;
}
#ifndef SVP_LENS_GEOMETRY_DECLARED
#define SVP_LENS_GEOMETRY_DECLARED
uniform float4 svp_lens_center; // xyz eyepiece center, w inverse lens diameter
uniform float4 svp_lens_right;
uniform float4 svp_lens_up;
#endif
#ifndef SVP_OPTICS_DECLARED
#define SVP_OPTICS_DECLARED
uniform float4 svp_optics; // x lens geometry, y reticle parallax, z legacy depth, w optical ratio
#endif
#ifndef SVP_CONTROL_DECLARED
#define SVP_CONTROL_DECLARED
uniform float4 svp_control; // engine intent, x/y/z = strip parallax shadow/chromatism/nvg blur under true PiP
#endif
#ifndef SVP_MAS_DECLARED
#define SVP_MAS_DECLARED
uniform float4 svp_mas; // x effective scale, y override active, z authored scale, w packed source
float svp_effective_mas(float authored)
{
	return shader_scope_params.w < -1.5 && svp_mas.y > 0.5 ? svp_mas.x : authored;
}
#endif
#ifndef SVP_GLASS_DECLARED
#define SVP_GLASS_DECLARED
uniform float4 svp_glass; // engine glass tunables, y = field curvature, w = auto reticle flip
#endif
#ifndef SVP_ENV_DECLARED
#define SVP_ENV_DECLARED
uniform float4 svp_env; // engine glass environment, x = veiling glare, y = rain intensity
#endif
#ifndef SVP_GLASS2_DECLARED
#define SVP_GLASS2_DECLARED
uniform float4 svp_glass2; // engine glass optics 2, x = coating, y = heat mirage, w = flat-panel V-crop
#endif
#ifndef SVP_GLASS3_DECLARED
#define SVP_GLASS3_DECLARED
uniform float4 svp_glass3; // engine glass optics 3, x = sharpen amount, y = field-stop onset, z = sharpen radial falloff, w = sharpen inner crisp radius
#endif
#ifndef SVP_GLASS4_DECLARED
#define SVP_GLASS4_DECLARED
uniform float4 svp_glass4; // engine glass optics 4, x = nvg bleach roll-off, y = nvg auto-gain, w = shadow swing envelope
#endif

#endif
