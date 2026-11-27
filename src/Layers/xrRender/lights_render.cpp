#include "stdafx.h"
#include "../../xrEngine/xr_object.h"
#include "FBasicVisual.h"
#include "SkeletonCustom.h"
#include "../../Include/xrAPI/xrAPI.h"
#include "../../Include/xrRender/DebugRender.h"
#include "xrRender_console.h"
#if RENDER == R_R4
#include "../../xrGame/debug_renderer.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
extern int ps_r4_parallel_cull;
#endif

extern int ps_r2_shadow_omnipart_vischeck;

bool check_grass_shadow(light* L, CFrustum VB)
{
	// Grass shadows are allowed?
	if (ps_ssfx_grass_shadows.x < 3 || !psDeviceFlags2.test(rsGrassShadow))
		return false;

	// Inside the range?
	if (L->vis.distance > ps_ssfx_grass_shadows.z)
		return false;

	// Is in view? L->vis.visible?
	u32 mask = 0xff;
	if (!VB.testSphere(L->position, L->range * 0.6f, mask))
		return false;

	return true;
}

#if RENDER == R_R4
void CRender::render_lights_shadowmaps(light_Package& LP)
{
	PIX_EVENT(SHADOWED_LIGHTS);

	// Merge any pending lights into LP before visibility filtering
	if (!LP_pending.v_shadowed.empty())
	{
		LP.v_shadowed.insert(LP.v_shadowed.end(), LP_pending.v_shadowed.begin(), LP_pending.v_shadowed.end());
		LP_pending.v_shadowed.clear();
	}
	if (!LP_pending.v_point.empty())
	{
		LP.v_point.insert(LP.v_point.end(), LP_pending.v_point.begin(), LP_pending.v_point.end());
		LP_pending.v_point.clear();
	}
	if (!LP_pending.v_spot.empty())
	{
		LP.v_spot.insert(LP.v_spot.end(), LP_pending.v_spot.begin(), LP_pending.v_spot.end());
		LP_pending.v_spot.clear();
	}

	stats.ls_shadowed_in += (u32)LP.v_shadowed.size();
	stats.ls_unshadowed_point_in += (u32)LP.v_point.size();
	stats.ls_unshadowed_spot_in += (u32)LP.v_spot.size();
	stats.ls_shadowed_peak_in = _max(stats.ls_shadowed_peak_in, (u32)LP.v_shadowed.size());

	{
		PIX_EVENT(PHASE_VIS_UPDATE);
		xr_vector<light*>& source = LP.v_shadowed;
		source.erase(std::remove_if(source.begin(), source.end(), [](light* L)
		{
			if (L->m_parent)
			{
				if (ps_r2_shadow_omnipart_vischeck)
				{
					L->vis_update();
				}
				else if (L->m_parent->omnipart[0] == L)
				{
					L->m_parent->vis_update();
					for (int f = 0; f < 6; f++)
					{
						L->m_parent->omnipart[f]->vis.pending = L->m_parent->vis.pending;
						L->m_parent->omnipart[f]->vis.visible = L->m_parent->vis.visible;
					}
				}
			}
			else
				L->vis_update();

			if (!L->vis.visible)
			{
				if (scope_debug >= 3)
					CDebugRenderer().draw_line(Fmatrix(), L->position, Fvector(L->direction).mul(L->range).add(L->position), 0xff999999, false);
				RImplementation.stats.ls_shadowed_invisible_skipped++;
				return true;
			}

			L->optimize_smap_size();
			return false;
		}), source.end());

		stats.ls_shadowed_after_vis += (u32)source.size();
		stats.ls_shadowed_peak_after_vis = _max(stats.ls_shadowed_peak_after_vis, (u32)source.size());
	}

	// Update visibility once for unshadowed lights before SVP/Main accumulation
	for (light* L : LP.v_point)
		L->vis_update();
	for (light* L : LP.v_spot)
		L->vis_update();

	stats.l_shadowed = (u32)LP.v_shadowed.size();
	stats.l_unshadowed = (u32)(LP.v_point.size() + LP.v_spot.size());
	stats.l_total = stats.l_shadowed + stats.l_unshadowed;

	// Only pre-pack all lights into one shared atlas when SVP is active;
	// hip-fire uses multi-batch pooling in render_lights() at full shadow resolution.
	if (!Device.m_SecondViewport.IsSVPActive())
		return;

	Target->phase_smap_spot_clear(Target->rt_smap_depth);

	if (LP.v_shadowed.empty())
		return;

	std::sort(LP.v_shadowed.begin(), LP.v_shadowed.end(), [](light* _1, light* _2) { return _1->X.S.size > _2->X.S.size; });

	// Rebuild single combined atlas until all lights fit (shared between SVP and Main)
	bool success = false;
	float size_factor = 1.0f;
	for (int attempt = 0; !success && attempt < 32; ++attempt)
	{
		success = true;
		LP_smap_pool.initialize(o.smapsize);
		for (light* L : LP.v_shadowed)
		{
			SMAP_Rect R;
			u32 req_size = _max(16u, u32(L->X.S.size * size_factor));
			if (LP_smap_pool.push(R, req_size))
			{
				L->X.S.posX = R.min.x;
				L->X.S.posY = R.min.y;
			}
			else
			{
				success = false;
				break;
			}
		}
		if (!success)
			size_factor *= 0.8f;
	}

	// Pre-cull static shadow geometry in parallel across all lights needing a rebuild
	{
		static xr_vector<light*> rebuild_lights;
		rebuild_lights.clear();
		for (light* L : LP.v_shadowed)
		{
			const bool hasDecor = (L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy());
			if (!hasDecor && L->m_moving_frames < 32u)
				rebuild_lights.push_back(L);
		}
		if (!rebuild_lights.empty())
		{
			phase = PHASE_SMAP;
			if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled() && rebuild_lights.size() >= 2)
			{
				tbb::parallel_for(tbb::blocked_range<size_t>(0, rebuild_lights.size(), 1),
					[](const tbb::blocked_range<size_t>& r)
					{
						for (size_t i = r.begin(); i < r.end(); ++i)
						{
							light* L = rebuild_lights[i];
							L->GMLight.RGraph.clear_static<false>();
							L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
							L->GMLight.r_dsgraph_capture_static();
							L->m_moving_frames++;
						}
					});
			}
			else
			{
				for (light* L : rebuild_lights)
				{
					L->GMLight.RGraph.clear_static<false>();
					L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
					L->GMLight.r_dsgraph_capture_static();
					L->m_moving_frames++;
				}
			}
		}
	}

	// Render the shadowmaps into the shared atlas
	for (light* L : LP.v_shadowed)
	{
		stats.s_used++;

		// Make sure to use the scaled size we computed above
		L->X.S.size = _max(16u, u32(L->X.S.size * size_factor));

		if (scope_debug >= 3)
		{
			float pixels = float(Device.dwWidth * Device.dwHeight);
			auto c = L->vis.visible
				? color_argb_f(1.f, float(L->vis.visible_frags) / pixels * 0.75f + 0.25f, 0.f, 0.f)
				: 0xffcccccc;
			CDebugRenderer().draw_line(Fmatrix(), L->position, Fvector(L->direction).mul(L->range).add(L->position), c, false);
		}

		PIX_EVENT(RENDER_SHADOWS);
		phase = PHASE_SMAP;
		bool decorative_light = false;
		{
			if ((L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy()))
			{
				for (int f = 0; f < 6; f++)
				{
					if (L->decor_object[f] && !L->decor_object[f]->getDestroy())
					{
						L->decor_object[f]->renderable_Render(&L->GMLight);
						decorative_light = true;
					}
				}
			}
			else
			{
				L->GMLight.r_dsgraph_capture_dynamic(L->ignore_object);
			}
		}

		bool bDeffered_Shadows = L->GMLight.RGraph.mapStaticPasses[0][0].size() || L->GMLight.RGraph.mapDynamicPasses[0][0].size();
		bool bForward_Shadows = L->GMLight.RGraph.mapStaticPasses[1][0].size() || L->GMLight.RGraph.mapDynamicPasses[1][0].size() || L->GMLight.RGraph.mapStaticSorted.Sorted.size() || L->GMLight.RGraph.mapDynamicSorted.Sorted.size();
		if (bDeffered_Shadows || bForward_Shadows)
		{
			stats.s_merged++;
			Target->phase_smap_spot(L);
			RCache.set_xform_world(Fidentity);
			RCache.set_xform_view(L->X.S.view);
			RCache.set_xform_project(L->X.S.project);
			L->GMLight.r_dsgraph_render_static(0, false);
			L->GMLight.r_dsgraph_render_dynamic(0, true);
			if (Details && Details->dtFS && check_grass_shadow(L, ViewBase) && L->flags.bShadow && !decorative_light)
			{
				Details->fade_distance = -1; // Use light position to calc "fade"
				Details->light_position.set(L->position);
				Details->hw_Render(L);
			}

			L->X.S.transluent = FALSE;
			if (bForward_Shadows)
			{
				L->X.S.transluent = TRUE;
				Target->phase_smap_spot_tsh(L);

				L->GMLight.r_dsgraph_render_static(1, false);
				L->GMLight.r_dsgraph_render_dynamic(1, true);

				L->GMLight.r_dsgraph_render_sorted(); // strict-sorted geoms
			}
		}
		else
		{
			stats.s_finalclip++;
		}
	}

	// Restore XForms
	RCache.set_xform_world(Fidentity);
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
}

