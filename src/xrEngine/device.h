#pragma once

// Note:
// ZNear - always 0.0f
// ZFar - always 1.0f

//class ENGINE_API CResourceManager;
//class ENGINE_API CGammaControl;

#include <functional>
#include <atomic>

#include "pure.h"
#include "../xrCore/xrSyncronize.h"
//#include "hw.h"
#include "../xrcore/ftimer.h"
#include "stats.h"
//#include "shader.h"
//#include "R_Backend.h"

#include "../build_config_defines.h"

#define VIEWPORT_NEAR  Device.ViewportNear //0.2f
#define R_VIEWPORT_NEAR 0.005f

// Shared projection volume for 3D UI icons. X/Y placement and model fitting
// use the same height, so the value defines a coordinate system rather than
// the final on-screen size.
constexpr float UI_3D_ICON_ORTHO_HEIGHT = 2.f;
constexpr float UI_3D_ICON_NEAR_PLANE = 0.01f;
constexpr float UI_3D_ICON_FAR_PLANE = 100.f;
constexpr float UI_3D_ICON_DISTANCE = 10.f;

#define DEVICE_RESET_PRECACHE_FRAME_COUNT 10

// demonized: toggle bone optimization
//#define OPTIMIZE_CALCULATE_BONES

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/RenderDeviceRender.h"
#include "imgui_base.h"

#ifdef INGAME_EDITOR
# include "../Include/editor/interfaces.hpp"
#endif // #ifdef INGAME_EDITOR
#include "../Include/xrRender/Kinematics.h"

class engine_impl;

class IRenderDevice
{
public:
	virtual CStatsPhysics* _BCL StatPhysics() = 0;
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt) = 0;
	virtual void _BCL RemoveSeqFrame(pureFrame* f) = 0;
};

class ENGINE_API CRenderDeviceData
{
public:
	u32 dwWidth;
	u32 dwHeight;
	u32 clientWidth;
	u32 clientHeight;

	u32 dwPrecacheFrame;
	BOOL b_is_Ready;
	BOOL b_is_Active;
	BOOL b_hide_cursor;
public:

	// Engine flow-control (Updates once per Present)
	u32 dwFrame;

	u32 dwViewport = 0;
	bool true_pip_on = false;

	float fTimeDelta;
	float fTimeGlobal;
	u32 dwTimeDelta;
	u32 dwTimeGlobal;
	u32 dwTimeContinual;

	Fvector vCameraPosition;
	Fvector vCameraDirection;
	Fvector vCameraTop;
	Fvector vCameraRight;

	// demonized: Cam has fixed 83 fov, Hud is psHUD_FOV * 83.f
	Fmatrix mView;
	Fmatrix mViewHud;
	Fmatrix mViewCam;
	Fmatrix mProject;
	Fmatrix mProjectHud;
	Fmatrix mProjectCam;
	Fmatrix mFullTransform;
	Fmatrix mFullTransformHud;
	Fmatrix mFullTransformCam;
	Fmatrix mProjectCamAttached;
	Fmatrix mProject3DIcons;
	Fmatrix mFullTransformCamAttached;
	Fmatrix mFullTransform3DIcons;

	Fmatrix mView_prev;
	Fmatrix mViewHud_prev;
	Fmatrix mViewCam_prev;
	Fmatrix mProject_prev;
	Fmatrix mProjectHud_prev;
	Fmatrix mProjectCam_prev;
	Fmatrix mFullTransform_prev;
	Fmatrix mFullTransformHud_prev;
	Fmatrix mFullTransformCam_prev;

	Fvector4 wind_anim_prev;
	Fvector4 wind_anim_saved;

	// Copies of corresponding members. Used for synchronization.
	Fvector vCameraPosition_saved;

	Fmatrix mView_saved;
	Fmatrix mProject_saved;
	Fmatrix mFullTransform_saved;

	// Dedicated camera snapshot for CalculateBonesThread so same-frame SVP SetActive flips cannot race with it.
	Fvector vCameraPosition_bones;
	Fmatrix mFullTransform_bones;

	float fFOV;
	float fASPECT;
	float ViewportNear = 0.2f;

