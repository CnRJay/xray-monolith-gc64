#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

#include "../../Include/xrRender/ParticleCustom.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaLOD_A; // lod-out threshold, the overlay counts main-view draws below it
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF bool R4FullDetailRejectSpatial(ISpatial* spatial, bool normalPhase)
{
#if RENDER == R_R4
	if (ps_r4_full_detail_distance_scale >= 1.f || !normalPhase || !spatial || !g_pGamePersistent ||
		!g_pGamePersistent->Environment().CurrentEnv)
		return false;

	const float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	const float fullDetailDistance = std::max(100.f, farPlane * ps_r4_full_detail_distance_scale);
	if (Device.vCameraPosition.distance_to_sqr(spatial->spatial.sphere.P) <=
		_sqr(fullDetailDistance + spatial->spatial.sphere.R))
		return false;

	if (spatial->spatial.type & STYPE_LIGHTSOURCE)
	{
		light* L = (light*)spatial->dcast_Light();
		if (L && L->flags.bHudMode)
			return false;
	}

	return true;
#else
	return false;
#endif
}

// pip svp stats overlay, tallies the cull skips only while the overlay is on
extern int ps_r__svp_stats;
extern u32 svp_stats_ssa_culled;
extern u32 svp_stats_cull_reject;
extern u32 svp_stats_lod_scale;
extern u32 svp_ledger_ssa_culled;
extern u32 svp_ledger_lod_scale;
extern u32 svp_stats_sort_calls;
extern u32 svp_stats_sort_packets;

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

namespace
{
class CCustomFOVInitializer
{
	bool enabled;
	bool near_depth;
	Fmatrix saved_project;
	Fmatrix saved_full;

public:
	explicit CCustomFOVInitializer(float fov, bool adjust_depth = false)
		: enabled(fov != 0.f), near_depth(adjust_depth)
	{
		if (!enabled) return;
		const bool on_rt = CRenderThread::IsOnRenderThread();
		saved_project = RCache.get_xform_project();
		saved_full = on_rt ? Device.m_rt_view.mFullTransform : Device.mFullTransform;
		const float aspect = on_rt ? Device.m_rt_view.fASPECT : Device.fASPECT;
		const float far_p = Device.m_rt_env.valid ? Device.m_rt_env.far_plane : g_pGamePersistent->Environment().CurrentEnv->far_plane;
		Fmatrix project;
		project.build_projection(deg2rad(fov), aspect, R_VIEWPORT_NEAR, far_p);
		if (near_depth) RImplementation.rmNear();
		if (on_rt)
			Device.m_rt_view.mFullTransform.mul(project, Device.m_rt_view.mView);
		else
			Device.mFullTransform.mul(project, Device.mView);
		RCache.set_xform_project(project);
	}

	~CCustomFOVInitializer()
	{
		if (!enabled) return;
		if (near_depth) RImplementation.rmNormal();
		if (CRenderThread::IsOnRenderThread())
			Device.m_rt_view.mFullTransform = saved_full;
		else
			Device.mFullTransform = saved_full;
		RCache.set_xform_project(saved_project);
	}
};
}

// pip SVP LOD, scales the captured main-frustum ssa to the SVP's true pixel coverage,
// armed only for the SVP gbuffer pass
static float s_svp_ssa_scale = 1.f;
static bool s_svp_lod_on = false;
ICF float svp_ssa(float ssa) { return s_svp_lod_on ? ssa * s_svp_ssa_scale : ssa; }

// pip SVP small-object cull, skips items below an ssa threshold pre-scaled for the SVP
// coverage, 0 = off, armed only for the SVP gbuffer pass
static float s_svp_ssa_cull = 0.f;

// armed only around current frame objective HUD lists
static bool s_svp_objective_suppress_wpnfx = false;
static LPCSTR s_svp_objective_hud_bucket = nullptr;

static bool svp_exact_hud_pose_window()
{
#if RENDER == R_R4
	extern bool svp_objective_hud_current();
	return svp_objective_hud_current() && Device.true_pip_on;
#else
	return false;
#endif
}