void CRender::render_lights(light_Package& LP)
{
	auto accum_shadowed_light = [this](light* L)
	{
		Target->accum_spot(L);
		render_indirect(L);
		if (L->flags.bVolumetric && RImplementation.o.advancedpp && ps_r2_ls_flags.is(R2FLAG_VOLUMETRIC_LIGHTS))
		{
			PIX_EVENT(ACCUM_VOLUMETRIC);
			float w = float(Device.dwWidth);
			float h = float(Device.dwHeight);

			if (RImplementation.o.ssfx_volumetric)
				Target->set_viewport_size(HW.pContext, w / RImplementation.o.volsize, h / RImplementation.o.volsize);

			if (ps_pfx_volumetric_mode == 1)
				Target->accum_volumetric_lv(L);
			else
				Target->accum_volumetric(L);

			if (RImplementation.o.ssfx_volumetric)
				Target->set_viewport_size(HW.pContext, w, h);
		}
	};

	if (Device.m_SecondViewport.IsSVPActive())
	{
		PIX_EVENT(SHADOWED_LIGHTS);
		Target->phase_accumulator();
		if (!LP.v_shadowed.empty())
		{
			PROF_EVENT("ACCUM_SPOT", PROF_CAT(Rendering));
			PIX_EVENT(SPOT_LIGHTS_ACCUM_SH);
			stats.ls_shadowed_rendered += (u32)LP.v_shadowed.size();
			for (light* L : LP.v_shadowed)
				accum_shadowed_light(L);
		}
	}
	else if (!LP.v_shadowed.empty())
	{
		PIX_EVENT(SHADOWED_LIGHTS);

		static xr_vector<light*> refactored;
		refactored.clear();
		{
			PIX_EVENT(PHASE_CALC_POOLS);
			xr_vector<light*>& source = LP.v_shadowed;
			const u32 total = (u32)source.size();

			for (u16 smap_ID = 0; refactored.size() != total; smap_ID++)
			{
				LP_smap_pool.initialize(o.smapsize);
				std::sort(source.begin(), source.end(), [](light* _1, light* _2) { return _1->X.S.size > _2->X.S.size; });
				source.erase(std::remove_if(source.begin(), source.end(), [smap_ID](light* L)
				{
					SMAP_Rect R;
					if (RImplementation.LP_smap_pool.push(R, L->X.S.size))
					{
						L->X.S.posX = R.min.x;
						L->X.S.posY = R.min.y;
						L->vis.smap_ID = smap_ID;
						refactored.push_back(L);
						return true;
					}
					return false;
				}), source.end());
			}
			std::reverse(refactored.begin(), refactored.end());
			LP.v_shadowed.swap(refactored);
		}

		{
			static xr_vector<light*> rebuild_lights;
			rebuild_lights.clear();
			for (light* L : LP.v_shadowed)
			{
				const bool hasDecor = (L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy());
				if (!hasDecor && L->m_moving_frames < 32u)
					rebuild_lights.push_back(L);
			}
			if (!rebuild_lights.empty())
			{
				phase = PHASE_SMAP;
				if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled() && rebuild_lights.size() >= 2)
				{
					tbb::parallel_for(tbb::blocked_range<size_t>(0, rebuild_lights.size(), 1),
						[](const tbb::blocked_range<size_t>& r)
						{
							for (size_t i = r.begin(); i < r.end(); ++i)
							{
								light* L = rebuild_lights[i];
								L->GMLight.RGraph.clear_static<false>();
								L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
								L->GMLight.r_dsgraph_capture_static();
								L->m_moving_frames++;
							}
						});
				}
				else
				{
					for (light* L : rebuild_lights)
					{
						L->GMLight.RGraph.clear_static<false>();
						L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
						L->GMLight.r_dsgraph_capture_static();
						L->m_moving_frames++;
					}
				}
			}
		}

		static xr_vector<light*> L_spot_s;
		while (!LP.v_shadowed.empty())
		{
			L_spot_s.clear();
			{
				PIX_EVENT(GENERATE_SHMAPS);
				Target->phase_smap_spot_clear(Target->rt_smap_depth);
				xr_vector<light*>& source = LP.v_shadowed;
				const u16 sid = source.back()->vis.smap_ID;

				while (!source.empty() && source.back()->vis.smap_ID == sid)
				{
					light* L = source.back();
					source.pop_back();
					stats.s_used++;

					PIX_EVENT(RENDER_SHADOWS);
					phase = PHASE_SMAP;
					bool decorative_light = false;
					if ((L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy()))
					{
						for (int f = 0; f < 6; f++)
						{
							if (L->decor_object[f] && !L->decor_object[f]->getDestroy())
							{
								L->decor_object[f]->renderable_Render(&L->GMLight);
								decorative_light = true;
							}
						}
					}
					else
					{
						L->GMLight.r_dsgraph_capture_dynamic(L->ignore_object);
					}

					const bool bDeffered_Shadows = L->GMLight.RGraph.mapStaticPasses[0][0].size() || L->GMLight.RGraph.mapDynamicPasses[0][0].size();
					const bool bForward_Shadows = L->GMLight.RGraph.mapStaticPasses[1][0].size() || L->GMLight.RGraph.mapDynamicPasses[1][0].size() || L->GMLight.RGraph.mapStaticSorted.Sorted.size() || L->GMLight.RGraph.mapDynamicSorted.Sorted.size();
					if (bDeffered_Shadows || bForward_Shadows)
					{
						stats.s_merged++;
						L_spot_s.push_back(L);
						Target->phase_smap_spot(L);
						RCache.set_xform_world(Fidentity);
						RCache.set_xform_view(L->X.S.view);
						RCache.set_xform_project(L->X.S.project);
						L->GMLight.r_dsgraph_render_static(0, false);
						L->GMLight.r_dsgraph_render_dynamic(0, true);
						if (Details && Details->dtFS && check_grass_shadow(L, ViewBase) && L->flags.bShadow && !decorative_light)
						{
							Details->fade_distance = -1;
							Details->light_position.set(L->position);
							Details->hw_Render(L);
						}

						L->X.S.transluent = FALSE;
						if (bForward_Shadows)
						{
							L->X.S.transluent = TRUE;
							Target->phase_smap_spot_tsh(L);
							L->GMLight.r_dsgraph_render_static(1, false);
							L->GMLight.r_dsgraph_render_dynamic(1, true);
							L->GMLight.r_dsgraph_render_sorted();
						}
					}
					else
					{
						stats.s_finalclip++;
						if (L->flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))
							L_spot_s.push_back(L);
					}
				}
			}

			RCache.set_xform_world(Fidentity);
			RCache.set_xform_view(Device.mView);
			RCache.set_xform_project(Device.mProject);

			if (!L_spot_s.empty())
			{
				PROF_EVENT("ACCUM_SPOT", PROF_CAT(Rendering));
				PIX_EVENT(SPOT_LIGHTS_ACCUM_SH);
				Target->phase_accumulator();
				stats.ls_shadowed_rendered += (u32)L_spot_s.size();
				for (light* L : L_spot_s)
					accum_shadowed_light(L);
			}
		}
	}

	Target->phase_accumulator();

	{
		PIX_EVENT(UNSHADOWED_LIGHTS);
		{
			PIX_EVENT(POINT_LIGHTS_ACCUM_UNSH);
			if (!LP.v_point.empty())
			{
				for (light* L : LP.v_point)
				{
					if (!L->vis.visible)
						continue;

					Target->accum_point(L);
					++stats.ls_unshadowed_point_rendered;
					render_indirect(L);
				}
			}
		}
		{
			PIX_EVENT(SPOT_LIGHTS_ACCUM_UNSH);
			if (!LP.v_spot.empty())
			{
				for (light* L : LP.v_spot)
				{
					if (!L->vis.visible)
						continue;

					Target->accum_spot(L);
					++stats.ls_unshadowed_spot_rendered;
					render_indirect(L);
				}
			}
		}
	}
}

