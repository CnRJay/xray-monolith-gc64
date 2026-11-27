// HOM.cpp: implementation of the CHOM class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "HOM.h"
#include "occRasterizer.h"
#include "occ_engine.h"
#include "svp_console.h"
#include "../../xrEngine/GameFont.h"
#include "../../xrEngine/GameMtlLib.h"

#include "../../xrCore/profiler.h"

#include "dxRenderDeviceRender.h"

float psOSSR = .001f;

void __stdcall CHOM::MT_RENDER()
{
	PROF_EVENT("Render HOM");

	xrCriticalSectionGuard guard(m_mt_render_guard);
	const u32 current_frame = Device.dwFrame;

	bool b_main_menu_is_active = (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
	if (b_main_menu_is_active)
	{
		MT_frame_rendered.store(current_frame, std::memory_order_release);
		return;
	}

	if (MT_frame_rendered.load(std::memory_order_acquire) != current_frame)
	{
		if (ps_r__svp_stats && GetCurrentThreadId() == m_mt_render_registration_tid)
			++svp_stats_hom_main_thread; // ran on the queuing thread, the worker task never picked it up

		CFrustum ViewBase;
		ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
		Enable();
		Render(ViewBase);
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CHOM::CHOM()
{
	bEnabled = FALSE;
	m_pModel = 0;
	m_pTris = 0;
    MT_frame_rendered.store(0, std::memory_order_relaxed);
    m_mt_render_registration_tid = 0;
	m_occ_mode = 0;
	m_occ_mode_logged = -1;
	m_occ_res_logged = -1;
	m_occ_time_queries = false;
	m_occ_raster = &occ_engine_legacy();
	m_occ_masked = nullptr;
	m_occ_query = m_occ_raster;
	m_terr_bounds.invalidate();
	m_terr_sx = 0.f;
	m_terr_sz = 0.f;
	m_terr_ready = false;
	m_terr_attempted = false;
#ifdef DEBUG
	Device.seqRender.Add(this,REG_PRIORITY_LOW-1000);
#endif
}

CHOM::~CHOM()
{
#ifdef DEBUG
	Device.seqRender.Remove(this);
#endif
}

#pragma pack(push,4)
struct HOM_poly
{
	Fvector v1, v2, v3;
	u32 flags;
};
#pragma pack(pop)

IC float Area(Fvector& v0, Fvector& v1, Fvector& v2)
{
	float e1 = v0.distance_to(v1);
	float e2 = v0.distance_to(v2);
	float e3 = v1.distance_to(v2);

	float p = (e1 + e2 + e3) / 2.f;
	return _sqrt(p * (p - e1) * (p - e2) * (p - e3));
}

// cform ground materials, a mod renaming one silently drops it so the match is logged
static const char* s_terr_materials[] =
{
	"materials\\earth", "materials\\earth_death", "materials\\earth_slide", "materials\\grass",
	"materials\\dirt", "materials\\sand", "materials\\gravel", "materials\\asphalt"
};

void CHOM::terrain_load()
{
	m_terr_ready = false;
	if (!ps_r__hom_terrain || !g_pGameLevel)
		return;

	CObjectSpace& os = g_pGameLevel->ObjectSpace;
	CDB::MODEL* cform = os.GetStaticModel();
	if (!cform)
		return;

	// the tri array and its material remap both land inside this task
	cform->async_cform_load.wait();

	const int face_count = cform->get_tris_count();
	const CDB::TRI* tris = cform->get_tris();
	const Fvector* verts = cform->get_verts();
	if (face_count <= 0 || !tris || !verts)
		return;

	const Fbox& bounds = os.GetBoundingVolume();
	const float ext_x = bounds.max.x - bounds.min.x;
	const float ext_z = bounds.max.z - bounds.min.z;
	if (ext_x < EPS_L || ext_z < EPS_L)
		return;

	// one flag per GMLib index, cform materials are indices once the level load remap ran
	const u32 mtl_count = GMLib.CountMaterial();
	if (0 == mtl_count)
		return;
	xr_vector<u8> is_ground(mtl_count, 0);
	string512 matched;
	matched[0] = 0;
	for (u32 m = 0; m < mtl_count; ++m)
	{
		SGameMtl* mtl = GMLib.GetMaterialByIdx(u16(m));
		if (!mtl || mtl->Flags.test(SGameMtl::flPassable) || mtl->Flags.test(SGameMtl::flDynamic))
			continue;
		for (u32 n = 0; n < sizeof(s_terr_materials) / sizeof(s_terr_materials[0]); ++n)
		{
			if (0 != _stricmp(mtl->m_Name.c_str(), s_terr_materials[n]))
				continue;
			is_ground[m] = 1;
			if (xr_strlen(matched) + xr_strlen(s_terr_materials[n]) + 2 < sizeof(matched))
			{
				if (matched[0])
					xr_strcat(matched, " ");
				xr_strcat(matched, s_terr_materials[n]);
			}
			break;
		}
	}
	Msg("* [MOC-TERR] materials %s", matched[0] ? matched : "none matched");

	m_terr_bounds.set(bounds);
	m_terr_sx = float(TERR_GRID) / ext_x;
	m_terr_sz = float(TERR_GRID) / ext_z;

	const u32 cell_count = u32(TERR_GRID) * u32(TERR_GRID);
	m_terr_grid.resize(cell_count);
	for (u32 c = 0; c < cell_count; ++c)
	{
		terr_cell& C = m_terr_grid[c];
		C.offset = 0;
		C.count = 0;
		C.ymin = flt_max;
		C.ymax = -flt_max;
	}

	xr_vector<u32> keep_tri, keep_cell;
	keep_tri.reserve(1 << 17);
	keep_cell.reserve(1 << 17);

	u32 bad_mtl = 0;
	for (int i = 0; i < face_count; ++i)
	{
		const CDB::TRI& T = tris[i];
		if (T.material >= mtl_count)
		{
			++bad_mtl;
			continue;
		}
		if (!is_ground[T.material])
			continue;

		const Fvector& v0 = verts[T.verts[0]];
		const Fvector& v1 = verts[T.verts[1]];
		const Fvector& v2 = verts[T.verts[2]];
		Fvector e1, e2, nrm;
		e1.sub(v1, v0);
		e2.sub(v2, v0);
		nrm.crossproduct(e1, e2);
		// area 4 without the root, the cross magnitude is twice the area
		if (nrm.square_magnitude() < 64.f)
			continue;

		int gi = iFloor(((v0.x + v1.x + v2.x) * (1.f / 3.f) - bounds.min.x) * m_terr_sx);
		int gj = iFloor(((v0.z + v1.z + v2.z) * (1.f / 3.f) - bounds.min.z) * m_terr_sz);
		gi = (gi < 0) ? 0 : ((gi >= TERR_GRID) ? (TERR_GRID - 1) : gi);
		gj = (gj < 0) ? 0 : ((gj >= TERR_GRID) ? (TERR_GRID - 1) : gj);
		const u32 cell = u32(gi) * TERR_GRID + u32(gj);

		terr_cell& C = m_terr_grid[cell];
		++C.count;
		C.ymin = _min(C.ymin, _min(v0.y, _min(v1.y, v2.y)));
		C.ymax = _max(C.ymax, _max(v0.y, _max(v1.y, v2.y)));
		keep_cell.push_back(cell);
		keep_tri.push_back(u32(i));
	}

	if (keep_tri.empty())
	{
		Msg("* [MOC-TERR] no ground tris on this level, terrain occluders off");
		terrain_unload();
		return;
	}

	// prefix sum the counts then scatter the kept indices so each cell owns one span
	u32 running = 0;
	u32 used = 0;
	for (u32 c = 0; c < cell_count; ++c)
	{
		m_terr_grid[c].offset = running;
		running += m_terr_grid[c].count;
		if (m_terr_grid[c].count)
			++used;
	}
	m_terr_index.resize(running);
	{
		const u32 kept = u32(keep_tri.size());
		xr_vector<u32> cursor(cell_count);
		for (u32 c = 0; c < cell_count; ++c)
			cursor[c] = m_terr_grid[c].offset;
		for (u32 k = 0; k < kept; ++k)
			m_terr_index[cursor[keep_cell[k]]++] = keep_tri[k];
	}

	if (bad_mtl)
		Msg("! [MOC-TERR] %u cform tris carry a material index past the library, skipped", bad_mtl);

	m_terr_hit.reserve(used);
	const u32 kb = u32((m_terr_index.size() * sizeof(u32) + cell_count * sizeof(terr_cell)
		+ m_terr_hit.capacity() * sizeof(terr_hit)) / 1024);
	Msg("* [MOC-TERR] faces %d kept %u cells %u of %u cell %.1fx%.1fm %ukb cap %u",
		face_count, u32(m_terr_index.size()), used, cell_count,
		ext_x / float(TERR_GRID), ext_z / float(TERR_GRID), kb, u32(TERR_EMIT_CAP));

	m_terr_ready = true;
}

void CHOM::terrain_unload()
{
	m_terr_ready = false;
	m_terr_grid.clear_and_free();
	m_terr_index.clear_and_free();
	m_terr_hit.clear_and_free();
	m_terr_bounds.invalidate();
	m_terr_sx = 0.f;
	m_terr_sz = 0.f;
}

void CHOM::Load()
{
	// Find and open file
	string_path fName;
	FS.update_path(fName, "$level$", "level.hom");
	if (!FS.exist(fName))
	{
		Msg(" WARNING: Occlusion map '%s' not found.", fName);
		return;
	}
	Msg("* Loading HOM: %s", fName);

	IReader* fs = FS.r_open(fName);
	IReader* S = fs->open_chunk(1);

	// Load tris and merge them
	CDB::Collector CL;
	while (!S->eof())
	{
		HOM_poly P;
		S->r(&P, sizeof(P));
		CL.add_face_packed_D(P.v1, P.v2, P.v3, P.flags, 0.01f);
	}

	// Determine adjacency
	xr_vector<u32> adjacency;
	CL.calc_adjacency(adjacency);

	// Create RASTER-triangles
	m_pTris = xr_alloc<occTri>(u32(CL.getTS()));
	for (u32 it = 0; it < CL.getTS(); it++)
	{
		CDB::TRI& clT = CL.getT()[it];
		occTri& rT = m_pTris[it];

		Fvector& v0 = CL.getV()[clT.verts[0]];
		Fvector& v1 = CL.getV()[clT.verts[1]];
		Fvector& v2 = CL.getV()[clT.verts[2]];

		rT.adjacent[0] = (0xffffffff == adjacency[3 * it + 0]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 0]);
		rT.adjacent[1] = (0xffffffff == adjacency[3 * it + 1]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 1]);
		rT.adjacent[2] = (0xffffffff == adjacency[3 * it + 2]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 2]);
		rT.flags = clT.dummy;
		rT.area = Area(v0, v1, v2);

		if (rT.area < EPS_L)
		{
			Msg("! Invalid HOM triangle (%f,%f,%f)-(%f,%f,%f)-(%f,%f,%f)", VPUSH(v0), VPUSH(v1), VPUSH(v2));
		}

		rT.plane.build(v0, v1, v2);
		rT.skip = 0;
		rT.center.add(v0, v1).add(v2).div(3.f);
	}

	// Create AABB-tree
	m_pModel = xr_new<CDB::MODEL>();
	m_pModel->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
	bEnabled = TRUE;
	occ_engine_moc_reserve(u32(CL.getTS()) + TERR_EMIT_CAP);
	S->close();
	FS.r_close(fs);

	m_terr_attempted = false;
	terrain_load();

	if (ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))
	{
		// MT-HOM (@front), ahead of the grass MT_CALC so every occlusion query reads a finished frame
		Device.seqParallelRender.insert(Device.seqParallelRender.begin(), xr_make_delegate(this, &CHOM::MT_RENDER));
		m_mt_render_registration_tid = GetCurrentThreadId(); // the thread that queues the worker call
	}
}