static bool svp_mode2_hud_window()
{
#if RENDER == R_R4
	extern int scope_svp_enabled;
	auto& vp = Device.m_SecondViewport;
	return scope_svp_enabled >= 2 && Device.true_pip_on && vp.IsSVPFrame();
#else
	return false;
#endif
}

static bool svp_objective_hud_window()
{
#if RENDER == R_R4
	auto& vp = Device.m_SecondViewport;
	return svp_mode2_hud_window() && vp.svp_camera_frame == Device.dwFrame
		&& vp.svp_camera_session == vp.GetSVPSession()
		&& vp.svp_front_use_m > EPS;
#else
	return false;
#endif
}

static bool svp_reject_hud_window()
{
	return svp_mode2_hud_window() && !svp_objective_hud_window();
}

struct SSvpHudPosePass
{
#if RENDER == R_R4
	bool frozen;
	bool history;
#endif
	SSvpHudPosePass(bool active, bool write_history)
	{
#if RENDER == R_R4
		extern bool g_svp_hud_frozen_pass;
		extern bool g_svp_hud_history_write;
		frozen = g_svp_hud_frozen_pass;
		history = g_svp_hud_history_write;
		if (active)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = write_history;
		}
#endif
	}
	~SSvpHudPosePass()
	{
#if RENDER == R_R4
		extern bool g_svp_hud_frozen_pass;
		extern bool g_svp_hud_history_write;
		g_svp_hud_frozen_pass = frozen;
		g_svp_hud_history_write = history;
#endif
	}
};

// pip lod for the SVP weapon drain, the inline lod helpers stay reachable from the drain's own TU
float CDSGraphManager::svp_drain_lod(float ssa, float R) { return calcLOD(svp_ssa(ssa), R); }

void CDSGraphManager::svp_set_lod_scale(float s)
{
	s_svp_ssa_scale = s;
	s_svp_lod_on = (s < 0.999f); // active only when it actually lowers detail
	if (s_svp_lod_on && ps_r__svp_stats) ++svp_stats_lod_scale; // overlay proof the lod scale armed this frame
	if (s_svp_lod_on) svp_ledger_lod_scale = 1; // ledger ever-armed latch
}

// strength scales the LOD-out ssa, cov divides it back to a main-frustum ssa so the loop can test item.ssa
// directly. higher strength culls bigger items, lower cov (low mag) culls more (objects are tiny in the scope)
void CDSGraphManager::svp_set_ssa_cull(float strength, float cov)
{
	s_svp_ssa_cull = (strength > 0.f && cov > 1e-4f) ? (r_ssaGLOD_end * strength / cov) : 0.f;
}

