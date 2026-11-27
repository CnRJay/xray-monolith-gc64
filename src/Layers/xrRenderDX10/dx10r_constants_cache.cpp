#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/r_constants_cache.h"

dx10ConstantBuffer& R_constants::GetCBuffer(R_constant* C, BufferType BType)
{
	PROF_EVENT("R_constants::GetCBuffer");
	if (BType == BT_PixelBuffer)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_pixel_cb_index_mask) >> RC_dest_pixel_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aPixelConstants[iBufferIndex]);
		return *RCache.m_aPixelConstants[iBufferIndex];
	}
	else if (BType == BT_VertexBuffer)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_vertex_cb_index_mask) >> RC_dest_vertex_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aVertexConstants[iBufferIndex]);
		return *RCache.m_aVertexConstants[iBufferIndex];
	}
	else if (BType == BT_GeometryBuffer)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_geometry_cb_index_mask) >> RC_dest_geometry_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aGeometryConstants[iBufferIndex]);
		return *RCache.m_aGeometryConstants[iBufferIndex];
	}
#ifdef USE_DX11
	else if (BType == BT_HullBuffer)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_hull_cb_index_mask) >> RC_dest_hull_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aHullConstants[iBufferIndex]);
		return *RCache.m_aHullConstants[iBufferIndex];
	}
	else if (BType == BT_DomainBuffer)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_domain_cb_index_mask) >> RC_dest_domain_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aDomainConstants[iBufferIndex]);
		return *RCache.m_aDomainConstants[iBufferIndex];
	}
	else if (BType == BT_Compute)
	{
		//	Decode index
		int iBufferIndex = (C->destination & RC_dest_compute_cb_index_mask) >> RC_dest_compute_cb_index_shift;

		VERIFY(iBufferIndex<CBackend::MaxCBuffers);
		VERIFY(RCache.m_aComputeConstants[iBufferIndex]);
		return *RCache.m_aComputeConstants[iBufferIndex];
	}
#endif

	FATAL("Unreachable code");
	//Just hack to avoid warning;
	dx10ConstantBuffer* ptr = 0;
	return *ptr;
}

// maps only the bound slots this stage recorded and only when the buffer says it changed
static void flush_cb_stage(u32 stage, ref_cbuffer* slots)
{
	const u8 count = RCache.m_cb_slot_count[stage];
	const u8* list = RCache.m_cb_slots[stage];
	for (u8 n = 0; n < count; ++n)
	{
		dx10ConstantBuffer* cb = &*slots[list[n]];
		if (cb->IsChanged())
			cb->Flush();
	}
}

// reports one slot per frame that the list walk left dirty
static void verify_cb_stage(const char* stage, ref_cbuffer* slots, u32 i)
{
	if (!slots[i] || !slots[i]->IsChanged()) return;
	static u32 last_frame = u32(-1);
	if (last_frame == Device.dwFrame) return;
	last_frame = Device.dwFrame;
	Msg("! [CBDIRTY] %s slot %u missed by the dirty list", stage, i);
	VERIFY(!"cb dirty list missed a bound buffer");
}

void R_constants::flush_cache()
{
	PROF_EVENT("R_constants::flush_cache");
	extern int ps_r__cb_dirty_list;
	const int mode = ps_r__cb_dirty_list;

	if (mode)
	{
		flush_cb_stage(CBackend::CBStage_Vertex, RCache.m_aVertexConstants);
		flush_cb_stage(CBackend::CBStage_Pixel, RCache.m_aPixelConstants);
		flush_cb_stage(CBackend::CBStage_Geometry, RCache.m_aGeometryConstants);
#ifdef USE_DX11
		flush_cb_stage(CBackend::CBStage_Hull, RCache.m_aHullConstants);
		flush_cb_stage(CBackend::CBStage_Domain, RCache.m_aDomainConstants);
		flush_cb_stage(CBackend::CBStage_Compute, RCache.m_aComputeConstants);
#endif
		if (mode < 2)
			return;
	}

	for (int i = 0; i < CBackend::MaxCBuffers; ++i)
	{
		if (mode)
		{
			verify_cb_stage("vs", RCache.m_aVertexConstants, i);
			verify_cb_stage("ps", RCache.m_aPixelConstants, i);
			verify_cb_stage("gs", RCache.m_aGeometryConstants, i);
#ifdef USE_DX11
			verify_cb_stage("hs", RCache.m_aHullConstants, i);
			verify_cb_stage("ds", RCache.m_aDomainConstants, i);
			verify_cb_stage("cs", RCache.m_aComputeConstants, i);
#endif
		}

		if (RCache.m_aVertexConstants[i])
			RCache.m_aVertexConstants[i]->Flush();

		if (RCache.m_aPixelConstants[i])
			RCache.m_aPixelConstants[i]->Flush();

		if (RCache.m_aGeometryConstants[i])
			RCache.m_aGeometryConstants[i]->Flush();

#ifdef USE_DX11
		if (RCache.m_aHullConstants[i])
			RCache.m_aHullConstants[i]->Flush();

		if (RCache.m_aDomainConstants[i])
			RCache.m_aDomainConstants[i]->Flush();

		if (RCache.m_aComputeConstants[i])
			RCache.m_aComputeConstants[i]->Flush();
#endif
	}
}

/*
void R_constants::flush_cache()
{
	if (a_pixel.b_dirty)
	{
		// fp
		R_constant_array::t_f&	F	= a_pixel.c_f;
		{
			//if (F.r_lo() <= 32) //. hack
			{		
				void	*pBuffer;
				const int iVectorElements = 4;
				const int iVectorNumber = 256;
				RCache.m_pPixelConstants->Map(D3Dxx_MAP_WRITE_DISCARD, 0, &pBuffer);
				CopyMemory(pBuffer, F.access(0), iVectorNumber*iVectorElements*sizeof(float));
				RCache.m_pPixelConstants->Unmap();
			}
		}
		a_pixel.b_dirty		= false;
	}
	if (a_vertex.b_dirty)
	{
		// fp
		R_constant_array::t_f&	F	= a_vertex.c_f;
		{
			u32		count		= F.r_hi()-F.r_lo();
			if (count)			{
#ifdef DEBUG
				if (F.r_hi() > HW.Caps.geometry.dwRegisters)
				{
					Debug.fatal(DEBUG_INFO,"Internal error setting VS-constants: overflow\nregs[%d],hi[%d]",
						HW.Caps.geometry.dwRegisters,F.r_hi()
						);
				}
				PGO		(Msg("PGO:V_CONST:%d",count));
#endif
				{		
					void	*pBuffer;
					const int iVectorElements = 4;
					const int iVectorNumber = 256;
					RCache.m_pVertexConstants->Map(D3Dxx_MAP_WRITE_DISCARD, 0, &pBuffer);
					CopyMemory(pBuffer, F.access(0), iVectorNumber*iVectorElements*sizeof(float));
					RCache.m_pVertexConstants->Unmap();
				}
				F.flush	();
			}
		}
		a_vertex.b_dirty	= false;
	}
}
*/
