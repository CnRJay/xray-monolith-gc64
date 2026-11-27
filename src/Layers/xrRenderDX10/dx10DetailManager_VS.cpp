#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "StateManager/dx10ShaderResourceStateCache.h"

// Vars to store wind prev frame data ( Motion vectors )
static u32 prev_frame = -1;
static float prev_time = 0;
static Fvector4	prev_dir1 = { 0, 0, 0 }, prev_dir2 = { 0, 0, 0 };

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

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

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

float GoToValue(float& current, float go_to)
{
	float diff = abs(current - go_to);

	float r_value = Device.fTimeDelta;

	if (diff - r_value <= 0)
	{
		current = go_to;
		return 0;
	}

	return current < go_to ? r_value : -r_value;
}

#ifdef USE_DX11
// reserved vs resource slot for the instance records, the grass vs itself only binds s_waves at t0
enum { DETAIL_INSTANCE_SRV_SLOT = 3 };

ICF u16 dm_unorm16(float v) { return u16(iFloor(clampr(v, 0.f, 1.f) * 65535.f + .5f)); }
ICF u8 dm_unorm8(float v) { return u8(iFloor(clampr(v, 0.f, 1.f) * 255.f + .5f)); }

// unit vector to an octahedral unorm16 pair
ICF void dm_oct_encode(const Fvector& n, u16& ox, u16& oy)
{
	const float inv = 1.f / (_abs(n.x) + _abs(n.y) + _abs(n.z));
	float px = n.x * inv;
	float py = n.y * inv;
	if (n.z <= 0.f)
	{
		const float fx = 1.f - _abs(py);
		const float fy = 1.f - _abs(px);
		px = px >= 0.f ? fx : -fx;
		py = py >= 0.f ? fy : -fy;
	}
	ox = dm_unorm16(px * .5f + .5f);
	oy = dm_unorm16(py * .5f + .5f);
}

// orthonormal basis to a smallest three quaternion, three snorm16 plus the dropped index
ICF void dm_quat_encode(const Fmatrix& M, s16* qs, u16& qidx)
{
	float t, q[4];
	if (M._33 < 0.f)
	{
		if (M._11 > M._22)
		{
			t = 1.f + M._11 - M._22 - M._33;
			q[0] = t; q[1] = M._12 + M._21; q[2] = M._31 + M._13; q[3] = M._23 - M._32;
		}
		else
		{
			t = 1.f - M._11 + M._22 - M._33;
			q[0] = M._12 + M._21; q[1] = t; q[2] = M._23 + M._32; q[3] = M._31 - M._13;
		}
	}
	else
	{
		if (M._11 < -M._22)
		{
			t = 1.f - M._11 - M._22 + M._33;
			q[0] = M._31 + M._13; q[1] = M._23 + M._32; q[2] = t; q[3] = M._12 - M._21;
		}
		else
		{
			t = 1.f + M._11 + M._22 + M._33;
			q[0] = M._23 - M._32; q[1] = M._31 - M._13; q[2] = M._12 - M._21; q[3] = t;
		}
	}

	const float mag = _sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	const float inv = mag > 0.f ? 1.f / mag : 0.f;

	u32 idx = 0;
	float best = _abs(q[0]);
	for (u32 c = 1; c < 4; c++)
	{
		const float a = _abs(q[c]);
		if (a > best) { best = a; idx = c; }
	}

	// the dropped component is forced positive so the shader can rebuild it from a square root
	const float enc = (q[idx] < 0.f ? -inv : inv) * 46340.95f;
	u32 w = 0;
	for (u32 c = 0; c < 4; c++)
	{
		if (c == idx) continue;
		qs[w++] = s16(clampr(iFloor(q[c] * enc + .5f), -32767, 32767));
	}
	qidx = u16(idx);
}

