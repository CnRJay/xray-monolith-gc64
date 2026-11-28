#include "stdafx.h"
#include "r4.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/fbasicvisual.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrCore/stream_reader.h"

#include "../xrRender/dxRenderDeviceRender.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRenderDX10/3DFluid/dx103DFluidVolume.h"

#include "../xrRender/FHierrarhyVisual.h"
#include "../xrRender/occ_engine.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

#include "../../3rd party/meshoptimizer/meshoptimizer.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

// MT_TREE_ST index reorder, load-time scratch shared by the visual pre-scan and LoadBuffers
namespace
{
struct tree_ib_range
{
	u32 vb_id, v_base, v_count;
	u32 ib_id, i_base, i_count;
	u32 pos_offset;
};

struct tree_vb_span
{
	u32 vb_id, v_base, v_end, pos_offset;
};

xr_vector<tree_ib_range> tree_ranges;
xr_vector<tree_vb_span> tree_spans;
xr_vector<u32> tree_range_span;
xr_vector<u8> tree_span_ok;
xr_vector<Fvector> tree_positions;
u32 tree_stat_cache = 0;
u32 tree_stat_overdraw = 0;
u32 tree_stat_skipped = 0;
u32 tree_stat_dup = 0;
u64 tree_stat_ticks = 0;
u32 tree_acmr_reported = 0;

bool tree_range_by_ib(const tree_ib_range& a, const tree_ib_range& b)
{
	if (a.ib_id != b.ib_id) return a.ib_id < b.ib_id;
	return a.i_base < b.i_base;
}

bool tree_range_by_vb(const u32 a, const u32 b)
{
	const tree_ib_range& ra = tree_ranges[a];
	const tree_ib_range& rb = tree_ranges[b];
	if (ra.vb_id != rb.vb_id) return ra.vb_id < rb.vb_id;
	return ra.v_base < rb.v_base;
}

void tree_ib_clear()
{
	// the merged position scratch runs to megabytes so hand the capacity back
	tree_ranges.clear_and_free();
	tree_spans.clear_and_free();
	tree_range_span.clear_and_free();
	tree_span_ok.clear_and_free();
	tree_positions.clear_and_free();
	tree_stat_cache = 0;
	tree_stat_overdraw = 0;
	tree_stat_skipped = 0;
	tree_stat_dup = 0;
	tree_stat_ticks = 0;
	tree_acmr_reported = 0;
}

// the reorder only reads the first 12 bytes of a vertex so anything else is left alone
bool tree_decl_pos_is_float3(const D3DVERTEXELEMENT9* dcl)
{
	for (u32 i = 0; i <= MAXD3DDECLLENGTH; ++i)
	{
		if (0xFF == dcl[i].Stream) break;
		if (D3DDECLUSAGE_POSITION == dcl[i].Usage && 0 == dcl[i].UsageIndex)
			return (D3DDECLTYPE_FLOAT3 == dcl[i].Type) && (0 == dcl[i].Offset);
	}
	return false;
}

// 32 entry fifo post transform cache model, matches the W0-5 offline measurement
float tree_ib_acmr(const u32* idx, u32 count)
{
	u32 fifo[32];
	u32 head = 0, fill = 0, misses = 0;
	for (u32 k = 0; k < count; ++k)
	{
		const u32 v = idx[k];
		bool hit = false;
		for (u32 j = 0; j < fill; ++j)
			if (fifo[j] == v) { hit = true; break; }
		if (hit) continue;
		++misses;
		if (fill < 32) fifo[fill++] = v;
		else { fifo[head] = v; head = (head + 1) & 31; }
	}
	return count ? (float(misses) * 3.f / float(count)) : 0.f;
}

// collect every MT_TREE_ST vb/ib window, indices are local to the range
void tree_ib_prescan(IReader* fs)
{
	tree_ib_clear();

	CTimer t;
	t.Start();

	IReader* vis = fs->open_chunk(fsL_VISUALS);
	if (0 == vis) return;

	IReader* chunk = 0;
	u32 index = 0;
	while ((chunk = vis->open_chunk(index)) != 0)
	{
		ogf_header H = {};
		chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
		if (MT_TREE_ST == H.type && chunk->find_chunk(OGF_GCONTAINER))
		{
			tree_ib_range r;
			r.vb_id = chunk->r_u32();
			r.v_base = chunk->r_u32();
			r.v_count = chunk->r_u32();
			r.ib_id = chunk->r_u32();
			r.i_base = chunk->r_u32();
			r.i_count = chunk->r_u32();
			r.pos_offset = u32(-1);
			tree_ranges.push_back(r);
		}
		chunk->close();
		++index;
	}
	vis->close();

	std::sort(tree_ranges.begin(), tree_ranges.end(), tree_range_by_ib);
	tree_stat_ticks += t.GetElapsed_ticks();
}

// Prepare merged VB spans before the single fsL_VB pass so tree positions are captured directly from RAM with zero second disk read.
void tree_ib_prepare_spans()
{
	tree_spans.clear();
	tree_range_span.clear();
	tree_span_ok.clear();
	tree_positions.clear();
	if (tree_ranges.empty()) return;

	xr_vector<u32> order(tree_ranges.size());
	for (u32 i = 0; i < order.size(); ++i) order[i] = i;
	std::sort(order.begin(), order.end(), tree_range_by_vb);

	tree_range_span.assign(tree_ranges.size(), u32(-1));
	for (u32 k = 0; k < order.size(); ++k)
	{
		const u32 ri = order[k];
		const tree_ib_range& r = tree_ranges[ri];
		if (0 == r.v_count) continue;
		if (!tree_spans.empty() && tree_spans.back().vb_id == r.vb_id && r.v_base <= tree_spans.back().v_end)
		{
			if (r.v_base + r.v_count > tree_spans.back().v_end) tree_spans.back().v_end = r.v_base + r.v_count;
		}
		else
		{
			tree_vb_span s = {r.vb_id, r.v_base, r.v_base + r.v_count, 0};
			tree_spans.push_back(s);
		}
		tree_range_span[ri] = u32(tree_spans.size() - 1);
	}

	u32 total = 0;
	for (u32 s = 0; s < tree_spans.size(); ++s)
	{
		tree_spans[s].pos_offset = total;
		total += tree_spans[s].v_end - tree_spans[s].v_base;
	}
	if (0 == total) return;
	tree_positions.resize(total);
	tree_span_ok.assign(tree_spans.size(), 0);
}

void tree_ib_capture_vb(u32 vb_id, const D3DVERTEXELEMENT9* dcl, const BYTE* pData, u32 v_count, u32 v_size, u32& span_cursor)
{
	if (tree_spans.empty()) return;
	const bool pos_ok = tree_decl_pos_is_float3(dcl) && v_size >= sizeof(Fvector);
	while (span_cursor < tree_spans.size() && tree_spans[span_cursor].vb_id < vb_id) ++span_cursor;

	if (!pos_ok && span_cursor < tree_spans.size() && tree_spans[span_cursor].vb_id == vb_id)
		Msg("! [TREE-IB] vb %d position is not float3 at offset 0, its tree ranges are skipped", vb_id);

	while (span_cursor < tree_spans.size() && tree_spans[span_cursor].vb_id == vb_id)
	{
		const tree_vb_span& sp = tree_spans[span_cursor];
		if (pos_ok && sp.v_end <= v_count)
		{
			const u32 n = sp.v_end - sp.v_base;
			const BYTE* src = pData + sp.v_base * v_size;
			Fvector* dst = &tree_positions[sp.pos_offset];
			for (u32 v = 0; v < n; ++v) CopyMemory(&dst[v], src + v * v_size, sizeof(Fvector));
			tree_span_ok[span_cursor] = 1;
		}
		++span_cursor;
	}
}

void tree_ib_finalize_spans()
{
	if (tree_spans.empty() || tree_positions.empty()) return;
	for (u32 ri = 0; ri < tree_ranges.size(); ++ri)
	{
		const u32 s = tree_range_span[ri];
		if (u32(-1) == s || 0 == tree_span_ok[s]) continue;
		tree_ranges[ri].pos_offset = tree_spans[s].pos_offset + (tree_ranges[ri].v_base - tree_spans[s].v_base);
	}
}

// Parallel meshoptimizer cache + overdraw reorder across all disjoint tree index ranges in this IB.
void tree_ib_reorder(u32 ib_id, u16* data, u32 ib_count, u32& cursor)
{
	const bool acmr_dbg = ps_r__svp_stats >= 2;

	while (cursor < tree_ranges.size() && tree_ranges[cursor].ib_id < ib_id)
	{
		++tree_stat_skipped;
		++cursor;
	}

	xr_vector<tree_ib_range> jobs;
	u32 prev_base = 0, prev_count = 0;
	bool have_prev = false;

	while (cursor < tree_ranges.size() && tree_ranges[cursor].ib_id == ib_id)
	{
		const tree_ib_range& r = tree_ranges[cursor++];
		if (have_prev && r.i_base == prev_base && r.i_count == prev_count)
		{
			++tree_stat_dup;
			continue;
		}
		if (have_prev && r.i_base < prev_base + prev_count)
		{
			Msg("! [TREE-IB] range overlap, skipped");
			++tree_stat_skipped;
			continue;
		}
		prev_base = r.i_base;
		prev_count = r.i_count;
		have_prev = true;

		if (u32(-1) == r.pos_offset || 0 == r.i_count || (r.i_count % 3) || 0 == r.v_count
			|| r.i_base + r.i_count > ib_count)
		{
			++tree_stat_skipped;
			continue;
		}
		jobs.push_back(r);
	}

	if (jobs.empty())
		return;

	xr_vector<u8> job_ok(jobs.size(), 0);
	auto runJob = [&](size_t ji)
	{
		const tree_ib_range& r = jobs[ji];
		xr_vector<u32> idx_a(r.i_count);
		xr_vector<u32> idx_b(r.i_count);
		u32 max_idx = 0;
		for (u32 k = 0; k < r.i_count; ++k)
		{
			const u32 v = data[r.i_base + k];
			idx_a[k] = v;
			if (v > max_idx) max_idx = v;
		}
		if (max_idx >= r.v_count)
			return;

		meshopt_optimizeVertexCache(&idx_b[0], &idx_a[0], r.i_count, r.v_count);
		meshopt_optimizeOverdraw(&idx_a[0], &idx_b[0], r.i_count, &tree_positions[r.pos_offset].x,
		                         r.v_count, sizeof(Fvector), 1.05f);

		for (u32 k = 0; k < r.i_count; ++k)
			data[r.i_base + k] = u16(idx_a[k]);
		job_ok[ji] = 1;
	};

	if (jobs.size() >= 4 && !acmr_dbg)
	{
		tbb::parallel_for(tbb::blocked_range<size_t>(0, jobs.size(), 4),
			[&](const tbb::blocked_range<size_t>& r)
			{
				for (size_t ji = r.begin(); ji < r.end(); ++ji)
					runJob(ji);
			});
	}
	else
	{
		for (size_t ji = 0; ji < jobs.size(); ++ji)
			runJob(ji);
	}

	for (size_t ji = 0; ji < jobs.size(); ++ji)
	{
		if (job_ok[ji])
		{
			++tree_stat_cache;
			++tree_stat_overdraw;
		}
		else
		{
			++tree_stat_skipped;
		}
	}
}
}

