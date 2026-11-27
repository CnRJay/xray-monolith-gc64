#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip DRender, the debug-line backend for the scope_debug world overlay
#include "../../Include/xrRender/DebugRender.h" // pip IDebugRender::add_lines
#include "../xrRender/SkeletonX.h"              // pip CSkeletonX for the skinned lens bone transform
#include "svp_camera.h"                         // pip svpCamera + the hud geometry snapshot
#include "svp_stats.h"                          // pip per-viewport render-stats overlay

// set all per-viewport camera globals from an explicit view/proj/proj_hud
// called by SetActive, the main path still uses on_idle
void CRender::SetMatrices(Fmatrix view, Fmatrix projection, Fmatrix projection_hud)
{
	auto& RV = Device.m_rt_view;
	RV.mView.set(view);
	RV.mProject.set(projection);
	RV.mProjectHud.set(projection_hud);
	RV.mFullTransform.mul(RV.mProject, RV.mView);
	RV.mFullTransformHud.mul(RV.mProjectHud, RV.mView);

	RV.mInvView.invert(view);
	RV.mInvProject.invert(projection);
	RV.mInvProjectHud.invert(projection_hud);
	D3DXMatrixInverse((D3DXMATRIX*)&RV.mInvFullTransform, 0, (D3DXMATRIX*)&RV.mFullTransform);

	RV.mInvView.transform(RV.vCameraPosition.set(0, 0, 0));
	RV.mInvView.transform_dir(RV.vCameraDirection.set(0, 0, 1));
	RV.mInvView.transform_dir(RV.vCameraTop.set(0, 1, 0));
	RV.mInvView.transform_dir(RV.vCameraRight.set(1, 0, 0));

	RV.vCameraPosition_saved.set(RV.vCameraPosition);
	RV.mView_saved.set(RV.mView);
	RV.mProject_saved.set(RV.mProject);
	RV.mFullTransform_saved.set(RV.mFullTransform);

	float fFov, fAspect, _;
	projection.decompose_projection(fFov, fAspect, _, _);
	RV.fFOV = rad2deg(fFov);
	RV.fASPECT = fAspect;

	if (!CRenderThread::IsOnRenderThread())
	{
		Device.mView.set(RV.mView);
		Device.mProject.set(RV.mProject);
		Device.mProjectHud.set(RV.mProjectHud);
		Device.mFullTransform.set(RV.mFullTransform);
		Device.mFullTransformHud.set(RV.mFullTransformHud);
		Device.mInvView.set(RV.mInvView);
		Device.mInvProject.set(RV.mInvProject);
		Device.mInvProjectHud.set(RV.mInvProjectHud);
		Device.mInvFullTransform.set(RV.mInvFullTransform);
		Device.vCameraPosition.set(RV.vCameraPosition);
		Device.vCameraDirection.set(RV.vCameraDirection);
		Device.vCameraTop.set(RV.vCameraTop);
		Device.vCameraRight.set(RV.vCameraRight);
		Device.vCameraPosition_saved.set(RV.vCameraPosition_saved);
		Device.mView_saved.set(RV.mView_saved);
		Device.mProject_saved.set(RV.mProject_saved);
		Device.mFullTransform_saved.set(RV.mFullTransform_saved);
		Device.fFOV = RV.fFOV;
		Device.fASPECT = RV.fASPECT;
	}

	Device.m_pRender->SetCacheXform(RV.mView, RV.mProject);
	const bool is_svp = Device.m_SecondViewport.IsSVPFrame();
	RV.mView_prev = RV.matrices_previous[is_svp].mView;
	RV.mProject_prev = RV.matrices_previous[is_svp].mProject;
	if (!CRenderThread::IsOnRenderThread())
	{
		Device.mView_prev = RV.mView_prev;
		Device.mProject_prev = RV.mProject_prev;
	}
	Device.m_pRender->SetCacheXform_prev(RV.mView_prev, RV.mProject_prev);
}


#include "../xrRender/dxRenderDeviceRender.h"
#include "../xrRenderDX10/dx10GpuTimeline.h"
#include "../xrRender/CHudInitializer.h"
#include "../../Include/xrRender/UIRender.h"
#include <tbb/task_group.h>

