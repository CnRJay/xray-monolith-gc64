#include "stdafx.h"
#include "dxUIRender.h"

#include "dxUIShader.h"
#include "dxRenderDeviceRender.h"

dxUIRender UIRenderImpl;

void dxUIRender::CreateUIGeom()
{
	hGeom_TL.create(FVF::F_TL, RCache.Vertex.Buffer(), 0);
	hGeom_LIT.create(FVF::F_LIT, RCache.Vertex.Buffer(), 0);
	hGeom_Font.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
}

void dxUIRender::DestroyUIGeom()
{
	m_recording_bucket = -1;
	m_recorded_shader = nullptr;
	for (int i = 0; i < kNumBuckets; ++i)
		m_buckets[i].clear();

    for (auto& it : g_UIShadersCache)
        it.second.destroy();
    g_UIShadersCache.clear();

	hGeom_TL = NULL;
	hGeom_LIT = NULL;
	hGeom_Font = NULL;
}

void dxUIRender::BeginRecord(int bucket, bool clear)
{
	VERIFY(bucket >= 0 && bucket < kNumBuckets);
	if (clear)
		m_buckets[bucket].clear();
	m_recording_bucket = bucket;
	m_recorded_shader = nullptr;
}

void dxUIRender::EndRecord()
{
	m_recording_bucket = -1;
	m_recorded_shader = nullptr;
}

void dxUIRender::RecordFontQuads(const ref_shader& shader, const FVF::TL* verts, u32 count)
{
	if (m_recording_bucket < 0 || !verts || count == 0)
		return;

	UICmdBucket& b = m_buckets[m_recording_bucket];
	u32 sh_idx = u32(-1);
	if (shader)
	{
		sh_idx = static_cast<u32>(b.shaders.size());
		b.shaders.push_back(shader);
	}
	const u32 offset = static_cast<u32>(b.tl_verts.size());
	b.tl_verts.insert(b.tl_verts.end(), verts, verts + count);

	UICmd cmd{};
	cmd.type = UICmd::cmdDrawFontQuads;
	cmd.shader_idx = sh_idx;
	cmd.vert_offset = offset;
	cmd.vert_count = count;
	b.cmds.push_back(cmd);
}