void CRender::extract_lights_for_rt()
{
	PROF_EVENT("Extract Lights", PROF_CAT(Visibility));

	// Merge pending lights into LP_normal before visibility and culling.
	if (!LP_pending.v_shadowed.empty())
	{
		LP_normal.v_shadowed.insert(LP_normal.v_shadowed.end(), LP_pending.v_shadowed.begin(), LP_pending.v_shadowed.end());
		LP_pending.v_shadowed.clear();
	}
	if (!LP_pending.v_point.empty())
	{
		LP_normal.v_point.insert(LP_normal.v_point.end(), LP_pending.v_point.begin(), LP_pending.v_point.end());
		LP_pending.v_point.clear();
	}
	if (!LP_pending.v_spot.empty())
	{
		LP_normal.v_spot.insert(LP_normal.v_spot.end(), LP_pending.v_spot.begin(), LP_pending.v_spot.end());
		LP_pending.v_spot.clear();
	}

	stats.ls_shadowed_in += (u32)LP_normal.v_shadowed.size();
	stats.ls_unshadowed_point_in += (u32)LP_normal.v_point.size();
	stats.ls_unshadowed_spot_in += (u32)LP_normal.v_spot.size();
	stats.ls_shadowed_peak_in = _max(stats.ls_shadowed_peak_in, (u32)LP_normal.v_shadowed.size());

	// Visibility culling (runs in sync window while HW.pContext is idle).
	{
		xr_vector<light*>& source = LP_normal.v_shadowed;
		source.erase(std::remove_if(source.begin(), source.end(), [](light* L)
		{
			if (L->m_parent)
			{
				if (ps_r2_shadow_omnipart_vischeck)
				{
					L->vis_update();
				}
				else if (L->m_parent->omnipart[0] == L)
				{
					L->m_parent->vis_update();
					for (int f = 0; f < 6; f++)
					{
						L->m_parent->omnipart[f]->vis.pending = L->m_parent->vis.pending;
						L->m_parent->omnipart[f]->vis.visible = L->m_parent->vis.visible;
					}
				}
			}
			else
				L->vis_update();

			if (!L->vis.visible)
			{
				RImplementation.stats.ls_shadowed_invisible_skipped++;
				return true;
			}

			L->optimize_smap_size();
			return false;
		}), source.end());

		stats.ls_shadowed_after_vis += (u32)source.size();
		stats.ls_shadowed_peak_after_vis = _max(stats.ls_shadowed_peak_after_vis, (u32)source.size());
	}

	for (light* L : LP_normal.v_point)
		L->vis_update();
	for (light* L : LP_normal.v_spot)
		L->vis_update();

	stats.l_shadowed = (u32)LP_normal.v_shadowed.size();
	stats.l_unshadowed = (u32)(LP_normal.v_point.size() + LP_normal.v_spot.size());
	stats.l_total = stats.l_shadowed + stats.l_unshadowed;

	if (!LP_normal.v_shadowed.empty())
	{
		// Pack shadowmap batches cleanly during extraction.
		static xr_vector<light*> refactored;
		refactored.clear();
		xr_vector<light*>& source = LP_normal.v_shadowed;
		const u32 total = (u32)source.size();

		for (u16 smap_ID = 0; refactored.size() != total; smap_ID++)
		{
			LP_smap_pool.initialize(o.smapsize);
			std::sort(source.begin(), source.end(), [](light* _1, light* _2) { return _1->X.S.size > _2->X.S.size; });
			source.erase(std::remove_if(source.begin(), source.end(), [smap_ID](light* L)
			{
				SMAP_Rect R;
				if (RImplementation.LP_smap_pool.push(R, L->X.S.size))
				{
					L->X.S.posX = R.min.x;
					L->X.S.posY = R.min.y;
					L->vis.smap_ID = smap_ID;
					refactored.push_back(L);
					return true;
				}
				return false;
			}), source.end());
		}
		std::reverse(refactored.begin(), refactored.end());
		LP_normal.v_shadowed.swap(refactored);

		// Parallel static shadow culling across lights needing static rebuild.
		static xr_vector<light*> rebuild_lights;
		rebuild_lights.clear();
		for (light* L : LP_normal.v_shadowed)
		{
			const bool hasDecor = (L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy());
			if (!hasDecor && L->m_moving_frames < 32u)
				rebuild_lights.push_back(L);
		}
		if (!rebuild_lights.empty())
		{
			phase = PHASE_SMAP;
			if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled() && rebuild_lights.size() >= 2)
			{
				tbb::parallel_for(tbb::blocked_range<size_t>(0, rebuild_lights.size(), 1),
					[](const tbb::blocked_range<size_t>& r)
					{
						for (size_t i = r.begin(); i < r.end(); ++i)
						{
							light* L = rebuild_lights[i];
							L->GMLight.RGraph.clear_static<false>();
							L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
							L->GMLight.r_dsgraph_capture_static();
							L->m_moving_frames++;
						}
					});
			}
			else
			{
				for (light* L : rebuild_lights)
				{
					L->GMLight.RGraph.clear_static<false>();
					L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
					L->GMLight.r_dsgraph_capture_static();
					L->m_moving_frames++;
				}
			}
		}

		// Capture dynamic shadow casters now while g_SpatialSpace and CObjects are quiescent.
		phase = PHASE_SMAP;
		for (light* L : LP_normal.v_shadowed)
		{
			const bool hasDecor = (L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy());
			if (hasDecor)
			{
				for (int f = 0; f < 6; f++)
				{
					if (L->decor_object[f] && !L->decor_object[f]->getDestroy())
						L->decor_object[f]->renderable_Render(&L->GMLight);
				}
			}
			else
			{
				L->GMLight.r_dsgraph_capture_dynamic(L->ignore_object);
			}
		}
	}
}