void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Main Render
	{
		Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		HW.pContext->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	auto C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::ResetSVPHistory()
{
	if (TargetMain)
		TargetMain->m_taa_seed_history = true;
	if (TargetSVP)
		TargetSVP->m_taa_seed_history = true;
	Device.m_SecondViewport.dlss_reset_next = true;

	if (TargetSVP)
	{
		Device.matrices_previous[1] = Device.matrices[1];
		TargetSVP->Matrix_current.set(Device.matrices[1].mProject);
		TargetSVP->Matrix_previous.set(Device.matrices[1].mProject);
		TargetSVP->Position_previous.set(Device.m_SecondViewport.svp_cam_pos);

		// invalid depth rejects every old screen space sample
		if (TargetSVP->rt_ssfx_prevPos)
		{
			FLOAT invalid_depth[4] = {0.f, 0.f, -0.001f, 0.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_prevPos->pRT, invalid_depth);
		}
		if (TargetSVP->rt_ssfx_ao)
		{
			FLOAT neutral_ao[4] = {1.f, 1.f, 1.f, 1.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_ao->pRT, neutral_ao);
		}
		if (TargetSVP->rt_ssfx_il)
		{
			FLOAT neutral_il[4] = {0.f, 0.f, 0.f, 1.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_il->pRT, neutral_il);
		}
		if (TargetSVP->rt_ssfx_ssr)
		{
			FLOAT neutral_ssr[4] = {0.f, 0.f, 0.f, 1.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_ssr->pRT, neutral_ssr);
		}

		if (TargetSVP->rt_ssfx_water && g_pGamePersistent
			&& g_pGamePersistent->Environment().CurrentEnv)
		{
			const Fvector3& fc = g_pGamePersistent->Environment().CurrentEnv->fog_color;
			FLOAT neutral_water[4] = {fc.x, fc.y, fc.z, 1.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_water->pRT, neutral_water);
		}
	}
}

void CRender::ExtractFrame()
{
	PROF_EVENT("CRender::ExtractFrame", PROF_CAT(Visibility));

	m_rt_extracted = false;
	m_rt_skip_scene = false;
	m_rt_menu_pp = false;
	m_rt_bSUN = FALSE;
	m_rt_svp = false;

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		m_rt_menu_pp = true;
		m_rt_extracted = true;
		return;
	}

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud) || bMenu)
	{
		m_rt_skip_scene = true;
		m_rt_extracted = true;
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights)
			L->m_moving_frames = 0;
		m_bFirstFrameAfterReset = false;
		m_rt_skip_scene = true;
		m_rt_extracted = true;
		return;
	}

	Calculate();

	Device.m_SecondViewport.LatchOpticConfig(
		Device.dwFrame, Device.m_SecondViewport.GetSVPSession());

	if ((Device.dwFrame % (u32)ps_r__tex_evict_interval) == 0)
		dxRenderDeviceRender::Instance().Resources->EvictStalledTextures();

	RImplementation.o.distortion = FALSE;
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;

	// Freeze bone palettes and dynamic transforms for the upcoming Render Thread frame.
	g_rt_bone_arena.begin_frame(Device.dwFrame);

	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	HOM.MT_RENDER();

	phase = PHASE_NORMAL;

	{
		extern int ps_r4_parallel_cull;
		const bool timed = ps_r__svp_stats != 0;
		CTimer capture_timer;
		if (timed) capture_timer.Start();
		GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
		if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled())
		{
			tbb::task_group static_cull_task;
			static_cull_task.run([this]()
			{
				GMBase.r_dsgraph_capture_static();
			});
			GMBase.r_dsgraph_capture_dynamic();
			static_cull_task.wait();
		}
		else
		{
			GMBase.r_dsgraph_capture_static();
			GMBase.r_dsgraph_capture_dynamic();
		}
		if (timed) svp_stats_capture_base_ms = capture_timer.GetElapsed_ms_f();
	}

	// Reset per-frame lens state.
	{
		auto& vp = Device.m_SecondViewport;
		if (vp.eyepiece.radius > EPS)
		{
			vp.dbg_eyepiece_r = vp.eyepiece.radius;
			vp.dbg_objective_r = vp.objective.radius;
		}
		if (!(Device.true_pip_on && vp.IsSVPActive()))
		{
			vp.dbg_eyepiece_r = 0.f;
			vp.dbg_objective_r = 0.f;
		}
		vp.eyepiece.radius = 0;
		vp.objective.radius = 0;
	}

	Device.m_SecondViewport.dual_accum = nullptr;
	bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();

	static bool s_prev_svp = false;
	static u32 s_disc_epoch = 0;
	const bool disc_swap = (svp && Device.m_SecondViewport.svp_optic_epoch != s_disc_epoch);
	if ((svp && !s_prev_svp) || disc_swap)
		Device.m_SecondViewport.svp_disc_applied = Device.m_SecondViewport.svp_disc_px;
	s_disc_epoch = Device.m_SecondViewport.svp_optic_epoch;
	const bool svp_edge = (svp != s_prev_svp);
	s_prev_svp = svp;

	if (Device.true_pip_on && g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
		EnsureTargetSVP();
	if (svp_edge)
		ResetSVPHistory();
	if (svp && !TargetSVP)
		svp = false;

	// Extract HUD geometry + derive SVP camera while game objects & skeletons are quiescent.
	GMBase.r_dsgraph_capture_hud();
	if (Device.true_pip_on)
		GMBase.svp_latch_hud_poses();
	if (scope_svp_enabled || scope_debug >= 2)
		svp_snapshot_hud_geom();

	if (scope_svp_enabled && g_pGamePersistent &&
		(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f
			|| (g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
				&& !GMBase.RGraph.mapScopeHUDSorted.empty())))
	{
		deriveScopeLens();
		{
			auto& vp = Device.m_SecondViewport;
			if (vp.eyepiece.radius <= EPS && vp.dbg_eyepiece_r > EPS)
			{
				vp.eyepiece.radius = vp.dbg_eyepiece_r;
				vp.objective.radius = vp.dbg_objective_r;
			}
		}
		if (Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP && svpCamera())
			ResetSVPHistory();
	}

	if (svp)
		EnsureTargetSVP();
	TargetMain->svp_objective_hud_prepare(svp);

	// Capture scene lights and pre-cull sun cascades + shadowed lights on Main Thread + TBB.
	// GPU OCCQ queries are deferred until Render Thread finishes PART-0 depth fill.
	LP_normal.clear();
	LP_pending.clear();
	m_rt_occq_lights.clear();
	m_rt_defer_occq = true;
	GMBase.r_dsgraph_capture_lights();
	m_rt_defer_occq = false;

	auto collectOccq = [this](const xr_vector<light*>& list)
	{
		for (light* L : list)
		{
			if (!L)
				continue;
			if (L->m_parent && !ps_r2_shadow_omnipart_vischeck)
			{
				if (L->m_parent->omnipart[0] == L)
					m_rt_occq_lights.push_back(L->m_parent);
			}
			else
			{
				m_rt_occq_lights.push_back(L);
			}
		}
	};
	collectOccq(LP_normal.v_shadowed);
	collectOccq(LP_normal.v_point);
	collectOccq(LP_normal.v_spot);
	collectOccq(LP_pending.v_shadowed);
	collectOccq(LP_pending.v_point);
	collectOccq(LP_pending.v_spot);
	if (ps_r__svp_stats)
	{
		const u32 lp_total = (u32)(LP_normal.v_point.size() + LP_normal.v_spot.size() + LP_normal.v_shadowed.size()
			+ LP_pending.v_point.size() + LP_pending.v_spot.size() + LP_pending.v_shadowed.size());
		const u32 lp_shadowed = (u32)(LP_normal.v_shadowed.size() + LP_pending.v_shadowed.size());
		svp_stats::note_main_lights(lp_total, lp_shadowed);
	}

	if (bSUN)
		extract_sun_cascades();

	extract_lights_for_rt();

	phase = PHASE_NORMAL;
	m_rt_bSUN = bSUN;
	m_rt_svp = svp;
	m_rt_gbuffer_frame = Device.dwFrame;
	m_rt_extracted = true;
}

