#include "stdafx.h"
#include "../xrRender/svp_console.h"

#if defined(USE_DX11)

// gen digit kept, tube class forced to the centered single so no mask furniture reaches the lens
static float svp_nvg_center_tubes(float packed)
{
	return floorf(packed) + 0.10f;
}

bool CRenderTarget::svp_nvg_pass()
{
	extern Fvector4 ps_dev_param_8;
	const bool active = ps_dev_param_8.x >= 1.f;
	const bool split = ps_r__svp_nvg_objective && active && Device.true_pip_on
		&& Device.m_SecondViewport.IsSVPActive()
		&& this == RImplementation.TargetMain
		&& !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty();
	if (!split)
		return false;

	PIX_EVENT(svp_nvg_split);
	EnsureScopeShaders();

	if (ps_r__svp_stats)
		++svp_stats_nvg_split;

	const u32 color = color_rgba(0, 0, 0, 255);
	const float width = float(Device.dwWidth);
	const float height = float(Device.dwHeight);
	u32 offset = 0;
	ref_rt& destination = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

	u_setrt(destination, nullptr, nullptr, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);

	auto draw_quad = [&]()
	{
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
		pv->set(0, height, EPS_S, 1.f, color, 0.f, 1.f); pv++;
		pv->set(0, 0, EPS_S, 1.f, color, 0.f, 0.f); pv++;
		pv->set(width, height, EPS_S, 1.f, color, 1.f, 1.f); pv++;
		pv->set(width, 0, EPS_S, 1.f, color, 1.f, 0.f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	};

	RCache.set_Element(s_nightvision->E[0]);
	RCache.set_ColorWriteEnable(0);
	RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0x80,
		D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	draw_quad();

	draw_scope(s_svp_distort_stamp, []()
	{
		RCache.set_c("scope_phase", 0);
		RCache.set_ColorWriteEnable(0);
		RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x80, 0xff, 0x80,
			D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	});
	RCache.set_ColorWriteEnable();

	// lens region draws the mod's own centered tube class, interior mask 1 across the glass
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
	RCache.set_c("shader_param_8", svp_nvg_center_tubes(ps_dev_param_8.x),
		ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x80, 0x80, 0x00);
	draw_quad();

	// wearer region keeps the authored tube class, set explicitly since the element may stay bound
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
	RCache.set_c("shader_param_8", ps_dev_param_8.x,
		ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x00, 0x80, 0x00);
	draw_quad();

	RCache.set_Stencil(FALSE);
	HW.pContext->CopyResource(rt_Generic_0->pSurface, destination->pSurface);

	if (ps_r__svp_diag)
	{
		static u32 last_log = 0;
		if (Device.dwTimeGlobal - last_log > 1000)
		{
			last_log = Device.dwTimeGlobal;
			const float generation = floorf(ps_dev_param_8.x);
			PipMsg("[SVP-NVG] split=1 gen=%.0f tubes=%.1f lens=center msaa=%d",
				generation, (ps_dev_param_8.x - generation) * 10.f,
				RImplementation.o.dx10_msaa ? 1 : 0);
		}
	}

	return true;
}

#else

bool CRenderTarget::svp_nvg_pass()
{
	return false;
}

#endif
