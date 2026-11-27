#ifndef	dxUIRender_included
#define	dxUIRender_included
#pragma once

#include "..\..\Include\xrRender\UIRender.h"

class dxUIRender : public IUIRender
{
public:
	dxUIRender() : PrimitiveType(ptNone), m_PointType(pttNone) { ; }

	virtual void CreateUIGeom();
	virtual void DestroyUIGeom();

	virtual void SetShader(IUIShader& shader);
	virtual void SetAlphaRef(int aref);
	//.	virtual void StartTriList(u32 iMaxVerts);
	//.	virtual void FlushTriList();
	//.	virtual void StartTriFan(u32 iMaxVerts);
	//.	virtual void FlushTriFan();
	//virtual void StartTriStrip(u32 iMaxVerts);
	//virtual void FlushTriStrip();
	//.	virtual void StartLineStrip(u32 iMaxVerts);
	//.	virtual void FlushLineStrip();
	//.	virtual void StartLineList(u32 iMaxVerts);
	//.	virtual void FlushLineList();
	virtual void SetScissor(Irect* rect = NULL);
	virtual void GetActiveTextureResolution(Fvector2& res);
	virtual bool GetTextureResolution(LPCSTR name, Fvector2& res);

	//.	virtual void PushPoint(float x, float y, u32 c, float u, float v);
	//	virtual void PushPoint(int x, int y, u32 c, float u, float v);
	virtual void PushPoint(float x, float y, float z, u32 C, float u, float v);

	virtual void StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType);
	virtual void FlushPrimitive();

	virtual LPCSTR UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name);

	virtual void CacheSetXformWorld(const Fmatrix& M);
	virtual void CacheSetCullMode(CullMode);

	virtual void BeginRecord(int bucket, bool clear = true) override;
	virtual void EndRecord() override;
	virtual void Replay(int bucket) override;
	virtual bool IsRecording() const override { return m_recording_bucket >= 0; }

	void RecordFontQuads(const ref_shader& shader, const FVF::TL* verts, u32 count);

private:
	struct UICmd
	{
		enum Type : u8
		{
			cmdSetShader,
			cmdSetAlphaRef,
			cmdSetScissor,
			cmdSetXformWorld,
			cmdSetCullMode,
			cmdDrawTL,
			cmdDrawLIT,
			cmdDrawFontQuads,
		};
		Type type = cmdSetShader;
		u8 prim_type = 0;
		bool has_scissor = false;
		int int_arg = 0;
		u32 vert_offset = 0;
		u32 vert_count = 0;
		u32 shader_idx = 0;
		Irect scissor{};
		Fmatrix xform{};
	};

	struct UICmdBucket
	{
		xr_vector<UICmd> cmds;
		xr_vector<ref_shader> shaders;
		xr_vector<FVF::TL> tl_verts;
		xr_vector<FVF::LIT> lit_verts;

		void clear()
		{
			cmds.clear();
			shaders.clear();
			tl_verts.clear();
			lit_verts.clear();
		}
	};

	static constexpr int kNumBuckets = 4;
	UICmdBucket m_buckets[kNumBuckets];
	int m_recording_bucket = -1;
	ref_shader m_recorded_shader;
	u32 m_rec_vert_start = 0;

	ref_geom hGeom_TL;
	ref_geom hGeom_LIT;
	ref_geom hGeom_Font;

	ePrimitiveType PrimitiveType;
	ePointType m_PointType;

	//	Vertex buffer attributes
	u32 m_iMaxVerts;
	u32 vOffset;

	FVF::TL* TL_start_pv;
	FVF::TL* TL_pv;

	FVF::LIT* LIT_start_pv;
	FVF::LIT* LIT_pv;
};

extern dxUIRender UIRenderImpl;

#endif	//	dxUIRender_included