void CRender::RenderSubmit()
{
	PIX_EVENT_C(CRender_Render, dx10_marker_frame);
	dx10_annotate_frame();

	rmNormal();

	if (!m_rt_extracted)
		return;

	if (m_rt_menu_pp)
	{
		render_menu();
		g_rt_bone_arena.end_frame();
		return;
	}

	if (m_rt_skip_scene)
	{
		Target->u_setrt(Device.m_rt_view.dwWidth, Device.m_rt_view.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
		g_rt_bone_arena.end_frame();
		return;
	}

	svp_stats::frame_begin();
	svp_stats::section_begin(svp_stats::SEC_FRAME);

	const bool svp = m_rt_svp;
	const BOOL bSUN = m_rt_bSUN;

	if (Device.true_pip_on)
		TargetMain->SetActive();
	svp_stats::section_begin(svp_stats::SEC_MAIN_GBUFFER);
	renderGBuffer(!svp, true);
	svp_stats::section_end(svp_stats::SEC_MAIN_GBUFFER);

	if (svp)
	{
		CTimer svp_t; svp_t.Start();
		const u32 calls0 = RCache.stat.calls;
		const u32 verts0 = RCache.stat.verts;
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		TargetSVP->SetActive();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);
		svp_stats::section_begin(svp_stats::SEC_SVP_GBUFFER);
		renderGBuffer(true, true);
		svp_stats::section_end(svp_stats::SEC_SVP_GBUFFER);
		if (ps_r__svp_diag)
		{
			static u32 s_perf_ms = 0;
			if (Device.m_rt_view.dwTimeGlobal - s_perf_ms > 1000)
			{
				s_perf_ms = Device.m_rt_view.dwTimeGlobal;
				PipMsg("[SVP-PERF] gbuffer %.2fms calls %u verts %uk", svp_t.GetElapsed_sec() * 1000.f,
					RCache.stat.calls - calls0, (RCache.stat.verts - verts0) / 1000);
			}
		}
		svp_stats::cpu_begin(svp_stats::CPU_HUD);
		TargetMain->svp_objective_hud_report();
		svp_stats::cpu_end(svp_stats::CPU_HUD);
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		if (ps_r__svp_corner_mask && !RImplementation.o.dx10_msaa && TargetSVP)
			TargetSVP->stamp_svp_corner_mask();
		TargetMain->SetActive();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);

		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			TargetSVP->SetActive();
			share_main_smaps();
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0);
			svp_stats::note_svp_blend();
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
			svp_stats::section_begin(svp_stats::SEC_SVP_LIGHTS);
			{ PIX_EVENT(SVP_ACCUM); accum(); }
			svp_stats::section_end(svp_stats::SEC_SVP_LIGHTS);
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			Device.m_SecondViewport.force_svp_sss = false;
			TargetMain->SetActive();
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
		};
	}

	renderSceneLighting(bSUN, svp, true);
	Device.m_SecondViewport.dual_accum = nullptr;

	svp_stats::section_end(svp_stats::SEC_FRAME);
	svp_stats::frame_end(svp);
	svp_stats::draw_overlay();

	g_rt_bone_arena.end_frame();
}