	// Per-viewport camera data: [0] = main, [1] = SVP
	struct MatrixData
	{
		Fmatrix mView;
		Fmatrix mProject;
		Fmatrix mProjectHud;
	};
	MatrixData matrices[2];
	MatrixData matrices_previous[2];
protected:

	u32 Timer_MM_Delta;
	CTimer_paused Timer;
	CTimer_paused TimerGlobal;

	//AVO: 
	CTimer frame_timer; //TODO: ïðîâåðèòü, íå äóáëèðóåòñÿ-ëè ñõîæèé òàéìåð (alpet)
	//-AVO

public:

	// Registrators
	CRegistrator<pureRender> seqRender;
	CRegistrator<pureAppActivate> seqAppActivate;
	CRegistrator<pureAppDeactivate> seqAppDeactivate;
	CRegistrator<pureAppStart> seqAppStart;
	CRegistrator<pureAppEnd> seqAppEnd;
	CRegistrator<pureFrame> seqFrame;
	CRegistrator<pureScreenResolutionChanged> seqResolutionChanged;

	HWND m_hWnd;
	// CStats* Statistic;
};

class ENGINE_API CRenderDeviceBase :
	public IRenderDevice,
	public CRenderDeviceData
{
public:
};

// refs
#include "svp_state.h" // pip the SVP cross-thread data bus
#include "RenderThread.h"
class ENGINE_API CRenderDevice : public CRenderDeviceBase
{
public:
	using CSecondVPParams = ::CSecondVPParams;
	
private:
	// Main objects used for creating and rendering the 3D scene
	u32 m_dwWindowStyle;
	RECT m_rcWindowBounds;
	RECT m_rcWindowClient;

	//u32 Timer_MM_Delta;
	//CTimer_paused Timer;
	//CTimer_paused TimerGlobal;
	CTimer TimerMM;

	void _Create(LPCSTR shName);
	void _Destroy(BOOL bKeepTextures);
	void _SetupStates();
public:
	// HWND m_hWnd;
	LRESULT MsgProc(HWND, UINT, WPARAM, LPARAM);

	// u32 dwFrame;
	// u32 dwPrecacheFrame;
	u32 dwPrecacheTotal;

	// u32 dwWidth, dwHeight;
	float fWidth_2, fHeight_2;
	// BOOL b_is_Ready;
	// BOOL b_is_Active;
	void OnWM_Activate(WPARAM wParam, LPARAM lParam);
public:
	//ref_shader m_WireShader;
	//ref_shader m_SelectionShader;

	IRenderDeviceRender* m_pRender;

	BOOL m_bNearer;

	void SetNearer(BOOL enabled)
	{
		if (enabled && !m_bNearer)
		{
			m_bNearer = TRUE;
			mProject._43 -= EPS_L;
		}
		else if (!enabled && m_bNearer)
		{
			m_bNearer = FALSE;
			mProject._43 += EPS_L;
		}
		m_pRender->SetCacheXform(mView, mProject);
		//R_ASSERT(0);
		// TODO: re-implement set projection
		//RCache.set_xform_project (mProject);
	}

	void DumpResourcesMemoryUsage() { m_pRender->ResourcesDumpMemoryUsage(); }
	void prepare_matrices();
public:
	// Registrators
	//CRegistrator <pureRender > seqRender;
	// CRegistrator <pureAppActivate > seqAppActivate;
	// CRegistrator <pureAppDeactivate > seqAppDeactivate;
	// CRegistrator <pureAppStart > seqAppStart;
	// CRegistrator <pureAppEnd > seqAppEnd;
	//CRegistrator <pureFrame > seqFrame;
	CRegistrator<pureFrame> seqFrameMT;
	CRegistrator<pureDeviceReset> seqDeviceReset;
	xr_vector<xr_delegate<void()>> seqParallel;

	// ForserX: Pre-Render sequence
	xr_vector<xr_delegate<void()>> seqParallelRender;
	xr_vector<xr_delegate<void()>> seqParallelBeforRender;