void CRender::level_Load(IReader* fs)
{
	R_ASSERT(0!=g_pGameLevel);
	R_ASSERT(!b_loaded);

	// Begin
	pApp->LoadBegin();
	dxRenderDeviceRender::Instance().Resources->DeferredLoad(TRUE);
	IReader* chunk;

	// Shaders
	//	g_pGamePersistent->LoadTitle		("st_loading_shaders");
	g_pGamePersistent->LoadTitle();
	{
		chunk = fs->open_chunk(fsL_SHADERS);
		R_ASSERT2(chunk, "Level doesn't builded correctly.");
		u32 count = chunk->r_u32();
		Shaders.resize(count);
		for (u32 i = 0; i < count; i++) // skip first shader as "reserved" one
		{
			string512 n_sh, n_tlist;
			LPCSTR n = LPCSTR(chunk->pointer());
			chunk->skip_stringZ();
			if (0 == n[0]) continue;
			xr_strcpy(n_sh, n);
			LPSTR delim = strchr(n_sh, '/');
			*delim = 0;
			xr_strcpy(n_tlist, delim + 1);
			Shaders[i] = dxRenderDeviceRender::Instance().Resources->Create(n_sh, n_tlist);
		}
		chunk->close();
	}

	// Components
	Wallmarks = xr_new<CWallmarksEngine>();
	Details = xr_new<CDetailManager>();

	if (!g_dedicated_server)
	{
		// the tree ranges have to exist before the shared ib is built
		if (ps_r__tree_ib_reorder) tree_ib_prescan(fs);

		// VB,IB,SWI
		//		g_pGamePersistent->LoadTitle("st_loading_geometry");
		g_pGamePersistent->LoadTitle();
		{
			CStreamReader* geom = FS.rs_open("$level$", "level.geom");
			R_ASSERT2(geom, "level.geom");
			LoadBuffers(geom,FALSE);
			LoadSWIs(geom);
			FS.r_close(geom);
		}

		//...and alternate/fast geometry
		{
			CStreamReader* geom = FS.rs_open("$level$", "level.geomx");
			R_ASSERT2(geom, "level.geomX");
			LoadBuffers(geom,TRUE);
			FS.r_close(geom);
		}

		// Visuals
		//		g_pGamePersistent->LoadTitle("st_loading_spatial_db");
		g_pGamePersistent->LoadTitle();
		chunk = fs->open_chunk(fsL_VISUALS);
		LoadVisuals(chunk);
		chunk->close();

		// Details
		//		g_pGamePersistent->LoadTitle("st_loading_details");
		g_pGamePersistent->LoadTitle();
		Details->Load();
	}

	// Sectors
	//	g_pGamePersistent->LoadTitle("st_loading_sectors_portals");
	g_pGamePersistent->LoadTitle();
	LoadSectors(fs);

	// 3D Fluid
	Load3DFluid();

	// HOM
	HOM.Load();

	// pip fps-audit summary, one line per level load so testers can diff runs against it
	{
		extern u32 svp_stats_detail_main_thread;
		extern u32 svp_stats_hom_main_thread;
		extern u32 svp_stats_hom_tested;
		extern u32 svp_stats_hom_rejected;
		extern int ps_r__hom_engine;
		extern int ps_r__hom_moc_res;
		u32 occ_w = 0, occ_h = 0;
		occ_engine_moc_preset_size(ps_r__hom_moc_res, occ_w, occ_h);
		PipMsg("[SVP-AUDIT] detail_main_thread=%u hom_main_thread=%u hom_tested=%u hom_rejected=%u hom_engine=%d hom_res=%ux%u",
			svp_stats_detail_main_thread, svp_stats_hom_main_thread, svp_stats_hom_tested, svp_stats_hom_rejected,
			ps_r__hom_engine, occ_w, occ_h);
	}

	// Lights
	// pApp->LoadTitle			("Loading lights...");
	LoadLights(fs);

	// End
	pApp->LoadEnd();

	// signal loaded
	b_loaded = TRUE;
}

