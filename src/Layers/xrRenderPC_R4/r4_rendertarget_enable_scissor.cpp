#include "stdafx.h"
#include "../../xrEngine/cl_intersect.h"
#include "../xrRender/du_cone.h"
#include "../xrRender/du_sphere.h"
#include "../xrRender/du_sphere_part.h"

//extern Fvector du_cone_vertices			[DU_CONE_NUMVERTEX];

BOOL tri_vs_sphere_intersect(Fvector& SC, float R, Fvector& v0, Fvector& v1, Fvector& v2)
{
	Fvector e0, e1;
	return CDB::TestSphereTri(SC, R, v0, e0.sub(v1, v0), e1.sub(v2, v0));
}

BOOL CRenderTarget::enable_scissor(light* L) // true if intersects near plane
{
	// Msg	("%d: %x type(%d), pos(%f,%f,%f)",Device.dwFrame,u32(L),u32(L->flags.type),VPUSH(L->position));

	// Near plane intersection
	BOOL near_intersect = FALSE;
	{
		Fmatrix& M = Device.mFullTransform;
		Fvector4 plane;
		plane.x = -(M._14 + M._13);
		plane.y = -(M._24 + M._23);
		plane.z = -(M._34 + M._33);
		plane.w = -(M._44 + M._43);
		float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
		plane.mul(denom);
		Fplane P;
		P.n.set(plane.x, plane.y, plane.z);
		P.d = plane.w;
		float p_dist = P.classify(L->SpatialComponent->spatial.sphere.P) - L->SpatialComponent->spatial.sphere.R;
		near_intersect = (p_dist <= 0);
	}
#ifdef DEBUG
	if (1)
	{
		Fsphere		S;	S.set	(L->SpatialComponent->spatial.sphere.P,L->SpatialComponent->spatial.sphere.R);
		dbg_spheres.push_back	(mk_pair(S,L->color));
	}
#endif

	// Scissor
	//. disable scissor because some bugs prevent it to work through multi-portals
	//. if (!HW.Caps.bScissor)	return		near_intersect;
	return near_intersect;

#if 0
	CSector*	S	= (CSector*)L->spatial.sector;
	_scissor	bb	= S->r_scissor_merged;
	Irect		R;
	R.x1		= clampr	(iFloor	(bb.min.x*Device.dwWidth),	int(0),int(Device.dwWidth));
	R.x2		= clampr	(iCeil	(bb.max.x*Device.dwWidth),	int(0),int(Device.dwWidth));
	R.y1		= clampr	(iFloor	(bb.min.y*Device.dwHeight),	int(0),int(Device.dwHeight));
	R.y2		= clampr	(iCeil	(bb.max.y*Device.dwHeight),	int(0),int(Device.dwHeight));
	if	( (Device.dwWidth==u32(R.right - R.left)) && (Device.dwHeight==u32(R.bottom-R.top)) )
	{
		// full-screen -> do nothing
	} else {
		// test if light-volume intersects scissor-rectangle
	// if it does - do nothing, if doesn't - we look on light through portal

		// 1. convert rect into -1..1 space
		Fbox2		b_pp	= bb;
		b_pp.min.x			= b_pp.min.x * 2 - 1;
		b_pp.max.x			= b_pp.max.x * 2 - 1;
		b_pp.min.y			= (1-b_pp.min.y) * 2 - 1;
		b_pp.max.y			= (1-b_pp.max.y) * 2 - 1;

		// 2. construct scissor rectangle in post-projection space
		Fvector3	s_points_pp			[4];
		s_points_pp[0].set	(bb.min.x,bb.min.y,bb.depth);	// LT
		s_points_pp[1].set	(bb.max.x,bb.min.y,bb.depth);	// RT
		s_points_pp[2].set	(bb.max.x,bb.max.y,bb.depth);	// RB
		s_points_pp[3].set	(bb.min.x,bb.max.y,bb.depth);	// LB

		// 3. convert it into world space
		Fvector3	s_points			[4];
		Fmatrix&	iVP					= Device.mInvFullTransform;
		iVP.transform	(s_points[0],s_points_pp[0]);
		iVP.transform	(s_points[1],s_points_pp[1]);
		iVP.transform	(s_points[2],s_points_pp[2]);
		iVP.transform	(s_points[3],s_points_pp[3]);

		// 4. check intersection of two triangles with (spatial, enclosing) sphere
		BOOL	bIntersect	= tri_vs_sphere_intersect	(
			L->spatial.sphere.P,L->spatial.sphere.R,
			s_points[0],s_points[1],s_points[2]
			);
		if (!bIntersect)	bIntersect	= tri_vs_sphere_intersect	(
				L->spatial.sphere.P,L->spatial.sphere.R,
				s_points[2],s_points[3],s_points[0]
				);
		if (!bIntersect)	{
			// volume doesn't touch scissor - enable mask
			RCache.set_Scissor(&R);
			//CHK_DX		(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
			//CHK_DX		(HW.pDevice->SetScissorRect(&R));
		} else {
			// __asm int 3;
			RCache.set_Scissor(NULL);
		}
	}

	return near_intersect;
#endif
}

