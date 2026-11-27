#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"

#include	"../xrRender/dxRenderDeviceRender.h"
#include "../xrRenderDX10/dx10GpuTimeline.h"
#include "../../xrGame/debug_renderer.h"
#if USE_DX11
#include "../../../gamedata/shaders/r3/scope_defines.h"
#endif

namespace
{
bool ui_3d_icon_atlas_active = false;
Fmatrix ui_3d_icon_project_saved;
Fmatrix ui_3d_icon_full_transform_saved;
}

#if XRCORE_PROFILER == PROFILER_OPTICK
extern CRender::_stats g_prof_stats_tagged;
#endif

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

static void debug_scope(Fmatrix scope_camera)
{
	auto draw_circle = [](Fmatrix m, u32 color, bool bHud) -> void {
		int n = 100;
		Fvector v0;
		for (int i = 0; i < n + 1; i++)
		{
			float angle = float(i) / float(n) * PI * 2.0f;
			Fvector v1 = { cosf(angle), sinf(angle), 0.0f };
			m.transform(v1);
			if (i > 0) CDebugRenderer().draw_line({}, v0, v1, color, bHud);
			v0 = v1;
		}
	};

	auto draw_lens = [draw_circle](CRenderDevice::CSecondVPParams::Lens lens, u32 color) -> void {
		draw_circle(Fmatrix(lens.m_W).mulB_43(Fmatrix().scale(lens.radius, lens.radius, 0.0f)), color, true);

		Fvector v0 = { 0, 0, 0 };
		Fvector v1 = { 0, 0, 100 };
		lens.m_W.transform(v0);
		lens.m_W.transform(v1);

		CDebugRenderer().draw_line(Fmatrix().identity(), v0, v1, color, true);
	};

	auto draw_camera = [scope_camera, draw_circle](u32 color) -> void {
		auto cm = 1.0f / 100.0f;
		draw_circle(Fmatrix(scope_camera).mulB_43(Fmatrix().scale(0.25f * cm, 0.25f * cm, 0.0f)), color, true);
	};

	auto p = Device.m_SecondViewport;
	draw_lens(p.eyepiece, 0xff0000ff);
	draw_lens(p.objective, 0xffffff00);
	draw_camera(0xffffffff);
}

static void svpCamera()
{
	float svp_fov = g_pGamePersistent->m_pGShaderConstants->hud_params.y * 0.75f;
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);

	auto mm = Device.matrices[0];
	auto params = Device.m_SecondViewport;

	// Project into NDC space to determine the amount of extra magnification we need to correct scope camera size
	Fvector4 top, bot;
	Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
	m_WVP.transform(top, { 0, params.eyepiece.radius, 0, 1 });
	m_WVP.transform(bot, { 0, -params.eyepiece.radius, 0, 1 });
	top.div(top.w);
	bot.div(bot.w);
	float scope_height_NDC = _abs(top.y - bot.y);
	float screen_height_NDC = 2.0f;
	float ratio_magnification = (scope_height_NDC > EPS) ? (screen_height_NDC / scope_height_NDC) : 1.0f;

	// The magnification of the scope (1X 4X etc)
	float scope_magnification = (svp_fov > EPS) ? (fov / deg2rad(svp_fov)) : 1.0f;

	// The fov we need to render at in order to have correct zoom
	float vFov = 2.0f * atanf(tanf(fov * 0.5f) / (ratio_magnification * scope_magnification));

	// The fov we need for camera placement
	float vFovMagOnly = 2.0f * atanf(tanf(fov * 0.5f) / scope_magnification);

	auto camera_offset_from_vfov_and_radius = [](float vFov, float radius) -> float {
		return radius / tanf(vFov / 2.0f);
	};

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W; // By default we use the eyepiece for camera placement
	if (scope_svp_enabled >= 2 && params.objective.radius > EPS)
	{
		// compute camera for objective lens placement
		auto d = camera_offset_from_vfov_and_radius(vFovMagOnly, params.objective.radius);
		m_W_svpcam = Fmatrix().mul(params.objective.m_W, Fmatrix().translate(0, 0, -d));
		near_plane = d;
	}

	auto aspect = float(RImplementation.TargetSVP->Width) / float(RImplementation.TargetSVP->Height);

	float fNearPlane_hud, fFarPlane_hud;
	Device.matrices[0].mProject.decompose_projection(_, _, fNearPlane_hud, fFarPlane_hud);
	auto svp_proj = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane);
	auto svp_proj_hud = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane_hud);

	if (scope_debug >= 2)
		debug_scope(m_W_svpcam);

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;
}

