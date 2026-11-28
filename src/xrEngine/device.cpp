#include "stdafx.h"
#include "../xrCDB/frustum.h"
#include "xr_ioconsole.h"
#include "xr_input.h"
#include "../xrCore/profiler.h"

#pragma warning(disable:4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
// d3dx9.h
#include <d3dx9.h>
#pragma warning(default:4995)

#include "x_ray.h"
#include "discord\discord.h"
#include "render.h"
#include "renderdoc_integration.h"
#include <chrono>

// must be defined before include of FS_impl.h
#define INCLUDE_FROM_ENGINE
#include "../xrCore/FS_impl.h"

#ifdef INGAME_EDITOR
# include "../include/editor/ide.hpp"
# include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "xrSash.h"
#include "igame_persistent.h"
#include "shader_bus.h"

#include "CustomHUD.h"
#include "EngineThreading.h"
#include "IGame_Level.h"

#include "Rain.h"
#include "../Layers/xrRender/svp_console.h"
#include "../Include/xrRender/UIRender.h"

extern bool use_reshade;
extern void render_reshade_effects();
extern ENGINE_API int ps_r4_hdr10_pda;

#pragma comment( lib, "d3dx9.lib" )

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;
ENGINE_API CRenderDevice* DevicePtr = nullptr;

ENGINE_API xr_atomic_bool g_bRendering = false;
#if XRCORE_PROFILER == PROFILER_OPTICK
// Frames left in a profiler capture started by prof_capture
ENGINE_API int g_prof_capture_frames = 0;
#endif
extern ENGINE_API float psHUD_FOV;
extern float psCAM_ATTACHED_FOV;
extern ENGINE_API float psCamAttachedFOV;

BOOL g_bLoaded = FALSE;
ref_light precache_light = 0;

BOOL mt_calc_bones = TRUE;
BOOL psLua_ParallelGC = TRUE;
BOOL psLua_ParallelGC_debug = FALSE;

extern discord::Core* discord_core;
extern bool use_discord;

extern Fvector4 ps_ssfx_grass_interactive;

#ifdef ECO_RENDER
std::chrono::high_resolution_clock::time_point tlastf = std::chrono::high_resolution_clock::now(), tcurrentf = std::
	                                               chrono::high_resolution_clock::now();
std::chrono::duration<float> time_span;
ENGINE_API float refresh_rate = 0;
#endif // ECO_RENDER


BOOL CRenderDevice::Begin()
{
	PROF_EVENT("Render: Begin", PROF_CAT(Rendering));

#ifndef DEDICATED_SERVER
	switch (m_pRender->GetDeviceState())
	{
	case IRenderDeviceRender::dsOK:
		break;

	case IRenderDeviceRender::dsLost:
		// If the device was lost, do not render until we get it back
		Sleep(33);
		return FALSE;
		break;

	case IRenderDeviceRender::dsNeedReset:
		// Check if the device is ready to be reset
		Reset();
		break;

	default:
		R_ASSERT(0);
	}

	m_pRender->Begin();

	FPU::m24r();
	g_bRendering = true;
#endif
	return TRUE;
}

void CRenderDevice::Clear()
{
	m_pRender->Clear();
}


void CRenderDevice::End(void)
{
	PROF_EVENT("Render: End", PROF_CAT(Rendering));

#ifndef DEDICATED_SERVER


#ifdef INGAME_EDITOR
    bool load_finished = false;
#endif // #ifdef INGAME_EDITOR
	if (dwPrecacheFrame)
	{
		::Sound->set_master_volume(0.f);
		dwPrecacheFrame--;

		if (!dwPrecacheFrame)
		{
#ifdef INGAME_EDITOR
            load_finished = true;
#endif // #ifdef INGAME_EDITOR

			m_pRender->updateGamma();

			if (precache_light)
			{
				precache_light->set_active(false);
				precache_light.destroy();
			}
			::Sound->set_master_volume(1.f);

			m_pRender->ResourcesDestroyNecessaryTextures();

			Msg("* [x-ray]: Handled Necessary Textures Destruction");
			Memory.mem_compact();
			//Msg("* MEMORY USAGE: %lld K", Memory.mem_usage() / 1024);
			//Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);

#ifdef FIND_CHUNK_BENCHMARK_ENABLE
            g_find_chunk_counter.flush();
#endif // FIND_CHUNK_BENCHMARK_ENABLE
		}
	}

	g_bRendering = false;
	// end scene
	// Present goes here, so call OA Frame end.
	if (g_SASH.IsBenchmarkRunning())
		g_SASH.DisplayFrame(Device.fTimeGlobal);
	m_pRender->End();

# ifdef INGAME_EDITOR
    if (load_finished && m_editor)
        m_editor->on_load_finished();
# endif // #ifdef INGAME_EDITOR
#endif
}

void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input)
{
#ifdef DEDICATED_SERVER
    amount = 0;
#else
	if (m_pRender->GetForceGPU_REF())
		amount = 0;
#endif

	dwPrecacheFrame = dwPrecacheTotal = amount;
	if (amount && !precache_light && g_pGameLevel && g_loading_events.empty())
	{
		precache_light = ::Render->light_create();
		precache_light->set_shadow(false);
		precache_light->set_position(vCameraPosition);
		precache_light->set_color(255, 255, 255);
		precache_light->set_range(5.0f);
		precache_light->set_active(true);
	}

	if (amount && b_draw_loadscreen && !load_screen_renderer.b_registered)
	{
		load_screen_renderer.start(b_wait_user_input);
	}
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

extern bool IsMainMenuActive(); //ECO_RENDER add

static HMONITOR g_StartupMonitor = NULL;

#include "MonitorList.h"

static void InitMonitor()
{
	if (g_StartupMonitor)
		return;

	HMONITOR chosen = ResolveSelectedMonitor();
	if (chosen)
	{
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoA(chosen, &mi))
		{
			g_StartupMonitor = chosen;
			return;
		}
		Msg("! vid_monitor: resolved handle is invalid, using Auto");
	}

	POINT cursorPos;
	GetCursorPos(&cursorPos);
	g_StartupMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
}