static Fbox build_volume_box(const Fvector* v, u32 n)
{
	Fbox b;
	b.invalidate();
	for (u32 i = 0; i < n; ++i)
		b.modify(v[i]);
	return b;
}

// local bounds of the volume mesh draw_volume rasterizes for this light type
static const Fbox* light_volume_box(light* L)
{
	static const Fbox s_sphere = build_volume_box(du_sphere_vertices, DU_SPHERE_NUMVERTEX);
	static const Fbox s_cone = build_volume_box(du_cone_vertices, DU_CONE_NUMVERTEX);
	static const Fbox s_part = build_volume_box(du_sphere_part_vertices, DU_SPHERE_PART_NUMVERTEX);

	switch (L->flags.type)
	{
	case IRender_Light::REFLECTED:
	case IRender_Light::POINT: return &s_sphere;
	case IRender_Light::SPOT: return &s_cone;
	case IRender_Light::OMNIPART: return &s_part;
	default: return nullptr;
	}
}

BOOL CRenderTarget::compute_light_scissor(light* L, BOOL near_intersect, Irect& R)
{
	// a volume touching the near plane has corners behind the eye and will not project
	if (near_intersect)
		return FALSE;

	const Fbox* lb = light_volume_box(L);
	if (!lb)
		return FALSE;

	// live camera, the svp replay reaches here with its own matrices and target size
	const Fmatrix& FT = Device.mFullTransform;
	const float fw = float(Device.dwWidth);
	const float fh = float(Device.dwHeight);

	float x0 = flt_max, y0 = flt_max, x1 = -flt_max, y1 = -flt_max;
	for (u32 c = 0; c < 8; ++c)
	{
		Fvector lp, p;
		lp.set((c & 1) ? lb->max.x : lb->min.x,
		       (c & 2) ? lb->max.y : lb->min.y,
		       (c & 4) ? lb->max.z : lb->min.z);
		L->m_xform.transform_tiny(p, lp);

		const float cw = p.x * FT._14 + p.y * FT._24 + p.z * FT._34 + FT._44;
		if (cw < 0.001f)
			return FALSE;
		const float inv = 1.f / cw;
		const float cx = (p.x * FT._11 + p.y * FT._21 + p.z * FT._31 + FT._41) * inv;
		const float cy = (p.x * FT._12 + p.y * FT._22 + p.z * FT._32 + FT._42) * inv;

		const float sx = (cx * .5f + .5f) * fw;
		const float sy = (.5f - cy * .5f) * fh;
		x0 = _min(x0, sx);
		x1 = _max(x1, sx);
		y0 = _min(y0, sy);
		y1 = _max(y1, sy);
	}

	// round outward so the rect never crops a pixel the unscissored draw would touch
	R.x1 = clampr(iFloor(clampr(x0, 0.f, fw)), 0, int(Device.dwWidth));
	R.x2 = clampr(iCeil(clampr(x1, 0.f, fw)), 0, int(Device.dwWidth));
	R.y1 = clampr(iFloor(clampr(y0, 0.f, fh)), 0, int(Device.dwHeight));
	R.y2 = clampr(iCeil(clampr(y1, 0.f, fh)), 0, int(Device.dwHeight));

	// covers the frame anyway, skip the state change
	if (R.x1 <= 0 && R.y1 <= 0 && R.x2 >= int(Device.dwWidth) && R.y2 >= int(Device.dwHeight))
		return FALSE;
	// an empty rect means the projection folded, keep the light rather than drop it
	if (R.x2 <= R.x1 || R.y2 <= R.y1)
		return FALSE;

	return TRUE;
}

/*
{
	Fmatrix& M						= RCache.xforms.m_wvp;
	BOOL	bIntersect				= FALSE;
	for (u32 vit=0; vit<DU_CONE_NUMVERTEX; vit++)	{
		Fvector&	v	= du_cone_vertices[vit];
		float _z = v.x*M._13 + v.y*M._23 + v.z*M._33 + M._43;
		float _w = v.x*M._14 + v.y*M._24 + v.z*M._34 + M._44;
		if (_z<=0 || _w<=0)	{
			bIntersect	= TRUE;
			break;
		}
	}
}
*/