void CRender::level_Unload()
{
	if (0 == g_pGameLevel) return;
	if (!b_loaded) return;

	GMBase.clear();
	GMRainWet.clear();
	for (sun::cascade& cascade : m_sun_cascades)
		cascade.GMCascade.clear();

	u32 I;

	// HOM
	HOM.Unload();

	//*** Details
	Details->Unload();

	//*** Sectors
	// 1.
	xr_delete(rmPortals);
	pLastSector = 0;
	pOutdoorSector = 0;
	vLastCameraPos.set(0, 0, 0);
	// 2.
	for (I = 0; I < Sectors.size(); I++) xr_delete(Sectors[I]);
	Sectors.clear();
	// 3.
	for (I = 0; I < Portals.size(); I++) xr_delete(Portals[I]);
	Portals.clear();

	//*** Lights
	// Glows.Unload			();
	Lights.Unload();

	//*** Visuals
	for (I = 0; I < Visuals.size(); I++)
	{
		Visuals[I]->Release();
		xr_delete(Visuals[I]);
	}
	Visuals.clear();

	//*** SWI
	for (I = 0; I < SWIs.size(); I++)xr_free(SWIs[I].sw);
	SWIs.clear();

	//*** VB/IB
	for (I = 0; I < nVB.size(); I++) _RELEASE(nVB[I]);
	for (I = 0; I < xVB.size(); I++) _RELEASE(xVB[I]);
	nVB.clear();
	xVB.clear();
	for (I = 0; I < nIB.size(); I++) _RELEASE(nIB[I]);
	for (I = 0; I < xIB.size(); I++) _RELEASE(xIB[I]);
	nIB.clear();
	xIB.clear();
	nDC.clear();
	xDC.clear();

	//*** Components
	xr_delete(Details);
	xr_delete(Wallmarks);

	//*** Shaders
	Shaders.clear_and_free();

	const bool clearResources = psDeviceFlags2.test(rsClearAllResources);
	if (psDeviceFlags2.test(rsClearModels) || clearResources)
	{
		Models->ClearPool(true);
		Visuals.clear_and_free();
		if (clearResources)
		{
			dxRenderDeviceRender::Instance().Resources->UnloadAllTexturesOnLevelUnload();
			dxRenderDeviceRender::Instance().ResourcesDestroyNecessaryTextures();
			dxRenderDeviceRender::Instance().Resources->Evict();
		}
		dxRenderDeviceRender::Instance().Resources->Dump(false);
		//static int unload_counter = 0;
		//Msg("The Level Unloaded.======================== %d", ++unload_counter);
	}

	b_loaded = FALSE;
}