ENGINE_API void ResetStartupMonitor()
{
	g_StartupMonitor = NULL;
}

ENGINE_API void SetStartupMonitor(HMONITOR h)
{
	g_StartupMonitor = h;
}

ENGINE_API HMONITOR GetStartupMonitor()
{
	InitMonitor();
	return g_StartupMonitor;
}

void GetMonitorResolution(u32& horizontal, u32& vertical)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		horizontal = mi.rcMonitor.right - mi.rcMonitor.left;
		vertical = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}
	else
	{
		RECT desktop;
		const HWND hDesktop = GetDesktopWindow();
		GetWindowRect(hDesktop, &desktop);
		horizontal = desktop.right - desktop.left;
		vertical = desktop.bottom - desktop.top;
	}
}

void GetMonitorPosition(int& x, int& y)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		x = mi.rcMonitor.left;
		y = mi.rcMonitor.top;
	}
	else
	{
		x = 0;
		y = 0;
	}
}

float GetMonitorRefresh()
{
	DEVMODE lpDevMode;
	memset(&lpDevMode, 0, sizeof(DEVMODE));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmDriverExtra = 0;

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0)
	{
		return 1.f / 60.f;
	}
	else
		return 1.f / lpDevMode.dmDisplayFrequency;
}

extern int ps_framelimiter;
extern u32 g_screenmode;

CTimer FreezeTimer;
void mt_FreezeThread(void *ptr) {
	float freezetime = 0.f;
	float repeatcheck = 500.f;

	while (true)
	{
		PROF_EVENT(PROF_CAT(Debug));

		if (g_loading_events.size())
			freezetime = 25000.0f;
		else
			freezetime = 5000.0f;

		repeatcheck = 500.f;

		START_PROFILE("Check timer", PROF_CAT(Debug));
		if (FreezeTimer.GetElapsed_sec()*1000.f > freezetime)
		{
			xrLogger::FlushLog();
			repeatcheck = 5000.f;
		}
		STOP_PROFILE;

		Sleep(repeatcheck);
	}
}

void CRenderDevice::prepare_matrices()
{
	auto svp = m_SecondViewport.IsSVPFrame();
	mView_prev = Device.matrices_previous[svp].mView;
	mProject_prev = Device.matrices_previous[svp].mProject;
	m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);
}