extern int ps_r4_parallel_cull;

void CRender::renderGBuffer()
{
	PIX_EVENT(RENDER_GBUFFER);
	Device.dwViewport++;
	CDSGraphManager& GM = ActiveGM();

	//******* Main calc - DEFERRED RENDERER
	phase = PHASE_NORMAL;

	// SVP HACK: Use main frame view matrix to prevent rendering the wrong sector
	auto main_ft = Fmatrix().mul(Device.mProject, Device.matrices[0].mView);
	ViewBase.CreateFromMatrix(main_ft, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

	GM.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, main_ft);
	if (ps_r4_parallel_cull && !PortalTraverseDbg_Enabled())
	{
		main_task_static.run([&GM]() { GM.r_dsgraph_capture_static(); });
		GM.r_dsgraph_capture_dynamic();
		Target->phase_scene_prepare();
		main_task_static.wait();
	}
	else
	{
		GM.r_dsgraph_capture_static();
		GM.r_dsgraph_capture_dynamic();
		Target->phase_scene_prepare();
	}

	if (RImplementation.o.ssfx_motionvectors)
	{
		PIX_EVENT(DEFER_SKY_MVECTORS);
		Target->u_setrt(Device.dwWidth, Device.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

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
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? Target->baseZB : Target->rt_MSAADepth->pZRT);
	}

	//******* Main render :: PART-0 -- first
	{
		PIX_EVENT_C(DEFER_PART0_SPLIT, dx10_marker_gbuffer);
		Target->phase_scene_begin();

		// Early HUD & Scope Lens capture/render
		{
			PIX_EVENT(RENDER_HUD_EARLY);

			if (Target == TargetMain)
			{
				GM.r_dsgraph_capture_hud();

				{
					PIX_EVENT(SCOPE_WRITE_LENS_DEPTH);
					// Write lens depth
					Target->draw_scope(Target->s_scope_depth_write, [&](auto N) -> void {
						// Mask the lens so we know what to push for svpscope 0
						// This must contain 0x01 to prevent the skybox from rendering into the diffuse buffer
						RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x3, 0x3, 0x3, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
						RCache.set_c("scope_phase", SCOPE_PHASE_GBUFFER);
						RCache.set_c("scope_depth_value", 0.f);
					});
				}

				svpCamera();
			}
			else
			{
				GM.r_dsgraph_capture_hud();
			}

			{
				PIX_EVENT(RENDER_HUD);
				RCache.set_ZFunc(D3DCMP_LESS);
				GM.r_dsgraph_render_hud();
				RCache.set_ZFunc(D3DCMP_LESSEQUAL);
			}

			if (Target == TargetMain && !Device.m_SecondViewport.IsSVPActive())
			{
				PIX_EVENT(SCOPE_HOLEPUNCH);
				// Clear depth anywhere the hud does not occlude the lens
				// which allows g-buffer to populate for us to sample later.
				Target->draw_scope(Target->s_scope_depth_write, [&](auto _) -> void {
					RImplementation.rmNormal();
					// holepunch everything that is not occluded by closer objects, and is part of the lens
					// making sure to clear the stencil to zero, so that the skybox can render.
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x3, 0x3, 0x3, D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO, D3DSTENCILOP_KEEP);
					RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE);
					RCache.set_c("scope_depth_value", 1.f);
				});
			}
		}

		Target->phase_scene_begin();
		GM.r_dsgraph_render_static(0);
		GM.r_dsgraph_render_dynamic(0);
		Target->disable_aniso();
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	bool locked = scope_debug == 4;
	if (!locked)
	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		if (Target == TargetMain)
		{
			LP_normal.clear();
			LP_pending.clear();
			GM.r_dsgraph_capture_lights();
		}
		else
		{
			// Issue SVP occlusion queries for Main's shadowed lights, then capture any SVP-only lights
			for (light* L : LP_normal.v_shadowed)
				L->vis_prepare();
			GM.r_dsgraph_capture_lights();
		}
	}

	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT_C(DEFER_PART1_SPLIT, dx10_marker_gbuffer);
		Target->phase_scene_begin();
		GM.r_dsgraph_render_lods(true, true);
		if (Details)
		{
			PIX_EVENT(CDETAILMANAGER_RENDER);
			Details->Render();
		}
		Target->phase_scene_end();
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

	// DX10 rain
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		if (!Device.m_SecondViewport.IsSVPFrame())
			shadowmap_rain();
		render_rain();
	}

	// Save previous and current matrices
	{
		static Fmatrix mm_saved_viewproj[2];
		const int vp = Device.m_SecondViewport.IsSVPFrame() ? 1 : 0;

		Target->GetPrevious()->Matrix_previous.mul(mm_saved_viewproj[vp], Device.mInvView);
		Target->GetPrevious()->Matrix_current.set(Device.mProject);
		mm_saved_viewproj[vp].set(Device.mFullTransform);
	}

	if (RImplementation.o.ssfx_sss)
	{
		static bool sss_rendered, sss_extended_rendered;

		// SSS Shadows
		if (ps_ssfx_sss_quality.z > 0)
		{
			PIX_EVENT(phase_ssfx_sss);
			Target->phase_ssfx_sss();
			sss_rendered = true;
		}
		else
		{
			if (sss_rendered) // Clear buffer
			{
				sss_rendered = false;
				FLOAT ColorRGBA[4] = { 1, 1, 1, 1 };
				HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
			}
		}

		if (ps_ssfx_sss_quality.w > 0)
		{
			if (!(Device.m_SecondViewport.IsSVPFrame() && ps_r__svp_quality == 0))
			{
				PIX_EVENT(phase_ssfx_sss_ext);
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
				sss_extended_rendered = true;
			}
		}
		else
		{
			if (sss_extended_rendered) // Clear buffer
			{
				sss_extended_rendered = false;
				FLOAT ColorRGBA[4] = { 1, 1, 1, 1 };
				HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
			}
		}
	}
}