void CHOM::Unload()
{
	terrain_unload();
	xr_delete(m_pModel);
	xr_free(m_pTris);
	bEnabled = FALSE;
	occ_engine_moc_release();

	auto I = std::find(Device.seqParallelRender.begin(), Device.seqParallelRender.end(), xr_make_delegate(this, &CHOM::MT_RENDER));
	if (I != Device.seqParallelRender.end())
		Device.seqParallelRender.erase(I);
}

class pred_fb
{
public:
	occTri* m_pTris;
	Fvector camera;
public:
	pred_fb(occTri* _t) : m_pTris(_t)
	{
	}

	pred_fb(occTri* _t, Fvector& _c) : m_pTris(_t), camera(_c)
	{
	}

	ICF bool operator()(const CDB::RESULT& _1, const CDB::RESULT& _2) const
	{
		occTri& t0 = m_pTris[_1.id];
		occTri& t1 = m_pTris[_2.id];
		return camera.distance_to_sqr(t0.center) < camera.distance_to_sqr(t1.center);
	}

	ICF bool operator()(const CDB::RESULT& _1) const
	{
		occTri& T = m_pTris[_1.id];
		return T.skip > Device.dwFrame;
	}
};

// cform ground into the masked buffer, near cells first so the cap spends on the closest terrain
void CHOM::terrain_emit(CFrustum& base, const Fvector& COP)
{
	const CDB::MODEL* cform = g_pGameLevel ? g_pGameLevel->ObjectSpace.GetStaticModel() : nullptr;
	const CDB::TRI* tris = cform ? cform->get_tris() : nullptr;
	const Fvector* verts = cform ? cform->get_verts() : nullptr;
	if (!tris || !verts)
		return;

	const float cw = (m_terr_bounds.max.x - m_terr_bounds.min.x) / float(TERR_GRID);
	const float ch = (m_terr_bounds.max.z - m_terr_bounds.min.z) / float(TERR_GRID);
	const u32 full_mask = base.getMask();

	const u32 side = u32(TERR_GRID);
	m_terr_hit.clear();

	// the view hull is the camera apex plus the far rectangle, walk only the cells its xz box reaches
	u32 gi_lo = 0, gi_hi = side - 1, gj_lo = 0, gj_hi = side - 1;
	if (cw > EPS_S && ch > EPS_S)
	{
		// clip cube corners with d3d z in zero to one, unprojected to world
		static const Fvector3 clip_corner[8] = {
			{ -1.f, -1.f, 0.f }, { 1.f, -1.f, 0.f }, { -1.f, 1.f, 0.f }, { 1.f, 1.f, 0.f },
			{ -1.f, -1.f, 1.f }, { 1.f, -1.f, 1.f }, { -1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }
		};
		Fmatrix inv;
		inv.identity();
		const bool inv_ok = (0 != D3DXMatrixInverse((D3DXMATRIX*)&inv, 0, (D3DXMATRIX*)&Device.mFullTransform));
		if (inv_ok && _valid(inv))
		{
			float x_lo = COP.x, x_hi = COP.x, z_lo = COP.z, z_hi = COP.z;
			for (u32 c = 0; c < 8; ++c)
			{
				const Fvector3& q = clip_corner[c];
				const float qw = q.x * inv._14 + q.y * inv._24 + q.z * inv._34 + inv._44;
				if (_abs(qw) < EPS_S)
					continue;
				const float iw = 1.f / qw;
				const float wx = (q.x * inv._11 + q.y * inv._21 + q.z * inv._31 + inv._41) * iw;
				const float wz = (q.x * inv._13 + q.y * inv._23 + q.z * inv._33 + inv._43) * iw;
				x_lo = _min(x_lo, wx); x_hi = _max(x_hi, wx);
				z_lo = _min(z_lo, wz); z_hi = _max(z_hi, wz);
			}
			// one cell of slack on each side absorbs the grid quantization
			const int i0 = _max(0, iFloor((x_lo - m_terr_bounds.min.x) / cw) - 1);
			const int i1 = _min(int(side) - 1, iFloor((x_hi - m_terr_bounds.min.x) / cw) + 1);
			const int j0 = _max(0, iFloor((z_lo - m_terr_bounds.min.z) / ch) - 1);
			const int j1 = _min(int(side) - 1, iFloor((z_hi - m_terr_bounds.min.z) / ch) + 1);
			if (i0 > i1 || j0 > j1)
				return; // the view box misses the terrain grid outright
			gi_lo = u32(i0); gi_hi = u32(i1);
			gj_lo = u32(j0); gj_hi = u32(j1);
		}
	}

	for (u32 gi = gi_lo; gi <= gi_hi; ++gi)
	{
		const float x0 = m_terr_bounds.min.x + cw * float(gi);
		for (u32 gj = gj_lo; gj <= gj_hi; ++gj)
		{
			const u32 cell = gi * side + gj;
			const terr_cell& C = m_terr_grid[cell];
			if (0 == C.count)
				continue;

			const float z0 = m_terr_bounds.min.z + ch * float(gj);
			const float aabb[6] = { x0, C.ymin, z0, x0 + cw, C.ymax, z0 + ch };
			u32 mask = full_mask;
			if (fcvNone == base.testAABB(aabb, mask))
				continue;

			Fvector mid;
			mid.set(x0 + cw * 0.5f, (C.ymin + C.ymax) * 0.5f, z0 + ch * 0.5f);
			terr_hit hit;
			hit.d2 = COP.distance_to_sqr(mid);
			hit.cell = cell;
			m_terr_hit.push_back(hit);
		}
	}

	svp_stats_hom_terr_cells = u32(m_terr_hit.size());
	if (m_terr_hit.empty())
		return;

	std::sort(m_terr_hit.begin(), m_terr_hit.end(),
		[](const terr_hit& a, const terr_hit& b) { return a.d2 < b.d2; });

	// the masked engine ignores the occTri, terrain never builds one
	occTri unused;
	const u32 cap = u32(TERR_EMIT_CAP);
	const u32 hits = u32(m_terr_hit.size());
	u32 emitted = 0;
	for (u32 h = 0; h < hits && emitted < cap; ++h)
	{
		const terr_cell& C = m_terr_grid[m_terr_hit[h].cell];
		for (u32 k = 0; k < C.count; ++k)
		{
			const CDB::TRI& T = tris[m_terr_index[C.offset + k]];
			const Fvector& v0 = verts[T.verts[0]];
			const Fvector& v1 = verts[T.verts[1]];
			const Fvector& v2 = verts[T.verts[2]];

			// the facing acceptance the hom tris get, sign only so no normalize
			Fvector e1, e2, nrm, eye;
			e1.sub(v1, v0);
			e2.sub(v2, v0);
			nrm.crossproduct(e1, e2);
			eye.sub(COP, v0);
			if (nrm.dotproduct(eye) <= 0.f)
				continue;

			const Fvector tri[3] = { v0, v1, v2 };
			m_occ_masked->emit(unused, tri);
			if (++emitted >= cap)
			{
				svp_stats_hom_terr_capped = 1;
				break;
			}
		}
	}
	svp_stats_hom_terr_emitted = emitted;
}