void CRender::LoadBuffers(CStreamReader* base_fs, BOOL _alternative)
{
	R_ASSERT2(base_fs, "Could not load geometry. File not found.");
	dxRenderDeviceRender::Instance().Resources->Evict();
	//	u32	dwUsage					= D3DUSAGE_WRITEONLY;

	xr_vector<VertexDeclarator>& _DC = _alternative ? xDC : nDC;
	xr_vector<ID3DVertexBuffer*>& _VB = _alternative ? xVB : nVB;
	xr_vector<ID3DIndexBuffer*>& _IB = _alternative ? xIB : nIB;

	const bool tree_reorder = !_alternative && !tree_ranges.empty();
	CTimer tree_timer;
	u32 tree_span_cursor = 0;
	u32 tree_range_cursor = 0;
	if (tree_reorder)
	{
		tree_timer.Start();
		tree_ib_prepare_spans();
		tree_stat_ticks += tree_timer.GetElapsed_ticks();
	}

	// Vertex buffers
	{
		// Use DX9-style declarators
		CStreamReader* fs = base_fs->open_chunk(fsL_VB);
		R_ASSERT2(fs, "Could not load geometry. File 'level.geom?' corrupted.");
		u32 count = fs->r_u32();
		_DC.resize(count);
		_VB.resize(count);
		u32 bufferSize = (MAXD3DDECLLENGTH + 1) * sizeof(D3DVERTEXELEMENT9);
		D3DVERTEXELEMENT9* dcl = (D3DVERTEXELEMENT9*)_alloca(bufferSize);
		for (u32 i = 0; i < count; i++)
		{
			// decl
			fs->r(dcl, bufferSize);
			fs->advance(-(int)bufferSize);

			u32 dcl_len = D3DXGetDeclLength(dcl) + 1;
			_DC[i].resize(dcl_len);
			fs->r(_DC[i].begin(), dcl_len * sizeof(D3DVERTEXELEMENT9));

			// count, size
			u32 vCount = fs->r_u32();
			u32 vSize = D3DXGetDeclVertexSize(dcl, 0);
			Msg("* [Loading VB] %d verts, %d Kb", vCount, (vCount * vSize) / 1024);

			BYTE* pData = xr_alloc<BYTE>(vCount * vSize);
			fs->r(pData, vCount * vSize);
			if (tree_reorder)
			{
				tree_timer.Start();
				tree_ib_capture_vb(i, dcl, pData, vCount, vSize, tree_span_cursor);
				tree_stat_ticks += tree_timer.GetElapsed_ticks();
			}
			dx10BufferUtils::CreateVertexBuffer(&_VB[i], pData, vCount * vSize);
			xr_free(pData);
		}
		fs->close();
	}

	if (tree_reorder)
	{
		tree_timer.Start();
		tree_ib_finalize_spans();
		tree_stat_ticks += tree_timer.GetElapsed_ticks();
	}

	// Index buffers
	{
		CStreamReader* fs = base_fs->open_chunk(fsL_IB);
		u32 count = fs->r_u32();
		_IB.resize(count);
		for (u32 i = 0; i < count; i++)
		{
			u32 iCount = fs->r_u32();
			Msg("* [Loading IB] %d indices, %d Kb", iCount, (iCount * 2) / 1024);

			// Create and fill
			//BYTE*	pData		= 0;
			//R_CHK				(HW.pDevice->CreateIndexBuffer(iCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_MANAGED,&_IB[i],0));
			//R_CHK				(_IB[i]->Lock(0,0,(void**)&pData,0));
			//			CopyMemory			(pData,fs().pointer(),iCount*2);
			//fs->r				(pData,iCount*2);
			//_IB[i]->Unlock		();

			//	TODO: DX10: Check fragmentation.
			//	Check if buffer is less then 2048 kb
			BYTE* pData = xr_alloc<BYTE>(iCount * 2);
			fs->r(pData, iCount * 2);
			if (tree_reorder)
			{
				tree_timer.Start();
				tree_ib_reorder(i, (u16*)pData, iCount, tree_range_cursor);
				tree_stat_ticks += tree_timer.GetElapsed_ticks();
			}
			dx10BufferUtils::CreateIndexBuffer(&_IB[i], pData, iCount * 2);
			xr_free(pData);

			//			fs().advance		(iCount*2);
		}
		fs->close();
	}

	if (tree_reorder)
	{
		tree_stat_skipped += u32(tree_ranges.size()) - tree_range_cursor;
		const u32 ms = u32(tree_stat_ticks * 1000ull / CPU::qpc_freq);
		Msg("* [TREE-IB] ranges %d cache %d overdraw %d dup %d skipped %d ms %d", u32(tree_ranges.size()),
		    tree_stat_cache, tree_stat_overdraw, tree_stat_dup, tree_stat_skipped, ms);
		if (ms > 100) Msg("! [TREE-IB] reorder over the 100 ms load budget");
	}
	if (!_alternative) tree_ib_clear();
}