void CRender::combineLightingAndBloom()
{
	CDSGraphManager& GM = ActiveGM();

	// FIXME: SVP: Hack to force SRVSManager to unbind the first slot.
	//  Required until more robust state invalidation is in place.
	auto unbind_s_base = []() -> void {
		ID3D11ShaderResourceView* crv[1] = { nullptr };
		HW.pContext->PSSetShaderResources(0, 1, crv);
		SRVSManager.SetPSResource(0, nullptr); // sync cache with direct write, or accum_spot may skip rebinding s_smap
	};

	{
		PIX_EVENT_C(DEFER_SELF_ILLUM, dx10_marker_lights);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos -
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();

		unbind_s_base();

		GM.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	if (RImplementation.o.ssfx_bloom && !Device.m_SecondViewport.IsSVPFrame())
	{
		PIX_EVENT(DEFER_EMISSIVE_BLOOM);
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0, 0, 0, 0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? Target->baseZB : Target->rt_MSAADepth->pZRT);

		unbind_s_base();

		// Keep regular HUD geometry out of the emissive target.
		GM.r_dsgraph_render_emissive(true, false);
	}
}

// Sun uses the entire depth texture, so we have to do an interleaved pass here
void CRender::renderSun()
{
	if (bSUN)
	{
		PIX_EVENT_C(DEFER_SUN, dx10_marker_lights);
		RImplementation.stats.l_visible++;
		render_sun_cascades();
	}
}

void CRender::renderShadowmaps()
{
	PIX_EVENT(RENDER_SHADOWMAPS);
	render_lights_shadowmaps(LP_normal);
}

// Combine runs in inverted order (svp -> main)
void CRender::combineGBuffer()
{
	PIX_EVENT(COMBINE_GBUFFER);
	Device.dwViewport++;

	{
		PIX_EVENT_C(DEFERRED_LIGHTS, dx10_marker_lights);
		Target->phase_accumulator();
		render_lights(LP_normal);
	}

	{
		if (RImplementation.o.ssfx_volumetric)
		{
			PIX_EVENT(phase_ssfx_volumetric_blur);
			Target->phase_ssfx_volumetric_blur();
		}
	}

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT_C(DEFER_LIGHT_COMBINE, dx10_marker_lights);
		Target->phase_combine();
	}
}