void CHOM::Render_DB(CFrustum& base)
{
	// Query DB
	xrc.frustum_options(0);
	xrc.frustum_query(m_pModel, base);
	if (0 == xrc.r_count()) return;

	// Prepare
	CDB::RESULT* it = xrc.r_begin();
	CDB::RESULT* end = xrc.r_end();

	Fvector COP = Device.vCameraPosition;
	// only an engine driven by per-tri pixel feedback carries the skip cadence
	// the shadow compare feeds both engines the full set so the tally stays honest
	const bool skip_filter = m_occ_query->wants_skip_filter() && !m_occ_masked;
	if (skip_filter)
		end = std::remove_if(it, end, pred_fb(m_pTris));
	std::sort(it, end, pred_fb(m_pTris, COP));

	u32 _frame = Device.dwFrame;
	const bool tally = (ps_r__svp_stats != 0);
	if (tally)
	{
		svp_stats_hom_tris_in = u32(end - it);
		svp_stats_hom_tris_emitted = 0;
	}
#ifdef DEBUG
	tris_in_frame				= xrc.r_count();
#endif

	// Perfrom selection, sorting, culling
	for (; it != end; it++)
	{
		// Control skipping
		occTri& T = m_pTris[it->id];
		// the skip cadence is the only reader, a masked frame draws no shared random at all
		u32 next = skip_filter ? _frame + ::Random.randI(3, 10) : 0;

		// Test for good occluder - should be improved :)
		if (!(T.flags || (T.plane.classify(COP) > 0)))
		{
			if (skip_filter)
				T.skip = next;
			continue;
		}
		if (tally)
			++svp_stats_hom_tris_emitted;

		// Access to triangle vertices
		CDB::TRI& t = m_pModel->get_tris()[it->id];
		Fvector* v = m_pModel->get_verts();
		const Fvector tri[3] = { v[t.verts[0]], v[t.verts[1]], v[t.verts[2]] };

		// the masked engine takes every facing tri, the legacy clip and pixel tests never gate it
		if (m_occ_masked)
			m_occ_masked->emit(T, tri);

		if (m_occ_raster && !m_occ_raster->emit(T, tri))
		{
			if (skip_filter)
				T.skip = next;
		}
	}
}