template<typename T, bool Reverse>
void CDSGraphManager::r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear)
{
    if (graph.empty())
        return;

    std::sort(graph.begin(), graph.end());

	for (auto& item : graph)
	{
		if (svp_cull_reject(item.pVisual, item.pMatrix)) { if (ps_r__svp_stats) ++svp_stats_cull_reject; continue; } // pip skip off-cone SVP geometry
		if (s_svp_ssa_cull > 0.f && !item.pMatrix && item.ssa < s_svp_ssa_cull) { if (ps_r__svp_stats) ++svp_stats_ssa_culled; if (!svp_ledger_ssa_culled) svp_ledger_ssa_culled = 1; continue; } // pip skip tiny STATIC clutter only (null matrix), never dynamic NPCs/items (matrix carriers)
		if (ps_r__svp_stats && !Device.m_SecondViewport.m_render_pass_is_svp && item.ssa < r_ssaLOD_A) ++svp_stats_tiny; // overlay count only, main view
		dxRender_Visual* V = item.pVisual;
		VERIFY(V && V->shader._get());
		if (s_svp_objective_suppress_wpnfx)
		{
			SSvpHudAdmission admission;
			svp_classify_objective_hud(V, item.pMatrix, item.hud_role, admission);
			extern int ps_r__svp_cop_diag;
			static u32 s_admission_diag_ms = 0;
			if (admission.candidate && ps_r__svp_cop_diag
				&& Device.dwTimeGlobal - s_admission_diag_ms > 3000)
			{
				s_admission_diag_ms = Device.dwTimeGlobal;
				auto texture = V->GetTexture();
				PipMsg("[SVP-ADMIT] %s queue=%s role=%u t=%.2f rad=%.1fcm R=%.1fcm reason=%s visual=%p root=%p lensVisual=%p lensRoot=%p %s",
					admission.reject ? "SUPPRESS" : "retain",
					s_svp_objective_hud_bucket ? s_svp_objective_hud_bucket : "sorted",
					item.hud_role,
					admission.objective > EPS ? admission.axial / admission.objective : 0.f,
					admission.radial * 100.f, admission.radius * 100.f, admission.reason,
					(void*)V, (void*)item.pMatrix,
					Device.m_SecondViewport.svp_lens_visual,
					Device.m_SecondViewport.svp_lens_root,
					texture ? texture->cName.c_str() : "?");
			}
			if (admission.reject)
				continue;
			IParticleCustom* particle = V->dcast_ParticleCustom();
			if (particle && particle->GetHudMode() && particle->GetWeaponFX())
				continue;
		}
		RCache.set_Element(item.pSE);
		CCustomFOVInitializer projection(item.fov);
		RCache.set_xform_world(*svp_pose_of(item.pMatrix));
		RImplementation.apply_object(item.pObject);
		RImplementation.apply_lmaterial();
		//if (item.b_hud_mode)
		//{
		//	//new feature
		//}
		V->Render(calcLOD(svp_ssa(item.ssa), V->vis.sphere.R));
#if RENDER == R_R4
		if (s_svp_objective_suppress_wpnfx)
		{
			extern void svp_objective_hud_note_draw(u8);
			svp_objective_hud_note_draw(item.hud_role);
		}
#endif
	}

	if (_clear)
		graph.clear();

	RCache.set_xform_world(Fidentity);
}

