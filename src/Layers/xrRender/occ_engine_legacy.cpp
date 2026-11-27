// occ_engine_legacy.cpp: the stock hierarchical occlusion raster behind the engine seam

#include "stdafx.h"
#include "occ_engine.h"
#include "occRasterizer.h"

ICF BOOL xform_b0(Fvector2& min, Fvector2& max, float& minz, Fmatrix& X, float _x, float _y, float _z)
{
	float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
	if (z < EPS) return TRUE;
	float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
	min.x = max.x = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
	min.y = max.y = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
	minz = 0.f + z * iw;
	return FALSE;
}

ICF BOOL xform_b1(Fvector2& min, Fvector2& max, float& minz, Fmatrix& X, float _x, float _y, float _z)
{
	float t;
	float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
	if (z < EPS) return TRUE;
	float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
	t = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
	if (t < min.x) min.x = t;
	else if (t > max.x) max.x = t;
	t = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
	if (t < min.y) min.y = t;
	else if (t > max.y) max.y = t;
	t = 0.f + z * iw;
	if (t < minz) minz = t;
	return FALSE;
}

#ifdef DEBUG
static u32 s_dbg_visible = 0;
#endif

namespace
{
	class occ_legacy final : public IOccEngine
	{
	public:
		void begin_frame(const Fmatrix& full, const Fvector& cop, float near_w) override;
		bool emit(occTri& T, const Fvector* verts) override;
		void end_frame() override;

		BOOL test_box(const Fbox& world) override;
		BOOL test_poly(const Fvector* v, u32 n) override;

		bool wants_skip_filter() override { return true; }

	private:
		Fmatrix m_full;
		Fmatrix m_xform;
		Fmatrix m_xform_01;
		CFrustum m_clip;
		sPoly m_src, m_dst;
	};

	void occ_legacy::begin_frame(const Fmatrix& full, const Fvector& cop, float near_w)
	{
		(void)cop;
		(void)near_w;

		float view_dim = occ_dim_0;
		Fmatrix m_viewport = {
			view_dim / 2.f, 0.0f, 0.0f, 0.0f,
			0.0f, -view_dim / 2.f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			view_dim / 2.f + 0 + 0, view_dim / 2.f + 0 + 0, 0.0f, 1.0f
		};
		Fmatrix m_viewport_01 = {
			1.f / 2.f, 0.0f, 0.0f, 0.0f,
			0.0f, -1.f / 2.f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			1.f / 2.f + 0 + 0, 1.f / 2.f + 0 + 0, 0.0f, 1.0f
		};
		m_full.set(full);
		m_xform.mul(m_viewport, m_full);
		m_xform_01.mul(m_viewport_01, m_full);

		// near plane only, emit clips every occluder against it
		m_clip.CreateFromMatrix(m_full, FRUSTUM_P_NEAR);

		Raster.clear();
#ifdef DEBUG
		s_dbg_visible = 0;
#endif
	}

	bool occ_legacy::emit(occTri& T, const Fvector* verts)
	{
		m_src.clear();
		m_dst.clear();
		m_src.push_back(verts[0]);
		m_src.push_back(verts[1]);
		m_src.push_back(verts[2]);
		sPoly* P = m_clip.ClipPoly(m_src, m_dst);
		if (0 == P)
			return false;

#ifdef DEBUG
		s_dbg_visible++;
#endif
		u32 pixels = 0;
		int limit = int(P->size()) - 1;
		for (int v = 1; v < limit; v++)
		{
			m_xform.transform(T.raster[0], (*P)[0]);
			m_xform.transform(T.raster[1], (*P)[v + 0]);
			m_xform.transform(T.raster[2], (*P)[v + 1]);
			pixels += Raster.rasterize(&T);
		}
		return 0 != pixels;
	}

	void occ_legacy::end_frame()
	{
		Raster.propagade();
	}

	BOOL occ_legacy::test_box(const Fbox& world)
	{
		// Find min/max points of xformed-box
		Fvector2 min, max;
		float z;
		if (xform_b0(min, max, z, m_xform_01, world.min.x, world.min.y, world.min.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.min.x, world.min.y, world.max.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.max.x, world.min.y, world.max.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.max.x, world.min.y, world.min.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.min.x, world.max.y, world.min.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.min.x, world.max.y, world.max.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.max.x, world.max.y, world.max.z)) return TRUE;
		if (xform_b1(min, max, z, m_xform_01, world.max.x, world.max.y, world.min.z)) return TRUE;
		return Raster.test(min.x, min.y, max.x, max.y, z);
	}

	BOOL occ_legacy::test_poly(const Fvector* v, u32 n)
	{
		Fvector2 min, max;
		float z;
		if (xform_b0(min, max, z, m_xform_01, v[0].x, v[0].y, v[0].z)) return TRUE;
		for (u32 it = 1; it < n; it++)
			if (xform_b1(min, max, z, m_xform_01, v[it].x, v[it].y, v[it].z)) return TRUE;
		return Raster.test(min.x, min.y, max.x, max.y, z);
	}
}

IOccEngine& occ_engine_legacy()
{
	static occ_legacy s_legacy;
	return s_legacy;
}

#ifdef DEBUG
u32 occ_legacy_dbg_visible()
{
	return s_dbg_visible;
}
#endif