void CRender::Render()
{
	PIX_EVENT_C(CRender_Render, dx10_marker_frame);
	dx10_annotate_frame();

	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	// lock one optic route for every scope consumer this frame
	Device.m_SecondViewport.LatchOpticConfig(
		Device.dwFrame, Device.m_SecondViewport.GetSVPSession());

	// pip svp render-stats frame boundary, self-gates to nothing when r__svp_stats is 0
	svp_stats::frame_begin();
	svp_stats::section_begin(svp_stats::SEC_FRAME);

	if ((Device.dwFrame % (u32)ps_r__tex_evict_interval) == 0)
		dxRenderDeviceRender::Instance().Resources->EvictStalledTextures();

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    HOM.MT_RENDER();

	//******* Main calc - DEFERRER RENDERER
	phase = PHASE_NORMAL;
	
	/*if (RImplementation.o.ssfx_core) // SSS23: DEPRECATED
	{
		// HUD Masking rendering
		FLOAT ColorRGBA[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_hud->pRT, ColorRGBA);

		Target->u_setrt(Target->rt_ssfx_hud, NULL, NULL, HW.pBaseZB);
		r_dsgraph_render_hud(true);

		// Reset Depth
		HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH, 1.0f, 0);
	}*/

    // pip cpu cost of the main graph traverse plus capture, read by the fps audit overlay
    {
        extern int ps_r4_parallel_cull;
        const bool timed = ps_r__svp_stats != 0;
        CTimer capture_timer;
        if (timed) capture_timer.Start();
        GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
        if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled())
        {
            tbb::task_group static_cull_task;
            static_cull_task.run([this]()
            {
                GMBase.r_dsgraph_capture_static();
            });
            GMBase.r_dsgraph_capture_dynamic();
            static_cull_task.wait();
        }
        else
        {
            GMBase.r_dsgraph_capture_static();
            GMBase.r_dsgraph_capture_dynamic();
        }
        if (timed) svp_stats_capture_base_ms = capture_timer.GetElapsed_ms_f();
    }

	// pip reset the per-frame lens, it is re-derived each frame so an un-scoped frame leaves radius 0
	// the debug overlays hold the last radii while the svp stays active so a culled weapon keeps its lines
	{
		auto& vp = Device.m_SecondViewport;
		if (vp.eyepiece.radius > EPS)
		{
			vp.dbg_eyepiece_r = vp.eyepiece.radius;
			vp.dbg_objective_r = vp.objective.radius;
		}
		if (!(Device.true_pip_on && vp.IsSVPActive()))
		{
			vp.dbg_eyepiece_r = 0.f;
			vp.dbg_objective_r = 0.f;
		}
		vp.eyepiece.radius = 0;
		vp.objective.radius = 0;
	}

	// pip a stale hook must never survive into this frame (it captures this + TargetSVP)
	Device.m_SecondViewport.dual_accum = nullptr;
	// pip double-pass, the captured graph renders once for the main viewport and, when a scope drives
	// the SVP, a second time into TargetSVP, true_pip off keeps the single stock main pass
	bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();
	// pip lock the adaptive SVP size at ADS-in (svp false -> true) from the disc learned on the last aim, so the
	// target is sized once per aim and never resized mid-ADS (the live disc keeps learning for next time)
	static bool s_prev_svp = false;
	static u32 s_disc_epoch = 0;
	// an optic swap while aimed re-locks from the re-learned disc so the target resizes once for the
	// new optic, the ads-in edge still owns the first lock
	const bool disc_swap = (svp && Device.m_SecondViewport.svp_optic_epoch != s_disc_epoch);
	if ((svp && !s_prev_svp) || disc_swap)
		Device.m_SecondViewport.svp_disc_applied = Device.m_SecondViewport.svp_disc_px;
	s_disc_epoch = Device.m_SecondViewport.svp_optic_epoch;
	const bool svp_edge = (svp != s_prev_svp);
	s_prev_svp = svp;
	// pip allocate the SVP target before the main pass derives the camera into it (lazy, only while a
	// PiP scope is aimed, never when off)
	svp_stats::cpu_begin(svp_stats::CPU_MISC);
	if (Device.true_pip_on && g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
		EnsureTargetSVP();
	// pip the lens content swaps at the scope edges, both taa histories seed from the current
	// frame so the stale scene never ghosts through the resolve
	if (svp_edge)
		ResetSVPHistory();
	svp_stats::cpu_end(svp_stats::CPU_MISC);
	// creation can fail at VRAM exhaustion, fall back to the single stock pass this frame
	if (svp && !TargetSVP)
		svp = false;
	// pip diag, an aimed weapon without an svp pass (y is nonzero even at hip, gate on x)
	{
		if (ps_r__svp_diag && Device.true_pip_on && !svp && g_pGamePersistent
			&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_ns_ms = 0;
			if (Device.dwTimeGlobal - s_ns_ms > 500)
			{
				s_ns_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-STATE] zoomed hud without an svp pass, active=%d target=%d",
					(int)Device.m_SecondViewport.IsSVPActive(), TargetSVP ? 1 : 0);
			}
		}
	}
	// pip [SVP-NVG] tuning diag, edge on generation change plus a slow heartbeat while active
	{
		extern Fvector4 ps_dev_param_7;
		extern Fvector4 ps_dev_param_8;
		extern int ps_r2_nightvision;
		static float s_nvg_gen = -1.f;
		static u32 s_nvg_ms = 0;
		const float genf = floorf(ps_dev_param_8.x);
		const bool edge = _abs(ps_dev_param_8.x - s_nvg_gen) > 0.001f;
		if (ps_r__svp_diag && (edge || (genf >= 1.f && Device.dwTimeGlobal - s_nvg_ms > 2000)))
		{
			s_nvg_gen = ps_dev_param_8.x;
			s_nvg_ms = Device.dwTimeGlobal;
			const float gain_off = floorf(ps_dev_param_8.w) / 10.f;
			PipMsg("[SVP-NVG] gen=%.0f tubes=%.1f gain_cur=%.2f gain_off=%.2f tm_scale=%.1f vig=%.2f r2nv=%d scoped=%d aim=%.2f p7=%.2f/%.2f/%.2f/%.2f",
				genf, (ps_dev_param_8.x - genf) * 10.f,
				floorf(ps_dev_param_8.y) / 10.f, gain_off, 10.f * gain_off,
				floorf(ps_dev_param_8.z) / 100.f, ps_r2_nightvision,
				(int)(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()),
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f,
				ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
		}
	}
	if (Device.true_pip_on)
		TargetMain->SetActive();
	svp_stats::section_begin(svp_stats::SEC_MAIN_GBUFFER);
	const u32 gbuffer_frame = Device.dwFrame; // grass fills its instance buffer once per frame for both gbuffers
	renderGBuffer(!svp); // keep the priority-0 graph when an SVP pass follows
	svp_stats::section_end(svp_stats::SEC_MAIN_GBUFFER);
	if (svp)
	{
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		EnsureTargetSVP();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);
	}
	svp_stats::cpu_begin(svp_stats::CPU_HUD);
	TargetMain->svp_objective_hud_prepare(svp);
	svp_stats::cpu_end(svp_stats::CPU_HUD);
	if (svp)
	{
		// pip CPU-side cost probe for the SVP gbuffer, throttled log while r__svp_diag is on
		CTimer svp_t; svp_t.Start();
		const u32 calls0 = RCache.stat.calls;
		const u32 verts0 = RCache.stat.verts;
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		TargetSVP->SetActive();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);
		R_ASSERT2(Device.dwFrame == gbuffer_frame, "svp gbuffer runs on a new frame, the grass instance ranges are stale");
		svp_stats::section_begin(svp_stats::SEC_SVP_GBUFFER);
		renderGBuffer(true);
		svp_stats::section_end(svp_stats::SEC_SVP_GBUFFER);
		if (ps_r__svp_diag)
		{
			static u32 s_perf_ms = 0;
			if (Device.dwTimeGlobal - s_perf_ms > 1000)
			{
				s_perf_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-PERF] gbuffer %.2fms calls %u verts %uk", svp_t.GetElapsed_sec() * 1000.f,
					RCache.stat.calls - calls0, (RCache.stat.verts - verts0) / 1000);
			}
		}
		svp_stats::cpu_begin(svp_stats::CPU_HUD);
		TargetMain->svp_objective_hud_report();
		svp_stats::cpu_end(svp_stats::CPU_HUD);
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		// pip stencil the dead corners after the native scope draw
		if (ps_r__svp_corner_mask && !RImplementation.o.dx10_msaa && TargetSVP)
			TargetSVP->stamp_svp_corner_mask();
		TargetMain->SetActive(); // shadow generation + main accumulation run on the main target
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);

		// pip install the dual-accumulate hook, each shadow unit builds its map once on the main atlas
		// then this re-accumulates it into the SVP with no second shadow render
		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			TargetSVP->SetActive();   // SVP gbuffer + accumulator (and, for now, the SVP shadow atlas)
			share_main_smaps();       // re-point the shadow atlas at the main maps the generation built
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0); // sun keeps the SSS contact term
			svp_stats::note_svp_blend();
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
			svp_stats::section_begin(svp_stats::SEC_SVP_LIGHTS);
			{ PIX_EVENT(SVP_ACCUM); accum(); } // SVP marginal lighting cost: accumulate this unit into the SVP (shared maps)
			svp_stats::section_end(svp_stats::SEC_SVP_LIGHTS);
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			Device.m_SecondViewport.force_svp_sss = false;
			TargetMain->SetActive();  // restore for the next unit's generation on the main atlas
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
		};
	}

	// single shared lighting pass, when svp the render_sun_cascades/render_lights dual-accumulate via
	// the hook and the SVP is combined + captured before the main combine composites it into the lens
	renderSceneLighting(bSUN, svp);
	Device.m_SecondViewport.dual_accum = nullptr;

	// pip close the whole-frame window then draw the stats panel over the finished image
	svp_stats::section_end(svp_stats::SEC_FRAME);
	svp_stats::frame_end(svp);
	svp_stats::draw_overlay();
}