	// seqParallelBeforRender is filled from the particle worker thread
	// (CParticleGroup::SItem::OnFrame) and drained on the main thread in
	// CRenderDevice::on_idle(). Guard every access with this section.
	// Same pattern as CEventAPI::CS and CEffect_Rain::rainCS.
	xrCriticalSection seqParallelBeforRenderCS;

	xr_delegate<void()> ParticleWorkerCallback;
	xr_delegate<void()> ModelDefferClear;

	bool isRendering;

	// CPU stamps of one frame for the perf hub, written only while frame_record_on
	// Each field has one writer, and the hub reads them after the join
	struct FrameRecord
	{
		enum { task_pre_render, task_parallel_render, task_bones, task_game, task_count };

		u32 frame;
		u64 start;
		u64 cap[2];
		u64 present[2];
		u64 join[2];
		u64 task[task_count][2];
		// Ticks the render thread spun on GPU readbacks, none block in this renderer
		u64 wait;
		bool presented;
	};
	FrameRecord frame_record;
	bool frame_record_on;

	// LuaGC
	int LuaGCCount;
	bool LuaGCDone;
    xr_delegate<int()> LuaGC;
    xr_delegate<void()> LuaGCDebug;

	// Dependent classes
	//CResourceManager* Resources;

	CStats* Statistic;

	// Engine flow-control
	//float fTimeDelta;
	//float fTimeGlobal;
	//u32 dwTimeDelta;
	//u32 dwTimeGlobal;
	//u32 dwTimeContinual;

	// Cameras & projection
	//Fvector vCameraPosition;
	//Fvector vCameraDirection;
	//Fvector vCameraTop;
	//Fvector vCameraRight;

	//Fmatrix mView;
	//Fmatrix mProject;
	//Fmatrix mFullTransform;

	Fmatrix mInvView;
	Fmatrix mInvProject;
	Fmatrix mInvProjectHud;
	Fmatrix mInvFullTransform;

	CSecondVPParams m_SecondViewport;	//--#SM+#-- +SecondVP+

	// SVP target is square, half the main height per side
	u32 svp_width() { return svp_height(); }
	u32 svp_height() { return dwHeight >> 1; }

	//float fFOV;
	//float fASPECT;

	CRenderDevice()
		:
		m_pRender(0)
#ifdef INGAME_EDITOR
        , m_editor_module(0),
        m_editor_initialize(0),
        m_editor_finalize(0),
        m_editor(0),
        m_engine(0)
#endif // #ifdef INGAME_EDITOR
#ifdef PROFILE_CRITICAL_SECTIONS
        
#endif // #ifdef PROFILE_CRITICAL_SECTIONS
	{
		m_hWnd = NULL;
		b_is_Active = FALSE;
		b_is_Ready = FALSE;
		b_hide_cursor = FALSE;
		Timer.Start();
		m_bNearer = FALSE;
		frame_record_on = false;
		
		m_SecondViewport.SetSVPActive(false);
	};

	void Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason);
	bool Paused();

	// Scene control
	void PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input);
	BOOL Begin();
	void Clear();
	void End();
	void FrameMove();

	void overdrawBegin();
	void overdrawEnd();

	//Console Screenshot
	void Screenshot();

	// Mode control
	void DumpFlags();
	IC CTimer_paused* GetTimerGlobal() { return &TimerGlobal; }
	u32 TimerAsync() { return TimerGlobal.GetElapsed_ms(); }
	u32 TimerAsync_MMT() { return TimerMM.GetElapsed_ms() + Timer_MM_Delta; }

	// Creation & Destroying
	void ConnectToRender();
	void Create(void);
	void Run(void);
	void Destroy(void);
	void Reset(bool precache = true);

	bool ChangeOutputMonitor(HMONITOR hTargetMon);

	void Initialize(void);
	void ShutDown(void);

