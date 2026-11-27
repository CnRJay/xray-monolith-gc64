#include "StdAfx.h"
#include "light.h"
#include "QueryHelper.h"
#include "../../xrEngine/cl_intersect.h"
#if RENDER == R_R4
#include "../../xrGame/debug_renderer.h"
#endif

const u32 delay_small_min = 1;
const u32 delay_small_max = 3;
const u32 delay_large_min = 10;
const u32 delay_large_max = 20;
const u32 cullfragments = 4;

#if RENDER == R_R4
void light::vis_prepare()
{
	PIX_EVENT(LIGHT_PREPARE);
	if (int(indirect_photons) != ps_r2_GI_photons)
		gi_generate();

	if (!flags.bActive)
	{
		vis.visible = false;
		vis.pending = false;
		return;
	}
	if (!flags.bOccq || flags.bHudMode)
	{
		vis.visible = true;
		vis.pending = false;
		return;
	}

	u32 frame = Device.dwFrame;
	if (frame < vis.frame2test)
		return; // Not time to test

	// Don't issue queries from inside the bounding sphere. CULL_CCW culls all
	// back-facing surfaces from inside, returning 0 fragments and falsely hiding
	// the light when that stale result is later processed with always=false.
	vis.distance = Device.vCameraPosition.distance_to(SpatialComponent->spatial.sphere.P);
	if (vis.distance < SpatialComponent->spatial.sphere.R)
		return;

	if (vis.r4_queries.empty())
		vis.queryframe = frame;

	if (frame != vis.queryframe)
		return; // Queries have already been sent

	R_occlusion::occq_try_result r;
	xform_calc();
	vis.query_order = RImplementation.occq_begin(vis.query_id);
	vis.r4_queries.push_back({ vis.query_id, r });
	RImplementation.Target->draw_occq_volume(this);
	RImplementation.occq_end(vis.query_id);
}

void light::vis_update()
{
	auto frame = Device.dwFrame;

	auto light_to_player = Fvector(Device.vCameraPosition).sub(position);
	auto distance = light_to_player.magnitude();
	// Use the spatial bounding sphere (not just apex range) so spot/omnipart lights
	// whose sphere center is offset from position are handled correctly.
	// 1.5x margin absorbs the 1-2 frame GPU query latency at the sphere boundary.
	auto inside_dist = Device.vCameraPosition.distance_to(SpatialComponent->spatial.sphere.P) < SpatialComponent->spatial.sphere.R * 1.5f;
	auto inside_fov = acos(clampr(light_to_player.normalize_safe().dotproduct(Fvector(direction).normalize_safe()), -1.f, 1.f)) < deg2rad(120.0f * 0.5f);
	auto critical_dist = distance < 1.0f;

	auto always = !flags.bOccq || flags.bHudMode || critical_dist || (inside_dist && inside_fov);

	if (scope_debug >= 3)
	{
		auto p = Fvector(direction).normalize_safe().mul(range).add(position);
		auto c = color_rgba_f(inside_dist && inside_fov, inside_dist && !inside_fov, inside_fov && !inside_dist, 1.0f);
		CDebugRenderer().draw_aabb(p, 0.05f, 0.05f, 0.05f, c, false);
	}

	if (always)
	{
		vis.visible = true;
		vis.visible_frags = float(RImplementation.Target->Width * RImplementation.Target->Height);
	}

	if (vis.r4_queries.empty())
		return;

	// Non blocking vis check.
	// Possible minor delays with light visibility changes
	for (auto& q : vis.r4_queries)
	{
		if (!q.second.complete)
		{
			auto r = RImplementation.occq_try_get(q.first);
			if (r.complete) q.second = r;
			else return;
		}
	}

	if (!always) vis.visible_frags = 0;
	for (auto& q : vis.r4_queries)
	{
		vis.visible_frags += float(q.second.fragments);
	}
	vis.r4_queries.clear();

	vis.visible = vis.visible_frags > cullfragments;
	vis.pending = false;
	if (vis.visible)
	{
		vis.frame2test = frame + ::Random.randI(delay_large_min, delay_large_max);
	}
	else
	{
		vis.frame2test = frame + 1;
	}
}
#else
void light::vis_prepare()
{
    if (int(indirect_photons) != ps_r2_GI_photons)
        gi_generate();

    //	. test is sheduled for future	= keep old result
    //	. test time comes :)
    //		. camera inside light volume	= visible,	shedule for 'small' interval
    //		. perform testing				= ???,		pending
    if (!flags.bActive)
    {
        vis.visible = false;
        vis.pending = false;
        return;
    }
    if (!flags.bOccq || flags.bHudMode)
    {
        vis.visible = true;
        vis.pending = false;
        return;
    }

    u32 frame = Device.dwFrame;
    if (frame < vis.frame2test)
        return;

    float safe_area = VIEWPORT_NEAR;
    {
        float a0 = deg2rad(Device.fFOV * Device.fASPECT * 0.5f);
        float a1 = deg2rad(Device.fFOV * 0.5f);
        float x0 = VIEWPORT_NEAR / _cos(a0);
        float x1 = VIEWPORT_NEAR / _cos(a1);
        float c = _sqrt(x0 * x0 + x1 * x1);
        safe_area = _max(_max(VIEWPORT_NEAR, _max(x0, x1)), c);
    }

    //Msg	("sc[%f,%f,%f]/c[%f,%f,%f] - sr[%f]/r[%f]",VPUSH(spatial.center),VPUSH(position),spatial.radius,range);
    //Msg	("dist:%f, sa:%f",Device.vCameraPosition.distance_to(spatial.center),safe_area);
    bool skiptest = false;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_DONT_TEST_UNSHADOWED) && !flags.bShadow) skiptest = true;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_DONT_TEST_SHADOWED) && flags.bShadow) skiptest = true;