void CRender::renderGBuffer(bool clearGraph, bool preExtracted)
{
	// label the pass so the SVP (scope) gbuffer is distinguishable from the main one in a capture
	PIX_EVENT_F("RENDER_GBUFFER[%s]", Target == TargetMain ? "MAIN" : "SVP");
	if (CRenderThread::IsOnRenderThread())
		Device.m_rt_view.dwViewport++;
	else
	{
		Device.dwViewport++; // pip per-viewport cache counter
		Device.m_rt_view.dwViewport = Device.dwViewport;
	}

	// pip cull the SVP geometry to the scope cone, the captured graph is main frustum so the SVP would
	// otherwise resubmit the whole world through a cone that sees a fraction
	const bool svp_pass = (Target == TargetSVP) && Device.true_pip_on;
	const bool svp_cull = svp_pass && ps_r__svp_cull;
	// the grass bracket below arms off the same matrix so it is built for the whole scope pass
	Fmatrix svp_full;
	if (svp_pass)
		svp_full.mul(Device.m_rt_view.matrices[1].mProject, Device.m_rt_view.matrices[1].mView);
	if (svp_cull)
		CDSGraphManager::svp_cull_begin(svp_full, svp_cull);
	// pip SVP coverage = (mag * svp_side / main_height)^2, capped at 1: the fraction of the main view's
	// pixel area an object covers in the scope. feeds the LOD scale and the small-object cull threshold
	if (svp_pass && TargetSVP && (ps_r__svp_lod > 0.f || ps_r__svp_cull_ssa > 0.f))
	{
		extern float g_pip_scope_magnification;
		const float mh = (float)Device.m_rt_view.dwHeight;
		const float svp_eff = sqrtf((float)TargetSVP->Width * (float)TargetSVP->Height); // geometric-mean side (== Width when square)
		float cov = (mh > 1.f) ? (g_pip_scope_magnification * svp_eff / mh) : 1.f;
		cov *= cov;
		if (cov > 1.f) cov = 1.f;
		if (ps_r__svp_lod > 0.f)
			CDSGraphManager::svp_set_lod_scale(1.f + (cov - 1.f) * ps_r__svp_lod);
		if (ps_r__svp_cull_ssa > 0.f)
			CDSGraphManager::svp_set_ssa_cull(ps_r__svp_cull_ssa, cov);
	}

	phase = PHASE_NORMAL;
	Target->phase_scene_prepare(); // clears + binds this viewport's gbuffer and depth

    if (RImplementation.o.ssfx_motionvectors)
    {
        Target->u_setrt(Device.m_rt_view.dwWidth, Device.m_rt_view.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

        RCache.set_Stencil(FALSE);
        g_pGamePersistent->Environment().RenderSky(true);

        RCache.Index.Flush();
        RCache.Vertex.Flush();

        RCache.set_xform_world(Fidentity);
    }

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.m_rt_view.dwWidth, Device.m_rt_view.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
	}

	//******* Main render :: PART-0	-- first
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_static(0, clearGraph);
		GMBase.r_dsgraph_render_dynamic(0, clearGraph);
		Target->disable_aniso();
	}

	//  Redotix99: for 3D Shader Based Scopes
	// legacy fallback, main view only, skip under true PiP where the fakescope consumer is excluded
	if (scope_3D_fake_enabled && Target == TargetMain
		&& !(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()))
	{
		ID3D11Resource* zbuffer_res;
		HW.pBaseZB->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	if (Target == TargetMain) // pip lights captured once on main, the SVP reuses them
	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		if (!preExtracted)
		{
			LP_normal.clear();
			LP_pending.clear();
			GMBase.r_dsgraph_capture_lights();
			if (ps_r__svp_stats)
			{
				const u32 lp_total = (u32)(LP_normal.v_point.size() + LP_normal.v_spot.size() + LP_normal.v_shadowed.size()
					+ LP_pending.v_point.size() + LP_pending.v_spot.size() + LP_pending.v_shadowed.size());
				const u32 lp_shadowed = (u32)(LP_normal.v_shadowed.size() + LP_pending.v_shadowed.size());
				svp_stats::note_main_lights(lp_total, lp_shadowed);
			}
		}
		else
		{
			for (light* L : m_rt_occq_lights)
			{
				if (L)
					L->vis_prepare();
			}
		}
	}

	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// level
		Target->phase_scene_begin();
		if (Target == TargetMain) // pip weapon HUD only in the main view, not the scope image
		{
			if (!preExtracted)
			{
				GMBase.r_dsgraph_capture_hud();
				svp_stats::cpu_begin(svp_stats::CPU_HUD);
				if (Device.true_pip_on)
					GMBase.svp_latch_hud_poses();
				// pip snapshot HUD geometry centers before render_hud clears the lists, so the geomscan (in
				// deriveScopeLens, after the clear) can auto-derive the objective distance against the optical axis
				if (scope_svp_enabled || scope_debug >= 2)
					svp_snapshot_hud_geom();
				svp_stats::cpu_end(svp_stats::CPU_HUD);
			}
			// keep the weapon list when an SVP pass follows, the scope image drains it second
			extern bool g_svp_hud_frozen_pass;
			extern bool g_svp_hud_history_write;
			const bool frozen_was = g_svp_hud_frozen_pass;
			const bool history_was = g_svp_hud_history_write;
			const bool exact_pose = !clearGraph && scope_svp_enabled >= 2
				&& ps_r__svp_weapon_continuity && Device.true_pip_on;
			g_svp_hud_frozen_pass = exact_pose;
			g_svp_hud_history_write = exact_pose;
			GMBase.r_dsgraph_render_hud(clearGraph);
			g_svp_hud_frozen_pass = frozen_was;
			g_svp_hud_history_write = history_was;

			// pip derive the scope lens then build the SVP camera (matrices[1]) while a PiP scope
			// is aimed, zoom-0 tube sights have no zoom fov so ADS + a captured lens also qualifies
			if (!preExtracted)
			{
				svp_stats::cpu_begin(svp_stats::CPU_HUD);
				if (scope_svp_enabled && g_pGamePersistent &&
					(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f
						|| (g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
							&& !GMBase.RGraph.mapScopeHUDSorted.empty())))
				{
					deriveScopeLens();
					// a culled weapon mid-aim re-arms the stale lens radius, m_W persists on its own
					{
						auto& vp = Device.m_SecondViewport;
						if (vp.eyepiece.radius <= EPS && vp.dbg_eyepiece_r > EPS)
						{
							vp.eyepiece.radius = vp.dbg_eyepiece_r;
							vp.objective.radius = vp.dbg_objective_r;
						}
					}
					if (Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP
						&& svpCamera())
						ResetSVPHistory();
				}
				svp_stats::cpu_end(svp_stats::CPU_HUD);
			}
			// pip [3DB] for aimed sights without a captured lens, reflex and irons keep 3d
			// ballistics so the sight independent markers draw, the pip overlay owns the rest
			{
				extern int ps_r__3db_debug;
				extern void ballistics_debug_overlay();
				if (ps_r__3db_debug > 0 && scope_svp_enabled && g_pGamePersistent
					&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
					&& !(Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP))
					ballistics_debug_overlay();
			}
		}
		else if (svp_pass) // pip the weapon renders through the scope at low mag, one unit inside and out
		{
			extern float g_pip_scope_magnification;
			auto& vp = Device.m_SecondViewport;
			// the weapon drains through the same scope camera as the world
			// the near plane hides everything behind the front lens
			svp_stats::cpu_begin(svp_stats::CPU_HUD);
			if (scope_svp_enabled >= 2 && vp.eyepiece.radius > EPS
				&& vp.svp_front_use_m > EPS)
			{
				RCache.set_xform_view(Device.matrices[1].mView);
				RCache.set_xform_project(Device.matrices[1].mProject);
				extern bool g_svp_hud_frozen_pass;
				extern bool g_svp_hud_history_write;
				extern bool svp_objective_hud_current();
				const bool frozen_was = g_svp_hud_frozen_pass;
				const bool history_was = g_svp_hud_history_write;
				const bool exact_pose = svp_objective_hud_current();
				g_svp_hud_frozen_pass = exact_pose;
				g_svp_hud_history_write = exact_pose;
				GMBase.r_dsgraph_render_hud_svp();
				g_svp_hud_frozen_pass = frozen_was;
				g_svp_hud_history_write = history_was;
			}
			else
				GMBase.RGraph.mapHUD.clear(); // consume the deferred main-pass clear
			svp_stats::cpu_end(svp_stats::CPU_HUD);
		}
		GMBase.r_dsgraph_render_lods(true, clearGraph);
		// pip r__svp_skip_grass drops the near-grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass))
		{
			// keep the grass visible set on the main drain when the SVP pass draws it second
			extern bool g_svp_defer_detail_clear;
			g_svp_defer_detail_clear = (!svp_pass && !clearGraph && !ps_r__svp_skip_grass);
			// the scope grass draws only the slot ranges the scope cone keeps
			if (svp_pass)
				Details->hw_Run_Begin_Scope(svp_full);
			Details->Render();
			if (svp_pass)
				Details->hw_Run_End();
			g_svp_defer_detail_clear = false;
		}
		Target->phase_scene_end();
	}

	if (svp_cull)
		CDSGraphManager::svp_cull_end(); // pip end SVP cull, the shared shadow/light passes below are unaffected
	if (svp_pass)
	{
		CDSGraphManager::svp_set_lod_scale(1.f); // pip restore full LOD + no cull before the shared light passes
		CDSGraphManager::svp_set_ssa_cull(0.f, 1.f);
	}

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}

	{
		// Save previus and current matrices
		{
			static Fmatrix mm_saved_viewproj;

			if (!Device.m_SecondViewport.IsSVPFrame())
			{
				Target->Matrix_previous.mul(mm_saved_viewproj, Device.m_rt_view.mInvView);
				Target->Matrix_current.set(Device.m_rt_view.mProject);
				mm_saved_viewproj.set(Device.m_rt_view.mFullTransform);
			}
			else if (svp_pass)
			{
				// pip the scope water SSR and SSDO reproject against these, the setter blocks are main only so
				// the SVP would accumulate against stale matrices, build them from the per viewport matrices
				Fmatrix svp_prev_full, svp_inv_view, svp_prev_inv_view;
				svp_prev_full.mul(Device.m_rt_view.matrices_previous[1].mProject, Device.m_rt_view.matrices_previous[1].mView);
				svp_inv_view.invert(Device.m_rt_view.matrices[1].mView);
				Target->Matrix_previous.mul(svp_prev_full, svp_inv_view);
				Target->Matrix_current.set(Device.m_rt_view.matrices[1].mProject);
				svp_prev_inv_view.invert(Device.m_rt_view.matrices_previous[1].mView);
				Target->Position_previous.set(svp_prev_inv_view.c);
			}
		}

		// pip the SVP skips its own SSS pass, r__svp_sss_sun computes it so the scope sun keeps the contact term
		if (RImplementation.o.ssfx_sss && (!Device.m_SecondViewport.IsSVPFrame() || (svp_pass && ps_r__svp_sss_sun)))
		{
			static bool sss_rendered, sss_extended_rendered;

			// SSS Shadows
			if (ps_ssfx_sss_quality.z > 0)
			{
				Target->phase_ssfx_sss();
				sss_rendered = true;
			}
			else
			{
				if (sss_rendered) // Clear buffer
				{
					sss_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
				}
			}

			if (ps_ssfx_sss_quality.w > 0)
			{
				// Extra lights
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
				sss_extended_rendered = true;
			}
			else
			{
				if (sss_extended_rendered) // Clear buffer
				{
					sss_extended_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
				}
			}
		}
	}
}