void CRenderDevice::SnapshotRenderFrame()
{
	m_rt_view.dwWidth = dwWidth;
	m_rt_view.dwHeight = dwHeight;
	m_rt_view.fWidth_2 = fWidth_2;
	m_rt_view.fHeight_2 = fHeight_2;
	m_rt_view.fFOV = fFOV;
	m_rt_view.fASPECT = fASPECT;
	m_rt_view.dwFrame = dwFrame;
	m_rt_view.dwViewport = dwViewport;
	m_rt_view.fTimeGlobal = fTimeGlobal;
	m_rt_view.fTimeDelta = fTimeDelta;
	m_rt_view.dwTimeGlobal = dwTimeGlobal;

	m_rt_view.vCameraPosition = vCameraPosition;
	m_rt_view.vCameraDirection = vCameraDirection;
	m_rt_view.vCameraTop = vCameraTop;
	m_rt_view.vCameraRight = vCameraRight;
	m_rt_view.vCameraPosition_saved = vCameraPosition_saved;

	m_rt_view.mView = mView;
	m_rt_view.mProject = mProject;
	m_rt_view.mProjectHud = mProjectHud;
	m_rt_view.mFullTransform = mFullTransform;
	m_rt_view.mFullTransformHud = mFullTransformHud;
	m_rt_view.mInvView = mInvView;
	m_rt_view.mInvProject = mInvProject;
	m_rt_view.mInvProjectHud = mInvProjectHud;
	m_rt_view.mInvFullTransform = mInvFullTransform;
	m_rt_view.mView_saved = mView_saved;
	m_rt_view.mProject_saved = mProject_saved;
	m_rt_view.mFullTransform_saved = mFullTransform_saved;

	m_rt_view.mView_prev = mView_prev;
	m_rt_view.mProject_prev = mProject_prev;
	m_rt_view.mFullTransform_prev = mFullTransform_prev;
	m_rt_view.mViewHud = mViewHud;
	m_rt_view.mViewHud_prev = mViewHud_prev;
	m_rt_view.mProjectHud_prev = mProjectHud_prev;
	m_rt_view.mFullTransformHud_prev = mFullTransformHud_prev;
	m_rt_view.mViewCam = mViewCam;
	m_rt_view.mProjectCam = mProjectCam;
	m_rt_view.mFullTransformCam = mFullTransformCam;
	m_rt_view.mViewCam_prev = mViewCam_prev;
	m_rt_view.mProjectCam_prev = mProjectCam_prev;
	m_rt_view.mFullTransformCam_prev = mFullTransformCam_prev;
	m_rt_view.mProjectCamAttached = mProjectCamAttached;
	m_rt_view.mFullTransformCamAttached = mFullTransformCamAttached;
	m_rt_view.mProject3DIcons = mProject3DIcons;
	m_rt_view.mFullTransform3DIcons = mFullTransform3DIcons;

	m_rt_view.matrices[0] = matrices[0];
	m_rt_view.matrices[1] = matrices[1];
	m_rt_view.matrices_previous[0] = matrices_previous[0];
	m_rt_view.matrices_previous[1] = matrices_previous[1];
	m_rt_view.wind_anim_prev = wind_anim_prev;
	m_rt_view.wind_anim_saved = wind_anim_saved;

	if (g_pGamePersistent)
	{
		CEnvironment& env = g_pGamePersistent->Environment();
		if (env.CurrentEnv)
		{
			const CEnvDescriptorMixer& desc = *env.CurrentEnv;
			m_rt_env.fog_near = desc.fog_near;
			m_rt_env.fog_far = desc.fog_far;
			m_rt_env.fog_density = desc.fog_density;
			m_rt_env.fog_color = desc.fog_color;
			m_rt_env.sky_color = desc.sky_color;
			m_rt_env.sky_rotation = desc.sky_rotation;
			m_rt_env.clouds_color = desc.clouds_color;
			m_rt_env.weight = desc.weight;
			m_rt_env.ambient = desc.ambient;
			m_rt_env.hemi_color = desc.hemi_color;
			m_rt_env.sun_color = desc.sun_color;
			m_rt_env.sun_dir = desc.sun_dir;
			m_rt_env.far_plane = desc.far_plane;
			m_rt_env.rain_density = desc.rain_density;
			m_rt_env.rain_color = desc.rain_color;
			m_rt_env.wind_direction = desc.wind_direction;
			m_rt_env.wind_velocity = desc.wind_velocity;
#ifdef TREE_WIND_EFFECT
			m_rt_env.tree_amplitude_intensity = desc.m_fTreeAmplitudeIntensity;
#endif
			m_rt_env.water_intensity = desc.m_fWaterIntensity;
			m_rt_env.bloom_threshold = desc.bloom_threshold;
			m_rt_env.bloom_exposure = desc.bloom_exposure;
			m_rt_env.bloom_sky_intensity = desc.bloom_sky_intensity;
		}
		m_rt_env.wetness_factor = env.wetness_factor;
		m_rt_env.wind_strength_factor = env.wind_strength_factor;
		m_rt_env.game_time = env.GetGameTime();
		m_rt_env.wind_anim = env.wind_anim;

		if (g_pGamePersistent->m_pGShaderConstants)
		{
			m_rt_env.hud_params = g_pGamePersistent->m_pGShaderConstants->hud_params;
			m_rt_env.hud_fov_params = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
			m_rt_env.script_params = g_pGamePersistent->m_pGShaderConstants->m_script_params;
			m_rt_env.blender_mode = g_pGamePersistent->m_pGShaderConstants->m_blender_mode;
		}

		m_rt_env.actor_health = g_pGamePersistent->actor_data.health;
		m_rt_env.actor_stamina = g_pGamePersistent->actor_data.stamina;
		m_rt_env.actor_bleeding = g_pGamePersistent->actor_data.bleeding;
		m_rt_env.actor_helmet = g_pGamePersistent->actor_data.helmet;

		m_rt_env.pda_display_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
		m_rt_env.pda_psy_influence = g_pGamePersistent->pda_shader_data.pda_psy_influence;
		m_rt_env.pda_display_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;

		const IGame_Persistent::grass_data& gd = g_pGamePersistent->grass_shader_data;
		for (int i = 0; i < 16; ++i)
		{
			m_rt_env.grass_pos[i].set(gd.pos[i].x, gd.pos[i].y, gd.pos[i].z, gd.radius_curr[i]);
			m_rt_env.grass_dir[i].set(gd.dir[i].x, gd.dir[i].y, gd.dir[i].z, gd.str[i]);
			m_rt_env.grass_prev_pos[i] = gd.prev_pos[i];
			m_rt_env.grass_prev_dir[i] = gd.prev_dir[i];
			m_rt_env.grass_str[i] = gd.str[i];
			m_rt_env.grass_radius_curr[i] = gd.radius_curr[i];
		}
		m_rt_env.valid = true;
	}
	else
	{
		m_rt_env.valid = false;
	}
}