void dxUIRender::Replay(int bucket)
{
	if (bucket < 0 || bucket >= kNumBuckets)
		return;

	UICmdBucket& b = m_buckets[bucket];
	if (b.cmds.empty())
		return;

	for (const UICmd& cmd : b.cmds)
	{
		switch (cmd.type)
		{
		case UICmd::cmdSetShader:
			if (cmd.shader_idx < b.shaders.size() && b.shaders[cmd.shader_idx])
				RCache.set_Shader(b.shaders[cmd.shader_idx]);
			break;

		case UICmd::cmdSetAlphaRef:
			RCache.set_AlphaRef(cmd.int_arg);
			break;

		case UICmd::cmdSetScissor:
		{
			Irect rect = cmd.scissor;
			Irect* pRect = cmd.has_scissor ? &rect : nullptr;
#if (RENDER == R_R3) || (RENDER == R_R4)
			RCache.set_Scissor(pRect);
			StateManager.OverrideScissoring(cmd.has_scissor, TRUE);
#else
			RCache.set_Scissor(pRect);
#endif
			break;
		}

		case UICmd::cmdSetXformWorld:
			RCache.set_xform_world(cmd.xform);
			break;

		case UICmd::cmdSetCullMode:
			RCache.set_CullMode(CULL_NONE + cmd.int_arg);
			break;

		case UICmd::cmdDrawTL:
		case UICmd::cmdDrawLIT:
		{
			if (cmd.vert_count == 0)
				break;

			u32 primCount = 0;
			_D3DPRIMITIVETYPE d3dPrimType = D3DPT_TRIANGLELIST;
			switch (static_cast<ePrimitiveType>(cmd.prim_type))
			{
			case ptTriStrip:
				if (cmd.vert_count < 3) continue;
				primCount = cmd.vert_count - 2;
				d3dPrimType = D3DPT_TRIANGLESTRIP;
				break;
			case ptTriList:
				primCount = cmd.vert_count / 3;
				d3dPrimType = D3DPT_TRIANGLELIST;
				break;
			case ptLineStrip:
				if (cmd.vert_count < 2) continue;
				primCount = cmd.vert_count - 1;
				d3dPrimType = D3DPT_LINESTRIP;
				break;
			case ptLineList:
				primCount = cmd.vert_count / 2;
				d3dPrimType = D3DPT_LINELIST;
				break;
			default:
				continue;
			}
			if (primCount == 0)
				break;

			u32 lockOffset = 0;
			if (cmd.type == UICmd::cmdDrawTL)
			{
				FVF::TL* dst = (FVF::TL*)RCache.Vertex.Lock(cmd.vert_count, hGeom_TL.stride(), lockOffset);
				CopyMemory(dst, b.tl_verts.data() + cmd.vert_offset, cmd.vert_count * sizeof(FVF::TL));
				RCache.Vertex.Unlock(cmd.vert_count, hGeom_TL.stride());
				RCache.set_Geometry(hGeom_TL);
			}
			else
			{
				FVF::LIT* dst = (FVF::LIT*)RCache.Vertex.Lock(cmd.vert_count, hGeom_LIT.stride(), lockOffset);
				CopyMemory(dst, b.lit_verts.data() + cmd.vert_offset, cmd.vert_count * sizeof(FVF::LIT));
				RCache.Vertex.Unlock(cmd.vert_count, hGeom_LIT.stride());
				RCache.set_Geometry(hGeom_LIT);
			}
			RCache.Render(d3dPrimType, lockOffset, primCount);
			break;
		}

		case UICmd::cmdDrawFontQuads:
		{
			if (cmd.vert_count == 0)
				break;
			if (cmd.shader_idx < b.shaders.size() && b.shaders[cmd.shader_idx])
				RCache.set_Shader(b.shaders[cmd.shader_idx]);

			u32 lockOffset = 0;
			FVF::TL* dst = (FVF::TL*)RCache.Vertex.Lock(cmd.vert_count, hGeom_Font.stride(), lockOffset);
			CopyMemory(dst, b.tl_verts.data() + cmd.vert_offset, cmd.vert_count * sizeof(FVF::TL));
			RCache.Vertex.Unlock(cmd.vert_count, hGeom_Font.stride());
			RCache.set_Geometry(hGeom_Font);
			RCache.Render(D3DPT_TRIANGLELIST, lockOffset, 0, cmd.vert_count, 0, cmd.vert_count / 2);
			break;
		}
		}
	}

	b.clear();
}

void dxUIRender::SetShader(IUIShader& shader)
{
	dxUIShader* pShader = (dxUIShader*)&shader;
	VERIFY(&pShader);
	VERIFY(pShader->hShader);
	if (m_recording_bucket >= 0)
	{
		UICmdBucket& b = m_buckets[m_recording_bucket];
		UICmd cmd{};
		cmd.type = UICmd::cmdSetShader;
		cmd.shader_idx = static_cast<u32>(b.shaders.size());
		b.shaders.push_back(pShader->hShader);
		b.cmds.push_back(cmd);
		m_recorded_shader = pShader->hShader;
		return;
	}
	RCache.set_Shader(pShader->hShader);
}

void dxUIRender::SetAlphaRef(int aref)
{
	if (m_recording_bucket >= 0)
	{
		UICmd cmd{};
		cmd.type = UICmd::cmdSetAlphaRef;
		cmd.int_arg = aref;
		m_buckets[m_recording_bucket].cmds.push_back(cmd);
		return;
	}
	//CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAREF,aref));
	RCache.set_AlphaRef(aref);
}

