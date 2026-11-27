#include "stdafx.h"
#pragma hdrstop

#include "detailmanager.h"

#ifdef _EDITOR
#	include "igame_persistent.h"
#	include "environment.h"
#else
#	include "../../xrEngine/igame_persistent.h"
#	include "../../xrEngine/environment.h"
#endif

#include "../xrRenderDX10/dx10BufferUtils.h"

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

// pip set by the main gbuffer pass while a SVP pass follows, the SVP drain then clears the set
bool g_svp_defer_detail_clear = false;

static D3DVERTEXELEMENT9 dwDecl[] =
{
	{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos
	{0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // uv
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertHW
{
	float x, y, z;
	short u, v, t, mid;
};
#pragma pack(pop)

short QC(float v)
{
	int t = iFloor(v * float(quant));
	clamp(t, -32768, 32767);
	return short(t & 0xffff);
}

void CDetailManager::hw_Load()
{
	hw_Load_Geom();
	hw_Load_Shaders();
}

#ifdef USE_DX11
// checks every element the instanced path draws for the instance record index and its record size
u32 CDetailManager::hw_Probe_Instance_Shaders()
{
	static shared_str strInstBase("dt_instance_base");

	for (u32 o = 0; o < objects.size(); o++)
	{
		Shader* S = objects[o]->shader._get();
		if (!S)
			continue;

		// lod 0 draws the wave variants and lod 1 the still one, both go through instanced draws
		for (u32 lod_id = 0; lod_id < 2; lod_id++)
		{
			ShaderElement* E = S->E[lod_id]._get();
			if (!E)
				continue;

			for (u32 p = 0; p < E->passes.size(); p++)
			{
				R_constant_table* T = E->passes[p]->constants._get();
				if (!T)
					return hw_probe_no_decode;

				R_constant* C = T->get(strInstBase);
				if (!C || 0 == (C->destination & RC_dest_vertex))
					return hw_probe_no_decode;

				// a stale shader keeps the name while decoding an older record layout
				if (T->dt_instance_size != u16(hw_InstanceStride))
					return hw_probe_bad_record;
			}
		}
	}
	return hw_probe_ok;
}
#endif

void CDetailManager::hw_Load_Geom()
{
	// Analyze batch-size
	hw_BatchSize = (u32(HW.Caps.geometry.dwRegisters) - c_hdr) / c_size;
	clamp(hw_BatchSize, (u32)0, (u32)64);
	Msg("* [DETAILS] VertexConsts(%d), Batch(%d)", u32(HW.Caps.geometry.dwRegisters), hw_BatchSize);

#ifdef USE_DX11
	// latch the instancing path for this level, an instanced draw needs one mesh copy not the baked batch
	hw_instancing = (ps_r__detail_instancing != 0);
	if (hw_instancing)
	{
		// the whole level stays on the legacy loop when any element fails the probe
		const u32 probe = hw_Probe_Instance_Shaders();
		if (probe == hw_probe_no_decode)
		{
			hw_instancing = false;
			Msg("! [DETAILS] instancing shaders missing, legacy path");
		}
		else if (probe == hw_probe_bad_record)
		{
			hw_instancing = false;
			Msg("! [DETAILS] instance record mismatch, legacy path");
		}
	}
	// which shader elements each detail object carries, element 2 is the depth only shadow one
	for (u32 o = 0; o < objects.size(); o++)
	{
		Shader* S = objects[o]->shader._get();
		if (!S)
			continue;
		Msg("* [DETAILS] %s e0 %u e1 %u e2 %u e3 %u e4 %u", objects[o]->shader_name.c_str(),
		    S->E[0]._get() ? 1u : 0u, S->E[1]._get() ? 1u : 0u, S->E[2]._get() ? 1u : 0u,
		    S->E[3]._get() ? 1u : 0u, S->E[4]._get() ? 1u : 0u);
	}
	hw_overflow_logged = false;
	const u32 dwCopies = hw_instancing ? 1 : hw_BatchSize;
#else
	const u32 dwCopies = hw_BatchSize;
#endif

	// Pre-process objects
	u32 dwVerts = 0;
	u32 dwIndices = 0;
	for (u32 o = 0; o < objects.size(); o++)
	{
		const CDetail& D = *objects[o];
		dwVerts += D.number_vertices * dwCopies;
		dwIndices += D.number_indices * dwCopies;
	}
	u32 vSize = sizeof(vertHW);
	Msg("* [DETAILS] %d v(%d), %d p", dwVerts, vSize, dwIndices / 3);

#if !defined(USE_DX10) && !defined(USE_DX11)
	// Determine POOL & USAGE
	u32 dwUsage = D3DUSAGE_WRITEONLY;

	// Create VB/IB
	R_CHK(HW.pDevice->CreateVertexBuffer (dwVerts*vSize,dwUsage,0,D3DPOOL_MANAGED,&hw_VB,0));
	HW.stats_manager.increment_stats_vb(hw_VB);
	R_CHK(HW.pDevice->CreateIndexBuffer (dwIndices*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_MANAGED,&hw_IB,0));
	HW.stats_manager.increment_stats_ib(hw_IB);

#endif	//	USE_DX10
	Msg("* [DETAILS] Batch(%d), VB(%dK), IB(%dK)", hw_BatchSize, (dwVerts * vSize) / 1024, (dwIndices * 2) / 1024);

	// Fill VB
	{
		vertHW* pV;
#if defined(USE_DX10) || defined(USE_DX11)
		vertHW* pVOriginal;
		pVOriginal = xr_alloc<vertHW>(dwVerts);
		pV = pVOriginal;
#else	//	USE_DX10
		R_CHK(hw_VB->Lock(0,0,(void**)&pV,0));
#endif	//	USE_DX10
		for (u32 o = 0; o < objects.size(); o++)
		{
			const CDetail& D = *objects[o];
			for (u32 batch = 0; batch < dwCopies; batch++)
			{
				u32 mid = batch * c_size;
				for (u32 v = 0; v < D.number_vertices; v++)
				{
					const Fvector& vP = D.vertices[v].P;
					pV->x = vP.x;
					pV->y = vP.y;
					pV->z = vP.z;
					pV->u = QC(D.vertices[v].u);
					pV->v = QC(D.vertices[v].v);
					pV->t = QC(vP.y / (D.bv_bb.max.y - D.bv_bb.min.y));
					pV->mid = short(mid);
					pV++;
				}
			}
		}
#if defined(USE_DX10) || defined(USE_DX11)
		R_CHK(dx10BufferUtils::CreateVertexBuffer(&hw_VB, pVOriginal, dwVerts*vSize));
		HW.stats_manager.increment_stats_vb(hw_VB);
		xr_free(pVOriginal);
#else	//	USE_DX10
		R_CHK(hw_VB->Unlock());
#endif	//	USE_DX10
	}

	// Fill IB
	{
		u16* pI;
#if defined(USE_DX10) || defined(USE_DX11)
		u16* pIOriginal;
		pIOriginal = xr_alloc<u16>(dwIndices);
		pI = pIOriginal;
#else	//	USE_DX10
		R_CHK(hw_IB->Lock(0,0,(void**)(&pI),0));
#endif	//	USE_DX10
		for (u32 o = 0; o < objects.size(); o++)
		{
			const CDetail& D = *objects[o];
			u16 offset = 0;
			for (u32 batch = 0; batch < dwCopies; batch++)
			{
				for (u32 i = 0; i < u32(D.number_indices); i++)
					*pI++ = u16(u16(D.indices[i]) + u16(offset));
				offset = u16(offset + u16(D.number_vertices));
			}
		}
#if defined(USE_DX10) || defined(USE_DX11)
		R_CHK(dx10BufferUtils::CreateIndexBuffer(&hw_IB, pIOriginal, dwIndices*2));
		HW.stats_manager.increment_stats_ib(hw_IB);
		xr_free(pIOriginal);
#else	//	USE_DX10
		R_CHK(hw_IB->Unlock());
#endif	//	USE_DX10
	}

	// Declare geometry
	hw_Geom.create(dwDecl, hw_VB, hw_IB);

#ifdef USE_DX11
	if (hw_instancing)
	{
		hw_inst_base.assign(3 * objects.size(), 0);
		hw_inst_count.assign(3 * objects.size(), 0);
		hw_run_base.assign(3 * objects.size(), 0);
		hw_run_count.assign(3 * objects.size(), 0);

		// expected instances per populated slot from the packed palette coverage over the decompress grid
		const u32 grid = u32(iCeil(dm_slot_size / ps_r__Detail_density)) + 1;
		const u32 cells = grid * grid;
		double items_total = 0.0;
		u32 slots_used = 0;
		const u32 slot_count = dtSlots ? (dtH.size_x * dtH.size_z) : 0;
		for (u32 i = 0; i < slot_count; i++)
		{
			DetailSlot& DS = dtSlots[i];
			double cover = 0.0;
			for (int p = 0; p < dm_obj_in_slot; p++)
			{
				if (DS.r_id(p) == DetailSlot::ID_Empty) continue;
				const DetailPalette& pal = DS.palette[p];
				cover += double(pal.a0 + pal.a1 + pal.a2 + pal.a3) / 60.0;
			}
			if (cover <= 0.0) continue;
			items_total += cover * double(cells);
			slots_used++;
		}
		const double per_slot = slots_used ? (items_total / double(slots_used)) : 0.0;
		double want = ceil(1.15 * double(dm_cache_size) * 0.997 * per_slot);
		if (want < double(1 << 18)) want = double(1 << 18);
		if (want > double(1 << 20)) want = double(1 << 20);
		hw_instance_cap = u32(want);

		D3D_BUFFER_DESC idesc;
		ZeroMemory(&idesc, sizeof(idesc));
		idesc.ByteWidth = hw_instance_cap * hw_InstanceStride;
		idesc.Usage = D3D_USAGE_DYNAMIC;
		idesc.BindFlags = D3D_BIND_SHADER_RESOURCE;
		idesc.CPUAccessFlags = D3D_CPU_ACCESS_WRITE;
		idesc.MiscFlags = D3D_RESOURCE_MISC_BUFFER_STRUCTURED;
		idesc.StructureByteStride = hw_InstanceStride;
		R_CHK(HW.pDevice->CreateBuffer(&idesc, 0, &hw_instanceVB));
		HW.stats_manager.increment_stats_vb(hw_instanceVB);

		D3D_SHADER_RESOURCE_VIEW_DESC sdesc;
		ZeroMemory(&sdesc, sizeof(sdesc));
		sdesc.Format = DXGI_FORMAT_UNKNOWN;
		sdesc.ViewDimension = D3D_SRV_DIMENSION_BUFFER;
		sdesc.Buffer.FirstElement = 0;
		sdesc.Buffer.NumElements = hw_instance_cap;
		R_CHK(HW.pDevice->CreateShaderResourceView(hw_instanceVB, &sdesc, &hw_instanceSRV));

		hw_frame_filled = u32(-1);
		Msg("* [DETAILS] InstanceVB(%dK), stride(%d), cap(%d), slot_avg(%.1f), slots(%d), shaders(ok)",
		    (hw_instance_cap * hw_InstanceStride) / 1024, u32(hw_InstanceStride), hw_instance_cap, per_slot,
		    slots_used);
	}
#endif
}

void CDetailManager::hw_Unload()
{
	// Destroy VS/VB/IB
	hw_Geom.destroy();
	HW.stats_manager.decrement_stats_vb(hw_VB);
	HW.stats_manager.decrement_stats_ib(hw_IB);
	_RELEASE(hw_IB);
	_RELEASE(hw_VB);
#ifdef USE_DX11
	_RELEASE(hw_instanceSRV);
	if (hw_instanceVB)
		HW.stats_manager.decrement_stats_vb(hw_instanceVB);
	_RELEASE(hw_instanceVB);
	hw_inst_base.clear();
	hw_inst_count.clear();
	hw_run_base.clear();
	hw_run_count.clear();
	hw_run_slots.clear();
	hw_run_frustum = nullptr;
	hw_run_site = hw_run_site_none;
	hw_instance_cap = 0;
	hw_frame_filled = u32(-1);
#endif
}

#if !defined(USE_DX10) && !defined(USE_DX11)
void CDetailManager::hw_Load_Shaders()
{
	// Create shader to access constant storage
	ref_shader S;
	S.create("details\\set");
	R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
	R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
	hwc_consts = T0.get("consts");
	hwc_wave = T0.get("wave");
	hwc_wind = T0.get("dir2D");
	hwc_array = T0.get("array");
	hwc_s_consts = T1.get("consts");
	hwc_s_xform = T1.get("xform");
	hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render(light* L)
{
	PROF_EVENT("CDetailManager::hw_Render");
	// Render-prepare
	//	Update timer
	//	Can't use RDEVICE.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
	float fDelta = RDEVICE.fTimeGlobal - m_global_time_old;
	if ((fDelta < 0) || (fDelta > 1)) fDelta = 0.03;
	m_global_time_old = RDEVICE.fTimeGlobal;

	m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
	m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
	m_time_pos += fDelta * swing_current.speed;

	//float		tm_rot1		= (PI_MUL_2*RDEVICE.fTimeGlobal/swing_current.rot1);
	//float		tm_rot2		= (PI_MUL_2*RDEVICE.fTimeGlobal/swing_current.rot2);
	float tm_rot1 = m_time_rot_1;
	float tm_rot2 = m_time_rot_2;

	Fvector4 dir1, dir2;
	dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
	dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

	// Setup geometry and DMA
	RCache.set_Geometry(hw_Geom);

	// Wave0
	float scale = 1.f / float(quant);
	Fvector4 wave;
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	RDEVICE.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	RCache.set_c(&*hwc_consts, scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient); // consts
	RCache.set_c(&*hwc_wave, wave.div(PI_MUL_2)); // wave
	RCache.set_c(&*hwc_wind, dir1); // wind-dir
	hw_Render_dump(&*hwc_array, 1, 0, c_hdr, L);

	// Wave1
	//wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	RDEVICE.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	RCache.set_c(&*hwc_wave, wave.div(PI_MUL_2)); // wave
	RCache.set_c(&*hwc_wind, dir2); // wind-dir
	hw_Render_dump(&*hwc_array, 2, 0, c_hdr, L);

	// Still
	RCache.set_c(&*hwc_s_consts, scale, scale, scale, 1.f);
	RCache.set_c(&*hwc_s_xform, RDEVICE.mFullTransform);
	hw_Render_dump(&*hwc_s_array, 0, 1, c_hdr, L);
}

void CDetailManager::hw_Render_dump(ref_constant x_array, u32 var_id, u32 lod_id, u32 c_offset, light* L)
{
#if RENDER==R_R2
	if (RImplementation.phase == CRender::PHASE_SMAP && var_id == 0)
		return;
#endif

	RDEVICE.Statistic->RenderDUMP_DT_Count = 0;

	// Matrices and offsets
	u32 vOffset = 0;
	u32 iOffset = 0;

	vis_list& list = m_visibles[var_id];

	Fvector c_sun, c_ambient, c_hemi;
#ifndef _EDITOR
	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
	c_sun.mul(.5f);
	c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
	c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);
#else
	c_sun.set				(1,1,1);	c_sun.mul(.5f);
	c_ambient.set			(1,1,1);
	c_hemi.set				(1,1,1);
#endif

	VERIFY(objects.size()<=list.size());

	// pip the main-pass drain keeps the visible set when the SVP pass draws the scope grass second
	extern bool g_svp_defer_detail_clear;

	// Iterate
	for (u32 O = 0; O < objects.size(); O++)
	{
		CDetail& Object = *objects[O];
		xr_vector<SlotItemVec*>& vis = list[O];
		if (!vis.empty())
		{
			// Setup matrices + colors (and flush it as nesessary)
			RCache.set_Element(Object.shader->E[lod_id]);
			RImplementation.apply_lmaterial();
			u32 c_base = x_array->vs.index;
			Fvector4* c_storage = RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);

			u32 dwBatch = 0;

			xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
			xr_vector<SlotItemVec*>::iterator _vE = vis.end();
			for (; _vI != _vE; _vI++)
			{
				SlotItemVec* items = *_vI;
				SlotItemVecIt _iI = items->begin();
				SlotItemVecIt _iE = items->end();
				for (; _iI != _iE; _iI++)
				{
					SlotItem& Instance = **_iI;

					if (!RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector))
						continue;

#if RENDER==R_R2
					if (RImplementation.phase == CRender::PHASE_SMAP && L)
					{
						if (!L->GMLight.is_sector_visible(RImplementation.pOutdoorSector))
							continue;

						if (L->position.distance_to_sqr(Instance.position) >= _sqr(L->range))
							continue;
					}
#endif

					u32 base = dwBatch * 4;

					// Build matrix ( 3x4 matrix, last row - color )
					Fmatrix M = Instance.mRotY;
					const float sc = Instance.scale_calculated;
					M._11 *= sc; M._21 *= sc; M._31 *= sc;
					M._12 *= sc; M._22 *= sc; M._32 *= sc;
					M._13 *= sc; M._23 *= sc; M._33 *= sc;
					c_storage[base+0].set(M._11, M._21, M._31, M._41);
					c_storage[base+1].set(M._12, M._22, M._32, M._42);
					c_storage[base+2].set(M._13, M._23, M._33, M._43);

					// Build color
#if RENDER==R_R1
					Fvector C;
					C.set(c_ambient);
					//					C.mad					(c_lmap,Instance.c_rgb);
					C.mad(c_hemi, Instance.c_hemi);
					C.mad(c_sun, Instance.c_sun);
					c_storage[base + 3].set(C.x, C.y, C.z, 1.f);
#else
					// R2 only needs hemisphere
					float h = Instance.c_hemi;
					float s = Instance.c_sun;
					c_storage[base + 3].set(s, s, s, h);
#endif
					dwBatch ++;
					if (dwBatch == hw_BatchSize)
					{
						// flush
						RDEVICE.Statistic->RenderDUMP_DT_Count += dwBatch;
						u32 dwCNT_verts = dwBatch * Object.number_vertices;
						u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
						RCache.get_ConstantCache_Vertex().b_dirty = TRUE;
						RCache.get_ConstantCache_Vertex().get_array_f().dirty(c_base, c_base + dwBatch * 4);
						RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
						RCache.stat.r.s_details.add(dwCNT_verts);

						// restart
						dwBatch = 0;
					}
				}
			}
			// flush if nessecary
			if (dwBatch)
			{
				RDEVICE.Statistic->RenderDUMP_DT_Count += dwBatch;
				u32 dwCNT_verts = dwBatch * Object.number_vertices;
				u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
				RCache.get_ConstantCache_Vertex().b_dirty = TRUE;
				RCache.get_ConstantCache_Vertex().get_array_f().dirty(c_base, c_base + dwBatch * 4);
				RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
				RCache.stat.r.s_details.add(dwCNT_verts);
			}
			// Clean up
			// KD: we must not clear vis on r2 since we want details shadows
#if RENDER==R_R2
			if (!psDeviceFlags2.test(rsGrassShadow) || RImplementation.PHASE_NORMAL == RImplementation.phase) // phase normal without shadows
			vis.clear_not_free();
#else
			if (!g_svp_defer_detail_clear)
				vis.clear_not_free();
#endif
		}
		vOffset += hw_BatchSize * Object.number_vertices;
		iOffset += hw_BatchSize * Object.number_indices;
	}
}

#endif	//	USE_DX10