void CRenderDevice::on_idle()
{

	FreezeTimer.Start();

	if (!b_is_Ready)
	{
		Sleep(100);
		return;
	}

	PROF_FRAME("Main Thread");

#if XRCORE_PROFILER == PROFILER_OPTICK
	if (g_prof_capture_frames > 0 && --g_prof_capture_frames == 0)
	{
		// Drain the decoupled Render Thread before Optick::StopCapture triggers gpu_timeline::prof_stop() on HW.pContext.
		FlushRenderThread();
		PROF_STOP_CAPTURE();
		string_path fn;
		FS.update_path(fn, "$logs$", "optick.opt");
		FILETIME before;
		GetSystemTimeAsFileTime(&before);
		PROF_SAVE_CAPTURE(fn);
		// Optick reports success even when it cannot open the file, so the file itself is checked
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (GetFileAttributesExA(fn, GetFileExInfoStandard, &fa) && CompareFileTime(&fa.ftLastWriteTime, &before) >= 0 && (fa.nFileSizeLow || fa.nFileSizeHigh))
			Msg("~ profiler capture saved to %s", fn);
		else
			Msg("! profiler capture could not be saved to %s", fn);
	}
#endif

#ifdef DEDICATED_SERVER
    u32 FrameStartTime = TimerGlobal.GetElapsed_ms();
#endif

	START_PROFILE("Set stat gathering", PROF_CAT(Debug));
	if (psDeviceFlags.test(rsStatistic))
		g_bEnableStatGather = TRUE;
	else g_bEnableStatGather = FALSE;
	STOP_PROFILE;

	if (g_loading_events.size())
	{
		FlushRenderThread();
		// Loads lower the perf hub's gate, so the hub skips the frames they span
		frame_record_on = false;
		{
			PROF_EVENT("Loading...", PROF_CAT(UI));
			if (g_loading_events.front()())
				g_loading_events.pop_front();
		}
		PROF_EVENT("LoadDraw", PROF_CAT(UI));
		pApp->LoadDraw();
		return;
	}

	if (!Device.dwPrecacheFrame && !g_SASH.IsBenchmarkRunning() && g_bLoaded)
	{
		PROF_EVENT("Start xrSASH Benchmark", PROF_CAT(Debug));
		g_SASH.StartBenchmark();
	}

	const bool can_decouple =
		ps_r__mt_decouple != 0 &&
		m_render_thread.IsRunning() &&
		::Render && ::Render->SupportsDecoupledRender() &&
		b_is_Active && !dwPrecacheFrame && !Paused() &&
		g_pGameLevel && g_hud &&
		!(g_pGamePersistent && g_pGamePersistent->OnRenderPPUI_query()) &&
		!(g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->CanSkipSceneRendering());

	if (can_decouple)
	{
		// Stage 1: FrameMove + GameThread + CalculateBones for Frame N overlap with Render Thread executing Frame N-1.
		if (frame_record_on)
			frame_record.start = CPU::QPC();
		FrameMove();
		const bool record = frame_record_on;

		vCameraPosition_bones = vCameraPosition;
		mFullTransform_bones.mul(mProject, mView);

		Device.isRendering = true;
		Device.LuaGCDone = false;
		Device.LuaGCCount = 0;

		if (mt_calc_bones)
			secondary_tasks.run(&XRay::Engine::CalculateBonesThread);
		else
			XRay::Engine::CalculateBonesThread();
		secondary_tasks.run(&XRay::Engine::GameThread);

		if (record)
			frame_record.cap[0] = CPU::QPC();
#ifdef ECO_RENDER
		if (Device.Paused() || IsMainMenuActive() || ps_framelimiter)
		{
			PROF_EVENT("Eco Render", PROF_CAT(Wait));
			if (refresh_rate == 0)
				refresh_rate = GetMonitorRefresh();
			float rr = ps_framelimiter ? (1.f / ps_framelimiter) : refresh_rate;
			time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
			while (time_span.count() < rr)
			{
				tcurrentf = std::chrono::high_resolution_clock::now();
				time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
			}
			tlastf = std::chrono::high_resolution_clock::now();
		}
#endif
		if (record)
			frame_record.cap[1] = CPU::QPC();

		// Sync barrier: join Frame N-1 Render Thread and Frame N secondary game tasks.
		FlushRenderThread();
		Device.isRendering = false;

		if (record)
			frame_record.join[0] = CPU::QPC();
		{
			PROF_EVENT("secondary_tasks.wait", PROF_CAT(Wait));
			extern int ps_r__svp_stats;
			extern float svp_stats_join_ms;
			const bool timed = ps_r__svp_stats != 0;
			CTimer join_timer;
			if (timed) join_timer.Start();
			secondary_tasks.wait();
			if (timed) svp_stats_join_ms = join_timer.GetElapsed_ms_f();
		}
		if (record)
		{
			frame_record.join[1] = CPU::QPC();
			frame_record.frame = dwFrame;
		}

		if (psLua_ParallelGC_debug && psLua_ParallelGC && Device.LuaGCDebug)
			Device.LuaGCDebug();

		if (m_rt_needs_reset.exchange(false, std::memory_order_acq_rel))
		{
			Reset();
			return;
		}

		// Safe sync window: both threads are quiescent. Drain deferred deletions from Frame N-1.
		if (Device.ModelDefferClear)
			Device.ModelDefferClear();
		{
			PROF_EVENT("seqParallelBeforRender", PROF_CAT(GameLogic));
			xrCriticalSectionGuard guard(&Device.seqParallelBeforRenderCS);
			for (size_t it = 0; it < Device.seqParallelBeforRender.size(); ++it)
			{
				auto Callback = Device.seqParallelBeforRender[it];
				if (Callback)
					Callback();
			}
			Device.seqParallelBeforRender.clear();
		}

		if (g_pGamePersistent != nullptr)
		{
			PROF_EVENT("Update Particles", PROF_CAT(VFX));
			g_pGamePersistent->UpdateParticles();
		}
		secondary_tasks.run(&XRay::Engine::PreRenderThread);

		START_PROFILE("Matrices", PROF_CAT(Camera));
		mFullTransform.mul(mProject, mView);
		mFullTransformHud.mul(mProjectHud, mView);
		mFullTransformCam.mul(mProjectCam, mView);
		mFullTransformCamAttached.mul(mProjectCamAttached, mView);
		mFullTransform3DIcons.mul(mProject3DIcons, mView);
		m_pRender->SetCacheXform(mView, mProject);

		Device.matrices_previous[0] = Device.matrices[0];
		Device.matrices_previous[1] = Device.matrices[1];
		Device.matrices[0].mView = mView;
		Device.matrices[0].mProject = mProject;
		Device.matrices[0].mProjectHud = mProjectHud;

		mViewHud_prev = mViewHud;
		mProjectHud_prev = mProjectHud;
		mFullTransformHud_prev = mFullTransformHud;
		mViewCam_prev = mViewCam;
		mProjectCam_prev = mProjectCam;
		mFullTransformCam_prev = mFullTransformCam;

		extern int g_svp_hud_true_fov;
		const float hud_fov_deg = (g_svp_hud_true_fov && true_pip_on && m_SecondViewport.IsSVPActive()
			&& g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.999f) ? fFOV : psHUD_FOV * 83.f;
		mProjectHud.build_projection(deg2rad(hud_fov_deg), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
		mProjectCam.build_projection(deg2rad(psCamAttachedFOV * psCAM_ATTACHED_FOV), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
		mProjectCamAttached = mProjectCam;

		Device.matrices[0].mView = mView;
		Device.matrices[0].mProject = mProject;
		Device.matrices[0].mProjectHud = mProjectHud;

		prepare_matrices();

		mFullTransform_prev = mFullTransform_saved;
		mViewHud.set(mView);
		mViewCam.set(mView);
		mFullTransformHud.mul(mProjectHud, mViewHud);
		mFullTransformCam.mul(mProjectCam, mViewCam);
		mFullTransformCamAttached = mFullTransformCam;

		if (true_pip_on)
			Device.matrices[0].mProjectHud = mProjectHud;

		IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
		GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
		GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);
		for (int pBend = 1; pBend < _min(16, ps_ssfx_grass_interactive.y + 1); pBend++)
		{
			GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z, GData.radius_curr[pBend]);
			GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z, GData.str[pBend]);
		}

		wind_anim_prev = wind_anim_saved;
		wind_anim_saved = g_pGamePersistent->Environment().wind_anim;
		D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, 0, (D3DXMATRIX*)&mFullTransform);

		vCameraPosition_saved = vCameraPosition;
		mFullTransform_saved = mFullTransform;
		mView_saved = mView;
		mProject_saved = mProject;
		STOP_PROFILE;

		secondary_tasks.run(&XRay::Engine::PreRenderPostTransformsThread);

		// Snapshot + Scene Extraction on Main Thread + TBB (while Rain/Particles/Details finish in parallel).
		SnapshotRenderFrame();
		::Render->ExtractFrame();
		secondary_tasks.wait();
		// Re-snapshot after ExtractFrame so SVP camera matrices[1] and latched viewport state are captured.
		SnapshotRenderFrame();

		// Record all 2D UI / 3D PDA / 3D HUD Item UI / BulletManager / HUD / Console / ImGui on Main Thread.
		m_decoupled_ui_recording = true;
		m_decoupled_pda_pending = false;
		UIRender->BeginRecord(2);
		if (g_hud && g_hud->RenderActiveItemUIQuery())
			g_hud->RenderActiveItemUI();
		UIRender->EndRecord();
		UIRender->BeginRecord(3);
		if (g_hud && g_hud->RenderCamAttachedUIQuery())
			g_hud->RenderCamAttachedUI();
		UIRender->EndRecord();
		UIRender->BeginRecord(1);
		seqRender.Process(rp_Render);
		if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())
		{
			PROF_EVENT("Record statistics", PROF_CAT(Debug));
			Statistic->Show();
		}
		UIRender->EndRecord();
		m_decoupled_ui_recording = false;

		const bool pda_pending = m_decoupled_pda_pending;
		const bool reshade_active = use_reshade;

		// Stage 2: Hand Frame N submission + Present to the Dedicated Render Thread and immediately return for Frame N+1!
		m_render_thread.Submit([this, pda_pending, reshade_active]()
		{
#ifndef DEDICATED_SERVER
			Statistic->RenderTOTAL_Real.FrameStart();
			Statistic->RenderTOTAL_Real.Begin();

			const auto state = m_pRender->GetDeviceState();
			if (state == IRenderDeviceRender::dsNeedReset)
			{
				m_rt_needs_reset.store(true, std::memory_order_release);
				Statistic->RenderTOTAL_Real.End();
				Statistic->RenderTOTAL_Real.FrameEnd();
				return;
			}
			if (state != IRenderDeviceRender::dsOK)
			{
				Statistic->RenderTOTAL_Real.End();
				Statistic->RenderTOTAL_Real.FrameEnd();
				return;
			}

			m_pRender->Begin();
			FPU::m24r();
			g_bRendering = true;

			if (pda_pending)
			{
				ps_r4_hdr10_pda = 1;
				UIRender->Replay(0);
				::Render->RenderToTarget(::Render->rtPDA);
				ps_r4_hdr10_pda = 0;
			}

			::Render->RenderSubmit();

			if (reshade_active)
			{
				RENDER_SCOPE(reshade);
				render_reshade_effects();
			}

			UIRender->Replay(1);
			m_imgui.RenderCaptured();

			g_bRendering = false;
			if (g_SASH.IsBenchmarkRunning())
				g_SASH.DisplayFrame(m_rt_view.fTimeGlobal);
			m_pRender->End();

			Statistic->RenderTOTAL_Real.End();
			Statistic->RenderTOTAL_Real.FrameEnd();
			Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
#endif
		});

		return;
	}

	// Coupled fallback path (MainMenu, Paused, Precache, Inactive, or r__mt_decouple 0): drain Render Thread first.
	FlushRenderThread();
	if (m_rt_needs_reset.exchange(false, std::memory_order_acq_rel))
		Reset();

	if (Device.ModelDefferClear)
	{
		Device.ModelDefferClear();
	}

	{
		PROF_EVENT("seqParallelBeforRender", PROF_CAT(GameLogic));
		xrCriticalSectionGuard guard(&Device.seqParallelBeforRenderCS);

		// A callback invoked here can destroy an object that owns a particle
		// group, and PS::CParticleGroup::SItem::~SItem cancels its own pending
		// entry in this very vector. That cancellation clears the entry in
		// place instead of erasing it, so the vector never changes size while
		// we walk it. Skip the cleared entries; the clear() below drops them.
		// Index by position and copy each delegate before invoking it, so an
		// entry appended by a callback is still handled safely.
		for (size_t it = 0; it < Device.seqParallelBeforRender.size(); ++it)
		{
			auto Callback = Device.seqParallelBeforRender[it];
			if (Callback)
				Callback();
		}

		Device.seqParallelBeforRender.clear();
	}

	// The perf hub raises the gate in seqFrame, so it holds from FrameMove to the next
	if (frame_record_on)
		frame_record.start = CPU::QPC();
	FrameMove();
	const bool record = frame_record_on;

    if (g_pGamePersistent != nullptr)
    {
        PROF_EVENT("Update Particles", PROF_CAT(VFX));
        g_pGamePersistent->UpdateParticles();
    }
    secondary_tasks.run(&XRay::Engine::PreRenderThread);

	// Precache
	if (dwPrecacheFrame)
	{
		// Precache frames are part of the load for the perf hub too
		frame_record_on = false;
		PROF_EVENT("Precache frame", PROF_CAT(Rendering));
		float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
		float angle = PI_MUL_2 * factor;
		vCameraDirection.set(_sin(angle), 0, _cos(angle));
		vCameraDirection.normalize();
		vCameraTop.set(0, 1, 0);
		vCameraRight.crossproduct(vCameraTop, vCameraDirection);

		mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
	}

	// Matrices
	START_PROFILE("Matrices", PROF_CAT(Camera));
	mFullTransform.mul(mProject, mView);
	mFullTransformHud.mul(mProjectHud, mView);
	mFullTransformCam.mul(mProjectCam, mView);
	mFullTransformCamAttached.mul(mProjectCamAttached, mView);
	mFullTransform3DIcons.mul(mProject3DIcons, mView);
	m_pRender->SetCacheXform(mView, mProject);

	// advance per-viewport history and store the main camera in slot 0
	// slot 1 = SVP, filled by svpCamera in the render layer
	Device.matrices_previous[0] = Device.matrices[0];
	Device.matrices_previous[1] = Device.matrices[1];
	Device.matrices[0].mView = mView;
	Device.matrices[0].mProject = mProject;
	Device.matrices[0].mProjectHud = mProjectHud;

	mViewHud_prev = mViewHud;
	mProjectHud_prev = mProjectHud;
	mFullTransformHud_prev = mFullTransformHud;
	mViewCam_prev = mViewCam;
	mProjectCam_prev = mProjectCam;
	mFullTransformCam_prev = mFullTransformCam;

	extern int g_svp_hud_true_fov;
	const float hud_fov_deg = (g_svp_hud_true_fov && true_pip_on && m_SecondViewport.IsSVPActive()
		&& g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.999f) ? fFOV : psHUD_FOV * 83.f;
	mProjectHud.build_projection(deg2rad(hud_fov_deg), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	mProjectCam.build_projection(deg2rad(psCamAttachedFOV * psCAM_ATTACHED_FOV), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	mProjectCamAttached = mProjectCam;

	Device.matrices[0].mView = mView;
	Device.matrices[0].mProject = mProject;
	Device.matrices[0].mProjectHud = mProjectHud;

	prepare_matrices();

	mFullTransform_prev = mFullTransform_saved;
	mViewHud.set(mView);
	mViewCam.set(mView);
	mFullTransformHud.mul(mProjectHud, mViewHud);
	mFullTransformCam.mul(mProjectCam, mViewCam);
	mFullTransformCamAttached = mFullTransformCam;

	if (true_pip_on)
		Device.matrices[0].mProjectHud = mProjectHud;

	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
	GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);
	for (int pBend = 1; pBend < _min(16, ps_ssfx_grass_interactive.y + 1); pBend++)
	{
		GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z, GData.radius_curr[pBend]);
		GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z, GData.str[pBend]);
	}

	wind_anim_prev = wind_anim_saved;
	wind_anim_saved = g_pGamePersistent->Environment().wind_anim;

	//RCache.set_xform_view ( mView );
	//RCache.set_xform_project ( mProject );
	D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, 0, (D3DXMATRIX*)&mFullTransform);

	vCameraPosition_saved = vCameraPosition;
	mFullTransform_saved = mFullTransform;
	mView_saved = mView;
	mProject_saved = mProject;


	vCameraPosition_bones = vCameraPosition;
	mFullTransform_bones = mFullTransform;
	SnapshotRenderFrame();

	STOP_PROFILE;

    // TODO: Try to move this upper
    secondary_tasks.run(&XRay::Engine::PreRenderPostTransformsThread);
	if (mt_calc_bones)
		secondary_tasks.run(&XRay::Engine::CalculateBonesThread);
	else
		XRay::Engine::CalculateBonesThread();

	Device.isRendering = true;
	Device.LuaGCDone = false;
	Device.LuaGCCount = 0;

	secondary_tasks.run(&XRay::Engine::GameThread);
	
	if (record)
		frame_record.cap[0] = CPU::QPC();