void CRender::render_lights_submit(light_Package& LP)
{
	const auto& RV = Device.m_rt_view;
	auto accum_shadowed_light = [this, &RV](light* L)
	{
		Target->accum_spot(L);
		render_indirect(L);
		if (L->flags.bVolumetric && RImplementation.o.advancedpp && ps_r2_ls_flags.is(R2FLAG_VOLUMETRIC_LIGHTS))
		{
			PIX_EVENT(ACCUM_VOLUMETRIC);
			float w = float(RV.dwWidth);
			float h = float(RV.dwHeight);

			if (RImplementation.o.ssfx_volumetric)
				Target->set_viewport_size(HW.pContext, w / RImplementation.o.volsize, h / RImplementation.o.volsize);

			if (ps_pfx_volumetric_mode == 1)
				Target->accum_volumetric_lv(L);
			else
				Target->accum_volumetric(L);

			if (RImplementation.o.ssfx_volumetric)
				Target->set_viewport_size(HW.pContext, w, h);
		}
	};

	if (!LP.v_shadowed.empty())
	{
		PIX_EVENT(SHADOWED_LIGHTS);
		static xr_vector<light*> L_spot_s;
		while (!LP.v_shadowed.empty())
		{
			L_spot_s.clear();
			{
				PIX_EVENT(GENERATE_SHMAPS);
				Target->phase_smap_spot_clear(Target->rt_smap_depth);
				xr_vector<light*>& source = LP.v_shadowed;
				const u16 sid = source.back()->vis.smap_ID;

				while (!source.empty() && source.back()->vis.smap_ID == sid)
				{
					light* L = source.back();
					source.pop_back();
					stats.s_used++;

					PIX_EVENT(RENDER_SHADOWS);
					phase = PHASE_SMAP;
					const bool decorative_light = (L->decor_object[0] != nullptr) || (L->decor_object[1] != nullptr) || (L->decor_object[2] != nullptr) || (L->decor_object[3] != nullptr) || (L->decor_object[4] != nullptr) || (L->decor_object[5] != nullptr);
					const bool bDeffered_Shadows = L->GMLight.RGraph.mapStaticPasses[0][0].size() || L->GMLight.RGraph.mapDynamicPasses[0][0].size();
					const bool bForward_Shadows = L->GMLight.RGraph.mapStaticPasses[1][0].size() || L->GMLight.RGraph.mapDynamicPasses[1][0].size() || L->GMLight.RGraph.mapStaticSorted.Sorted.size() || L->GMLight.RGraph.mapDynamicSorted.Sorted.size();
					if (bDeffered_Shadows || bForward_Shadows)
					{
						stats.s_merged++;
						L_spot_s.push_back(L);
						Target->phase_smap_spot(L);
						RCache.set_xform_world(Fidentity);
						RCache.set_xform_view(L->X.S.view);
						RCache.set_xform_project(L->X.S.project);
						L->GMLight.r_dsgraph_render_static(0, false);
						L->GMLight.r_dsgraph_render_dynamic(0, true);
						if (Details && Details->dtFS && check_grass_shadow(L, ViewBase) && L->flags.bShadow && !decorative_light)
						{
							Details->fade_distance = -1;
							Details->light_position.set(L->position);
							Details->hw_Render(L);
						}

						L->X.S.transluent = FALSE;
						if (bForward_Shadows)
						{
							L->X.S.transluent = TRUE;
							Target->phase_smap_spot_tsh(L);
							L->GMLight.r_dsgraph_render_static(1, false);
							L->GMLight.r_dsgraph_render_dynamic(1, true);
							L->GMLight.r_dsgraph_render_sorted();
						}
					}
					else
					{
						stats.s_finalclip++;
						if (L->flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))
							L_spot_s.push_back(L);
					}
				}
			}

			RCache.set_xform_world(Fidentity);
			RCache.set_xform_view(RV.mView);
			RCache.set_xform_project(RV.mProject);

			if (!L_spot_s.empty())
			{
				PROF_EVENT("ACCUM_SPOT", PROF_CAT(Rendering));
				PIX_EVENT(SPOT_LIGHTS_ACCUM_SH);
				Target->phase_accumulator();
				stats.ls_shadowed_rendered += (u32)L_spot_s.size();
				auto accum_batch = [&]()
				{
					for (light* L : L_spot_s)
						accum_shadowed_light(L);
				};
				accum_batch();
				if (Device.m_SecondViewport.dual_accum)
					Device.m_SecondViewport.dual_accum(accum_batch);
			}
		}
	}

	Target->phase_accumulator();

	{
		PIX_EVENT(UNSHADOWED_LIGHTS);
		{
			PIX_EVENT(POINT_LIGHTS_ACCUM_UNSH);
			if (!LP.v_point.empty())
			{
				auto accum_pts = [&]()
				{
					for (light* L : LP.v_point)
					{
						if (!L->vis.visible)
							continue;
						Target->accum_point(L);
						++stats.ls_unshadowed_point_rendered;
						render_indirect(L);
					}
				};
				accum_pts();
				if (Device.m_SecondViewport.dual_accum)
					Device.m_SecondViewport.dual_accum(accum_pts);
				LP.v_point.clear();
			}
		}
		{
			PIX_EVENT(SPOT_LIGHTS_ACCUM_UNSH);
			if (!LP.v_spot.empty())
			{
				auto accum_spots = [&]()
				{
					for (light* L : LP.v_spot)
					{
						if (!L->vis.visible)
							continue;
						Target->accum_spot(L);
						++stats.ls_unshadowed_spot_rendered;
						render_indirect(L);
					}
				};
				accum_spots();
				if (Device.m_SecondViewport.dual_accum)
					Device.m_SecondViewport.dual_accum(accum_spots);
				LP.v_spot.clear();
			}
		}
	}
}
#else
void CRender::render_lights(light_Package& LP)
{
	stats.ls_shadowed_in += (u32)LP.v_shadowed.size();
	stats.ls_unshadowed_point_in += (u32)LP.v_point.size();
	stats.ls_unshadowed_spot_in += (u32)LP.v_spot.size();
	stats.ls_shadowed_peak_in = _max(stats.ls_shadowed_peak_in, (u32)LP.v_shadowed.size());
	//////////////////////////////////////////////////////////////////////////

	// pip build the scope cone once so the mirrored svp blends can drop a light whose sphere never
	// meets it, main accumulation below always runs the full list, only the svp mirror is filtered
	CFrustum svp_cone;
	const bool svp_cull_lights = Device.m_SecondViewport.dual_accum && ps_r__svp_light_cull;
	if (svp_cull_lights)
	{
		Fmatrix svp_full;
		svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		svp_cone.CreateFromMatrix(svp_full, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	}
	static xr_vector<light*> svp_mirror_subset;
	// returns the batch subset inside the cone (cull off returns the whole batch), empty = skip the mirror
	auto svp_cone_subset = [&](xr_vector<light*>& src) -> xr_vector<light*>&
	{
		if (!svp_cull_lights)
			return src;
		svp_mirror_subset.clear();
		for (light* L : src)
		{
			Fvector wc = L->position;
			if (svp_cone.testSphere_dirty(wc, L->range))
			{
				svp_mirror_subset.push_back(L);
				if (ps_r__svp_stats) ++svp_stats_lights_mirrored; // overlay cone-cull tally
				if (!svp_ledger_lights_mirrored) svp_ledger_lights_mirrored = 1;
			}
			else
			{
				if (ps_r__svp_stats) ++svp_stats_lights_skipped;
				if (!svp_ledger_lights_skipped) svp_ledger_lights_skipped = 1;
			}
		}
		return svp_mirror_subset;
	};

	{
#if defined(USE_DX10) || defined(USE_DX11)
		PIX_EVENT(SHADOWED_LIGHTS);
#endif
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(PHASE_VIS_UPDATE);
#endif
			xr_vector<light*>& source = LP.v_shadowed;
			source.erase(std::remove_if(source.begin(), source.end(), [](light* L)
			{
				if(L->m_parent)
				{
					if (ps_r2_shadow_omnipart_vischeck)
					{
						L->vis_update();
					}
					else if(L->m_parent->omnipart[0] == L)
					{
						L->m_parent->vis_update();
						for (int f = 0; f < 6; f++)
						{
							L->m_parent->omnipart[f]->vis.pending = L->m_parent->vis.pending;
							L->m_parent->omnipart[f]->vis.visible = L->m_parent->vis.visible;
						}
					}
				}
				else
					L->vis_update();
				if (!L->vis.visible)
				{
					RImplementation.stats.ls_shadowed_invisible_skipped++;
					if (scope_debug >= 3)
					{
						// pip grey world direction/range vector for each culled shadowed light (r__scope_debug 3+)
						Fvector v[2] = { L->position, Fvector(L->direction).mul(L->range).add(L->position) };
						u16 idx[2] = { 0, 1 };
						DRender->add_lines(v, 2, idx, 1, 0xff999999, false);
					}
					return true;
				}

				L->optimize_smap_size();

				return false;
			}), source.end());
			stats.ls_shadowed_after_vis += (u32)source.size();
			stats.ls_shadowed_peak_after_vis = _max(stats.ls_shadowed_peak_after_vis, (u32)source.size());
		}

		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(PHASE_CALC_POOLS);
#endif
			xr_vector<light*>& source = LP.v_shadowed;
			static xr_vector<light*> refactored;
			refactored.clear();
			u32 total = (u32)source.size();

			for (u16 smap_ID = 0; refactored.size() != total; smap_ID++)
			{
				// one light placed per pass is the worst case, past total passes something never fits
				R_ASSERT2(smap_ID <= total, "shadow atlas layout spin, a light's shadow map exceeds o.smapsize");
				LP_smap_pool.initialize(o.smapsize);
				std::sort(source.begin(), source.end(), [](light* _1, light* _2) {return _1->X.S.size > _2->X.S.size; });
				source.erase(std::remove_if(source.begin(), source.end(), [smap_ID](light* L)
				{
					SMAP_Rect R;
					if (RImplementation.LP_smap_pool.push(R, L->X.S.size))
					{
						L->X.S.posX = R.min.x;
						L->X.S.posY = R.min.y;
						L->vis.smap_ID = smap_ID;
						refactored.push_back(L);
						return true;
					}
					return false;
				}), source.end());
			}

			std::reverse(refactored.begin(), refactored.end());
			LP.v_shadowed = refactored;
		}

		//////////////////////////////////////////////////////////////////////////
		// sort lights by importance???
		// while (has_any_lights_that_cast_shadows) {
		//		if (has_point_shadowed)		->	generate point shadowmap
		//		if (has_spot_shadowed)		->	generate spot shadowmap
		//		switch-to-accumulator
		//		if (has_point_unshadowed)	-> 	accum point unshadowed
		//		if (has_spot_unshadowed)	-> 	accum spot unshadowed
		//		if (was_point_shadowed)		->	accum point shadowed
		//		if (was_spot_shadowed)		->	accum spot shadowed
		//	}
		//	if (left_some_lights_that_doesn't cast shadows)
		//		accumulate them
		while (!LP.v_shadowed.empty())
		{
			// if (has_spot_shadowed)
			static xr_vector<light*> L_spot_s;
			{
#if defined(USE_DX10) || defined(USE_DX11)
				PIX_EVENT(GENERATE_SHMAPS);
#endif
				// generate spot shadowmap
				Target->phase_smap_spot_clear();
				xr_vector<light*>& source = LP.v_shadowed;
				light* L = source.back();
				u16			sid = L->vis.smap_ID;
				while (!source.empty())
				{
					if (source.empty())		break;
					L = source.back();
					if (L->vis.smap_ID != sid)	break;
					source.pop_back();
					// render
					phase = PHASE_SMAP;
#if defined(USE_DX10) || defined(USE_DX11)
					PIX_EVENT(RENDER_SHADOWS);
#endif
					bool decorative_light = false;
					{
						if ((L->decor_object[0] && !L->decor_object[0]->getDestroy()) || (L->decor_object[1] && !L->decor_object[1]->getDestroy()) || (L->decor_object[2] && !L->decor_object[2]->getDestroy()) || (L->decor_object[3] && !L->decor_object[3]->getDestroy()) || (L->decor_object[4] && !L->decor_object[4]->getDestroy()) || (L->decor_object[5] && !L->decor_object[5]->getDestroy()))
						{
							for (int f = 0; f < 6; f++)
							{
								if (L->decor_object[f] && !L->decor_object[f]->getDestroy())
								{
									L->decor_object[f]->renderable_Render(&L->GMLight);
									decorative_light = true;
								}
							}
						}
						else
						{
							if (L->m_moving_frames<32u)
							{
								L->GMLight.RGraph.clear_static<false>();
								L->GMLight.traverse((CSector*)L->SpatialComponent->spatial.sector, L->X.S.frustum, L->position, L->X.S.combine);
								L->GMLight.r_dsgraph_capture_static();
								L->m_moving_frames++;
							}
							L->GMLight.r_dsgraph_capture_dynamic(L->ignore_object);
						}
					}

					bool bDeffered_Shadows = L->GMLight.RGraph.mapStaticPasses[0][0].size() || L->GMLight.RGraph.mapDynamicPasses[0][0].size();
					bool bForward_Shadows = L->GMLight.RGraph.mapStaticPasses[1][0].size() || L->GMLight.RGraph.mapDynamicPasses[1][0].size() || L->GMLight.RGraph.mapStaticSorted.Sorted.size() || L->GMLight.RGraph.mapDynamicSorted.Sorted.size();
					if (bDeffered_Shadows || bForward_Shadows)
					{
						L_spot_s.push_back(L);
						Target->phase_smap_spot(L);
						RCache.set_xform_world(Fidentity);
						RCache.set_xform_view(L->X.S.view);
						RCache.set_xform_project(L->X.S.project);
						L->GMLight.r_dsgraph_render_static(0, false);
						L->GMLight.r_dsgraph_render_dynamic(0, true);
						if (Details && Details->dtFS && check_grass_shadow(L, ViewBase) && L->flags.bShadow && !decorative_light)
						{
							Details->fade_distance = -1; // Use light position to calc "fade"
							Details->light_position.set(L->position);
							Details->hw_Render(L);
						}
					
						L->X.S.transluent = FALSE;
						if (bForward_Shadows)
						{
							L->X.S.transluent = TRUE;
							Target->phase_smap_spot_tsh(L);
					
							L->GMLight.r_dsgraph_render_static(1, false);
							L->GMLight.r_dsgraph_render_dynamic(1, true);
					
							L->GMLight.r_dsgraph_render_sorted();			// strict-sorted geoms
						}
					}
					else if (L->flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))
					{
						L_spot_s.push_back(L);
					}
				}
			}
			//		if (was_spot_shadowed)		->	accum spot shadowed
			if (!L_spot_s.empty())
			{
				PROF_EVENT("ACCUM_SPOT", PROF_CAT(Rendering));
#if defined(USE_DX10) || defined(USE_DX11)
				PIX_EVENT(SPOT_LIGHTS_ACCUM_SH);
#endif
				stats.ls_shadowed_rendered += (u32)L_spot_s.size();
				// pip shared-shadow, this group's smaps are built on the main atlas, accumulate the
				// group into the main viewport then replay it into the SVP (the hook re-points the atlas
				// at the main maps so the SVP reads them, no second smap render)
				auto accum_group = [&](xr_vector<light*>& list)
				{
					for (light* L : list)
					{
						Target->accum_spot(L);
						render_indirect(L);
						if (L->flags.bVolumetric && RImplementation.o.advancedpp && ps_r2_ls_flags.is(R2FLAG_VOLUMETRIC_LIGHTS))
						{
#if defined(USE_DX10) || defined(USE_DX11)
							PIX_EVENT(ACCUM_VOLUMETRIC);
#endif
#ifdef USE_DX11
							float w = float(Device.dwWidth);
							float h = float(Device.dwHeight);

							if (RImplementation.o.ssfx_volumetric)
								Target->set_viewport_size(HW.pContext, w / RImplementation.o.volsize, h / RImplementation.o.volsize);
#endif

							if (ps_pfx_volumetric_mode == 1)
								Target->accum_volumetric_lv(L);
							else
								Target->accum_volumetric(L);

#ifdef USE_DX11
							// Restore resolution
							if (RImplementation.o.ssfx_volumetric)
								Target->set_viewport_size(HW.pContext, w, h);
#endif
						}
					}
				};
				accum_group(L_spot_s);
				if (Device.m_SecondViewport.dual_accum)
				{
					xr_vector<light*>& mlist = svp_cone_subset(L_spot_s);
					if (!mlist.empty())
						Device.m_SecondViewport.dual_accum([&] { accum_group(mlist); });
				}

				L_spot_s.clear();
			}
		}
	}

	{
#if defined(USE_DX10) || defined(USE_DX11)
		PIX_EVENT(UNSHADOWED_LIGHTS);
#else
        PROF_EVENT("UNSHADOWED_LIGHTS");
#endif
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(POINT_LIGHTS_ACCUM_UNSH);
#endif
			// Point lighting (unshadowed, if left), pip accumulate into both viewports (no smap)
			if (!LP.v_point.empty())
			{
				auto accum_point = [&](xr_vector<light*>& list)
				{
					for (light* L : list)
					{
						L->vis_update();
						if (!L->vis.visible)
							continue;

						Target->accum_point(L);
						++stats.ls_unshadowed_point_rendered;
						render_indirect(L);
					}
				};
				accum_point(LP.v_point);
				if (Device.m_SecondViewport.dual_accum)
				{
					xr_vector<light*>& mlist = svp_cone_subset(LP.v_point);
					if (!mlist.empty())
						Device.m_SecondViewport.dual_accum([&] { accum_point(mlist); });
				}
				LP.v_point.clear();
			}
		}
		{
#if defined(USE_DX10) || defined(USE_DX11)
			PIX_EVENT(SPOT_LIGHTS_ACCUM_UNSH);
#endif
			// Spot lighting (unshadowed, if left), pip accumulate into both viewports (no smap)
			if (!LP.v_spot.empty())
			{
				auto accum_spot = [&](xr_vector<light*>& list)
				{
					for (light* L : list)
					{
						L->vis_update();
						if (!L->vis.visible)
							continue;

						Target->accum_spot(L);
						++stats.ls_unshadowed_spot_rendered;
						render_indirect(L);
					}
				};
				accum_spot(LP.v_spot);
				if (Device.m_SecondViewport.dual_accum)
				{
					xr_vector<light*>& mlist = svp_cone_subset(LP.v_spot);
					if (!mlist.empty())
						Device.m_SecondViewport.dual_accum([&] { accum_spot(mlist); });
				}
				LP.v_spot.clear();
			}
		}
	}


}
#endif