// picks this frame's engines once, no query ever reads the request
void CHOM::latch_engine(float near_w)
{
	const int mode = (ps_r__hom_engine < 0) ? 0 : ((ps_r__hom_engine > 2) ? 2 : ps_r__hom_engine);
	const int res = (ps_r__hom_moc_res < 0) ? 0 : ((ps_r__hom_moc_res > 3) ? 3 : ps_r__hom_moc_res);

	m_occ_mode = mode;
	m_occ_time_queries = (ps_r__svp_stats >= 2);
	m_occ_raster = (1 == mode) ? nullptr : &occ_engine_legacy();
	m_occ_masked = (0 == mode) ? nullptr : &occ_engine_moc();
	m_occ_query = (1 == mode) ? m_occ_masked : m_occ_raster;

	u32 rw = 0, rh = 0;
	if (m_occ_masked)
	{
		occ_engine_moc_set_res(res);
		occ_engine_moc_preset_size(res, rw, rh);
	}
	svp_stats_hom_engine = u32(mode);
	svp_stats_hom_res_w = rw;
	svp_stats_hom_res_h = rh;

	if (mode != m_occ_mode_logged || res != m_occ_res_logged)
	{
		m_occ_mode_logged = mode;
		m_occ_res_logged = res;
		// no masked buffer under engine 0 so there is no resolution to name
		if (m_occ_masked)
			Msg("* [MOC] engine %d res %ux%u near %.3f", mode, rw, rh, near_w);
		else
			Msg("* [MOC] engine %d res n/a near %.3f", mode, near_w);
	}
}