#ifdef ECO_RENDER // ECO_RENDER START
	if (Device.Paused() || IsMainMenuActive() || ps_framelimiter)
	{
		PROF_EVENT("Eco Render", PROF_CAT(Wait));

		if (refresh_rate == 0)
			refresh_rate = GetMonitorRefresh();

		float rr;

		if (ps_framelimiter)
			rr = 1.f / ps_framelimiter;
		else
			rr = refresh_rate;

		time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		while (time_span.count() < rr)
		{
			tcurrentf = std::chrono::high_resolution_clock::now();
			time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		}
		tlastf = std::chrono::high_resolution_clock::now();
	}
#endif // ECO_RENDER END
	if (record)
		frame_record.cap[1] = CPU::QPC();

#ifndef DEDICATED_SERVER
	Statistic->RenderTOTAL_Real.FrameStart();
	Statistic->RenderTOTAL_Real.Begin();

	if (b_is_Active && Begin())
	{
		START_PROFILE("Process seqRender", PROF_CAT(Rendering));
		seqRender.Process(rp_Render);
		STOP_PROFILE;

		if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())
		{
			PROF_EVENT("Draw statistics", PROF_CAT(Debug));
			RENDER_SCOPE(stats);
			Statistic->Show();
		}

		End();
	}
	Statistic->RenderTOTAL_Real.End();
	Statistic->RenderTOTAL_Real.FrameEnd();
	Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
