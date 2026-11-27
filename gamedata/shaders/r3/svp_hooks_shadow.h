// PIP_HOOK svp_hooks_shadow 20260715
// true pip logic for the 3DSS shadow chunk, rides mod.db0, the compat patch includes it

#ifndef SVP_HOOKS_SHADOW_H
#define SVP_HOOKS_SHADOW_H

#include "svp_hooks_common.h"

float2 svp_optical_eye_offset()
{
	return svp_eyebox.xy;
}

float2 svp_scope_lens_tc(float3 world_position)
{
	const float3 from_center = world_position - svp_lens_center.xyz;
	return 0.5 + float2(
		dot(from_center, svp_lens_right.xyz),
		dot(from_center, svp_lens_up.xyz)) * svp_lens_center.w;
}

float svp_magnification_fraction()
{
	const float range = svp_aperture.w - svp_aperture.z;
	if (range <= 0.001)
		return 0.0;
	return smoothstep(0.0, 1.0, saturate((svp_aperture.y - svp_aperture.z) / range));
}

float4 svp_profiled_tunneling_shadow(float2 lens_tc)
{
	static const float reference_width = 0.15;
	const float profile_amount = lerp(svp_optic_profile.y, svp_optic_profile.z, svp_magnification_fraction());
	const float amount = saturate(profile_amount * max(svp_optic_profile.w, 0.0) + svp_pupil_model.z);
	const float width = min(reference_width * amount, 0.09);
	if (width <= 0.0001)
		return float4(0.0, 0.0, 0.0, 0.0);

	const float alpha = smoothstep(
		0.5 - width, 0.5,
		distance(lens_tc, float2(0.5, 0.5)));
	return float4(0.0, 0.0, 0.0, alpha);
}

float2 svp_scope_tunneling_offset()
{
	if (svp_aperture.x < 0.5 || svp_eyebox.z <= 0.0 || svp_eyebox.w <= 0.0)
		return float2(0.0, 0.0);
	const float pupil_span = max(svp_eyebox.z + svp_eyebox.w, 0.001);
	const float2 normalized_eye = clamp(svp_optical_eye_offset() / pupil_span, -1.0, 1.0);
	return -normalized_eye * svp_optic_profile.x;
}

float svp_pupil_overlap(float separation, float exit_radius, float eye_radius)
{
	const float smaller = min(exit_radius, eye_radius);
	const float larger = max(exit_radius, eye_radius);
	if (separation <= larger - smaller)
		return 1.0;
	if (separation >= exit_radius + eye_radius)
		return 0.0;

	const float d2 = separation * separation;
	const float exit2 = exit_radius * exit_radius;
	const float eye2 = eye_radius * eye_radius;
	const float exit_angle = acos(clamp((d2 + exit2 - eye2) / (2.0 * separation * exit_radius), -1.0, 1.0));
	const float eye_angle = acos(clamp((d2 + eye2 - exit2) / (2.0 * separation * eye_radius), -1.0, 1.0));
	const float radical = max(
		(-separation + exit_radius + eye_radius)
		* (separation + exit_radius - eye_radius)
		* (separation - exit_radius + eye_radius)
		* (separation + exit_radius + eye_radius), 0.0);
	const float area = exit2 * exit_angle + eye2 * eye_angle - 0.5 * sqrt(radical);
	return saturate(area / (3.14159265 * smaller * smaller));
}

// max field scale that keeps the exit and eye pupil discs overlapping
float svp_pupil_field_scale(float exit_radius, float eye_radius)
{
	const float smaller = min(exit_radius, eye_radius);
	const float full_overlap_limit = abs(exit_radius - eye_radius);
	const float maximum_field_separation = full_overlap_limit + smaller * 0.5;
	return maximum_field_separation / max(1.41421356 * exit_radius, 0.001);
}

float svp_exit_pupil_transmission(float2 lens_tc)
{
	if (shader_scope_params.w >= -1.5 || svp_aperture.x < 0.5
		|| svp_eyebox.z <= 0.0 || svp_eyebox.w <= 0.0)
		return 1.0;

	const float2 eye_offset = svp_optical_eye_offset() * max(svp_pupil_model.y, 0.0);
	const float eye_separation = length(eye_offset);
	const float exit_radius = svp_eyebox.z;
	const float eye_radius = svp_eyebox.w;
	if (eye_separation <= 0.0001)
		return 1.0;

	// approximate field pupil decenter then compare finite pupil discs
	// normalize by the aligned overlap so a centered optic stays clear
	const float2 field = (lens_tc - 0.5) * 2.0;
	const float field_scale = min(
		max(svp_pupil_model.x, 0.0),
		svp_pupil_field_scale(exit_radius, eye_radius));
	const float2 field_pupil_offset = field * exit_radius * field_scale;
	const float aligned_overlap = svp_pupil_overlap(
		length(field_pupil_offset), exit_radius, eye_radius);
	const float displaced_overlap = svp_pupil_overlap(
		length(field_pupil_offset - eye_offset), exit_radius, eye_radius);
	// the ratio only means anything while the aligned baseline holds, past the
	// design field it blends to the absolute overlap so the edge falls dark
	const float normalized = saturate(displaced_overlap / max(aligned_overlap, 0.001));
	return lerp(displaced_overlap, normalized, smoothstep(0.0, 0.25, aligned_overlap));
}

// union of two black shadow alphas
float4 svp_merge_black_shadow(float4 persistent_shadow, float4 transient_shadow)
{
	const float alpha = 1.0 - (1.0 - persistent_shadow.a) * (1.0 - transient_shadow.a);
	return float4(0.0, 0.0, 0.0, saturate(alpha));
}

// the swing side slides the pupil center so the shadow enters from the side of the motion
// and a symmetric ring cannot form at center
float2 svp_shadow_swing(float2 off)
{
	if (shader_scope_params.w < -1.5)
		off += svp_exposure.zw;
	return off;
}

// the parallax crescent rides the swing envelope, calm aim shows none and a hard weapon
// swing sweeps in a dark crescent that fades back out quickly
float4 svp_shadow_soften(float4 shadow)
{
	if (shader_scope_params.w < -1.5)
	{
		shadow.rgb = 0;
		shadow.a *= saturate((svp_glass4.w - 0.3) / 0.35);
	}
	return shadow;
}

// the ocular field stop, a static rim vignette where the physical field ends
// onset rides the capped pupil penumbra and the ramp completes at the rim
float svp_field_stop_alpha(Scope S)
{
	if (shader_scope_params.w < -1.5 && RETICLE_TYPE != RT_FLAT_SCREEN)
	{
		float onset = svp_glass3.y;
		float band = 1.0 - onset;
		if (band > 0.001)
		{
			float r = length((S.tc0.xy - 0.5) * 2.0);
			return saturate((r - onset) / band);
		}
	}
	return 0.0;
}

#endif