void CHOM::Render(CFrustum& base)
{
	if (!bEnabled) return;

	// clip space near from the projection, w carries view z under it
	const float pz = -Device.mProject._33;
	const float near_w = (_abs(pz) > EPS) ? (Device.mProject._43 / pz) : 0.f;
	latch_engine(near_w);

	// the terrain scan latches lazily so a mid session cvar flip works without a reload
	if (ps_r__hom_terrain && m_occ_masked && !m_terr_ready && !m_terr_attempted)
	{
		m_terr_attempted = true;
		terrain_load();
	}

	// the occlusion frame starts here on whichever thread renders it, the latch just wrote engine and res
	// a menu frame never reaches this so the counters keep the last rendered frame
	svp_stats_hom_tris_in = 0;
	svp_stats_hom_tris_emitted = 0;
	svp_stats_hom_render_us = 0;
	svp_stats_hom_test_ticks = 0;
	svp_stats_hom_terr_cells = 0;
	svp_stats_hom_terr_emitted = 0;
	svp_stats_hom_terr_capped = 0;

	Device.Statistic->RenderCALC_HOM.Begin();
	const u64 t0 = CPU::QPC();
	if (m_occ_raster) m_occ_raster->begin_frame(Device.mFullTransform, Device.vCameraPosition, near_w);
	if (m_occ_masked) m_occ_masked->begin_frame(Device.mFullTransform, Device.vCameraPosition, near_w);
	Render_DB(base);
	// the hom loop can bail on an empty frustum query so terrain rides outside it
	if (m_occ_masked && m_terr_ready && ps_r__hom_terrain)
		terrain_emit(base, Device.vCameraPosition);
	if (m_occ_raster) m_occ_raster->end_frame();
	if (m_occ_masked) m_occ_masked->end_frame();
	svp_stats_hom_render_us = u32((CPU::QPC() - t0) * 1000000ull / CPU::qpc_freq);
	MT_frame_rendered.store(Device.dwFrame, std::memory_order_release);
	Device.Statistic->RenderCALC_HOM.End();
}