#ifdef USE_DX11
    //if (ps_ssfx_volumetric.x > 0 && flags.bShadow) skiptest = true; // Temp Fix
#endif

    vis.distance = Device.vCameraPosition.distance_to(SpatialComponent->spatial.sphere.P);

    if (skiptest || vis.distance <= (SpatialComponent->spatial.sphere.R * 1.01f + safe_area + (SpatialComponent->spatial.sphere.R * 0.1f))) // small error
    {
        // small error
        vis.visible = true;
        vis.pending = false;
        vis.frame2test = frame + ::Random.randI(delay_small_min, delay_small_max);
        return;
    }

    // testing
    if (vis.pending)
        return;

    vis.pending = true;
    RCache.set_xform_world(m_xform);
    CHK_DX(BeginQuery(vis.Q));
    //	Hack: Igor. Light is visible if it's frutum is visible. (Only for volumetric)
    //	Hope it won't slow down too much since there's not too much volumetric lights
    //	TODO: sort for performance improvement if this technique hurts
    if ((flags.type == IRender_Light::SPOT) && flags.bShadow && flags.bVolumetric)
        RCache.set_Stencil(FALSE);
    else
        RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
    RImplementation.Target->draw_volume(this);
    CHK_DX(EndQuery(vis.Q));
}

void light::vis_update()
{
    //	. not pending	->>> return (early out)
    //	. test-result:	visible:
    //		. shedule for 'large' interval
    //	. test-result:	invisible:
    //		. shedule for 'next-frame' interval

    if (!vis.pending) return;

    u32 frame = Device.dwFrame;
    R_occlusion::occq_result fragments = 0;
    HRESULT hr = GetData(vis.Q, &fragments, sizeof(fragments), 0x1 /*D3D11_ASYNC_GETDATA_DONOTFLUSH*/);

    vis.pending = hr != S_OK;
    if (hr != S_OK)
    {
        vis.frame2test = frame + 1;
        if (hr != S_FALSE)
        {
            // Fail-open on timeout/device/query errors to avoid popping.
            vis.visible = true;
            vis.pending = false;
            vis.frame2test = frame + ::Random.randI(delay_small_min, delay_small_max);
        }  
        return;
    }
    
    vis.visible = (fragments > cullfragments);
    vis.frame2test = vis.visible ? (frame + ::Random.randI(delay_large_min, delay_large_max)) : (frame + 1);
}
#endif
