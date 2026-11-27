#include "stdafx.h"

void CRenderTarget::phase_occq()
{
	if (!RImplementation.o.dx10_msaa)
		u_setrt(Device.m_rt_view.dwWidth, Device.m_rt_view.dwHeight, baseRT, NULL, NULL, baseZB);
	else
		u_setrt(Device.m_rt_view.dwWidth, Device.m_rt_view.dwHeight,NULL,NULL,NULL, rt_MSAADepth->pZRT);
	RCache.set_Shader(s_occq);
	RCache.set_CullMode(CULL_CCW); //       
	RCache.set_Stencil(FALSE);           // We need the depth test fragment count
	RCache.set_ColorWriteEnable(FALSE);
}