/*
void dxUIRender::StartTriList(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_PointType = pttLIT;
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriList;
}

void dxUIRender::FlushTriList()
{
	VERIFY(PrimitiveType==ptTriList);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= (pv-start_pv)/3;							
	RCache.Vertex.Unlock		(u32(pv-start_pv),hGeom_fan.stride());
	RCache.set_Geometry			(hGeom_fan);
	if (p_cnt!=0)RCache.Render	(D3DPT_TRIANGLELIST,vOffset,u32(p_cnt));

	PrimitiveType = ptNone;
}

void dxUIRender::StartTriFan(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriFan;
	m_PointType	= pttLIT;

}

void dxUIRender::FlushTriFan()
{
	VERIFY(PrimitiveType==ptTriFan);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>2) RCache.Render	(D3DPT_TRIANGLEFAN,vOffset,u32(p_cnt-2));

	PrimitiveType = ptNone;
}

void dxUIRender::StartTriStrip(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::TL*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriStrip;
}

void dxUIRender::FlushTriStrip()
{
}


void dxUIRender::StartLineStrip(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptLineStrip;
	m_PointType = pttLIT;
}

void dxUIRender::FlushLineStrip()
{
	VERIFY(PrimitiveType==ptLineStrip);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>1) RCache.Render	(D3DPT_LINESTRIP,vOffset,u32(p_cnt-1));

	PrimitiveType = ptNone;
}

void dxUIRender::StartLineList(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptLineList;
}

void dxUIRender::FlushLineList()
{
	VERIFY(PrimitiveType==ptLineList);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>1) RCache.Render	(D3DPT_LINELIST,vOffset,u32(p_cnt)/2);

	PrimitiveType = ptNone;
}
*/
void dxUIRender::SetScissor(Irect* rect)
{
	if (m_recording_bucket >= 0)
	{
		UICmd cmd{};
		cmd.type = UICmd::cmdSetScissor;
		cmd.has_scissor = (rect != nullptr);
		if (rect)
			cmd.scissor = *rect;
		m_buckets[m_recording_bucket].cmds.push_back(cmd);
		return;
	}
#if (RENDER == R_R3) || (RENDER == R_R4)
	RCache.set_Scissor(rect);
	StateManager.OverrideScissoring(rect ? true : false, TRUE);
#else	//	(RENDER == R_R3) || (RENDER == R_R4)
	RCache.set_Scissor(rect);
#endif	//	(RENDER == R_R3) || (RENDER == R_R4)
}

void dxUIRender::GetActiveTextureResolution(Fvector2& res)
{
	if (m_recording_bucket >= 0 && m_recorded_shader && m_recorded_shader->E[0] &&
		!m_recorded_shader->E[0]->passes.empty() && m_recorded_shader->E[0]->passes[0]->T &&
		!m_recorded_shader->E[0]->passes[0]->T->empty())
	{
		CTexture* T = &*(m_recorded_shader->E[0]->passes[0]->T->at(0).second);
		if (T)
		{
			res.set(float(T->get_Width()), float(T->get_Height()));
			return;
		}
	}
	CTexture* T = RCache.get_ActiveTexture(0);
	res.set(float(T->get_Width()), float(T->get_Height()));
}

bool dxUIRender::GetTextureResolution(LPCSTR name, Fvector2& res)
{
	res.set(0.f, 0.f);

	CTexture* T = DEV->_FindTexture(name);
	if (!T || !T->flags.bLoaded) return false;

	u32 w = T->get_Width();
	u32 h = T->get_Height();
	if (!w || !h) return false;

	res.set(float(w), float(h));
	return true;
}

LPCSTR dxUIRender::UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name)
{
	string_path buff;
	u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
	u32 v_need = CAP_VERSION(2, 0);
	if ((v_dev >= v_need) && FS.exist(buff, "$game_textures$", tex_name, ".ogm"))
		return "hud\\movie";
	else
		return sh_name;
}

/*
void dxUIRender::PushPoint(float x, float y, u32 c, float u, float v)
{
	VERIFY(m_PointType==pttNone);
	pv->set(x, y, 0.0f, c, u, v);
	++pv;
}
*/
/*
void dxUIRender::PushPoint(int x, int y, u32 c, float u, float v)
{
	VERIFY(m_PointType==pttNone);
	pv->set(x, y, 0, c, u, v);
	++pv;
}
*/

void dxUIRender::PushPoint(float x, float y, float z, u32 C, float u, float v)
{
	if (m_recording_bucket >= 0)
	{
		UICmdBucket& b = m_buckets[m_recording_bucket];
		switch (m_PointType)
		{
		case pttLIT:
		{
			FVF::LIT vert;
			vert.set(x, y, z, C, u, v);
			b.lit_verts.push_back(vert);
			break;
		}
		case pttTL:
		{
			FVF::TL vert;
			vert.set(x, y, C, u, v);
			b.tl_verts.push_back(vert);
			break;
		}
		}
		return;
	}
	//.	VERIFY(m_PointType==pttLIT);
	switch (m_PointType)
	{
	case pttLIT:
		LIT_pv->set(x, y, z, C, u, v);
		++LIT_pv;
		break;
	case pttTL:
		TL_pv->set(x, y, C, u, v);
		++TL_pv;
		break;
	}
}