void CRender::Render()
{
	PIX_EVENT_C(CRender_Render, dx10_marker_frame);
	dx10_annotate_frame();

	rmNormal();

	TargetMain->SetActive();

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
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights) // critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	if ((Device.dwFrame % (u32)ps_r__tex_evict_interval) == 0)
		dxRenderDeviceRender::Instance().Resources->EvictStalledTextures();

	// Configure
	RImplementation.o.distortion = FALSE; // disable distortion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	HOM.MT_RENDER();

	// Clear the stored lenses
	GMBase.RGraph.mapScopeHUDSorted.clear();
	GMBase.RGraph.mapReflexHUDSorted.clear();
	GMBaseSVP.RGraph.mapScopeHUDSorted.clear();
	GMBaseSVP.RGraph.mapReflexHUDSorted.clear();

	Device.m_SecondViewport.eyepiece.radius = 0;
	Device.m_SecondViewport.objective.radius = 0;

	auto mainCameraPos = Device.vCameraPosition;
	TargetMain->SetActive();
	{
		PIX_EVENT(DRAW_MAIN);
		renderGBuffer();
	}

	if (Device.m_SecondViewport.IsSVPActive())
	{
		TargetSVP->SetActive();
		{
			PIX_EVENT(DRAW_SVP);
			// SVP HACK: Use main frame view matrix to prevent rendering the wrong sector
			Device.vCameraPosition = mainCameraPos;
			renderGBuffer();
		}
	}

	{
		PIX_EVENT(RENDER_SUN);
		TargetMain->SetActive();
		renderSun();
	}

	{
		PIX_EVENT(COMBINE_GBUFFER_CONT);
		TargetMain->SetActive();
		combineLightingAndBloom();
		if (Device.m_SecondViewport.IsSVPActive())
		{
			TargetSVP->SetActive();
			combineLightingAndBloom();
		}
	}

	{
		PIX_EVENT(DRAW_SHADOWMAPS);
		TargetMain->SetActive();
		renderShadowmaps();
	}

	if (Device.m_SecondViewport.IsSVPActive())
	{
		TargetSVP->SetActive();
		{
			PIX_EVENT(COMBINE_SVP);

			// NVG shader is required, but tube overlay is not
			//   so we increase the size to the max that doesn't break the shader
			auto nvg_tube_radius = ps_dev_param_7.y;
			ps_dev_param_7.y = 0.99f;
			combineGBuffer();
			ps_dev_param_7.y = nvg_tube_radius;
		}
	}

	TargetMain->SetActive();
	{
		PIX_EVENT(COMBINE_MAIN);
		combineGBuffer();
	}

	Target->phase_scope_debug();

	if (Details)
		Details->details_clear();

#if XRCORE_PROFILER == PROFILER_OPTICK
	// Counts since the last tag, the stats screen zeroes these counters
	PROF_TAG("lights visible", stats.l_visible - g_prof_stats_tagged.l_visible);
	PROF_TAG("lights shadowed", stats.ls_shadowed_rendered - g_prof_stats_tagged.ls_shadowed_rendered);
	PROF_TAG("occlusion queries", stats.o_queries - g_prof_stats_tagged.o_queries);
	PROF_TAG("occlusion culled", stats.o_culled - g_prof_stats_tagged.o_culled);
	g_prof_stats_tagged = stats;
#endif
}
#include "../xrRender/CHudInitializer.h"

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
	// Match the allocated atlas aspect ratio to keep model proportions intact.
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
	// The MT graph retains every submission, including repeated visuals in different cells.
}

void CRender::EndUI3DIconAtlas()
{
	if (!ui_3d_icon_atlas_active)
		return;

	RCache.set_Z(FALSE);
	GMUI3D.r_dsgraph_render_ui_3d();

	// The postprocess expects an ordinary Texture2D. Resolve the multisampled
	// model atlas after all cells have been rendered, before sampling it.
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
	CDSGraphManager& GM = ActiveGM();

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GM.r_dsgraph_render_static(1); // normal level, secondary priority
		CParticlesAsync::Wait();
		GM.r_dsgraph_render_dynamic(1);
		GM.fade_render(); // faded-portals
		GM.r_dsgraph_render_sorted(false); // strict-sorted geoms
		//g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts

		// Item and camera attachment UI draws before the sorted HUD so nearer glass and the post chain cover it
		if (g_hud)
		{
			PROF_EVENT("render_hud", PROF_CAT(Rendering));
			if (g_hud->RenderActiveItemUIQuery())
				GM.r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				GM.r_dsgraph_render_cam_ui();
		}

		GM.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

void CRender::RenderToTarget(RRT target)
{
	PIX_EVENT(render_to_target);
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