void CRender::LoadVisuals(IReader* fs)
{
	IReader* chunk = 0;
	u32 index = 0;
	dxRender_Visual* V = 0;
	ogf_header H;

	while ((chunk = fs->open_chunk(index)) != 0)
	{
		chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
		V = Models->Instance_Create(H.type);
		V->Load(0, chunk, 0);
		Visuals.push_back(V);

		chunk->close();
		index++;
	}
}

void CRender::LoadLights(IReader* fs)
{
	// lights
	Lights.Load(fs);
	Lights.LoadHemi();
}

struct b_portal
{
	u16 sector_front;
	u16 sector_back;
	svector<Fvector, 6> vertices;
};

void CRender::LoadSectors(IReader* fs)
{
	// allocate memory for portals
	u32 size = fs->find_chunk(fsL_PORTALS);
	R_ASSERT(0==size%sizeof(b_portal));
	u32 count = size / sizeof(b_portal);
	Portals.resize(count);
	for (u32 c = 0; c < count; c++)
		Portals[c] = xr_new<CPortal>();

	// load sectors
	IReader* S = fs->open_chunk(fsL_SECTORS);
	for (u32 i = 0; ; i++)
	{
		IReader* P = S->open_chunk(i);
		if (0 == P) break;

		CSector* __S = xr_new<CSector>();
		__S->load(*P);
		Sectors.push_back(__S);

		P->close();
	}
	S->close();

	// load portals
	if (count)
	{
		CDB::Collector CL;
		fs->find_chunk(fsL_PORTALS);
		for (u32 i = 0; i < count; i++)
		{
			b_portal P;
			fs->r(&P, sizeof(P));
			CPortal* __P = (CPortal*)Portals[i];
			__P->Setup(P.vertices.begin(), P.vertices.size(),
			           (CSector*)getSector(P.sector_front),
			           (CSector*)getSector(P.sector_back));
			for (u32 j = 2; j < P.vertices.size(); j++)
				CL.add_face_packed_D(
					P.vertices[0], P.vertices[j - 1], P.vertices[j],
					u32(i)
				);
		}
		if (CL.getTS() < 2)
		{
			Fvector v1, v2, v3;
			v1.set(-20000.f, -20000.f, -20000.f);
			v2.set(-20001.f, -20001.f, -20001.f);
			v3.set(-20002.f, -20002.f, -20002.f);
			CL.add_face_packed_D(v1, v2, v3, 0);
		}

		// build portal model
		rmPortals = xr_new<CDB::MODEL>();
		rmPortals->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
	}
	else
	{
		rmPortals = 0;
	}

	// debug
	//	for (int d=0; d<Sectors.size(); d++)
	//		Sectors[d]->DebugDump	();

	pLastSector = 0;

	// Search for default sector - assume "default" or "outdoor" sector is the largest one
	//. hack: need to know real outdoor sector
	CSector* largest_sector = 0;
	float largest_sector_vol = 0;
	for (u32 s = 0; s < Sectors.size(); s++)
	{
		CSector* S = (CSector*)Sectors[s];
		dxRender_Visual* V = S->root();
		float vol = V->vis.box.getvolume();
		if (vol > largest_sector_vol)
		{
			largest_sector_vol = vol;
			largest_sector = S;
		}
	}
	pOutdoorSector = largest_sector;
}