// shadow compare bookkeeping, legacy answers and the masked result only feeds the divergence tally
ICF void hom_shadow_note(BOOL primary, BOOL other)
{
	++svp_stats_hom_shadow_queries;
	if (!!primary != !!other)
	{
		++svp_stats_hom_disagree;
		// legacy culling what the masked engine keeps is the blur artifact direction
		if (!primary)
			++svp_stats_hom_dis_keep;
	}
}

BOOL CHOM::query_box(const Fbox& B)
{
	const u64 t0 = m_occ_time_queries ? CPU::QPC() : 0;
	BOOL result = m_occ_query->test_box(B);
	if (2 == m_occ_mode)
		hom_shadow_note(result, m_occ_masked->test_box(B));
	if (m_occ_time_queries)
		svp_stats_hom_test_ticks += CPU::QPC() - t0;
	return result;
}

BOOL CHOM::query_poly(const Fvector* v, u32 n)
{
	const u64 t0 = m_occ_time_queries ? CPU::QPC() : 0;
	BOOL result = m_occ_query->test_poly(v, n);
	if (2 == m_occ_mode)
		hom_shadow_note(result, m_occ_masked->test_poly(v, n));
	if (m_occ_time_queries)
		svp_stats_hom_test_ticks += CPU::QPC() - t0;
	return result;
}