// pip re-point the shadow-atlas textures at the main maps so the SVP accumulation reads the shared
// shadow maps with no second shadow render (SetActive(SVP) had pointed them at the SVP's empty atlas)
void CRender::share_main_smaps()
{
	// an svp target with no atlas of its own leaves the stock name on the main maps already
	if (TargetSVP && !TargetSVP->rt_smap_depth)
		return;
	for (auto& r : TargetMain->RenderTargetRemaps)
		if (r.second == TargetMain->rt_smap_depth ||
			(TargetMain->rt_smap_depth_minmax && r.second == TargetMain->rt_smap_depth_minmax))
			r.first->surface_set(r.second->pSurface);
	RCache.Invalidate();
}

void CRender::renderSceneLighting(BOOL bSUN, bool svp, bool preExtracted)
{
	// Directional light - fucking sun, cascades build their shadow map once on the main atlas and the
	// accumulate is replayed into the SVP by the dual_accum hook (inside render_sun_cascade + below)
	if (bSUN) //bSUN && Device.dwFrame & 1 --Delayed sun update. Worth to check it in future
	{
		PIX_EVENT(DEFER_SUN);
		svp_stats::note_sun();
		svp_stats::section_begin(svp_stats::SEC_MAIN_LIGHTS);
		RImplementation.stats.l_visible ++;
		if (preExtracted)
			render_sun_cascades_submit();
		else
			render_sun_cascades();
		auto sun_blend = [this] { Target->increment_light_marker(); Target->accum_direct_blend(); };
		sun_blend();
		if (Device.m_SecondViewport.dual_accum)
			Device.m_SecondViewport.dual_accum(sun_blend);
		svp_stats::section_end(svp_stats::SEC_MAIN_LIGHTS);
	}

	phase = PHASE_NORMAL;

	// pip gated self-illum replay into the SVP before the main drain touches the shared list
	if (svp && ps_r__svp_emissive)
	{
		PIX_EVENT(SVP_SELF_ILLUM);
		svp_stats::section_begin(svp_stats::SEC_SVP_EMISSIVE);
		TargetSVP->SetActive();
		TargetSVP->phase_accumulator();
		RCache.set_xform_project(Device.m_rt_view.mProject);
		RCache.set_xform_view(Device.m_rt_view.mView);
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		// the shared list is main frustum wide, reject static items outside the scope cone
		if (ps_r__svp_cull)
		{
			Fmatrix svp_full;
			svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
			CDSGraphManager::svp_cull_begin(svp_full, true);
		}
		GMBase.r_dsgraph_render_emissive(false);
		// the cone must not leak into the main emissive drain below
		CDSGraphManager::svp_cull_end();
		TargetMain->SetActive();
		svp_stats::section_end(svp_stats::SEC_SVP_EMISSIVE);
	}

	// emissive runs on the main viewport only (active here), the SVP skips self-illum (minor) so it does
	// not re-render + clear the shared GMBase emissive list that the main pass still needs
	{
		PIX_EVENT(DEFER_SELF_ILLUM);
		svp_stats::section_begin(svp_stats::SEC_MAIN_EMISSIVE);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.m_rt_view.mProject);
		RCache.set_xform_view(Device.m_rt_view.mView);
		// Stencil - write 0x1 at pixel pos -
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
		svp_stats::section_end(svp_stats::SEC_MAIN_EMISSIVE);
	}

	if (RImplementation.o.ssfx_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
		// hud sorted glass (collimator dots, pda screen) stays out of the bloom emissive buffer,
		// the bloom build's hud mask owns the hud response and feeding it here double counts unmasked
		GMBase.r_dsgraph_render_emissive(true, ps_r__ssfx_bloom_hud != 0);
	}

	// Lighting, shadow maps build once on the main atlas, render_lights accumulates per viewport
	// (it replays the accumulate into the SVP via dual_accum, sharing the maps), non dependant on OCCQ
	svp_stats::section_begin(svp_stats::SEC_MAIN_LIGHTS); // svp mirror nests here through the hook
	if (preExtracted)
	{
		PIX_EVENT(DEFER_LIGHT_PREEXTRACTED);
		Target->phase_accumulator();
		render_lights_submit(LP_normal);
	}
	else
	{
		{
			PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
			Target->phase_accumulator();
			render_lights(LP_normal);
		}

		// Lighting, dependant on OCCQ
		{
			PIX_EVENT(DEFER_LIGHT_OCCQ);
			render_lights(LP_pending);
		}
	}
	svp_stats::section_end(svp_stats::SEC_MAIN_LIGHTS);

	// pip volumetric blur + combine per viewport, the SVP is combined and captured first so the main
	// combine can composite the ready SVP image into the lens
	if (svp)
	{
		svp_stats::section_begin(svp_stats::SEC_SVP_COMBINE);
		TargetSVP->SetActive();
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
		phase = PHASE_NORMAL;
		{
			PIX_EVENT(COMBINE_SVP);
			Target->phase_combine();
		}

		svp_stats::cpu_begin(svp_stats::CPU_MISC);
		TargetSVP->phase_svp_capture(); // SVP combined color -> rt_secondVP, ready for the main lens

		// The real reflex mesh draws through the objective after SVP post processing
		Device.m_SecondViewport.svp_reflex_capture_ok = false;
		Device.m_SecondViewport.svp_reflex_capture_epoch = u32(-1);
		Device.m_SecondViewport.svp_reflex_capture_session = 0;
		if (ps_r__svp_reflex_capture)
		{
			Device.m_SecondViewport.svp_reflex_capture_ok = TargetSVP->draw_hybrid_reflex();
			if (Device.m_SecondViewport.svp_reflex_capture_ok)
			{
				Device.m_SecondViewport.svp_reflex_capture_epoch =
					Device.m_SecondViewport.svp_optic_epoch;
				Device.m_SecondViewport.svp_reflex_capture_session =
					Device.m_SecondViewport.GetSVPSession();
			}
		}
		svp_stats::cpu_end(svp_stats::CPU_MISC);

		TargetMain->SetActive();
		svp_stats::section_end(svp_stats::SEC_SVP_COMBINE);
	}

	{
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
	}

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		svp_stats::section_begin(svp_stats::SEC_MAIN_COMBINE);
		Target->phase_combine();
		svp_stats::section_end(svp_stats::SEC_MAIN_COMBINE);
	}

	// pip r__scope_debug on-screen inspector overlay (gated + main-only inside the call)
	Target->phase_scope_debug();

	// detail-clear + HUD UI on the main viewport, the scope image has no separate HUD UI
	if (Details)
		Details->details_clear();

	if (preExtracted)
	{
		PROF_EVENT("render_hud_replay");
		{
			CHudInitializer initializer(true);
			rmNear();
			UIRender->Replay(2);
			rmNormal();
		}
		{
			CHudInitializer initializer(2);
			rmNear();
			UIRender->Replay(3);
			rmNormal();
		}
	}
	else if (g_hud)
	{
		PROF_EVENT("render_hud");
		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
	}
}
namespace
{
bool ui_3d_icon_atlas_active = false;
Fmatrix ui_3d_icon_project_saved;
Fmatrix ui_3d_icon_full_transform_saved;
}