void CDSGraphManager::r_dsgraph_render_graph(RenderQueueArray& queues, u32 _priority, bool _clear, bool static_geometry)
{
	RCache.set_xform_world(Fidentity);

	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		auto& queue = queues[_priority][iPass];
		if (queue.empty())
			continue;

		// 1. Sort by generated sort key to replicate previous fixed map behaviour
		// the memo skips a queue nothing appended to since its last sort, the counters tally real invocations only
		if (!ps_r__dsgraph_sort_memo || !queue.sorted)
		{
			if (ps_r__svp_stats)
			{
				++svp_stats_sort_calls;
				svp_stats_sort_packets += (u32)queue.size();
			}
			PROF_EVENT("r_dsgraph_sort", PROF_CAT(Rendering));
			if (queue.size() < 4096)
				std::sort(queue.begin(), queue.end());
			else
				xr_parallel_sort(queue.begin(), queue.end());
			queue.sorted = true;
		}

		// 2. Render
		PROF_EVENT("r_dsgraph_submit", PROF_CAT(Rendering));
		vs_type pVS = nullptr;

#if defined(USE_DX10) || defined(USE_DX11)
		gs_type pGS = nullptr;
#endif

		ps_type pPS = nullptr;

#ifdef USE_DX11
		hs_type pHS = nullptr;
		ds_type pDS = nullptr;
#endif
		
		R_constant_table* pCS = nullptr;
		ID3DState* pState = nullptr;
		STextureList* pTextures = nullptr;

        for (auto& packet : queue)
        {
            if (svp_cull_reject(packet.item.pVisual, packet.item.pMatrix)) { if (ps_r__svp_stats) ++svp_stats_cull_reject; continue; } // pip skip off-cone SVP geometry
            if (s_svp_ssa_cull > 0.f && !packet.item.pMatrix && packet.item.ssa < s_svp_ssa_cull) { if (ps_r__svp_stats) ++svp_stats_ssa_culled; if (!svp_ledger_ssa_culled) svp_ledger_ssa_culled = 1; continue; } // pip skip tiny STATIC clutter only (null matrix), never dynamic NPCs/items (matrix carriers)
            if (ps_r__svp_stats && !Device.m_SecondViewport.m_render_pass_is_svp && packet.item.ssa < r_ssaLOD_A) ++svp_stats_tiny; // overlay count only, main view

            if (packet.pState != pState)
            {
                pState = packet.pState;
                RCache.set_States(pState);
            }

#if defined(USE_DX10) || defined(USE_DX11)
            if (packet.pGS != pGS)
            {
                pGS = packet.pGS;
                RCache.set_GS(pGS);
            }
#endif

#ifdef USE_DX11
            if (packet.pHS != pHS)
            {
                pHS = packet.pHS;
                RCache.set_HS(pHS);
            }
            if (packet.pDS != pDS)
            {
                pDS = packet.pDS;
                RCache.set_DS(pDS);
            }
#endif

            // Compare low key stuff regardless, too high collision probability
            if (packet.pVS != pVS)
            {
                pVS = packet.pVS;
                RCache.set_VS(pVS);
            }

            if (packet.pPS != pPS)
            {
                pPS = packet.pPS;
                RCache.set_PS(pPS);
            }

            if (packet.pCS != pCS)
            {
                pCS = packet.pCS;
                RCache.set_Constants(pCS);
            }

            if (packet.pTextures != pTextures)
            {
                pTextures = packet.pTextures;
                RCache.set_Textures(pTextures);
                RImplementation.apply_lmaterial();
            }

			auto& item = packet.item;
			// Opaque custom-FOV geometry keeps all of its original material passes.
			CCustomFOVInitializer projection(item.fov, true);
			if (!static_geometry)
			{
				RCache.set_xform_world(*item.pMatrix);
				RImplementation.apply_object(item.pObject);
				RImplementation.apply_lmaterial();
			}

			float LOD = calcLOD(svp_ssa(item.ssa), item.pVisual->vis.sphere.R);
#ifdef USE_DX11
			RCache.LOD.set_LOD(LOD);
#endif
			item.pVisual->Render(LOD);
		}

		if (_clear)
		{
			queue.clear();
			queue.sorted = false;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void CDSGraphManager::r_dsgraph_render_hud(bool _clear)
{
	PROF_EVENT("r_dsgraph_render_hud", PROF_CAT(Rendering));
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUD, _clear);
	r_dsgraph_render_graph_sorted(RGraph.mapCustomFOV, _clear);

	if (!RGraph.mapCamAttached.empty())
	{
		initializer.SetCamMode();
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttached);
	}
	RImplementation.rmNormal();
}

void CDSGraphManager::r_dsgraph_render_hud_ui()
{
	PROF_EVENT("r_dsgraph_render_hud_ui", PROF_CAT(Rendering));
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	g_hud->RenderActiveItemUI();
	RImplementation.rmNormal();
}

void CDSGraphManager::r_dsgraph_render_cam_ui()
{
	PROF_EVENT("r_dsgraph_render_cam_ui", PROF_CAT(Rendering));

	// Change projection
	CHudInitializer initializer(2);
	
	// Rendering
	RImplementation.rmNear();
	g_hud->RenderCamAttachedUI();
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_sorted(bool render_hud)
{
	r_dsgraph_render_ui();
	{
		PROF_EVENT("r_dsgraph_render_sorted");
		// combine order, only the last pass (main) clears the shared sorted list, else main loses translucents
		const bool clear_sorted = svp_clear_shared_list(true);
		r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Sorted, clear_sorted);
		r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Sorted, clear_sorted);
		r_dsgraph_render_graph_sorted(RGraph.mapCustomFOVSorted.Sorted, clear_sorted);
	}

	if (render_hud)
		r_dsgraph_render_sorted_hud();

	// Camera Script Attachments
	if (RGraph.mapCamAttachedSorted.Sorted.size())
	{
		RImplementation.rmNear();
		// Change projection
		CHudInitializer initializer(2);

		// Rendering
		// combine order, main clears last so cam-attached overlays survive the scope pass
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttachedSorted.Sorted, svp_clear_shared_list(true));
		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_capture_hud()
{
	if (g_hud)
	{
		g_hud->Render_Last(dcast_IPortalTraverser());
		set_Object();
	}
}