#endif 
	Device.isRendering = false;

	if (record)
		frame_record.join[0] = CPU::QPC();
	{
		PROF_EVENT("secondary_tasks.wait", PROF_CAT(Wait));
		extern int ps_r__svp_stats;
		extern float svp_stats_join_ms;
		const bool timed = ps_r__svp_stats != 0;
		CTimer join_timer;
		if (timed) join_timer.Start();
		secondary_tasks.wait();
		if (timed) svp_stats_join_ms = join_timer.GetElapsed_ms_f();
	}
	if (record)
	{
		frame_record.join[1] = CPU::QPC();
		frame_record.frame = dwFrame;
	}

	if (psLua_ParallelGC_debug && psLua_ParallelGC && Device.LuaGCDebug)
	{
		Device.LuaGCDebug();
	}

#ifdef DEDICATED_SERVER
    u32 FrameEndTime = TimerGlobal.GetElapsed_ms();
    u32 FrameTime = (FrameEndTime - FrameStartTime);
    u32 DSUpdateDelta = 1000 / g_svDedicateServerUpdateReate;
    if (FrameTime < DSUpdateDelta)
        Sleep(DSUpdateDelta - FrameTime);
#endif
	if (!b_is_Active)
		Sleep(1);
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor()
{
    m_editor->run();
    m_editor_finalize(m_editor);
    xr_delete(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::Screenshot()
{
	PROF_EVENT(PROF_CAT(IO));
	FlushRenderThread();
	Render->Screenshot();
}

void CRenderDevice::message_loop()
{
#ifdef INGAME_EDITOR
    if (editor())
    {
        message_loop_editor();
        return;
    }
#endif
	MSG msg;
	PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		on_idle();
	}
}

void mt_DiscordThread(void*)
{
	while (true)
	{
		if (!pApp)
		{
			Msg("[Discord] pApp destroyed, killing thread");
			return;
		}

		//Discord
		if (use_discord && psDeviceFlags2.test(rsDiscord))
		{
			START_PROFILE("Discord", PROF_CAT(Network));
			discord_core->RunCallbacks();
			updateDiscordPresence();
			STOP_PROFILE;
			Sleep(int(discord_update_rate * 1000));
		}
		else
		{
			Sleep(1000); // Sleep for 1 second if Discord is not used or disabled
		}
	}
}

void CRenderDevice::Run()
{
	// DUMP_PHASE;
	g_bLoaded = FALSE;
	Log("Starting engine...");
	thread_name("X-RAY Primary thread");
	// Startup timers and calculate timer delta
	dwTimeGlobal = 0;
	Timer_MM_Delta = 0;
	{
		u32 time_mm = timeGetTime();
		while (timeGetTime() == time_mm); // wait for next tick
		u32 time_system = timeGetTime();
		u32 time_local = TimerAsync();
		Timer_MM_Delta = time_system - time_local;
	}

	// Start extra threads
	thread_spawn(mt_FreezeThread, "Freeze detecting thread", 0, 0);
	thread_spawn(mt_DiscordThread, "X-RAY Discord thread", 0, 0);
	m_render_thread.Start();

	// Message cycle
	seqAppStart.Process(rp_AppStart);

	//m_pRender->ClearTarget();
	SetForegroundWindow(m_hWnd);
	message_loop();

	FlushRenderThread();
	m_render_thread.Stop();

	seqAppEnd.Process(rp_AppEnd);

	secondary_tasks.wait();
	ParticleWorkerCallback.clear();
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void CRenderDevice::FrameMove()
{
	PROF_EVENT("Render: Frame Move", PROF_CAT(Rendering));

	if (InterlockedExchange(&g_monitor_list_dirty, 0))
		refresh_vid_monitor_list();

	dwFrame++;
	Core.dwFrame = dwFrame;
	renderdoc_poll_captures();
	dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;
	if (psDeviceFlags.test(rsConstantFPS))
	{
		PROF_EVENT("Constant FPS", PROF_CAT(Debug));

		// 20ms = 50fps
		//fTimeDelta = 0.020f;
		//fTimeGlobal += 0.020f;
		//dwTimeDelta = 20;
		//dwTimeGlobal += 20;
		// 33ms = 30fps
		fTimeDelta = 0.033f;
		fTimeGlobal += 0.033f;
		dwTimeDelta = 33;
		dwTimeGlobal += 33;
	}
	else
	{
		PROF_EVENT("Timer FPS", PROF_CAT(Debug));

		// Timer
		float fPreviousFrameTime = Timer.GetElapsed_sec();
		Timer.Start(); // previous frame
		fTimeDelta = 0.1f * fTimeDelta + 0.9f * fPreviousFrameTime;
		// smooth random system activity - worst case ~7% error
		//fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime; // smooth random system activity
		if (fTimeDelta > .1f)
			fTimeDelta = .1f; // limit to 15fps minimum
		if (fTimeDelta <= 0.f)
			fTimeDelta = EPS_S + EPS_S; // limit to 15fps minimum
		if (Paused())
			fTimeDelta = 0.0f;
		// u64 qTime = TimerGlobal.GetElapsed_clk();
		fTimeGlobal = TimerGlobal.GetElapsed_sec(); //float(qTime)*CPU::cycles2seconds;
		u32 _old_global = dwTimeGlobal;
		dwTimeGlobal = TimerGlobal.GetElapsed_ms();
		dwTimeDelta = dwTimeGlobal - _old_global;
	}

	// Frame move
	Statistic->EngineTOTAL.Begin();

	ShaderBus::frame_latch();
	
	START_PROFILE("Process seqFrame", PROF_CAT(GameLogic));
	Device.seqFrame.Process(rp_Frame);
	STOP_PROFILE;
	
	g_bLoaded = TRUE;
	
	Statistic->EngineTOTAL.End();
}

ENGINE_API BOOL bShowPauseString = TRUE;

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason)
{
	PROF_EVENT(PROF_CAT(GameLogic));

	static int snd_emitters_ = -1;

	if (g_bBenchmark)
		return;
#ifndef DEDICATED_SERVER
	if (bOn)
	{
		if (!Paused())
			bShowPauseString =
#ifdef INGAME_EDITOR
                editor() ? FALSE :
#endif // #ifdef INGAME_EDITOR
#ifdef DEBUG
                !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
				TRUE;

		if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()))
		{
			g_pauseMngr().Pause(true);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
		}

		if (bSound && ::Sound)
		{

			bool is_focus_loss = (strstr(reason, "app_deactivate") || strstr(reason, "deactivate") ||
				strstr(reason, "wm_deactivate") || strstr(reason, "inactive"));

			// Skip sound pause ONLY when it's borderless + real focus loss (alt-tab)
			bool should_skip = (g_screenmode == 1 && is_focus_loss && !strstr(reason, "mm_activate1"));

			if (!should_skip)  // Skip ONLY borderless + focus loss
			{
				snd_emitters_ = ::Sound->pause_emitters(true);
#ifdef DEBUG
				// Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
			}
		}
	}
	else
	{
		if (bTimer && g_pauseMngr().Paused())
		{
			fTimeDelta = EPS_S + EPS_S;
			g_pauseMngr().Pause(false);
		}

		if (bSound)
		{
			if (snd_emitters_ > 0) //avoid crash
			{
				snd_emitters_ = ::Sound->pause_emitters(false);
#ifdef DEBUG
				// Log("snd_emitters_[false]",snd_emitters_);
#endif
			}
			else
			{
#ifdef DEBUG
                Log("Sound->pause_emitters underflow");
#endif
			}
		}
	}

