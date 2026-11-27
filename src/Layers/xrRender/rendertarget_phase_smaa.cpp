#include "stdafx.h"


void CRenderTarget::phase_smaa()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);
	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif

	//////////////////////////////////////////////////////////////////////////
	//Edge detection
	u_setrt(rt_smaa_edgetex, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x1, 0, 0, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
#if defined(USE_DX10) || defined(USE_DX11)	
	HW.pContext->ClearRenderTargetView(rt_smaa_edgetex->pRT, ColorRGBA);
#else
	CHK_DX( HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L) );
#endif

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_smaa->E[0]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	//Blending
	u_setrt(rt_smaa_blendtex, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x1, 0, 0, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
#if defined(USE_DX10) || defined(USE_DX11)	
	HW.pContext->ClearRenderTargetView(rt_smaa_blendtex->pRT, ColorRGBA);
#else
	CHK_DX( HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L) );	
#endif

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_smaa->E[1]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX11)
	ref_rt& dest_rt = post_dest_rt();
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#elif defined(USE_DX10)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif	

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_smaa->E[2]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if defined(USE_DX11)
	post_commit(dest_rt);
#elif defined(USE_DX10)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
}

#if defined(USE_DX11)

void CRenderTarget::phase_ssfx_taa()
{
	// a scope edge swaps the content source under the resolve, seed the history with the current
	// frame so a stale scene never ghosts through
	if (m_taa_seed_history)
	{
		m_taa_seed_history = false;
		svp_copy_begin(SVP_CP_HIST, rt_copy_bytes(rt_ssfx_prev_frame));
		HW.pContext->CopyResource(rt_ssfx_prev_frame->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());
		svp_copy_end(SVP_CP_HIST);
	}

	u32 Offset = 0;
	Fvector2 p0, p1;

	u32 C = color_rgba(255, 255, 255, 255);
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;

	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	// Temp motion vectors
	u_setrt(rt_ssfx_accum, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[0]);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	
	svp_copy_begin(SVP_CP_HIST, rt_copy_bytes(rt_ssfx_taa));
	HW.pContext->CopyResource(rt_ssfx_taa->pTexture->surface_get(), rt_ssfx_accum->pTexture->surface_get());
	svp_copy_end(SVP_CP_HIST);

	// stamp the raw TAA skip mask over the lens after prepare
	// the main resolve then keeps the SVP resolved image
	if (ps_r__svp_taa_mask && Device.true_pip_on && !Device.m_SecondViewport.m_render_pass_is_svp
		&& this == RImplementation.TargetMain && Device.m_SecondViewport.IsSVPActive()
		&& !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty())
	{
		PIX_EVENT(SVP_TAA_HARD_MASK);
		EnsureScopeShaders();
		u_setrt(rt_ssfx_motion_vectors, nullptr, nullptr, nullptr);
		const u32 calls_before = RCache.stat.calls;
		draw_scope(s_svp_taa_stamp, []()
		{
			RCache.set_c("scope_phase", 0);
			RCache.set_ColorWriteEnable(D3DCOLORWRITEENABLE_ALPHA);
		});
		RCache.set_ColorWriteEnable();
		const u32 draws = RCache.stat.calls - calls_before;
		if (draws && ps_r__svp_stats)
			++svp_stats_taa_stamp;
		if (draws && ps_r__svp_diag)
		{
			static u32 s_taa_mask_ms = 0;
			if (Device.dwTimeGlobal - s_taa_mask_ms > 1000)
			{
				s_taa_mask_ms = Device.dwTimeGlobal;
				auto& vp = Device.m_SecondViewport;
				const bool hybrid = vp.svp_reflex_capture_ok
					&& vp.svp_reflex_capture_epoch == vp.svp_optic_epoch
					&& vp.svp_reflex_capture_session == vp.GetSVPSession();
				PipMsg("[SVP-TAA] hard-mask target=motion-vectors draws=%u continuity=%d hybrid=%d capture_epoch=%u optic_epoch=%u frame=%u session=%u",
					draws, ps_r__svp_weapon_continuity, hybrid ? 1 : 0,
					vp.svp_reflex_capture_epoch, vp.svp_optic_epoch,
					Device.dwFrame, vp.GetSVPSession());
			}
		}
	}

	// TAA
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

	u_setrt(dest_rt, nullptr, nullptr, nullptr); // rt_ssfx_taa
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[1]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// Accumulate
	svp_copy_begin(SVP_CP_HIST, rt_copy_bytes(rt_ssfx_prev_frame));
	HW.pContext->CopyResource(rt_ssfx_prev_frame->pTexture->surface_get(), dest_rt->pTexture->surface_get());
	svp_copy_end(SVP_CP_HIST);

	// Sharpening phase
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[2]);
	
	RCache.set_c("taa_setup", ps_ssfx_taa);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

#endif