BOOL CHOM::visible(Fbox3& B)
{
	if (!bEnabled) return TRUE;
	if (B.contains(Device.vCameraPosition)) return TRUE;
	return query_box(B);
}

BOOL CHOM::visible(Fbox2& B, float depth)
{
	// reads the legacy buffer directly, it is frozen under any other engine and has no caller
	VERIFY(0 == m_occ_mode);
	if (!bEnabled) return TRUE;
	return Raster.test(B.min.x, B.min.y, B.max.x, B.max.y, depth);
}

BOOL CHOM::visible(Fsphere& S)
{
	Fbox B;
	B.setb(S.P,Fvector().set(S.R, S.R, S.R));
	return visible(B);
}

BOOL CHOM::visible(vis_data& vis)
{
	if (Device.dwFrame < vis.hom_frame) return TRUE; // not at this time :)
	if (!bEnabled) return TRUE; // return - everything visible

	// Now, the test time comes
	// 0. The object was hidden, and we must prove that each frame	- test		| frame-old, tested-new, hom_res = false;
	// 1. The object was visible, but we must to re-check it		- test		| frame-new, tested-???, hom_res = true;
	// 2. New object slides into view								- delay test| frame-old, tested-old, hom_res = ???;
	u32 frame_current = Device.dwFrame;
	// u32	frame_prev		= frame_current-1;

#ifdef DEBUG
	Device.Statistic->RenderCALC_HOM.Begin	();
#endif
	BOOL result = query_box(vis.box);
	u32 delay = 1;
	if (result)
	{
		// visible	- delay next test, hashed per object and frame so worker and main queries never share a draw
		u32 h = u32(uintptr_t(&vis) >> 4) ^ (frame_current * 2654435761u);
		h ^= h >> 15; h *= 2246822519u;
		h ^= h >> 13; h *= 3266489917u;
		h ^= h >> 16;
		delay = 10 + (h % 15);
	}
	else
	{
		// hidden	- shedule to next frame
	}
	vis.hom_frame = frame_current + delay;
	vis.hom_tested = frame_current;
#ifdef DEBUG
	Device.Statistic->RenderCALC_HOM.End	();
#endif

	return result;
}