#pragma pack(push, 1)
struct InstanceHW
{
	float px, py, pz; // world position
	s16 q[3];         // rotation, smallest three
	u16 qidx;         // dropped rotation component
	u16 nx, ny;       // terrain normal, octahedral
	u16 alpha;        // unorm
	u8 c_sun;         // unorm
	u8 c_hemi;        // unorm
	float scale;      // uniform draw scale
};
#pragma pack(pop)
static_assert(sizeof(InstanceHW) == CDetailManager::hw_InstanceStride, "InstanceHW must match hw_InstanceStride");
#endif

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
	//	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
	float fDelta = Device.fTimeGlobal - m_global_time_old;
	if ((fDelta < 0) || (fDelta > 1)) fDelta = 0.03;
	m_global_time_old = Device.fTimeGlobal;

	m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
	m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
	m_time_pos += fDelta * swing_current.speed;

	//float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
	//float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
	float tm_rot1 = m_time_rot_1;
	float tm_rot2 = m_time_rot_2;

	Fvector4 dir1, dir2;
	dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
	dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

	// Setup geometry and DMA
	RCache.set_CullMode(CULL_NONE);
	RCache.set_xform_world(Fidentity);
	RCache.set_Geometry(hw_Geom);

#ifdef USE_DX11
	// fill once per frame, the later phases replay the same buffer through their own ranges
	if (hw_instancing && hw_frame_filled != Device.dwFrame)
	{
		Device.Statistic->RenderDUMP_DT_Count = 0; // accumulates across this frame's phases
		hw_Fill_Instances();
	}
#endif

	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;

	// Wave0
	consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
	//RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);				// consts
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir1);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0, L);

	// Wave1
	//wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir2);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0, L);

	// Still
	consts.set(scale, scale, scale, 1.f);
	//RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
	//RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
	//hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 0, 1, L);

	if (prev_frame != Device.dwFrame) 
	{
		prev_frame = Device.dwFrame;
		
		// Prev Frame swing time
		prev_time = m_time_pos;

		// Prev frame dir
		prev_dir1.set(dir1);
		prev_dir2.set(dir2);
	}

	RCache.set_CullMode(CULL_CCW);
}

