// svp_hooks_reticle 20260727 thinhook
// relocated true-PiP sight-reticle terms, included by scope_custom_reticle.h after scope_3dss_common.h
#ifndef SVP_HOOKS_RETICLE_INCLUDED
#define SVP_HOOKS_RETICLE_INCLUDED

#include "svp_hooks_common.h"

bool svp_reticle_kill_chroma()
{
	return shader_scope_params.w < -1.5 && svp_control.y > 0.5 && IMAGE_TYPE != IT_THERMAL && IMAGE_TYPE != IT_THERMAL_COLOR;
}

// retired, kept so stale patch installs still compile
float svp_reticle_acog_fiber(float lum)
{
	return lum;
}

// the pip tangent substitutes the eye-coupled field with a centered one of the same rim
// magnitude, svp_optics.z carries the sine-exact slope and zero falls back to the tan in x
float2 svp_reticle_tangent(Scope S, float2 V_tangent)
{
	if (shader_scope_params.w < -1.5)
	{
		float slope = (svp_optics.z > 0.0001) ? svp_optics.z : svp_optics.x;
		return -(S.tc0 - 0.5) * slope;
	}
	return V_tangent.xy;
}

// Sight reticle. true PiP adds a true-scale parallax term (svp_optics.y) on the pip tangent
float2 svp_reticle_t_field(Scope S, float2 V_tangent)
{
	float mas = svp_effective_mas(mas_scale());
	float2 t_field = svp_reticle_tangent(S, V_tangent) * mas;
	if (shader_scope_params.w < -1.5)
		t_field += V_tangent.xy * (mas * svp_optics.y);
	return t_field;
}

float svp_reticle_pip_pin()
{
	return (shader_scope_params.w < -1.5) ? 0.0 : 1.0;
}

// mirror the sample V around the reticle center under true PiP so it matches the auto-flipped world
// same raw mesh UV test as the image chunk so the two can never disagree
void svp_reticle_flip(inout float2 reticle_tc, inout float2 reticle_lens_tc, Scope S)
{
	if (RETICLE_TYPE != RT_SCREEN && RETICLE_TYPE != RT_FLAT_SCREEN
		&& shader_scope_params.w < -1.5 && svp_glass.w > 0.5 && ddy(S.tc0.y) < 0.0)
	{
		reticle_tc.y = 1.0 - reticle_tc.y;
		reticle_lens_tc.y = 1.0 - reticle_lens_tc.y;
	}
}

// retired, kept so stale patch installs still compile
void svp_reticle_washout(inout float4 result, float lum)
{
}

#endif
