// HOM.h: interface for the CHOM class.
//
//////////////////////////////////////////////////////////////////////
#pragma once

#include "../../xrEngine/IGame_Persistent.h"

class occTri;
class IOccEngine;

class CHOM
#ifdef DEBUG
	: public pureRender
#endif
{
private:
	xrXRC xrc;
	CDB::MODEL* m_pModel;
	occTri* m_pTris;
	BOOL bEnabled;
#ifdef DEBUG
	u32						tris_in_frame			;
#endif

	xr_atomic_u32 MT_frame_rendered;
	xrCriticalSection m_mt_render_guard;
	u32 m_mt_render_registration_tid; // thread that queued MT_RENDER, a same-thread run means it fell back inline

	// latched once per frame under the render guard, queries never re-read the request
	int m_occ_mode;
	int m_occ_mode_logged;
	int m_occ_res_logged;
	bool m_occ_time_queries;  // query timing costs a clock read so the breakdown panel arms it
	IOccEngine* m_occ_query;  // the engine every visible() answers from
	IOccEngine* m_occ_raster; // legacy raster when this frame renders it, null otherwise
	IOccEngine* m_occ_masked; // masked rasterizer when this frame renders it, null otherwise

	// cform ground occluders, scanned once at load and walked per frame under the render guard
	enum
	{
		TERR_GRID = 128,     // xz cells per side over the level bounding volume
		TERR_EMIT_CAP = 16384 // terrain tris one frame may push into the masked buffer
	};

	struct terr_cell
	{
		u32 offset, count;  // span in m_terr_index
		float ymin, ymax;   // vertical extent of the tris bucketed here
	};

	struct terr_hit
	{
		float d2;  // squared distance cell center to camera
		u32 cell;
	};

	xr_vector<terr_cell> m_terr_grid;
	xr_vector<u32> m_terr_index;    // cform tri indices sorted by cell
	xr_vector<terr_hit> m_terr_hit; // per frame survivor list, reused to keep the walk allocation free
	Fbox m_terr_bounds;
	float m_terr_sx, m_terr_sz; // world to cell scale on x and z
	bool m_terr_ready;          // the grid is built and safe to walk
	bool m_terr_attempted;      // one lazy scan per level so a mid session cvar flip works

	void latch_engine(float near_w);
	BOOL query_box(const Fbox& B);
	BOOL query_poly(const Fvector* v, u32 n);
	void Render_DB(CFrustum& base);
	void terrain_load();
	void terrain_unload();
	void terrain_emit(CFrustum& base, const Fvector& COP);
public:
	void Load();
	void Unload();
	void Render(CFrustum& base);
	void Render_ZB();
	//	void					Debug		();

	void occlude(Fbox2& space)
	{
	}

	void Disable();
	void Enable();

	void __stdcall MT_RENDER();

	BOOL visible(vis_data& vis);
	BOOL visible(Fbox3& B);
	BOOL visible(Fsphere& S);
	BOOL visible(sPoly& P);
	BOOL visible(Fbox2& B, float depth); // viewport-space (0..1)

	CHOM();
	~CHOM();

#ifdef DEBUG
	virtual void			OnRender	();
			void			stats		();
#endif
};