#ifdef USE_DX11
void CDetailManager::hw_Fill_Instances()
{
	// pack every visible instance into the buffer with one map and record the per object ranges
	D3D11_MAPPED_SUBRESOURCE mapped;
	CHK_DX(HW.pContext->Map(hw_instanceVB, 0, D3D_MAP_WRITE_DISCARD, 0, &mapped));
	InstanceHW* pInst = (InstanceHW*)mapped.pData;

	const u32 nObj = (u32)objects.size();
	u32 instTotal = 0;
	BOOL bOverflow = FALSE;
	const bool runs = (ps_r__sun_grass_runs || ps_r__sun_grass_cull || ps_r__svp_grass_cull);
	if (runs)
		hw_run_slots.clear();
	for (u32 vid = 0; vid < 3; vid++)
	{
		vis_list& list = m_visibles[vid];
		for (u32 O = 0; O < nObj; O++)
		{
			hw_inst_base[vid * nObj + O] = instTotal;
			if (runs)
				hw_run_base[vid * nObj + O] = (u32)hw_run_slots.size();

			xr_vector<SlotItemVec*>& vis = list[O];
			xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
			xr_vector<SlotItemVec*>::iterator _vE = vis.end();
			for (; _vI != _vE; _vI++)
			{
				SlotItemVec* items = *_vI;
				// every item of a vec belongs to one slot so the head names that slot's sphere
				u32 slot_entry = u32(-1);
				if (runs && !items->empty())
				{
					SlotSphere sph;
					sph.center = items->front()->position;
					sph.radius = items->front()->radius;
					sph.inst_first = instTotal;
					sph.inst_count = 0;
					slot_entry = (u32)hw_run_slots.size();
					hw_run_slots.push_back(sph);
				}
				SlotItemVecIt _iI = items->begin();
				SlotItemVecIt _iE = items->end();
				for (; _iI != _iE; _iI++)
				{
					SlotItem& Instance = **_iI;

					// alpha smoothing advances once per frame now
					Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

					if (Instance.alpha <= 0)
						break;

					if (instTotal >= hw_instance_cap)
					{
						bOverflow = TRUE;
						break;
					}

					// position and scale stay exact, the vs rebuilds the rows from the quaternion
					const Fmatrix& M = Instance.mRotY;
					InstanceHW& R = pInst[instTotal];
					R.px = M._41;
					R.py = M._42;
					R.pz = M._43;
					dm_quat_encode(M, R.q, R.qidx);
					dm_oct_encode(Instance.normal, R.nx, R.ny);
					R.alpha = dm_unorm16(Instance.alpha);
					R.c_sun = dm_unorm8(Instance.c_sun);
					R.c_hemi = dm_unorm8(Instance.c_hemi);
					R.scale = Instance.scale_calculated;
					instTotal++;
				}
				// the item loop can leave early so the range length comes from the packed delta
				if (slot_entry != u32(-1))
					hw_run_slots[slot_entry].inst_count = instTotal - hw_run_slots[slot_entry].inst_first;
				if (instTotal >= hw_instance_cap)
					break;
			}
			hw_inst_count[vid * nObj + O] = instTotal - hw_inst_base[vid * nObj + O];
			if (runs)
				hw_run_count[vid * nObj + O] = (u32)hw_run_slots.size() - hw_run_base[vid * nObj + O];

			// UpdateVisibleM rebuilds the lists on the next frame
			if (!vis.empty())
				vis.clear_not_free();
		}
	}

	HW.pContext->Unmap(hw_instanceVB, 0);

	if (bOverflow && !hw_overflow_logged)
	{
		hw_overflow_logged = true;
		Msg("! [DETAILS] instance buffer overflow, cap(%d) - some grass dropped", hw_instance_cap);
	}

	hw_frame_filled = Device.dwFrame;
}

void CDetailManager::hw_Run_Begin(const CFrustum* F, u32 cascade)
{
	if (!hw_instancing)
		return;
	VERIFY(hw_run_frustum == nullptr);
	hw_run_frustum = F;
	hw_run_site = hw_run_site_sun;
	hw_run_cascade = cascade;
	hw_run_tested = hw_run_kept = hw_run_runs = hw_run_max = 0;
	hw_run_dropped = hw_run_draws = 0;
}