BOOL CHOM::visible(sPoly& P)
{
	if (!bEnabled) return TRUE;
	return query_poly(&P.front(), P.size());
}

void CHOM::Disable()
{
	bEnabled = FALSE;
}

void CHOM::Enable()
{
	bEnabled = m_pModel ? TRUE : FALSE;
}

#ifdef DEBUG
void CHOM::OnRender	()
{
	// the pixel dump reads the legacy buffer, it is stale under any other engine
	if (0 == m_occ_mode)
		Raster.on_dbg_render();

	if (psDeviceFlags.is(rsOcclusionDraw)){
		if (m_pModel){
			DEFINE_VECTOR		(FVF::L,LVec,LVecIt);
			static LVec	poly;	poly.resize(m_pModel->get_tris_count()*3);
			static LVec	line;	line.resize(m_pModel->get_tris_count()*6);
			for (int it=0; it<m_pModel->get_tris_count(); it++){
				CDB::TRI* T		= m_pModel->get_tris()+it;
				Fvector* verts	= m_pModel->get_verts();
				poly[it*3+0].set(*(verts+T->verts[0]),0x80FFFFFF);
				poly[it*3+1].set(*(verts+T->verts[1]),0x80FFFFFF);
				poly[it*3+2].set(*(verts+T->verts[2]),0x80FFFFFF);
				line[it*6+0].set(*(verts+T->verts[0]),0xFFFFFFFF);
				line[it*6+1].set(*(verts+T->verts[1]),0xFFFFFFFF);
				line[it*6+2].set(*(verts+T->verts[1]),0xFFFFFFFF);
				line[it*6+3].set(*(verts+T->verts[2]),0xFFFFFFFF);
				line[it*6+4].set(*(verts+T->verts[2]),0xFFFFFFFF);
				line[it*6+5].set(*(verts+T->verts[0]),0xFFFFFFFF);
			}
			RCache.set_xform_world(Fidentity);
			// draw solid
			Device.SetNearer(TRUE);
			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_Draw		(D3DPT_TRIANGLELIST,&*poly.begin(),poly.size()/3);
			Device.SetNearer(FALSE);
			// draw wire
			if (bDebug){
				RImplementation.rmNear();
			}else{
				Device.SetNearer(TRUE);
			}
			RCache.set_Shader	(dxRenderDeviceRender::Instance().m_SelectionShader);
			RCache.dbg_Draw		(D3DPT_LINELIST,&*line.begin(),line.size()/2);
			if (bDebug){
				RImplementation.rmNormal();
			}else{
				Device.SetNearer(FALSE);
			}
		}
	}
}
void CHOM::stats()
{
	if (m_pModel){
		CGameFont& F		= *Device.Statistic->Font();
		F.OutNext			(" **** HOM-occ ****");
		if (0 == m_occ_mode)
			F.OutNext		("  visible:  %2d", occ_legacy_dbg_visible());
		else
			F.OutNext		("  occ moc");
		F.OutNext			("  frustum:  %2d", tris_in_frame);
		F.OutNext			("    total:  %2d", m_pModel->get_tris_count());
	}
}
#endif