#endif
}

bool CRenderDevice::Paused()
{
	return g_pauseMngr().Paused();
}

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam)
{
	u16 fActive = LOWORD(wParam);
	BOOL fMinimized = (BOOL)HIWORD(wParam);
	BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;

	if (psDeviceFlags2.test(rsAlwaysActive) && g_screenmode != 2)
	{
		Device.b_is_Active = TRUE;

		if (Device.b_hide_cursor != bActive)
		{
			Device.b_hide_cursor = bActive;

			if (Device.b_hide_cursor)
			{
				ShowCursor(FALSE);
				if (m_hWnd)
				{
					RECT winRect;
					GetClientRect(m_hWnd, &winRect);
					MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
					ClipCursor(&winRect);
				}
				pInput->OnAppActivate();
			}
			else
			{
				ShowCursor(TRUE);
				ClipCursor(NULL);
				pInput->OnAppDeactivate();
			}
		}

		return;
	}

	if (bActive != Device.b_is_Active)
	{
		Device.b_is_Active = bActive;

		if (Device.b_is_Active)
		{
			Device.seqAppActivate.Process(rp_AppActivate);
			app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;

#ifndef DEDICATED_SERVER
# ifdef INGAME_EDITOR
            if (!editor())
# endif // #ifdef INGAME_EDITOR
			ShowCursor(FALSE);
			if (m_hWnd)
			{
				RECT winRect;
				GetClientRect(m_hWnd, &winRect);
				MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
				ClipCursor(&winRect);
			}
#endif // #ifndef DEDICATED_SERVER
		}
		else
		{
			app_inactive_time_start = TimerMM.GetElapsed_ms();
			Device.seqAppDeactivate.Process(rp_AppDeactivate);
			ShowCursor(TRUE);
			ClipCursor(NULL);
		}
	}
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt)
{
	PROF_EVENT(PROF_CAT(GameLogic));

	if (mt)
		seqFrameMT.Add(f, REG_PRIORITY_HIGH);
	else
		seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f)
{
	PROF_EVENT(PROF_CAT(GameLogic));

	seqFrameMT.Remove(f);
	seqFrame.Remove(f);
}