void CDetailManager::hw_Run_Begin_Scope(Fmatrix& full_xform)
{
	if (!hw_instancing || !ps_r__svp_grass_cull)
		return;
	VERIFY(hw_run_frustum == nullptr);
	// lateral planes and a far plane, no near plane, same build the scope world cull uses
	hw_run_frustum_own.CreateFromMatrix(full_xform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	hw_run_frustum = &hw_run_frustum_own;
	hw_run_site = hw_run_site_svp;
	hw_run_cascade = 0;
	hw_run_tested = hw_run_kept = hw_run_runs = hw_run_max = 0;
	hw_run_dropped = hw_run_draws = 0;
}

void CDetailManager::hw_Run_End()
{
	if (!hw_run_frustum)
		return;
	// the token arms on every cascade, only a walked site has numbers to publish
	const u32 site = hw_run_site;
	const bool sun = (site == hw_run_site_sun) && (ps_r__sun_grass_runs || ps_r__sun_grass_cull);
	const bool svp = (site == hw_run_site_svp) && ps_r__svp_grass_cull;
	hw_run_frustum = nullptr;
	hw_run_site = hw_run_site_none;

	if (svp && hw_run_dropped)
		svp_ledger_grass_cull = 1;

	if (ps_r__svp_stats && svp)
	{
		svp_stats_grass_svp_slots += hw_run_tested;
		svp_stats_grass_svp_keep += hw_run_kept;
		svp_stats_grass_svp_runs += hw_run_runs;
		svp_stats_grass_svp_drop += hw_run_dropped;
		svp_stats_grass_svp_draws += hw_run_draws;
		if (hw_run_max > svp_stats_grass_svp_run_max)
			svp_stats_grass_svp_run_max = hw_run_max;
	}

	if (!sun)
		return;

	if (ps_r__svp_stats)
	{
		svp_stats_grass_slots += hw_run_tested;
		svp_stats_grass_keep += hw_run_kept;
		svp_stats_grass_runs += hw_run_runs;
		svp_stats_grass_drop += hw_run_dropped;
		svp_stats_grass_draws += hw_run_draws;
		if (hw_run_max > svp_stats_grass_run_max)
			svp_stats_grass_run_max = hw_run_max;
	}

	if (ps_r__sun_grass_runs && (Device.dwFrame & 255) == 0)
		Msg("[SUN-RUNS] c%u slots %u keep %u runs %u max %u drop %u draws %u",
		    hw_run_cascade, hw_run_tested, hw_run_kept, hw_run_runs, hw_run_max,
		    hw_run_dropped, hw_run_draws);
}
#endif

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind,
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id, light* L)
{
	if (RImplementation.phase == CRender::PHASE_SMAP && var_id == 0)
		return;

	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strArray("array");
	static shared_str strXForm("xform");

	// Vanilla grass/trees wind
	static shared_str strWavePrev("wave_prev");
	static shared_str strDir2DPrev("dir2D_prev");

	// Grass Benders
	static shared_str strPrevPos("benders_prevpos");
	static shared_str strPos("benders_pos");
	static shared_str strGrassSetup("benders_setup");

	static shared_str strExData("exdata");
	static shared_str strGrassAlign("grass_align");

	// Grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	Fvector4 player_pos = { 0, 0, 0, 0 };
	int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

	// Add Player?
	if (ps_ssfx_grass_interactive.x > 0)
		player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);