void CRender::LoadSWIs(CStreamReader* base_fs)
{
	// allocate memory for portals
	if (base_fs->find_chunk(fsL_SWIS))
	{
		CStreamReader* fs = base_fs->open_chunk(fsL_SWIS);
		u32 item_count = fs->r_u32();

		xr_vector<FSlideWindowItem>::iterator it = SWIs.begin();
		xr_vector<FSlideWindowItem>::iterator it_e = SWIs.end();

		for (; it != it_e; ++it)
			xr_free((*it).sw);

		SWIs.clear_not_free();

		SWIs.resize(item_count);
		for (u32 c = 0; c < item_count; c++)
		{
			FSlideWindowItem& swi = SWIs[c];
			swi.reserved[0] = fs->r_u32();
			swi.reserved[1] = fs->r_u32();
			swi.reserved[2] = fs->r_u32();
			swi.reserved[3] = fs->r_u32();
			swi.count = fs->r_u32();
			VERIFY(NULL==swi.sw);
			swi.sw = xr_alloc<FSlideWindow>(swi.count);
			fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
		}
		fs->close();
	}
}

void CRender::Load3DFluid()
{
	if (!RImplementation.o.volumetricfog)
		return;

	string_path fn_game;
	if (FS.exist(fn_game, "$level$", "level.fog_vol"))
	{
		IReader* F = FS.r_open(fn_game);
		u16 version = F->r_u16();

		if (version == 3)
		{
			u32 cnt = F->r_u32();
			for (u32 i = 0; i < cnt; ++i)
			{
				dx103DFluidVolume* pVolume = xr_new<dx103DFluidVolume>();
				pVolume->Load("", F, 0);

				//	Attach to sector's static geometry
				CSector* pSector = (CSector*)detectSector(pVolume->getVisData().sphere.P);
				//	3DFluid volume must be in render sector
				VERIFY(pSector);

				dxRender_Visual* pRoot = pSector->root();
				//	Sector must have root
				VERIFY(pRoot);
				VERIFY(pRoot->getType() == MT_HIERRARHY);

				((FHierrarhyVisual*)pRoot)->children.push_back(pVolume);
			}
		}

		FS.r_close(F);
	}
}