void CRender::RenderUI3DDirect()
{
	HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.0f, 0);

	RCache.set_Z(FALSE);
	RCache.set_RT(HW.pBaseRT, 0);
	GMUI3D.r_dsgraph_render_ui_3d();
}

bool CRender::GetUI3DIconAtlasInfo(u32& width, u32& height, float& resolution)
{
	if (!ps_r4_atlas || !Target || !Target->rt_ui_3d_icons)
		return false;
	width = Target->rt_ui_3d_icons->dwWidth;
	height = Target->rt_ui_3d_icons->dwHeight;
	resolution = clampr(ps_r4_atlas_resolution, 0.1f, 1.f);
	return true;
}

bool CRender::BeginUI3DIconAtlas()
{
	if (ui_3d_icon_atlas_active || !ps_r4_atlas || !Target || !Target->rt_ui_3d_icons)
		return false;

	ui_3d_icon_atlas_active = true;
	ui_3d_icon_project_saved = Device.mProject3DIcons;
	ui_3d_icon_full_transform_saved = Device.mFullTransform3DIcons;

	const u32 width = Target->rt_ui_3d_icons->dwWidth;
	const u32 height = Target->rt_ui_3d_icons->dwHeight;
	Device.mProject3DIcons.build_projection_ortho(UI_3D_ICON_ORTHO_HEIGHT * float(width) / float(height),
		UI_3D_ICON_ORTHO_HEIGHT, UI_3D_ICON_NEAR_PLANE, UI_3D_ICON_FAR_PLANE);
	Device.mFullTransform3DIcons.mul(Device.mProject3DIcons, Device.mView);

	const ref_rt& raw = Target->rt_ui_3d_icons_raw_msaa ?
		Target->rt_ui_3d_icons_raw_msaa : Target->rt_ui_3d_icons_raw;
	Target->u_setrt(raw, nullptr, nullptr, Target->rt_ui_3d_icons_depth->pZRT);
	Target->set_viewport_size(HW.pContext, width, height);

	const float clear_color[4] = {0.f, 0.f, 0.f, 0.f};
	HW.pContext->ClearRenderTargetView(raw->pRT, clear_color);
	HW.pContext->ClearDepthStencilView(Target->rt_ui_3d_icons_depth->pZRT,
		D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.f, 0);
	return true;
}