#if defined(USE_DX11)
//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_ScopeSorted()  //  Redotix99: for 3D Shader Based Scopes
{
	// Change projection
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapScopeHUDSorted, true);
	RImplementation.rmNormal();
}
#endif

void CDSGraphManager::r_dsgraph_render_sorted_hud()
{
	PROF_EVENT("r_dsgraph_render_sorted_hud");
#if	RENDER==R_R4
	{
		// guard the seed CopyResource, it is a silent no-op + debug-layer spam when accum and generic0
		// formats mismatch (MSAA vs 1x, or HDR-off A8 vs A16F)
		auto* dst = (ID3DTexture2D*)RImplementation.Target->rt_Accumulator->pSurface;
		auto* src = (ID3DTexture2D*)RImplementation.Target->rt_Generic_0->pSurface;
		D3D_TEXTURE2D_DESC dd, sd;
		dst->GetDesc(&dd);
		src->GetDesc(&sd);
		if (dd.Format == sd.Format && dd.SampleDesc.Count == sd.SampleDesc.Count && dd.Width == sd.Width && dd.Height == sd.Height)
			HW.pContext->CopyResource(dst, src);
	}
#endif
	const bool clear_hud = svp_clear_shared_list(true);
	if (svp_reject_hud_window())
	{
		if (clear_hud)
			RGraph.mapHUDSorted.Sorted.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initializer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	// combine order, main clears last so hud-sorted overlays survive the scope pass
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "sorted" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, clear_hud);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
	PROF_EVENT("r_dsgraph_render_emissive", PROF_CAT(Rendering));
#if	RENDER!=R_R1
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Emissive, clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Emissive, clear);
	r_dsgraph_render_graph_sorted(RGraph.mapCustomFOVSorted.Emissive, clear);
	if (svp_reject_hud_window())
	{
		if (clear)
			RGraph.mapHUDSorted.Emissive.clear();
		return;
	}
	
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initializer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "emissive" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Emissive, clear);
	
	if (renderHUD)
	{
		s_svp_objective_hud_bucket = objective_hud ? "sorted" : nullptr;
		r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, false);
	}
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;

	RImplementation.rmNormal();
#endif
}

void CDSGraphManager::r_dsgraph_render_water_ssr()
{
#ifdef USE_DX11
	PROF_EVENT("r_dsgraph_render_water_ssr", PROF_CAT(Rendering));
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
	// docked once, the LPCSTR overload builds a shared_str per name per item
	static shared_str c_cam_pos = "cam_pos";
	static shared_str c_m_current = "m_current";
	static shared_str c_m_previous = "m_previous";
	for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

		RCache.set_Shader(RImplementation.Target->s_ssfx_water_ssr);

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c(c_cam_pos, RImplementation.Target->Position_previous.x, RImplementation.Target->Position_previous.y, RImplementation.Target->Position_previous.z, 0.0f);

		// Previous matrix data
		RCache.set_c(c_m_current, RImplementation.Target->Matrix_current);
		RCache.set_c(c_m_previous, RImplementation.Target->Matrix_previous);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
#endif
}