#ifdef USE_DX11
	if (hw_instancing)
	{
		if (!RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector))
			return;

		if (RImplementation.phase == CRender::PHASE_SMAP && L)
		{
			if (!L->GMLight.is_sector_visible(RImplementation.pOutdoorSector))
				return;
		}

		// scale fade moved into the vs, x 1 fades on camera distance vs y, x 2 fades on light xz vs zw
		static shared_str strFadeParams("dt_fade_params");
		static shared_str strInstBase("dt_instance_base");

		const u32 nObj = (u32)objects.size();
		const u32 rbase = var_id * nObj;

		u32 total = 0;
		for (u32 obj = 0; obj < nObj; obj++)
			total += hw_inst_count[rbase + obj];
		if (total == 0)
			return;

		// slot spheres against the armed frustum, stretches of consecutive keeps only
		// the still set casts no cascade shadow so the sun site skips it, the scope gbuffer draws it
		const bool sun_site = hw_run_frustum && hw_run_site == hw_run_site_sun && var_id != 0;
		const bool svp_site = hw_run_frustum && hw_run_site == hw_run_site_svp;
		const bool cull = (sun_site && ps_r__sun_grass_cull) || (svp_site && ps_r__svp_grass_cull);
		if (cull || (sun_site && ps_r__sun_grass_runs))
		{
			for (u32 O = 0; O < nObj; O++)
			{
				const u32 first = hw_run_base[rbase + O];
				const u32 last = first + hw_run_count[rbase + O];
				u32 run = 0;
				for (u32 s = first; s < last; s++)
				{
					SlotSphere& sph = hw_run_slots[s];
					++hw_run_tested;
					if (hw_run_frustum->testSphere_dirty(sph.center, sph.radius))
					{
						++hw_run_kept;
						if (++run == 1)
							++hw_run_runs;
						if (run > hw_run_max)
							hw_run_max = run;
					}
					else
						run = 0;
				}
			}
		}

		Fvector4 fade_params;
		if (fade_distance <= -1)
			fade_params.set(2.f, 0.f, light_position.x, light_position.z);
		else
			fade_params.set(1.f, fade_distance, 0.f, 0.f);

		// the cascade grass draw prefers the shadow element, both resolutions must agree or the
		// pass counts diverge and the draw binds the wrong program
		const bool smap_element = sun_site && ps_r__grass_smap_element;
		auto pick_element = [&](u32 O) -> ShaderElement*
		{
			Shader* S = objects[O]->shader._get();
			if (!S)
				return nullptr;
			if (smap_element)
			{
				ShaderElement* Esm = S->E[2]._get();
				if (Esm)
					return Esm;
			}
			return S->E[lod_id]._get();
		};

		u32 maxPasses = 1;
		for (u32 O = 0; O < nObj; O++)
		{
			if (!hw_inst_count[rbase + O])
				continue;
			ShaderElement* E = pick_element(O);
			if (E)
				maxPasses = _max(maxPasses, (u32)E->passes.size());
		}

		for (u32 iPass = 0; iPass < maxPasses; ++iPass)
		{
			// group consecutive objects that share an element so the binds do not repeat
			ShaderElement* curE = 0;
			u32 vOffset = 0;
			u32 iOffset = 0;
			for (u32 O = 0; O < nObj; O++)
			{
				CDetail& Object = *objects[O];
				u32 count = hw_inst_count[rbase + O];
				ShaderElement* E = pick_element(O);
				if (count && E && iPass < E->passes.size())
				{
					if (E != curE)
					{
						curE = E;
						RCache.set_Element(E, iPass);
						// set_Element can disturb the vs resource slots, re-bind the record buffer
						SRVSManager.SetVSResource(DETAIL_INSTANCE_SRV_SLOT, hw_instanceSRV);
						RImplementation.apply_lmaterial();

						RCache.set_c(strConsts, consts);
						RCache.set_c(strWave, wave);
						RCache.set_c(strDir2D, wind);
						RCache.set_c(strXForm, Device.mFullTransform);
						RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);
						RCache.set_c(strWavePrev, prev_wave);
						RCache.set_c(strDir2DPrev, prev_wind);
						RCache.set_c(strFadeParams, fade_params);

						if (ps_ssfx_grass_interactive.y > 0)
						{
							RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

							Fvector4* c_grass;
							{
								void* GrassData;
								RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
								c_grass = (Fvector4*)GrassData;
							}
							if (c_grass)
							{
								c_grass[0].set(player_pos);
								c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

								for (int Bend = 1; Bend < BendersQty; Bend++)
								{
									c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
									c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
								}
							}

							Fvector4* c_prev_grass;
							{
								void* prev_GrassData;
								RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
								c_prev_grass = (Fvector4*)prev_GrassData;
							}
							if (c_prev_grass)
							{
								for (int Bend = 0; Bend < BendersQty; Bend++)
								{
									c_prev_grass[Bend].set(GData.prev_pos[Bend]);
									c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
								}
							}
						}
					}

					// SV_InstanceID restarts at zero every draw so the range base travels as a constant
					Fvector4 inst_base;
					if (cull)
					{
						// one draw per stretch of kept slots, the rejected stretches are never submitted
						const u32 first = hw_run_base[rbase + O];
						const u32 last = first + hw_run_count[rbase + O];
						u32 run_base = 0, run_end = 0, sent = 0;
						bool in_run = false;
						for (u32 s = first; s <= last; s++)
						{
							SlotSphere* sph = (s < last) ? &hw_run_slots[s] : nullptr;
							if (sph && hw_run_frustum->testSphere_dirty(sph->center, sph->radius))
							{
								if (!in_run)
								{
									in_run = true;
									run_base = sph->inst_first;
								}
								run_end = sph->inst_first + sph->inst_count;
								continue;
							}
							if (!in_run)
								continue;
							in_run = false;
							const u32 n = run_end - run_base;
							if (!n)
								continue;
							inst_base.set(float(run_base), 0.f, 0.f, 0.f);
							RCache.set_c(strInstBase, inst_base);
							RCache.RenderInstanced(D3DPT_TRIANGLELIST, n, vOffset, 0, Object.number_vertices,
							                       iOffset, Object.number_indices / 3);
							Device.Statistic->RenderDUMP_DT_Count += n;
							RCache.stat.r.s_details.add(n * Object.number_vertices);
							sent += n;
							++hw_run_draws;
						}
						hw_run_dropped += count - sent;
					}
					else
					{
						inst_base.set(float(hw_inst_base[rbase + O]), 0.f, 0.f, 0.f);
						RCache.set_c(strInstBase, inst_base);

						RCache.RenderInstanced(D3DPT_TRIANGLELIST, count, vOffset, 0, Object.number_vertices,
						                       iOffset, Object.number_indices / 3);
						Device.Statistic->RenderDUMP_DT_Count += count;
						RCache.stat.r.s_details.add(count * Object.number_vertices);
					}
				}
				vOffset += Object.number_vertices;
				iOffset += Object.number_indices;
			}
		}
		return;
	}