void CRender::render_indirect(light* L)
{
	if (!ps_r2_ls_flags.test(R2FLAG_GI)) return;

	light LIGEN;
	LIGEN.set_type(IRender_Light::REFLECTED);
	LIGEN.set_shadow(false);
	LIGEN.set_cone(PI_DIV_2 * 2.f);

	xr_vector<light_indirect>& Lvec = L->indirect;
	if (Lvec.empty()) return;
	float LE = L->color.intensity();
	for (u32 it = 0; it < Lvec.size(); it++)
	{
		light_indirect& LI = Lvec[it];

		// energy and color
		float LIE = LE * LI.E;
		if (LIE < ps_r2_GI_clip) continue;
		Fvector T;
		T.set(L->color.r, L->color.g, L->color.b).mul(LI.E);
		LIGEN.set_color(T.x, T.y, T.z);

		// geometric
		Fvector L_up, L_right;
		L_up.set(0, 1, 0);
		if (_abs(L_up.dotproduct(LI.D)) > .99f) L_up.set(0, 0, 1);
		L_right.crossproduct(L_up, LI.D).normalize();
		LIGEN.SpatialComponent->spatial.sector = LI.S;
		LIGEN.set_position(LI.P);
		LIGEN.set_rotation(LI.D, L_right);

		// range
		// dist^2 / range^2 = A - has infinity number of solutions
		// approximate energy by linear fallof Emax / (1 + x) = Emin
		float Emax = LIE;
		float Emin = 1.f / 255.f;
		float x = (Emax - Emin) / Emin;
		if (x < 0.1f) continue;
		LIGEN.set_range(x);

		Target->accum_reflected(&LIGEN);
	}
}