void dxUIRender::StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType)
{
	VERIFY(PrimitiveType==ptNone);
	VERIFY(m_PointType==pttNone);
	//.	R_ASSERT(pointType==pttLIT);

	m_iMaxVerts = iMaxVerts;
	PrimitiveType = primType;
	m_PointType = pointType;

	if (m_recording_bucket >= 0)
	{
		UICmdBucket& b = m_buckets[m_recording_bucket];
		if (m_PointType == pttLIT)
			m_rec_vert_start = static_cast<u32>(b.lit_verts.size());
		else
			m_rec_vert_start = static_cast<u32>(b.tl_verts.size());
		return;
	}

	switch (m_PointType)
	{
	case pttLIT:
		LIT_start_pv = (FVF::LIT*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_LIT.stride(), vOffset);
		LIT_pv = LIT_start_pv;
		break;
	case pttTL:
		TL_start_pv = (FVF::TL*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_TL.stride(), vOffset);
		TL_pv = TL_start_pv;
		break;
	}
}

void dxUIRender::FlushPrimitive()
{
	if (m_recording_bucket >= 0)
	{
		UICmdBucket& b = m_buckets[m_recording_bucket];
		const u32 end_sz = (m_PointType == pttLIT) ? static_cast<u32>(b.lit_verts.size()) : static_cast<u32>(b.tl_verts.size());
		const u32 count = end_sz - m_rec_vert_start;
		if (count > 0)
		{
			UICmd cmd{};
			cmd.type = (m_PointType == pttLIT) ? UICmd::cmdDrawLIT : UICmd::cmdDrawTL;
			cmd.prim_type = static_cast<u8>(PrimitiveType);
			cmd.vert_offset = m_rec_vert_start;
			cmd.vert_count = count;
			b.cmds.push_back(cmd);
		}
		PrimitiveType = ptNone;
		m_PointType = pttNone;
		return;
	}

	u32 primCount = 0;
	_D3DPRIMITIVETYPE d3dPrimType = D3DPT_FORCE_DWORD;
	std::ptrdiff_t p_cnt = 0;

	switch (m_PointType)
	{
	case pttLIT:
		p_cnt = LIT_pv - LIT_start_pv;
		VERIFY(u32(p_cnt)<=m_iMaxVerts);

		RCache.Vertex.Unlock(u32(p_cnt), hGeom_LIT.stride());
		RCache.set_Geometry(hGeom_LIT);
		break;
	case pttTL:
		p_cnt = TL_pv - TL_start_pv;
		VERIFY(u32(p_cnt)<=m_iMaxVerts);

		RCache.Vertex.Unlock(u32(p_cnt), hGeom_TL.stride());
		RCache.set_Geometry(hGeom_TL);
		break;
	default:
		NODEFAULT;
	}

	//	Update data for primitive type
	switch (PrimitiveType)
	{
	case ptTriStrip:
		primCount = (u32)(p_cnt - 2);
		d3dPrimType = D3DPT_TRIANGLESTRIP;
		break;
	case ptTriList:
		primCount = (u32)(p_cnt / 3);
		d3dPrimType = D3DPT_TRIANGLELIST;
		break;
	case ptLineStrip:
		primCount = (u32)(p_cnt - 1);
		d3dPrimType = D3DPT_LINESTRIP;
		break;
	case ptLineList:
		primCount = (u32)(p_cnt / 2);
		d3dPrimType = D3DPT_LINELIST;
		break;
	default:
		NODEFAULT;
	}

	if (primCount > 0)
		RCache.Render(d3dPrimType, vOffset, primCount);

	PrimitiveType = ptNone;
	m_PointType = pttNone;
}

void dxUIRender::CacheSetXformWorld(const Fmatrix& M)
{
	if (m_recording_bucket >= 0)
	{
		UICmd cmd{};
		cmd.type = UICmd::cmdSetXformWorld;
		cmd.xform = M;
		m_buckets[m_recording_bucket].cmds.push_back(cmd);
		return;
	}
	RCache.set_xform_world(M);
}

void dxUIRender::CacheSetCullMode(CullMode m)
{
	if (m_recording_bucket >= 0)
	{
		UICmd cmd{};
		cmd.type = UICmd::cmdSetCullMode;
		cmd.int_arg = static_cast<int>(m);
		m_buckets[m_recording_bucket].cmds.push_back(cmd);
		return;
	}
	RCache.set_CullMode(CULL_NONE + m);
}