void CDSGraphManager::r_dsgraph_render_water(bool clearGraph)
{
	PROF_EVENT("r_dsgraph_render_water_ssr", PROF_CAT(Rendering));
    std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
	// docked once, and the wind is frame constant so the whole setup is loop invariant
	static shared_str c_wind_setup = "wind_setup";
	const float WindDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
	const float WindVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
    for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

#ifdef USE_DX11
		if (RImplementation.o.ssfx_water)
		{
			RCache.set_Shader(RImplementation.Target->s_ssfx_water);
		}
#endif

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		// Wind settings
		RCache.set_c(c_wind_setup, WindDir, WindVel, 0, 0);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
	// pip keep the shared captured water list for the next viewport, only the final (main) pass clears
	// it, the SVP combine runs first and must not consume mapWater or the main loses its periphery water
	if (clearGraph)
		RGraph.mapWater.clear();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_wmarks()
{
	PROF_EVENT("r_dsgraph_render_wmarks", PROF_CAT(Rendering));
#if	RENDER!=R_R1
	// gbuffer order, main drains wmarks first so the last pass (SVP when scoped) clears
	const bool clear_wmark = svp_clear_shared_list(false);
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Wmark, clear_wmark);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Wmark, clear_wmark);
	if (svp_reject_hud_window())
	{
		if (clear_wmark)
			RGraph.mapHUDSorted.Wmark.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initalizer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "wmark" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Wmark, clear_wmark);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_distort()
{
	PROF_EVENT("r_dsgraph_render_distort");
	// combine order, main clears distort last, else main loses distortion + the bDistort-gated screen filters
	const bool _clear = svp_clear_shared_list(true);
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Distort, _clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Distort, _clear);
	r_dsgraph_render_graph_sorted(RGraph.mapCustomFOVSorted.Distort, _clear);
	// pip the muzzle heat billboard sits on the entrance pupil in the scope pass and warps the whole
	// image, world distortion above stays, the hud layer is skipped
	if (ps_r__svp_skip_hud_distort && Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp)
		return;
	if (svp_reject_hud_window())
	{
		if (_clear)
			RGraph.mapHUDSorted.Distort.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initalizer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "distort" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Distort, _clear);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
}