CLoadScreenRenderer::CLoadScreenRenderer()
	: b_registered(false)
{
}

void CLoadScreenRenderer::start(bool b_user_input)
{
	PROF_EVENT(PROF_CAT(UI));

	Device.seqRender.Add(this, 0);
	b_registered = true;
	b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop()
{
	PROF_EVENT(PROF_CAT(UI));

	if (!b_registered)
		return;
	Device.seqRender.Remove(this);
	pApp->destroy_loading_shaders();
	b_registered = false;
	b_need_user_input = false;
}

void CLoadScreenRenderer::OnRender()
{
	PROF_EVENT(PROF_CAT(UI));
	RENDER_SCOPE(load_screen);

	pApp->load_draw_internal();
}

void CSecondVPParams::SetSVPActive(bool bState) //--#SM+#-- +SecondVP+
{
	const bool was_active = isActive.load(std::memory_order_acquire);
	if (scope_svp_enabled < 2)
	{
		if (bState != was_active)
		{
			if (!bState)
				isActive.store(false, std::memory_order_release);
			m_svp_session.fetch_add(1, std::memory_order_acq_rel);
			InvalidateOpticConfig();
			if (bState)
			{
				ClearWeaponPose();
				ClearSight();
				dlss_reset_next = true;
				isActive.store(true, std::memory_order_release);
			}
		}
		if (!bState)
		{
			ClearWeaponPose();
			ClearSight();
		}
		if (g_pGamePersistent != NULL)
			g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = bState ? 1.0f : 0.0f;
		return;
	}
	if (bState != was_active)
	{
		if (!bState)
		{
			isActive.store(false, std::memory_order_release);
			m_svp_session.fetch_add(1, std::memory_order_acq_rel);
			InvalidateOpticConfig();
		}
		if (bState)
		{
			xrCriticalSectionGuard guard(m_snapshot_lock);
			m_weapon_pose = WeaponPoseSnapshot{};
			dlss_reset_next = true;
			isActive.store(true, std::memory_order_release);
		}
	}
	if (!bState)
	{
		xrCriticalSectionGuard guard(m_snapshot_lock);
		m_weapon_pose = WeaponPoseSnapshot{};
		m_sight = SightSnapshot{};
	}
	if (g_pGamePersistent != NULL)
		g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = bState ? 1.0f : 0.0f;
}
bool CSecondVPParams::IsSVPFrame()
{
	if (Device.true_pip_on)
		return m_render_pass_is_svp;
	return IsSVPActive() && Device.dwFrame % frameDelay == 0;
}