#endif

	Device.Statistic->RenderDUMP_DT_Count = 0;

	// Matrices and offsets
	u32 vOffset = 0;
	u32 iOffset = 0;

	vis_list& list = m_visibles[var_id];

	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	Fvector c_sun, c_ambient, c_hemi;
	c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
	c_sun.mul(.5f);
	c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
	c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

	// pip the main-pass drain keeps the visible set when the SVP pass draws the scope grass second
	extern bool g_svp_defer_detail_clear;

	// Iterate
	for (u32 O = 0; O < objects.size(); O++)
	{
		CDetail& Object = *objects[O];
		xr_vector<SlotItemVec*>& vis = list[O];
		if (!vis.empty())
		{
			for (u32 iPass = 0; iPass < Object.shader->E[lod_id]->passes.size(); ++iPass)
			{
				// Setup matrices + colors (and flush it as necessary)
				//RCache.set_Element				(Object.shader->E[lod_id]);
				RCache.set_Element(Object.shader->E[lod_id], iPass);
				RImplementation.apply_lmaterial();

				//	This could be cached in the corresponding consatant buffer
				//	as it is done for DX9
				RCache.set_c(strConsts, consts);
				RCache.set_c(strWave, wave);
				RCache.set_c(strDir2D, wind);
				RCache.set_c(strXForm, Device.mFullTransform);
				RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);

				RCache.set_c(strWavePrev, prev_wave);
				RCache.set_c(strDir2DPrev, prev_wind);

				if (ps_ssfx_grass_interactive.y > 0)
				{
					RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

					Fvector4* c_grass;
					{
						void* GrassData;
						RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
						c_grass = (Fvector4*)GrassData;
					}
					VERIFY(c_grass);

					if (c_grass)
					{
						c_grass[0].set(player_pos);
						c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

						for (int Bend = 1; Bend < BendersQty; Bend++)
						{
							c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
							c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
						}
					}

					Fvector4* c_prev_grass;
					{
						void* prev_GrassData;
						RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
						c_prev_grass = (Fvector4*)prev_GrassData;
					}
					VERIFY(c_prev_grass);

					if (c_prev_grass)
					{
						for (int Bend = 0; Bend < BendersQty; Bend++)
						{
							c_prev_grass[Bend].set(GData.prev_pos[Bend]);
							c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
						}
					}
				}

				Fvector4* c_ExData = 0;
				{
					void* pExtraData;
					RCache.get_ConstantDirect(strExData, hw_BatchSize * sizeof(Fvector4), &pExtraData, 0, 0);
					c_ExData = (Fvector4*)pExtraData;
				}
				VERIFY(c_ExData);

				//ref_constant constArray = RCache.get_c(strArray);
				//VERIFY(constArray);

				//u32			c_base				= x_array->vs.index;
				//Fvector4*	c_storage			= RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
				Fvector4* c_storage = 0;
				//	Map constants to memory directly
				{
					void* pVData;
					RCache.get_ConstantDirect(strArray,
					                          hw_BatchSize * sizeof(Fvector4) * 4,
					                          &pVData, 0, 0);
					c_storage = (Fvector4*)pVData;
				}
				VERIFY(c_storage);

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

						if (RImplementation.phase == CRender::PHASE_SMAP && L)
						{
							if (!L->GMLight.is_sector_visible(RImplementation.pOutdoorSector))
								continue;

							if (L->position.distance_to_sqr(Instance.position) >= _sqr(L->range))
								continue;
						}

						u32 base = dwBatch * 4;

						Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

						float scale = 1.f;

						// Sort of fade using the scale
						// fade_distance == -1 use light_position to define "fade", anything else uses fade_distance
						if (fade_distance <= -1)
							scale *= 1.0f - Instance.position.distance_to_xz_sqr(light_position) * 0.005f;
						else if (Instance.distance > fade_distance)
							scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;

						if (scale <= 0 || Instance.alpha <= 0)
							break;

						// Build matrix ( 3x4 matrix, last row - color )
						Fmatrix M = Instance.mRotY;
						const float sc = Instance.scale_calculated;
						M._11 *= sc; M._21 *= sc; M._31 *= sc;
						M._12 *= sc; M._22 *= sc; M._32 *= sc;
						M._13 *= sc; M._23 *= sc; M._33 *= sc;
						c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
						c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
						c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
						//RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,	M._31*scale,	M._41	);
						//RCache.set_ca(&*constArray, base+1, M._12*scale,	M._22*scale,	M._32*scale,	M._42	);
						//RCache.set_ca(&*constArray, base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

						// Build color
						// R2 only needs hemisphere
						float h = Instance.c_hemi;
						float s = Instance.c_sun;
						c_storage[base + 3].set(s, s, s, h);

						if (c_ExData)
							c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);

						//RCache.set_ca(&*constArray, base+3, s,				s,				s,				h		);
						dwBatch ++;
						if (dwBatch == hw_BatchSize)
						{
							// flush
							Device.Statistic->RenderDUMP_DT_Count += dwBatch;
							u32 dwCNT_verts = dwBatch * Object.number_vertices;
							u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
							//RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
							//RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
							RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
							RCache.stat.r.s_details.add(dwCNT_verts);

							// restart
							dwBatch = 0;

							//	Remap constants to memory directly (just in case anything goes wrong)
							{
								void* pVData;
								RCache.get_ConstantDirect(strArray,
								                          hw_BatchSize * sizeof(Fvector4) * 4,
								                          &pVData, 0, 0);
								c_storage = (Fvector4*)pVData;
							}
							VERIFY(c_storage);
						}
					}
				}
				// flush if nessecary
				if (dwBatch)
				{
					Device.Statistic->RenderDUMP_DT_Count += dwBatch;
					u32 dwCNT_verts = dwBatch * Object.number_vertices;
					u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
					//RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
					//RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
					RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
					RCache.stat.r.s_details.add(dwCNT_verts);
				}
			}
			// Clean up
			// KD: we must not clear vis on r2 since we want details shadows
			// pip the deferred clear keeps the set alive for the SVP drain, its own pass clears after
			if (ps_ssfx_grass_shadows.x <= 0 && !g_svp_defer_detail_clear)
			{
				if (!psDeviceFlags2.test(rsGrassShadow) || RImplementation.PHASE_NORMAL == RImplementation.phase) // phase normal without shadows
					vis.clear_not_free();
			}
		}
		vOffset += hw_BatchSize * Object.number_vertices;
		iOffset += hw_BatchSize * Object.number_indices;
	}
}