#include"LightTrack.h"
void CDSGraphManager::r_dsgraph_capture_static()
{
	PROF_EVENT("r_dsgraph_capture_static", PROF_CAT(Visibility))
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		// Traverse sector/portal structure
		if (psDeviceFlags.test(rsDrawStatic))
		{
			if (dbg)
			{
				const u32 sector_nodes = u32(m_sector_frustums.size());
				dbg->static_sector_nodes += sector_nodes;
				if (opt_bucket)
					dbg->static_sector_nodes_opt += sector_nodes;
				else
					dbg->static_sector_nodes_noopt += sector_nodes;
			}

			// Determine visibility for static geometry hierrarhy
			for (auto& pair : m_sector_frustums)
			{
				xr_vector<CFrustum>& frustums = pair.val.first;
				if (frustums.empty())
					continue;

				if (frustums.size() == 1)
				{
					if (dbg)
					{
						++dbg->static_frustum_nodes;
						++dbg->static_add_root_calls;
						if (opt_bucket)
						{
							++dbg->static_frustum_nodes_opt;
							++dbg->static_add_root_calls_opt;
						}
						else
						{
							++dbg->static_frustum_nodes_noopt;
							++dbg->static_add_root_calls_noopt;
						}
					}
					add_Static((IRenderVisual*)pair.key->root(), frustums.front(), frustums.front().getMask());
					continue;
				}

				xr_vector<u32> masks;
				masks.reserve(frustums.size());
				for (CFrustum& frustum_node : frustums)
					masks.push_back(frustum_node.getMask());

				if (dbg)
				{
					dbg->static_frustum_nodes += u32(frustums.size());
					++dbg->static_add_root_calls;
					if (opt_bucket)
					{
						dbg->static_frustum_nodes_opt += u32(frustums.size());
						++dbg->static_add_root_calls_opt;
					}
					else
					{
						dbg->static_frustum_nodes_noopt += u32(frustums.size());
						++dbg->static_add_root_calls_noopt;
					}
				}

				add_Static_MultiFrustum((IRenderVisual*)pair.key->root(), frustums, masks);
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_lights()
{
	PROF_EVENT("r_dsgraph_capture_lights", PROF_CAT(Visibility))
	g_SpatialSpaceLights->q_frustum
	(
		lstLights,
		ISpatial_DB::O_ORDERED,
		STYPE_LIGHTSOURCE,
		i_frustum
	);

#if	RENDER==R_R1
	std::sort(lstLights.begin(), lstLights.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
	{
		if (!_1.get() || !_2.get()) return false;

		return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
	});
#endif

	for (ISpatialShared spatial : lstLights)
	{
		if (0 == spatial) continue;
		if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
		spatial->spatial_updatesector();
		CSector* sector = (CSector*)spatial->spatial.sector;
		if (0 == sector) continue;

		if (!RImplementation.HOM.visible(spatial->spatial.sphere)) continue;

		if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
		{
			// lightsource
			if (light* L = (light*)(spatial->dcast_Light()))
			{
#if	RENDER==R_R1
				RImplementation.L_DB->add_light(L);
#else
				if (L->get_LOD() > ps_r2_shadow_lod_min && L->has_light_visible_from_sectors(*this))
				{
					RImplementation.Lights.add_light(L);
				}
#endif
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_dynamic(CObject* O)
{
	PROF_EVENT("r_dsgraph_capture_dynamic", PROF_CAT(Visibility))
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		if (psDeviceFlags.test(rsDrawDynamic))
		{
			// Traverse object database
			g_SpatialSpace->q_frustum
			(
				lstRenderables,
				ISpatial_DB::O_ORDERED,
				i_doptions,
				i_frustum
			);
			if (dbg)
			{
				const u32 spatial_count = u32(lstRenderables.size());
				dbg->dynamic_spatials += spatial_count;
				if (opt_bucket)
					dbg->dynamic_spatials_opt += spatial_count;
				else
					dbg->dynamic_spatials_noopt += spatial_count;
			}
			set_Object();
#if	RENDER==R_R1
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				std::sort(lstRenderables.begin(), lstRenderables.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
				{
					if (!_1.get() || !_2.get()) return false;

					return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
				});

				if (ps_actor_shadow_flags.test(1))
					g_hud->Render_First(dcast_IPortalTraverser());

				r_dsgraph_capture_hud();
			}
#endif
			u32 uID_LTRACK = u32(-1);
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				// update light-vis for current entity / actor
				if (CObject* O = g_pGameLevel->CurrentViewEntity())
				{
					if (!O->getDestroy())
					{
						if (CROS_impl* R = (CROS_impl*)O->ROS())
							R->update(O);
					}
				}

				RImplementation.uLastLTRACK++;
				if (!lstRenderables.empty())
				{
					uID_LTRACK = RImplementation.uLastLTRACK % lstRenderables.size();
#if	RENDER!=R_R1
					// update light-vis for selected entity
					// track lighting environment
					if (IRenderable* renderable = (IRenderable*)lstRenderables[uID_LTRACK]->dcast_Renderable())
					{
						if (CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
							T->update(renderable);
					}
#endif
				}

			}

			// Determine visibility for dynamic part of scene
			for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
			{
				ISpatialShared spatial = lstRenderables[o_it];
				if (0 == spatial) continue;
				if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
				CSector* sector = (CSector*)spatial->spatial.sector;
				if (0 == sector) continue;

				if (i_mask[CDSGraphManager::fl_normal] && !RImplementation.HOM.visible(spatial->spatial.sphere))
					continue;

#if	RENDER==R_R1
				if ((spatial->spatial.type & STYPE_GLOW))
				{
					if (CGlow* glow = spatial->dcast_CGlow())
					{
						// It may be an glow
						RImplementation.L_Glows->add(glow);
					}
					continue;
				}
#endif

//				if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
//				{
//					// lightsource
//					if (light* L = (light*)(spatial->dcast_Light()))
//					{
//#if	RENDER==R_R1
//						RImplementation.L_DB->add_light(L);
//#else
//						if (L->get_LOD() > EPS_L && L->has_light_visible_from_sectors(PT))
//						{
//							RImplementation.Lights.add_light(L);
//						}
//#endif
//					}
//					continue;
//				}

				if(!(spatial->spatial.type & STYPE_RENDERABLE) && !(spatial->spatial.type & STYPE_PARTICLE) && !(spatial->spatial.type & STYPE_RENDERABLESHADOW))
					continue;
				sector_frustums_t::TNode* snode = nullptr;
				if (!is_sector_visible(sector, &snode))
					continue;
				if (!snode) // i_start is visible without a lookup, everything else came back with its node
					snode = m_sector_frustums.find(sector);

				for (CFrustum& frustum : snode->val.first)
				{
					if (dbg)
					{
						++dbg->dynamic_frustum_tests;
						if (opt_bucket)
							++dbg->dynamic_frustum_tests_opt;
						else
							++dbg->dynamic_frustum_tests_noopt;
					}

					if (frustum.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
					{
						if (dbg)
						{
							++dbg->dynamic_frustum_hits;
							if (opt_bucket)
								++dbg->dynamic_frustum_hits_opt;
							else
								++dbg->dynamic_frustum_hits_noopt;
						}

						// renderable
						IRenderable* renderable = spatial->dcast_Renderable();
						if (0 == renderable) break;

						if (O && O->dcast_Renderable() == renderable) break;

						// Rendering
#if	RENDER==R_R1
						if (i_mask[CDSGraphManager::fl_normal] && o_it == uID_LTRACK && renderable->renderable_ROS())
						{
							// track lighting environment
							if(CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
								T->update(renderable);
						}
#endif
						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object(renderable);

						renderable->renderable_Render(dcast_IPortalTraverser());
						if (dbg)
						{
							++dbg->dynamic_rendered;
							if (opt_bucket)
								++dbg->dynamic_rendered_opt;
							else
								++dbg->dynamic_rendered_noopt;
						}

						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object();
						break;
					}
				}
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture(bool lights, bool dynamic, CObject* O)
{
	PROF_EVENT("r_dsgraph_capture", PROF_CAT(Visibility))
	r_dsgraph_capture_static();

	if(lights)
		r_dsgraph_capture_lights();

	if (dynamic)
		r_dsgraph_capture_dynamic(O);
}

void CDSGraphManager::r_dsgraph_render_ui_3d()
{
    const Fmatrix saved_world = RCache.get_xform_world();
    const Fmatrix saved_view = RCache.get_xform_view();
    const Fmatrix saved_project = RCache.get_xform_project();
    const Fmatrix saved_full = Device.mFullTransform;
    Device.mFullTransform = Device.mFullTransform3DIcons;
    RCache.set_xform_view(Device.mView);
    RCache.set_xform_project(Device.mProject3DIcons);
    auto render_icons = [&](auto& graph, bool material_shader = false)
    {
        std::sort(graph.begin(), graph.end());
        for (auto& item : graph)
        {
            dxRender_Visual* visual = item.pVisual;
            VERIFY(visual && visual->shader._get());
            if (material_shader)
            {
                // DX11 sorted icons use main's selected mesh material and distance-based LOD.
                RCache.set_Element(item.pSE);
                RCache.set_xform_world(*item.pMatrix);
                RImplementation.apply_object(item.pObject);
                RImplementation.apply_lmaterial();
                visual->Render(calcLOD(item.sortKey, visual->vis.sphere.R));
                continue;
            }
            R_ASSERT(visual->skinning >= -1 && visual->skinning <= 4);
            RCache.set_Shader(RImplementation.Target->s_ui_icons[visual->skinning + 1]);
            RCache.set_Textures(visual->shader->E[0]->passes[0]->T);
            RCache.set_xform_world(*item.pMatrix);
#if RENDER == R_R4
            RImplementation.apply_object(nullptr);
            RImplementation.apply_lmaterial();
#endif
            visual->Render(0.f);
        }
        graph.clear();
    };
    render_icons(RGraph.mapUI3D);
#if RENDER != R_R1
    render_icons(RGraph.mapUI3DEmissive);
#endif
#if RENDER == R_R4
    render_icons(RGraph.mapUI3DSorted, true);
#else
    render_icons(RGraph.mapUI3DSorted);
#endif
    owned_transforms.clear();
    Device.mFullTransform = saved_full;
    RCache.set_xform_world(saved_world);
    RCache.set_xform_view(saved_view);
    RCache.set_xform_project(saved_project);
}

void CDSGraphManager::r_dsgraph_render_ui()
{
	// Capture may run on workers; UI drawing touches RCache and runs here on the render thread.
	for (IRenderable* object : renderable_ui)
		object->renderable_RenderUI();
	renderable_ui.clear();
}