public:
	void time_factor(const float& time_factor)
	{
		Timer.time_factor(time_factor);
		TimerGlobal.time_factor(time_factor);
	}

	IC const float& time_factor() const
	{
		VERIFY(Timer.time_factor() == TimerGlobal.time_factor());
		return (Timer.time_factor());
	}

	Fvector& hud_to_world(Fvector& v, const Fmatrix& p)
	{
		mView.transform_tiny(v);
		p.transform_tiny(v);

		v.z -= ViewportNear;

		mInvProject.transform_tiny(v);
		mInvView.transform_tiny(v);

		return v;
	}

	Fvector& hud_to_world(Fvector& v)
	{
		return hud_to_world(v, mProjectHud);
	}

	Fvector& hud_to_world_dir(Fvector& v, const Fmatrix& p)
	{
		mView.transform_dir(v);
		p.transform_dir(v);

		mInvProject.transform_dir(v);
		mInvView.transform_dir(v);

		return v;
	}

	Fvector& hud_to_world_dir(Fvector& v)
	{
		return hud_to_world_dir(v, mProjectHud);
	}

	Fmatrix& hud_to_world(Fmatrix& m, const Fmatrix& p)
	{
		hud_to_world(m.c, p);
		hud_to_world_dir(m.i, p).normalize();
		hud_to_world_dir(m.j, p).normalize();
		hud_to_world_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& hud_to_world(Fmatrix& m)
	{
		return hud_to_world(m, mProjectHud);
	}

	Fmatrix& cam_to_world(Fmatrix& m)
	{
		return hud_to_world(m, mProjectCamAttached);
	}

	Fvector& hud_to_world(Fvector& v, const Fmatrix& p, const Fmatrix& view, const Fmatrix& inv_view, const Fmatrix& inv_proj)
	{
		view.transform_tiny(v);
		p.transform_tiny(v);
		v.z -= ViewportNear;
		inv_proj.transform_tiny(v);
		inv_view.transform_tiny(v);
		return v;
	}

	Fvector& hud_to_world_dir(Fvector& v, const Fmatrix& p, const Fmatrix& view, const Fmatrix& inv_view, const Fmatrix& inv_proj)
	{
		view.transform_dir(v);
		p.transform_dir(v);
		inv_proj.transform_dir(v);
		inv_view.transform_dir(v);
		return v;
	}

	Fmatrix& hud_to_world(Fmatrix& m, const Fmatrix& p, const Fmatrix& view, const Fmatrix& inv_view, const Fmatrix& inv_proj)
	{
		hud_to_world(m.c, p, view, inv_view, inv_proj);
		hud_to_world_dir(m.i, p, view, inv_view, inv_proj).normalize();
		hud_to_world_dir(m.j, p, view, inv_view, inv_proj).normalize();
		hud_to_world_dir(m.k, p, view, inv_view, inv_proj).normalize();
		return m;
	}
	Fvector& world_to_hud(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_tiny(v);
		mInvProject.transform_tiny(v);

		v.z += ViewportNear;

		p.transform_tiny(v);
		mView.transform_tiny(v);
		return v;
	}

	Fvector& world_to_hud(Fvector& v)
	{
		return world_to_hud(v, mProjectHud);
	}

	Fvector& world_to_hud_dir(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_dir(v);
		mInvProject.transform_dir(v);

		p.transform_dir(v);
		mView.transform_dir(v);

		return v;
	}

	Fvector& world_to_hud_dir(Fvector& v)
	{
		return world_to_hud_dir(v, mProjectHud);
	}

	Fmatrix& world_to_hud(Fmatrix& m, const Fmatrix& p)
	{
		world_to_hud(m.c, p);
		world_to_hud_dir(m.i, p).normalize();
		world_to_hud_dir(m.j, p).normalize();
		world_to_hud_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& world_to_hud(Fmatrix& m)
	{
		return world_to_hud(m, mProjectHud);
	}

	// Multi-threading
	xr_task_group secondary_tasks;
	CRenderThread m_render_thread;

	// Frozen environment & shader-constant snapshot published to the Render Thread at each frame sync barrier.
	struct SRenderEnvSnapshot
	{
		bool valid = false;
		float fog_near = 0.f;
		float fog_far = 100.f;
		float fog_density = 0.f;
		Fvector3 fog_color{ 0.f, 0.f, 0.f };
		Fvector3 sky_color{ 1.f, 1.f, 1.f };
		float sky_rotation = 0.f;
		Fvector4 clouds_color{ 1.f, 1.f, 1.f, 1.f };
		float weight = 0.f;
		Fvector3 ambient{ 0.f, 0.f, 0.f };
		Fvector4 hemi_color{ 0.f, 0.f, 0.f, 0.f };
		Fvector3 sun_color{ 0.f, 0.f, 0.f };
		Fvector3 sun_dir{ 0.f, -1.f, 0.f };
		float far_plane = 350.f;
		float rain_density = 0.f;
		Fvector3 rain_color{ 1.f, 1.f, 1.f };
		float wetness_factor = 0.f;
		float game_time = 0.f;
		float wind_direction = 0.f;
		float wind_velocity = 0.f;
		float wind_strength_factor = 0.f;
		float tree_amplitude_intensity = 0.f;
		float water_intensity = 0.f;
		Fvector4 wind_anim{ 0.f, 0.f, 0.f, 0.f };
		float bloom_threshold = 3.5f;
		float bloom_exposure = 3.0f;
		float bloom_sky_intensity = 0.6f;

		Fvector4 hud_params{ 0.f, 0.f, 0.f, 0.f };
		Fvector4 hud_fov_params{ 0.f, 0.f, 0.f, 0.f };
		Fmatrix script_params{ Fidentity };
		Fvector4 blender_mode{ 0.f, 0.f, 0.f, 0.f };

		float actor_health = 1.f;
		float actor_stamina = 1.f;
		float actor_bleeding = 0.f;
		int actor_helmet = 0;

		float pda_display_factor = 0.f;
		float pda_psy_influence = 0.f;
		float pda_display_brightness = 1.f;

		Fvector4 grass_pos[16]{};
		Fvector4 grass_dir[16]{};
		Fvector4 grass_prev_pos[16]{};
		Fvector4 grass_prev_dir[16]{};
		float grass_str[16]{};
		float grass_radius_curr[16]{};
	};
	SRenderEnvSnapshot m_rt_env;

	// Frozen camera/viewport/timing state owned by the Render Thread during Frame N
	// while Main Thread advances Frame N+1 independently.
	struct SRenderViewSnapshot
	{
		u32 dwWidth = 0;
		u32 dwHeight = 0;
		float fWidth_2 = 0.f;
		float fHeight_2 = 0.f;
		float fFOV = 90.f;
		float fASPECT = 1.f;
		u32 dwFrame = 0;
		u32 dwViewport = 0;
		float fTimeGlobal = 0.f;
		float fTimeDelta = 0.f;
		u32 dwTimeGlobal = 0;

		Fvector vCameraPosition{};
		Fvector vCameraDirection{};
		Fvector vCameraTop{};
		Fvector vCameraRight{};
		Fvector vCameraPosition_saved{};

		Fmatrix mView{};
		Fmatrix mProject{};
		Fmatrix mProjectHud{};
		Fmatrix mFullTransform{};
		Fmatrix mFullTransformHud{};
		Fmatrix mInvView{};
		Fmatrix mInvProject{};
		Fmatrix mInvProjectHud{};
		Fmatrix mInvFullTransform{};
		Fmatrix mView_saved{};
		Fmatrix mProject_saved{};
		Fmatrix mFullTransform_saved{};

		Fmatrix mView_prev{};
		Fmatrix mProject_prev{};
		Fmatrix mFullTransform_prev{};
		Fmatrix mViewHud{};
		Fmatrix mViewHud_prev{};
		Fmatrix mProjectHud_prev{};
		Fmatrix mFullTransformHud_prev{};
		Fmatrix mViewCam{};
		Fmatrix mProjectCam{};
		Fmatrix mFullTransformCam{};
		Fmatrix mViewCam_prev{};
		Fmatrix mProjectCam_prev{};
		Fmatrix mFullTransformCam_prev{};
		Fmatrix mProjectCamAttached{};
		Fmatrix mFullTransformCamAttached{};
		Fmatrix mProject3DIcons{};
		Fmatrix mFullTransform3DIcons{};

		MatrixData matrices[2]{};
		MatrixData matrices_previous[2]{};
		Fvector4 wind_anim_prev{};
		Fvector4 wind_anim_saved{};
	};
	SRenderViewSnapshot m_rt_view;

	void SnapshotRenderFrame();
	void FlushRenderThread() { m_render_thread.Wait(); }
	bool IsDecoupledRenderActive() const noexcept { return ps_r__mt_decouple != 0 && m_render_thread.IsRunning(); }
	bool IsDecoupledRenderBusy() const noexcept { return m_render_thread.IsBusy(); }
	bool m_decoupled_ui_recording = false;
	bool m_decoupled_pda_pending = false;
	std::atomic<bool> m_rt_needs_reset{ false };

	ICF void remove_from_seq_parallel(const xr_delegate<void()>& delegate)
	{
		xr_vector<xr_delegate<void()>>::iterator I = std::find(
			seqParallel.begin(),
			seqParallel.end(),
			delegate
		);
		if (I != seqParallel.end())
			seqParallel.erase(I);
	}

	//AVO: elapsed famed counter (by alpet)
	IC u32 frame_elapsed()
	{
		return frame_timer.GetElapsed_ms();
	}

	// demonized: Perceivable distance depending on FOV, so that objects will behave normal in binoculars
	IC float GetPerceivedDist(const Fvector& p, float* real_dist = nullptr)
	{
		float dist = vCameraPosition.distance_to(p);
		float fov_rad = deg2rad(fFOV);
		float perceived_dist = dist * tanf(fov_rad * 0.5f);
		if (real_dist) *real_dist = dist;
		return perceived_dist;
	}

	IC float CalcSSADynamic(const Fvector& C, float R)
	{
		Fvector4 v_res1, v_res2;
		mFullTransform.transform(v_res1, C);
		mFullTransform.transform(v_res2, Fvector(C).mad(vCameraRight, R));
		return v_res1.sub(v_res2).magnitude();
	}

public:
	void xr_stdcall on_idle();
	bool xr_stdcall on_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	void message_loop();
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt);
	virtual void _BCL RemoveSeqFrame(pureFrame* f);
	virtual CStatsPhysics* _BCL StatPhysics() { return Statistic; }

private:
	xr_imgui::ide m_imgui;

public:
	xr_imgui::ide& imgui() { return m_imgui; }
	bool imgui_shown() const { return m_imgui.is_shown(); }
#ifdef INGAME_EDITOR
public:
    IC editor::ide* editor() const { return m_editor; }

private:
    void initialize_editor();
    void message_loop_editor();

private:
    typedef editor::initialize_function_ptr initialize_function_ptr;
    typedef editor::finalize_function_ptr finalize_function_ptr;

private:
    HMODULE m_editor_module;
    initialize_function_ptr m_editor_initialize;
    finalize_function_ptr m_editor_finalize;
    editor::ide* m_editor;
    engine_impl* m_engine;
#endif // #ifdef INGAME_EDITOR
};

extern ENGINE_API CRenderDevice Device;
extern ENGINE_API CRenderDevice* DevicePtr;

// pip clear a draw-list shared across the main + SVP passes only on the last pass to drain it
// svp_first = SVP pass drains before main (combine order), false = main first (gbuffer/wmark order)
inline bool svp_clear_shared_list(bool svp_first)
{
	if (!(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive())) return true;
	return svp_first ? !Device.m_SecondViewport.m_render_pass_is_svp : Device.m_SecondViewport.m_render_pass_is_svp;
}

#ifndef _EDITOR
#define RDEVICE Device
#else
#define RDEVICE EDevice
#endif

#ifdef ECO_RENDER
extern ENGINE_API float refresh_rate;
#endif // ECO_RENDER

extern ENGINE_API bool g_bBenchmark;

typedef xr_delegate<bool()> LOADING_EVENT;
extern ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

class ENGINE_API CLoadScreenRenderer : public pureRender
{
public:
	CLoadScreenRenderer();
	void start(bool b_user_input);
	void stop();
	virtual void OnRender();

	bool b_registered;
	bool b_need_user_input;

	bool IsActive() const {
		return b_registered;
	}
};

extern ENGINE_API CLoadScreenRenderer load_screen_renderer;