void CRender::BeginUI3DIconAtlasItem()
{
}

void CRender::EndUI3DIconAtlas()
{
	if (!ui_3d_icon_atlas_active)
		return;

	RCache.set_Z(FALSE);
	GMUI3D.r_dsgraph_render_ui_3d();

	Target->u_setrt(Target->rt_ui_3d_icons->dwWidth, Target->rt_ui_3d_icons->dwHeight,
		nullptr, nullptr, nullptr, nullptr);
	if (Target->rt_ui_3d_icons_raw_msaa)
		HW.pContext->ResolveSubresource(Target->rt_ui_3d_icons_raw->pSurface, 0,
			Target->rt_ui_3d_icons_raw_msaa->pSurface, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
	Target->phase_ui_3d_icons_postprocess();

	Device.mProject3DIcons = ui_3d_icon_project_saved;
	Device.mFullTransform3DIcons = ui_3d_icon_full_transform_saved;
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, nullptr, nullptr, HW.pBaseZB);
	Target->set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
	RCache.set_Z(FALSE);
	ui_3d_icon_atlas_active = false;
}

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	// combine order, main clears the shared priority-1 forward lists last (keeps smoke + blended fx)
	const bool fwd_clear = svp_clear_shared_list(true);
	if (!fwd_clear) { if (ps_r__svp_stats) ++svp_stats_fwd_keep; svp_ledger_fwd_keep = 1; } // overlay + ledger proof a forward-list keep fired
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1, fwd_clear); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1, fwd_clear);
		GMBase.fade_render(); // faded-portals
		GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
		GMBase.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// Redotix99: for 3D Shader Based Scopes
void CRender::render_Reticle()
{
	VERIFY(0 == GMBase.RGraph.mapHUDSorted.Distort.size() + GMBase.RGraph.mapStaticSorted.Distort.size() + GMBase.RGraph.mapDynamicSorted.Distort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;

	GMBase.r_dsgraph_render_ScopeSorted();

	RImplementation.o.distortion = FALSE;
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3DTexture2D* pBuffer = nullptr;
	HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBuffer);
	HW.pContext->CopyResource((*RT)->pSurface, pBuffer);
	pBuffer->Release();
}

u32 CRender::ScopeBegin(LPCWSTR name)
{
	return ps_r__gpu_timeline ? gpu_timeline::scope_begin(name) : gpu_timeline::no_scope;
}

void CRender::ScopeEnd(u32 scope)
{
	if (scope != gpu_timeline::no_scope)
		gpu_timeline::scope_end(scope);
}
