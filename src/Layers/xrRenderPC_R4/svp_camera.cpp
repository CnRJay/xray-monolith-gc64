#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip DRender, the debug-line backend for the scope_debug world overlay
#include "../../Include/xrRender/DebugRender.h" // pip IDebugRender::add_lines
#include "../xrRender/SkeletonX.h"              // pip CSkeletonX for the skinned lens bone transform
#include "../../xrEngine/svp_crash_context.h"   // pip svp state ring for tester crash reports
#include "../../xrEngine/xr_ioconsole.h"         // pip Console registry for the [SVP-CFG] fingerprint
#include "../../xrEngine/xr_ioc_cmd.h"           // pip IConsole_Command Name/Status/TStatus
#include "../../xrEngine/svp_gameplay_cvars.h"
#include "svp_camera.h"
#include "svp_optics.h"
#include "svp_physical_optics.h"

static bool svp_rigid_camera_basis(Fmatrix& camera, const Fmatrix& main_eye,
	const Fvector& eyepiece, const Fvector& objective,
	const char*& forward_lane, const char*& basis_lane, bool& flipped)
{
	const Fvector raw_right = camera.i;
	Fvector forward = camera.k;
	bool main_fallback = false;
	forward_lane = "camera";

	if (!_valid(camera.c))
	{
		camera = main_eye;
		forward = camera.k;
		forward_lane = "main-eye";
		main_fallback = true;
	}
	else if (!_valid(forward) || forward.square_magnitude() <= EPS_S)
	{
		forward.sub(objective, eyepiece);
		if (_valid(forward) && forward.square_magnitude() > EPS_S)
			forward_lane = "optic-axis";
		else
		{
			camera = main_eye;
			forward = camera.k;
			forward_lane = "main-eye";
			main_fallback = true;
		}
	}
	forward.normalize_safe();

	Fvector projection;
	projection.set(forward);
	projection.mul(camera.j.dotproduct(forward));
	Fvector up;
	up.sub(camera.j, projection);
	Fvector right;
	if (_valid(up) && up.square_magnitude() > EPS_S)
	{
		up.normalize_safe();
		right.crossproduct(up, forward);
		right.normalize_safe();
		up.crossproduct(forward, right);
		up.normalize_safe();
		basis_lane = "up";
	}
	else
	{
		projection.set(forward);
		projection.mul(camera.i.dotproduct(forward));
		right.sub(camera.i, projection);
		if (_valid(right) && right.square_magnitude() > EPS_S)
		{
			right.normalize_safe();
			up.crossproduct(forward, right);
			up.normalize_safe();
			basis_lane = "right";
		}
		else
		{
			Fvector seed;
			_abs(forward.y) < 0.9f ? seed.set(0.f, 1.f, 0.f) : seed.set(1.f, 0.f, 0.f);
			right.crossproduct(seed, forward);
			right.normalize_safe();
			up.crossproduct(forward, right);
			up.normalize_safe();
			basis_lane = "world";
		}
	}

	Fvector raw_right_unit = raw_right;
	raw_right_unit.normalize_safe();
	flipped = _valid(raw_right_unit) && raw_right_unit.square_magnitude() > EPS_S
		&& raw_right_unit.dotproduct(right) < 0.f;
	const Fvector position = camera.c;
	camera.set(right, up, forward, position);
	return main_fallback;
}

// pip scope_debug >= 2 world overlay, eyepiece (blue) objective (yellow) camera (white)
// via DRender->add_lines, flushed by the stock debug render
void debug_scope(Fmatrix scope_camera, const Fmatrix& projection)
{
	auto dbg_line = [](const Fvector& a, const Fvector& b, u32 color, bool bHud) {
		Fvector v[2] = { a, b };
		u16 idx[2] = { 0, 1 };
		DRender->add_lines(v, 2, idx, 1, color, bHud);
	};

	auto draw_circle = [&](Fmatrix m, u32 color, bool bHud) {
		const int n = 100;
		Fvector v0 = { 0, 0, 0 };
		for (int i = 0; i <= n; i++) {
			float angle = float(i) / float(n) * PI * 2.0f;
			Fvector v1 = { cosf(angle), sinf(angle), 0.f };
			m.transform(v1);
			if (i > 0) dbg_line(v0, v1, color, bHud);
			v0 = v1;
		}
	};

	auto draw_lens = [&](CRenderDevice::CSecondVPParams::Lens lens, u32 color) {
		draw_circle(Fmatrix(lens.m_W).mulB_43(Fmatrix().scale(lens.radius, lens.radius, 0.f)), color, true);
		Fvector v0 = { 0, 0, 0 }, v1 = { 0, 0, 100 };
		lens.m_W.transform(v0);
		lens.m_W.transform(v1);
		dbg_line(v0, v1, color, true);
		// up-spoke, a circle + optical axis can't show a ROLL about the axis (rotationally symmetric),
		// so draw the lens up-vector as a radial spoke, a rolled lens shows the spoke pointing off-up
		Fvector u0 = { 0, 0, 0 }, u1 = { 0, lens.radius, 0 };
		lens.m_W.transform(u0);
		lens.m_W.transform(u1);
		dbg_line(u0, u1, color, true);
	};

	auto draw_camera = [&](u32 color) {
		const float cm = 1.0f / 100.0f;
		draw_circle(Fmatrix(scope_camera).mulB_43(Fmatrix().scale(.25f * cm, .25f * cm, 0.f)), color, true);
	};

	// pip wireframe cube at a transform (orientation + position), half-extent h, 12 edges
	auto draw_cube = [&](const Fmatrix& m, float h, u32 color) {
		Fvector c[8];
		for (int i = 0; i < 8; i++) {
			Fvector q; q.set((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
			m.transform_tiny(c[i], q);
		}
		static const int e[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
		for (int k = 0; k < 12; k++)
			dbg_line(c[e[k][0]], c[e[k][1]], color, true);
	};

	auto& p = Device.m_SecondViewport;
	// a culled weapon leaves the live radii 0 this frame, fall back to the held debug radii
	CRenderDevice::CSecondVPParams::Lens eye = p.eyepiece;
	if (eye.radius <= EPS)
		eye.radius = p.dbg_eyepiece_r;
	draw_lens(eye, 0xff0000ff);          // eyepiece blue
	// objective yellow at the CAMERA (the real entrance the scope views from), the stored p.objective.m_W
	// is a forward math intermediate (it derives the camera pull-back d), not the visible front lens
	CRenderDevice::CSecondVPParams::Lens objAtCam = p.objective;
	if (objAtCam.radius <= EPS)
		objAtCam.radius = p.dbg_objective_r;
	objAtCam.m_W = scope_camera;
	draw_lens(objAtCam, 0xffffff00);     // objective yellow (at the camera/entrance)
	// orange disc at the true derived objective, its gap from the yellow camera disc shows the pull-back
	CRenderDevice::CSecondVPParams::Lens objTrue = p.objective;
	if (objTrue.radius <= EPS)
		objTrue.radius = p.dbg_objective_r;
	draw_circle(Fmatrix(objTrue.m_W).mulB_43(Fmatrix().scale(objTrue.radius, objTrue.radius, 0.f)), 0xffff8000, true);
	draw_camera(0xffffffff);             // scope cam white
	// Magenta marks the live SVP camera
	// It shows main eye fallback versus the objective at a glance
	draw_cube(scope_camera, eye.radius * 0.6f, 0xffff00ff);

	// green frustum follows the final projection
	if (_abs(projection._11) > EPS && _abs(projection._22) > EPS)
	{
		const float d = 30.f;
		Fvector corner[4];
		for (int i = 0; i < 4; i++)
		{
			const float sx = (i == 0 || i == 3) ? -1.f : 1.f;
			const float sy = (i < 2) ? 1.f : -1.f;
			const float vx = (sx - projection._31) / projection._11;
			const float vy = (sy - projection._32) / projection._22;
			corner[i].mad(scope_camera.c, scope_camera.k, d);
			corner[i].mad(scope_camera.i, vx * d);
			corner[i].mad(scope_camera.j, vy * d);
			dbg_line(scope_camera.c, corner[i], 0xff00ff00, true);
		}
		for (int i = 0; i < 4; i++)
			dbg_line(corner[i], corner[(i + 1) & 3], 0xff00ff00, true);
	}

	// cyan front plane disc at the objective distance
	{
		float vfov, aspect, near_plane, far_plane;
		Fmatrix projection_copy = projection;
		projection_copy.decompose_projection(vfov, aspect, near_plane, far_plane);
		Fvector ax; ax.sub(p.objective.m_W.c, p.eyepiece.m_W.c);
		const float tube = ax.magnitude();
		if (tube > EPS)
		{
			ax.div(tube);
			const float front = (p.svp_front_use_m > EPS) ? p.svp_front_use_m : tube;
			Fmatrix fm; fm.identity();
			fm.k.set(ax);
			Fvector seed = (_abs(ax.y) < 0.9f) ? Fvector{0, 1, 0} : Fvector{1, 0, 0};
			fm.i.crossproduct(seed, ax); fm.i.normalize();
			fm.j.crossproduct(ax, fm.i);
			fm.c.mad(p.eyepiece.m_W.c, ax, front);
			const float fr = std::max(eye.radius, objAtCam.radius) * 1.75f;
			draw_circle(Fmatrix(fm).mulB_43(Fmatrix().scale(fr, fr, 0.f)), 0xff00ffff, true);
			extern int ps_r__svp_cop_diag;
			static u32 s_cam_ms = 0;
			if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_cam_ms > 1000)
			{
				s_cam_ms = Device.dwTimeGlobal;
				Fvector ec; ec.sub(scope_camera.c, p.eyepiece.m_W.c);
				extern Fvector4 ps_s3ds_param_3;
				PipMsg("[SVP-CAM] pos=(%.2f,%.2f,%.2f) fwd=(%.2f,%.2f,%.2f) vfov=%.2fdeg cam2eye=%.1fcm tube=%.1fcm frontplane=%.1fcm it=%.0f",
					scope_camera.c.x, scope_camera.c.y, scope_camera.c.z,
					scope_camera.k.x, scope_camera.k.y, scope_camera.k.z,
					rad2deg(vfov), ec.magnitude() * 100.f, tube * 100.f, front * 100.f,
					ps_s3ds_param_3.x);
			}
		}
	}
}

// pip STUB sub-pixel jitter for the SVP scene projection (DLSS scaffolding), swap for Ascii's
// shared helper. Halton(2,3), 16-sample phase, returns a centered offset in [-0.5,0.5] px
static float svp_halton(u32 i, u32 b)
{
	float f = 1.0f, r = 0.0f;
	while (i > 0) { f /= (float)b; r += f * (float)(i % b); i /= b; }
	return r;
}
static Fvector2 svp_jitter_offset(u32 frame)
{
	const u32 phase = 16; // TODO match Ascii's confirmed sequence length
	u32 i = (frame % phase) + 1; // Halton is 1-based
	Fvector2 o;
	o.set(svp_halton(i, 2) - 0.5f, svp_halton(i, 3) - 0.5f);
	return o;
}
// apply a pixel jitter to a projection by shifting the post-perspective NDC center, the axis/sign/
// remap convention lives here so the eval swap is one line
static void svp_apply_jitter(Fmatrix& proj, Fvector2 px, float w, float h)
{
	proj.m[2][0] += 2.0f * px.x / w;  // NDC x, clip.x gains z * this, the w-divide cancels z
	proj.m[2][1] -= 2.0f * px.y / h;  // NDC y, negated for texture-down
}

// pip [3DB] ballistics overlay core, has_sight draws the captured red sight line and gaps the fire
// axis against it (else the camera axis), draw_lens adds the eyepiece/objective markers + zeroed ray
static void svp_3db_overlay(float fNearPlane, bool has_sight, const Fvector& sight_org, const Fvector& sight_axis,
	bool draw_lens, const Fvector& eyepiece_pos, bool has_objective, const Fvector& objective_pos)
{
	extern int ps_r__3db_debug;
	auto& vp = Device.m_SecondViewport;
	CSecondVPParams::WeaponPoseSnapshot pose;
	const bool pose_valid = vp.ReadWeaponPose(pose)
		&& vp.SnapshotExact(pose.frame, pose.session, Device.dwFrame);
	// display distance, the ranged zero re-picks every tick and would teleport every
	// endpoint while panning, settle it for drawing, the log keeps the live number
	const float D_live = (pose.fire_ray_zero > 0.f) ? pose.fire_ray_zero : 100.f;
	static float s_disp_D = 0.f;
	static u32 s_disp_frame = 0;
	if (Device.dwFrame != s_disp_frame + 1 || s_disp_D <= 0.f)
		s_disp_D = D_live;
	else
		s_disp_D += (D_live - s_disp_D) * (1.f - expf(-Device.fTimeDelta / 0.25f));
	s_disp_frame = Device.dwFrame;
	const float D = s_disp_D;
	Fmatrix eyeW2; eyeW2.invert(Device.matrices[0].mView);
	// clip to the near plane, a behind-plane endpoint rasterizes as a screen streak
	const float znear = fNearPlane + 0.01f;
	auto line = [&](const Fvector& a, const Fvector& b, u32 color) {
		Fvector ca; ca.sub(a, eyeW2.c);
		Fvector cb; cb.sub(b, eyeW2.c);
		const float za = ca.dotproduct(eyeW2.k), zb = cb.dotproduct(eyeW2.k);
		if (za < znear && zb < znear)
			return;
		Fvector aa = a, bb = b;
		if (za < znear)
			aa.lerp(a, b, (znear - za) / (zb - za));
		else if (zb < znear)
			bb.lerp(a, b, (znear - za) / (zb - za));
		Fvector v[2] = { aa, bb }; u16 i[2] = { 0, 1 };
		DRender->add_lines(v, 2, i, 1, color, true);
	};
	auto cross = [&](const Fvector& p, float s, u32 color) {
		Fvector a, b;
		a.set(p); a.mad(eyeW2.i, -s); b.set(p); b.mad(eyeW2.i, s);
		line(a, b, color);
		a.set(p); a.mad(eyeW2.j, -s); b.set(p); b.mad(eyeW2.j, s);
		line(a, b, color);
	};

	// yellow muzzle marker, red eyepiece and cyan objective only with a captured lens
	if (pose_valid)
		cross(pose.muzzle_pos, 0.01f, 0xffffff00);
	if (draw_lens)
	{
		cross(eyepiece_pos, 0.01f, 0xffff0000);
		if (has_objective)
			cross(objective_pos, 0.01f, 0xff00ffff);
	}

	// the sight line to the zero distance, drawn from the stable published copy ballistics converge on
	if (has_sight)
	{
		Fvector sight; sight.mad(sight_org, sight_axis, D);
		line(sight_org, sight, 0xffff0000);
		cross(sight, D * 0.02f, 0xffff0000);
	}

	// the camera crosshair ray from the mirrored actor eye, the ballistic truth when aimpos
	// is off, stays the shooter's eye while demo_record flies the device camera
	Fvector cpos, cfwd;
	if (pose.eye_ray_dir.square_magnitude() > EPS)
	{
		cpos.set(pose.eye_ray_pos);
		cfwd.set(pose.eye_ray_dir);
	}
	else
	{
		cpos.set(eyeW2.c);
		cfwd.set(eyeW2.k);
	}
	cfwd.normalize_safe();
	Fvector chp; chp.mad(cpos, cfwd, D);
	line(cpos, chp, 0xff4080ff);
	cross(chp, D * 0.02f, 0xff4080ff);

	// the raw fire axis, where bullets go with g_svp_zero 0
	if (pose_valid)
	{
		Fvector faxis; faxis.set(pose.fire_ray_dir); faxis.normalize_safe();
		Fvector fire; fire.mad(pose.fire_ray_pos, faxis, D);
		line(pose.fire_ray_pos, fire, 0xff00ff00);
		cross(fire, D * 0.02f, 0xff00ff00);
		// the gap reads the fire axis against the sight line when captured, else the aim axis
		float c = has_sight ? sight_axis.dotproduct(faxis) : cfwd.dotproduct(faxis);
		clamp(c, -1.f, 1.f);
		static u32 s_aim_ms = 0;
		if (Device.dwTimeGlobal - s_aim_ms > 1000)
		{
			s_aim_ms = Device.dwTimeGlobal;
			if (has_sight)
				PipMsg("[3DB] axis gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, pose.fire_ray_zero);
			else
				PipMsg("[3DB] fire-aim gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, pose.fire_ray_zero);
		}

		// the zeroed departure ray the shot actually flies, only with a captured sight line
		if (has_sight && ps_r__3db_debug >= 2 && pose.fire_ray_zero > 0.f)
		{
			Fvector zp; zp.mad(sight_org, sight_axis, D);
			line(pose.fire_ray_pos, zp, 0xffffffff);
		}
	}

	// fading shot tracers, brightness decays over 5s
	if (ps_r__3db_debug >= 3)
	{
		CSecondVPParams::FireTrace traces[16];
		vp.ReadFireTraces(traces);
		for (const auto& tr : traces)
		{
			const u32 age = Device.dwTimeGlobal - tr.time_ms;
			if (!tr.time_ms || age >= 5000)
				continue;
			const u32 k = 255u - age * 255u / 5000u;
			const u32 color = 0xff000000 | (k << 16) | (k << 8);
			Fvector end; end.mad(tr.pos, tr.dir, D * 2.f);
			line(tr.pos, end, color);
		}
	}
}

// pip [3DB] overlay for sights without a captured pip lens, reflex and irons keep 3d
// ballistics so the sight independent markers draw here, the pip overlay owns the rest
void ballistics_debug_overlay()
{
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	const Fvector zero = { 0.f, 0.f, 0.f };
	svp_3db_overlay(fNearPlane, false, zero, zero, false, zero, false, zero);
}

// pip build the SVP camera (fills Device.matrices[1]) from the captured lens + the weapon
// zoom factor, called after the lens derives so TargetSVP->SetActive reads it ready
static LPCSTR svp_camera_domain_name(CSecondVPParams::ECameraDomain domain)
{
	switch (domain)
	{
	case CSecondVPParams::camera_main_eye:
		return "main-eye";
	case CSecondVPParams::camera_objective:
		return "objective";
	default:
		return "unknown";
	}
}

// objective near plane, r__svp_near above zero is a manual override, otherwise it tracks the nearest
// drawn weapon extent the drain published last frame, rising slowly and dropping at once
static float svp_auto_near(CSecondVPParams& vp, float cap, float& out_min, bool& out_manual,
	bool& out_fresh)
{
	extern float ps_r__svp_near;
	static float s_slew = R_VIEWPORT_NEAR;
	static u32 s_frame = u32(-1);
	static u32 s_epoch = u32(-1);
	static u32 s_session = 0;
	out_min = vp.svp_hud_min_axial; // raw published value, the log needs the sentinel verbatim
	out_fresh = false;
	out_manual = (ps_r__svp_near > 0.f);
	if (out_manual)
	{
		s_slew = R_VIEWPORT_NEAR;
		s_frame = Device.dwFrame;
		s_epoch = vp.svp_optic_epoch;
		return ps_r__svp_near;
	}
	if (!(cap > R_VIEWPORT_NEAR))
		return R_VIEWPORT_NEAR;
	// this runs once per svp camera build, which is not once per main frame, so the window spans
	// from the previous build rather than a fixed frame
	const u32 prev_build = s_frame;
	const bool gap = (s_frame == u32(-1)) || (Device.dwFrame < s_frame)
		|| (Device.dwFrame - s_frame > 8) || (s_session != vp.GetSVPSession());
	if (gap || s_epoch != vp.svp_optic_epoch)
		s_slew = R_VIEWPORT_NEAR;
	s_frame = Device.dwFrame;
	s_epoch = vp.svp_optic_epoch;
	s_session = vp.GetSVPSession();

	// a missed publish holds the last value, real transitions floor through the slew reset above
	float target = s_slew;
	const bool fresh = vp.svp_hud_min_frame != u32(-1) && prev_build != u32(-1) && !gap
		&& vp.svp_hud_min_frame >= prev_build
		&& vp.svp_hud_min_session == vp.GetSVPSession()
		&& vp.svp_hud_min_epoch == vp.svp_optic_epoch;
	out_fresh = fresh;
	if (!fresh)
	{
		extern int ps_r__svp_cop_diag;
		static u32 s_stale_ms = 0;
		if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_stale_ms > 5000)
		{
			s_stale_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-CAM] near hold, no fresh publish, r__svp_optic_body_suppress off kills the derive");
		}
	}
	if (fresh)
	{
		// half the measured clearance absorbs the one frame of lag, the sentinel means nothing
		// ahead so the cap is safe, and a zero reaches the plane so only the floor is
		target = (out_min < 0.f) ? cap
			: (out_min > 0.f ? 0.5f * out_min : (float)R_VIEWPORT_NEAR);
	}
	// an engaged hybrid publishes its reflex near extent, the near plane never clips the capture
	if (vp.svp_hybrid_front >= 0.f && vp.svp_hybrid_front_frame != u32(-1)
		&& prev_build != u32(-1) && !gap
		&& vp.svp_hybrid_front_frame >= prev_build
		&& vp.svp_hybrid_front_session == vp.GetSVPSession()
		&& vp.svp_hybrid_front_epoch == vp.svp_optic_epoch)
		target = _min(target, _max(0.5f * vp.svp_hybrid_front, (float)R_VIEWPORT_NEAR));
	clamp(target, (float)R_VIEWPORT_NEAR, cap);
	const float dt = Device.fTimeDelta;
	const float a = (dt > 0.f) ? (1.f - exp(-dt / 0.25f)) : 1.f;
	s_slew += a * (target - s_slew);
	s_slew = _min(target, s_slew); // rises on the constant, drops the frame the target does
	clamp(s_slew, (float)R_VIEWPORT_NEAR, cap);
	return s_slew;
}

bool svpCamera()
{
	// the published zoom is raise transient free, unset falls back to the shader constant
	const float zoom_src = (Device.m_SecondViewport.svp_zoom_pub > 1.f)
		? Device.m_SecondViewport.svp_zoom_pub
		: g_pGamePersistent->m_pGShaderConstants->hud_params.y;
	// the scale rides the live fov so the scope keeps its fov-75 look at any user fov
	float svp_fov = svp_factor_to_fov(zoom_src, Device.m_SecondViewport.svp_fov_scale);
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	// the mag reads the steady wide aim fov (punch free from the weapon publish), the live decomposed
	// fov keeps feeding the vFov/projection so the scope image tracks the actual main view
	const float aim_fov_pub = Device.m_SecondViewport.svp_aim_fov;
	const float fov_aim = (aim_fov_pub > 1.f) ? deg2rad(aim_fov_pub) : fov;

	// a zoom-0 tube sight (1x thermal/nv) has no zoom fov and re-images at 1x, the near-0 value
	// would also blow up the vFov/offset tan() math, the floor sits under the highest ladder mag
	const float floor_scale = _max(Device.m_SecondViewport.svp_fov_scale, 0.1f);
	const float fov_floor = 0.5f * SVP_ZOOM_BASE_FOV * floor_scale / SVP_MAG_LIMIT;
	if (svp_fov < fov_floor) svp_fov = rad2deg(fov_aim);


	auto mm = Device.matrices[0];
	auto& params = Device.m_SecondViewport;
	const u32 camera_session = params.GetSVPSession();
	extern float ps_s3ds_pupil_parity;
	const float pupil_parity = params.RenderOpticConfig().typed_route
		? params.RenderOpticConfig().pupil_parity : ps_s3ds_pupil_parity;

	// analytic eyepiece fit, a disc of radius r at view depth d projects to ndc height 2*r*_22/d
	// under mProjectHud, the fit is screen height 2 over that. depth on the view forward, exact off axis
	Fmatrix eyeW0; eyeW0.invert(mm.mView);
	Fvector camfwd; camfwd.set(eyeW0.k); camfwd.normalize();
	Fvector eyed; eyed.sub(params.eyepiece.m_W.c, eyeW0.c);
	const float lens_depth = eyed.dotproduct(camfwd);
	const float ndc_height = (lens_depth > EPS)
		? (2.f * params.eyepiece.radius * mm.mProjectHud._22 / lens_depth) : 0.f;
	const bool analytic_ok = params.eyepiece.radius > EPS
		&& lens_depth > EPS && _valid(ndc_height) && ndc_height > 1e-4f;
	const float ratio_analytic = analytic_ok ? (2.f / ndc_height) : 0.f;
	float ratio_magnification = 1.f;

	// the magnification of the scope (1X 4X etc)
	float scope_magnification = fov_aim / deg2rad(svp_fov);

	// flat screen optic (binocular), the panel is a see-through window so the svp fov is the
	// panel's angular subtense at the weapon zoom, magnification then tracks the stock look
	extern int ps_r__svp_flat_window;
	extern Fvector4 ps_s3ds_param_3;
	const bool flat_optic = ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8;
	const bool flat_window = flat_optic && Device.m_SecondViewport.svp_disc_px > 1.f;
	auto window_fov = [&](float zdeg) {
		const float p = _min(Device.m_SecondViewport.svp_disc_px / (float)Device.dwHeight, 1.5f);
		return 2.f * atanf(p * tanf(deg2rad(zdeg) * 0.5f));
	};
	if (flat_window)
	{
		// authored mag flat optics keep the clean optical mag, only the see-through panel takes the subtense ratio
		if (!params.svp_authored_mag)
			scope_magnification = fov_aim / window_fov(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 1.f
				? g_pGamePersistent->m_pGShaderConstants->hud_params.y : svp_fov);
		ratio_magnification = 1.f;
	}
	else
	{
		// coast the analytic fit through the raise and one pole it while settled, the raw lens pose
		// bobs but the along axis depth barely moves so an unsettled aim or alt sight holds the value
		const float aim_rot = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
		const bool alt_sight = Device.m_SecondViewport.svp_alt_sight;
		static float s_ratio = 0.f;
		static u32 s_ratio_frame = 0;
		static u32 s_ratio_epoch = 0;
		const bool gap = (Device.dwFrame != s_ratio_frame + 1);
		s_ratio_frame = Device.dwFrame;
		// subscribe to the optic epoch, a magnifier flip or scope swap bumps it with no frame gap so
		// the fit drops the old optic's value and reseeds at the next settle
		const bool epoch = (params.svp_optic_epoch != s_ratio_epoch);
		s_ratio_epoch = params.svp_optic_epoch;
		const bool can_track = (aim_rot > 0.999f) && !alt_sight;
		if (analytic_ok)
		{
			// a session gap or an optic swap drops the latch, the raise value is meaningless so the
			// seed waits for the first settled frame, then the one pole eases residual pose noise
			if (gap || epoch || !_valid(s_ratio) || s_ratio <= 0.f)
				s_ratio = can_track ? ratio_analytic : 0.f;
			else if (can_track)
				s_ratio += (ratio_analytic - s_ratio) * (1.f - expf(-Device.fTimeDelta / 0.15f));
		}
		ratio_magnification = (s_ratio > 0.f) ? s_ratio : ratio_analytic;
	}

	// magnification ceiling, the optic's authored config max zoom (hud_fov_params.x, the same source
	// as g_pip_scope_max_mag), held through section churn so a flip cannot push the mag past the optic
	{
		const Fvector4& fovp_c = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		const float fscale_c = rad2deg(fov_aim) / 75.f;
		const float cfg_max = (fovp_c.x > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp_c.x, fscale_c)) : 0.f;
		static float s_mag_ceiling = 0.f;
		static u32 s_ceiling_frame = 0;
		if (Device.dwFrame != s_ceiling_frame + 1) s_mag_ceiling = 0.f; // session gap drops the hold
		s_ceiling_frame = Device.dwFrame;
		const float rot_c = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
		if (cfg_max > EPS && rot_c > 0.999f) s_mag_ceiling = cfg_max; // latch the settled config max
		const float ceiling = (s_mag_ceiling > EPS) ? s_mag_ceiling : cfg_max;
		if (ceiling > EPS && scope_magnification > ceiling) scope_magnification = ceiling;
	}

	// pip ratio pipeline diag ([SVP-RATIO]), cross checks the analytic fit against the measured
	// screen ndc extent, they agree for a settled on axis pose, measured lives here for the check
	{
		extern int ps_r__svp_diag;
		if (ps_r__svp_diag)
		{
			static u32 s_rat_ms = 0;
			if (Device.dwTimeGlobal - s_rat_ms > 1000)
			{
				s_rat_ms = Device.dwTimeGlobal;
				Fvector4 top, bot;
				Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
				m_WVP.transform(top, {0, params.eyepiece.radius, 0, 1});
				m_WVP.transform(bot, {0, -params.eyepiece.radius, 0, 1});
				top.div(top.w); bot.div(bot.w);
				float meas_h = abs(top.y - bot.y);
				if (!_valid(meas_h) || meas_h < 0.001f) meas_h = 0.001f;
				float hf, ha, hn, hff;
				mm.mProjectHud.decompose_projection(hf, ha, hn, hff);
				PipMsg("[SVP-RATIO] meas %.3f analytic %.3f final %.3f flat %d p3y %.1f measH %.3f ndcH %.3f depth %.1fcm r %.2fcm aim %.2f alt %d ok %d hfov %.1f",
					2.f / meas_h, ratio_analytic, ratio_magnification, (int)flat_window,
					ps_s3ds_param_3.y, meas_h, ndc_height,
					lens_depth * 100.f, params.eyepiece.radius * 100.f,
					g_pGamePersistent->m_pGShaderConstants->hud_params.x,
					(int)Device.m_SecondViewport.svp_alt_sight, (int)analytic_ok,
					rad2deg(hf));
			}
		}
	}

	// expose engine magnification so the shader has a curMag with no 3DSS config
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	extern float g_pip_scope_ratio;
	// eyepiece-fit factor, rated on-screen magnification = ratio * scope, a small clip-on window
	// legitimately fits past 8 so the cap bounds the total on-screen mag by the shared limit
	const float ratio_cap = (scope_magnification > EPS) ? _max(1.f, SVP_MAG_LIMIT / scope_magnification) : 1.f;
	const float ratio_use = (ratio_magnification > 0.5f) ? _min(ratio_magnification, ratio_cap) : 1.f;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		g_pip_scope_ratio = ratio_use;
		// derive min/max mag from hud_fov_params for variable reticles (fixed scope: x == y)
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		// the 75-base bounds rescale to the aim fov, authored mins are 75-base too,
		// only the legacy optical-model min already rides the aim fov and passes through
		const float fscale = rad2deg(fov_aim) / 75.f;
		const float yscale = (_abs(fovp.y - fovp.x) < 0.01f || params.svp_min_75base) ? fscale : 1.f;
		if (flat_window && !params.svp_authored_mag)
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / window_fov(fovp.x * fscale) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / window_fov(fovp.y * yscale) : scope_magnification;
		}
		else
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp.x, fscale)) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp.y, yscale)) : scope_magnification;
		}
	}

	SSvpEyeSample eye_sample;
	if (scope_svp_enabled >= 2 && params.IsSVPActive())
		eye_sample = svp_update_eye_sample(mm.mView);

	// the fov we render at to get the correct zoom, the eyepiece-fit ratio scales the vFov
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_use * scope_magnification));
	// flat window renders exactly the panel subtense (tan-correct, the mag division is not)
	if (flat_window)
	{
		if (params.svp_authored_mag)
		{
			// authored mag flat optic renders the floor panel subtense then the optical mag crops in
			const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
			const float fscale = rad2deg(fov_aim) / 75.f;
			const float yscale = (_abs(fovp.y - fovp.x) < 0.01f) ? fscale : 1.f;
			vFov = window_fov(fovp.y * yscale) / scope_magnification;
		}
		else
			vFov = fov / scope_magnification;
	}

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W;
	params.svp_camera_domain = CSecondVPParams::camera_main_eye;
	Fvector2 exit_height_mm = {};
	Fvector2 entrance_height_mm = {};
	Fvector2 principal_ndc = {};
	Fvector registration_eye_local = {};
	Fvector registration_objective_local = {};
	SvpPhysicalOptics::ObjectiveRegistration objective_registration;
	float entrance_limit_mm = 0.f;
	float near_min_axial = 0.f;
	bool near_manual = false;
	bool near_fresh = false;
	float pupil_mag_error = -1.f;
	bool entrance_enabled = false;
	bool entrance_clipped = false;
	int entrance_ray_mode = 0;
	float entrance_parity_state = 0.f;
	extern int ps_r__svp_weapon_continuity;
	// True PiP falls back to the main eye until a valid objective is available
	if (scope_svp_enabled >= 2)
	{
		m_W_svpcam.c.set(eyeW0.c);
		params.svp_camera_domain = CSecondVPParams::camera_main_eye;
		Fvector ax;
		ax.set(params.eyepiece.m_W.k);
		ax.normalize_safe();
		Fvector objective_delta;
		objective_delta.sub(params.objective.m_W.c, params.eyepiece.m_W.c);
		const float front_use_lens = objective_delta.dotproduct(ax);
		float front_use = front_use_lens;
		// a clip-on ahead of the objective owns the entrance, the camera clears its far face
		extern int ps_r__svp_clipon;
		if (ps_r__svp_clipon && _valid(params.svp_clipon_axial)
			&& params.svp_clipon_axial > front_use
			&& params.svp_hud_min_frame != u32(-1)
			&& Device.dwFrame >= params.svp_hud_min_frame
			&& Device.dwFrame - params.svp_hud_min_frame <= 8
			&& params.svp_hud_min_session == params.GetSVPSession()
			&& params.svp_hud_min_epoch == params.svp_optic_epoch)
			front_use = params.svp_clipon_axial + R_VIEWPORT_NEAR;
		params.svp_front_use_m = (_valid(front_use) && front_use > EPS) ? front_use : 0.f;
		if (params.svp_front_use_m > EPS)
		{
			m_W_svpcam.c.set(params.objective.m_W.c);
			// the camera slides up the axis when the entrance sits past the objective lens
			if (front_use > front_use_lens + EPS)
				m_W_svpcam.c.mad(ax, front_use - front_use_lens);
			near_plane = svp_auto_near(params, fNearPlane, near_min_axial, near_manual, near_fresh);
			params.svp_camera_domain = CSecondVPParams::camera_objective;

			// an uncoupled display panel holds its principal point, the image never rides the eye
			if (ps_r__svp_weapon_continuity && !flat_optic && svp_optic_eye_coupled()
				&& params.objective.radius > EPS)
			{
				// registration frame rides the gaze basis at the ocular, design and fold offsets
				// move only the camera origin while the eye residual owns the decenter
				Fmatrix lens_frame;
				bool lens_frame_ok = false;
				if (_valid(params.eyepiece.m_W.c) && _valid(Device.vCameraDirection)
					&& _valid(Device.vCameraTop) && _valid(Device.vCameraRight))
				{
					lens_frame.identity();
					lens_frame.i.set(Device.vCameraRight);
					lens_frame.j.set(Device.vCameraTop);
					lens_frame.k.set(Device.vCameraDirection);
					lens_frame.c.set(params.eyepiece.m_W.c);
					lens_frame_ok = true;
				}
				Fmatrix eyepiece_inverse;
				if (lens_frame_ok && eyepiece_inverse.invert_b(lens_frame))
				{
					eyepiece_inverse.transform_tiny(registration_eye_local, eyeW0.c);
					eyepiece_inverse.transform_tiny(
						registration_objective_local, params.objective.m_W.c);
					// a design offset objective folds back to the eyepiece axis, only the
					// eye residual decenters the projection
					objective_registration = SvpPhysicalOptics::MapObjectiveAxisToEyepiece(
						{ registration_eye_local.x, registration_eye_local.y,
							registration_eye_local.z },
						{ 0.f, 0.f, registration_objective_local.z },
						{ params.eyepiece.radius, params.eyepiece.radius });
					if (objective_registration.valid)
					{
						principal_ndc.set(objective_registration.principal.x,
							objective_registration.principal.y);
						entrance_enabled = true;
						entrance_ray_mode = 2;
					}
				}
			}
		}
	}

	if (scope_svp_enabled >= 2)
	{
		Fvector raw_len;
		raw_len.set(m_W_svpcam.i.magnitude(), m_W_svpcam.j.magnitude(), m_W_svpcam.k.magnitude());
		Fvector raw_dot;
		raw_dot.set(m_W_svpcam.i.dotproduct(m_W_svpcam.j),
			m_W_svpcam.i.dotproduct(m_W_svpcam.k),
			m_W_svpcam.j.dotproduct(m_W_svpcam.k));
		Fvector raw_cross;
		raw_cross.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
		const float raw_hand = raw_cross.dotproduct(m_W_svpcam.k);
		const char* forward_lane;
		const char* basis_lane;
		bool flipped;
		const bool main_fallback = svp_rigid_camera_basis(m_W_svpcam, eyeW0,
			params.eyepiece.m_W.c, params.objective.m_W.c,
			forward_lane, basis_lane, flipped);
		if (main_fallback)
		{
			near_plane = fNearPlane;
			params.svp_camera_domain = CSecondVPParams::camera_main_eye;
			params.svp_front_use_m = 0.f;
			entrance_enabled = false;
			entrance_ray_mode = 0;
			principal_ndc.set(0.f, 0.f);
			objective_registration = {};
		}

		extern int ps_r__svp_cop_diag;
		static u32 s_basis_ms = 0;
		if (ps_r__svp_cop_diag && (main_fallback || Device.dwTimeGlobal - s_basis_ms > 500))
		{
			s_basis_ms = Device.dwTimeGlobal;
			Fvector final_cross;
			final_cross.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
			PipMsg("[SVP-BASIS] rawLen=(%.4f,%.4f,%.4f) rawDot=(%.4f,%.4f,%.4f) rawHand=%.4f finalDot=(%.4f,%.4f,%.4f) finalHand=%.4f forward=%s basis=%s flipped=%d fallback=%d session=%u epoch=%u frame=%u",
				raw_len.x, raw_len.y, raw_len.z,
				raw_dot.x, raw_dot.y, raw_dot.z, raw_hand,
				m_W_svpcam.i.dotproduct(m_W_svpcam.j),
				m_W_svpcam.i.dotproduct(m_W_svpcam.k),
				m_W_svpcam.j.dotproduct(m_W_svpcam.k),
				final_cross.dotproduct(m_W_svpcam.k),
				forward_lane, basis_lane, flipped ? 1 : 0, main_fallback ? 1 : 0,
				camera_session, params.svp_camera_epoch, Device.dwFrame);
		}
	}

	// the world image through rotationally symmetric optics never rolls with mount cant,
	// the svp camera up always follows the view up, cant lives in the tube and reticle
	{
		Fvector fwd, wup, right, up;
		fwd.set(m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
		fwd.normalize();
		wup.set(Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z);
		right.crossproduct(wup, fwd);
		if (right.magnitude() > EPS_S)
		{
			right.normalize();
			up.crossproduct(fwd, right);
			up.normalize();
			m_W_svpcam.i.x = right.x; m_W_svpcam.i.y = right.y; m_W_svpcam.i.z = right.z;
			m_W_svpcam.j.x = up.x;    m_W_svpcam.j.y = up.y;    m_W_svpcam.j.z = up.z;
			m_W_svpcam.k.x = fwd.x;   m_W_svpcam.k.y = fwd.y;   m_W_svpcam.k.z = fwd.z;
		}
	}

	// pip lens flip diagnostic ([SVP-ORIENT]), the mesh basis vs the final svp camera basis
	{
		extern int ps_r__svp_cop_diag;
		extern int ps_r__svp_roll_stabilize;
		if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS)
		{
			static u32 s_orient_ms = 0;
			if (Device.dwTimeGlobal - s_orient_ms > 500)
			{
				s_orient_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-ORIENT] meshUp=(%.2f,%.2f,%.2f) meshFwd=(%.2f,%.2f,%.2f) camUp=(%.2f,%.2f,%.2f) rollStab=%d camUp_out=(%.2f,%.2f,%.2f) camRight_out=(%.2f,%.2f,%.2f) camFwd_out=(%.2f,%.2f,%.2f)",
					params.eyepiece.m_W.j.x, params.eyepiece.m_W.j.y, params.eyepiece.m_W.j.z,
					params.eyepiece.m_W.k.x, params.eyepiece.m_W.k.y, params.eyepiece.m_W.k.z,
					Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z,
					ps_r__svp_roll_stabilize,
					m_W_svpcam.j.x, m_W_svpcam.j.y, m_W_svpcam.j.z,
					m_W_svpcam.i.x, m_W_svpcam.i.y, m_W_svpcam.i.z,
					m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
			}
		}
	}

	// aspect = height/width (engine convention, matches SetActive's fASPECT); square == 1
	// derived from the same policy that sizes the target this frame, not the pre-frame target shape
	extern void svp_target_wh(u32&, u32&);
	u32 tw, th;
	svp_target_wh(tw, th);
	float aspect = (float)th / (float)tw;
	// a non-square SVP keeps the horizontal window subtense at vFov, the vertical follows the aspect
	float vfov_use = (aspect < 0.999f) ? (2.f * atanf(aspect * tanf(vFov * 0.5f))) : vFov;

	float fNearPlane_hud, fFarPlane_hud;
	Device.matrices[0].mProject.decompose_projection(_, _, fNearPlane_hud, fFarPlane_hud);
	auto svp_proj = Fmatrix().build_projection(vfov_use, aspect, near_plane, fFarPlane);
	auto svp_proj_hud = Fmatrix().build_projection(vfov_use, aspect, near_plane, fFarPlane_hud);
	// Place the objective axis on its physical eyepiece chart point
	if (entrance_ray_mode == 2)
	{
		svp_proj._31 += principal_ndc.x;
		svp_proj._32 += principal_ndc.y;
		svp_proj_hud._31 += principal_ndc.x;
		svp_proj_hud._32 += principal_ndc.y;
	}

	if (scope_svp_enabled >= 2)
	{
		extern int ps_r__svp_cop_diag;
		static u32 s_frontcam_ms = 0;
		if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_frontcam_ms > 500)
		{
			s_frontcam_ms = Device.dwTimeGlobal;
			Fvector ax;
			ax.set(params.eyepiece.m_W.k);
			ax.normalize_safe();
			Fvector axis_center;
			axis_center.mad(params.eyepiece.m_W.c, ax, params.svp_front_use_m);
			Fvector eye_to_eyepiece;
			eye_to_eyepiece.sub(params.eyepiece.m_W.c, eyeW0.c);
			const float eye_axial = eye_to_eyepiece.dotproduct(ax);
			Fvector eye_axis;
			eye_axis.mad(eyeW0.c, ax, eye_axial);
			PipMsg("[SVP-CAM] domain=%s front=%.1fcm near=%.1fcm nearMode=%s minAxial=%.4fm nearFresh=%d nearBones=%u nearSkip=%u hybridFront=%.4fm eyeCoupling=%d objectiveLateral=%.1fcm eyeOff=%.1fcm raw=(%.1f,%.1f)mm entranceHeight=(%.1f,%.1f)mm principal=(%.5f,%.5f) limit=%.1fmm entranceScale=%.2f parity=%.2f enabled=%d clipped=%d mag=%.2f opticEpoch=%u cameraEpoch=%u",
				svp_camera_domain_name(params.svp_camera_domain),
				params.svp_front_use_m * 100.f, near_plane * 100.f,
				near_manual ? "manual" : "auto", near_min_axial, near_fresh ? 1 : 0,
				params.svp_hud_min_bones, params.svp_hud_axis_skip,
				params.svp_hybrid_front,
				svp_optic_eye_coupled() ? 1 : 0,
				params.objective.m_W.c.distance_to(axis_center) * 100.f,
				params.eyepiece.m_W.c.distance_to(eye_axis) * 100.f,
				eye_sample.raw_mm.x, eye_sample.raw_mm.y,
				entrance_height_mm.x, entrance_height_mm.y,
				principal_ndc.x, principal_ndc.y,
				entrance_limit_mm,
				eye_sample.entrance_scale, std::clamp(pupil_parity, -1.f, 1.f),
				entrance_enabled ? 1 : 0,
				entrance_clipped ? 1 : 0,
				scope_magnification, params.svp_optic_epoch, params.svp_camera_epoch);
			const char* ray_mode = entrance_ray_mode == 2 ? "objective-register" : "fixed";
			Fvector handed;
			handed.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
			Fvector base_forward = params.eyepiece.m_W.k;
			base_forward.normalize_safe();
			PipMsg("[SVP-RAY] control=%d mode=%s registerVersion=1 raw=(%.2f,%.2f)mm eyeLocal=(%.5f,%.5f,%.5f) objectiveLocal=(%.5f,%.5f,%.5f) hit=(%.5f,%.5f) principal=(%.5f,%.5f) fraction=%.5f valid=%d inside=%d exitHeight=(%.2f,%.2f)mm entranceHeight=(%.2f,%.2f)mm pupilMag=%.3f renderMag=%.3f error=%.3f limit=%.1fmm clipped=%d fwdDot=%.5f handed=%.5f session=%u epoch=%u frame=%u",
				2, ray_mode, eye_sample.raw_mm.x, eye_sample.raw_mm.y,
				registration_eye_local.x, registration_eye_local.y, registration_eye_local.z,
				registration_objective_local.x, registration_objective_local.y,
				registration_objective_local.z,
				objective_registration.hit.x, objective_registration.hit.y,
				principal_ndc.x, principal_ndc.y,
				objective_registration.fraction, objective_registration.valid ? 1 : 0,
				objective_registration.inside_aperture ? 1 : 0,
				exit_height_mm.x, exit_height_mm.y,
				entrance_height_mm.x, entrance_height_mm.y,
				eye_sample.entrance_scale,
				scope_magnification, pupil_mag_error, entrance_limit_mm,
				entrance_clipped ? 1 : 0,
				base_forward.dotproduct(m_W_svpcam.k),
				handed.dotproduct(m_W_svpcam.k),
				camera_session, params.svp_camera_epoch, Device.dwFrame);
		}
	}

	// pip DLSS jitter the SVP scene projection (gated), {0,0} otherwise, applied to mProject only
	Device.m_SecondViewport.svp_jitter_px.set(0, 0);
	if (ps_r__svp_dlss != 0)
	{
		const u32 jf = Device.dwFrame; // latch once, stable on the render thread this frame
		Fvector2 jpx = svp_jitter_offset(jf);
		svp_apply_jitter(svp_proj, jpx, (float)RImplementation.TargetSVP->Width, (float)RImplementation.TargetSVP->Height);
		Device.m_SecondViewport.svp_jitter_px = jpx;
	}

	// the held dbg radius keeps the lines through a culled weapon, it zeroes on unscope
	if (scope_debug >= 2 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		debug_scope(m_W_svpcam, svp_proj);

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;

	// pip cache the SVP scene constants for the DLSS eval inputs and the defocus bind (render thread,
	// written then read the same frame). svp_fov is radians from the projection, the basis is the camera world
	if (params.IsSVPActive() && params.GetSVPSession() == camera_session)
	{
		auto& vp = Device.m_SecondViewport;
		Device.matrices[1].mProject.decompose_projection(vp.svp_fov, vp.svp_aspect, vp.svp_near, vp.svp_far);
		vp.svp_cam_pos = m_W_svpcam.c;
		vp.svp_right = m_W_svpcam.i;
		vp.svp_up = m_W_svpcam.j;
		vp.svp_fwd = m_W_svpcam.k;
		vp.svp_camera_session = camera_session;
		vp.svp_camera_frame = Device.dwFrame;
	}

	// pip snapshot the svp runtime state into the crash-context ring for tester crash logs
	{
		auto& vp = Device.m_SecondViewport;
		SvpCrashFrame cf;
		cf.frame = Device.dwFrame;
		cf.mode = scope_svp_enabled;
		cf.active = vp.IsSVPActive();
		cf.render_pass_is_svp = vp.m_render_pass_is_svp;
		cf.hud_front_m = vp.svp_front_use_m;
		cf.mag = vp.svp_mag;
		cf.fov_deg = rad2deg(vp.svp_fov);
		cf.disc_px = vp.svp_disc_px;
		svp_crash_context_push(cf);
	}

	// pip optics diagnostic: throttled [SVPCOP] log of the camera center-of-projection offset from the
	// eye, settled frames only (ADS transitions blow up ratio_magnification)
	extern int ps_r__svp_cop_diag;
	if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS
		&& ratio_magnification > 1.0f && ratio_magnification < ratio_cap)
	{
		static u32 s_last_ms = 0;
		static float s_last_mag = 0.f;
		const float eff_mag = ratio_magnification * scope_magnification;
		const bool mag_moved = (s_last_mag < EPS) || (fabsf(eff_mag - s_last_mag) > 0.03f * s_last_mag);
		if (Device.dwTimeGlobal - s_last_ms > 400 || mag_moved)
		{
			s_last_ms = Device.dwTimeGlobal;
			s_last_mag = eff_mag;
			Fmatrix eyeW; eyeW.invert(Device.matrices[0].mView);
			Fvector camdir; camdir.set(eyeW.k); camdir.normalize();
			Fvector d; d.sub(m_W_svpcam.c, eyeW.c);
			const float fwd = d.dotproduct(camdir);
			Fvector fwd_v; fwd_v.set(camdir); fwd_v.mul(fwd);
			Fvector lat_v; lat_v.sub(d, fwd_v);
			Fvector eyefwd; eyefwd.set(params.eyepiece.m_W.k); eyefwd.normalize();
			Fvector od; od.sub(params.objective.m_W.c, params.eyepiece.m_W.c);
			// signed scope cant vs world up, proves whether lean actually rolls the weapon on a rig
			float cant = 0.f;
			{
				Fvector jup; jup.set(params.eyepiece.m_W.j); jup.normalize();
				Fvector lvl; lvl.set(0.f, 1.f, 0.f); lvl.mad(eyefwd, -lvl.dotproduct(eyefwd));
				if (lvl.magnitude() > EPS)
				{
					lvl.normalize();
					Fvector cx; cx.crossproduct(lvl, jup);
					cant = rad2deg(atan2f(cx.dotproduct(eyefwd), lvl.dotproduct(jup)));
				}
			}
			PipMsg("[SVPCOP] mode=%d mag=%.3f eff=%.3f min=%.3f max=%.3f ratio=%.3f svpfov=%.2f vfov=%.2f cop_cm=%.2f fwd_cm=%.2f lat_cm=%.2f eye_r_cm=%.2f obj_fwd_cm=%.2f obj_r_cm=%.2f cant=%.1f",
				scope_svp_enabled, scope_magnification, eff_mag, g_pip_scope_min_mag, g_pip_scope_max_mag, ratio_magnification,
				svp_fov, rad2deg(vFov), d.magnitude() * 100.f, fwd * 100.f, lat_v.magnitude() * 100.f,
				params.eyepiece.radius * 100.f, od.dotproduct(eyefwd) * 100.f, params.objective.radius * 100.f,
				cant);
		}
	}

	// pip [SVP-AIM] compares objective registration against the main HUD projection
	if (ps_r__svp_cop_diag >= 2 && params.eyepiece.radius > EPS)
	{
		Fmatrix vpm; vpm.mul(Device.matrices[0].mProjectHud, Device.matrices[0].mView);
		auto project_screen = [&](const Fvector& point, Fvector2& screen) {
			Fvector4 clip;
			vpm.transform(clip, { point.x, point.y, point.z, 1.f });
			if (clip.w <= EPS || !_valid(clip.x) || !_valid(clip.y)
				|| !_valid(clip.z) || !_valid(clip.w))
				return false;
			screen.x = (clip.x / clip.w * 0.5f + 0.5f) * float(Device.dwWidth);
			screen.y = (0.5f - clip.y / clip.w * 0.5f) * float(Device.dwHeight);
			return _valid(screen.x) && _valid(screen.y);
		};
		Fvector mapped_local;
		mapped_local.set(principal_ndc.x * params.eyepiece.radius,
			principal_ndc.y * params.eyepiece.radius, 0.f);
		Fvector mapped_world;
		params.eyepiece.m_W.transform_tiny(mapped_world, mapped_local);
		Fvector camera_forward = m_W_svpcam.k;
		camera_forward.normalize_safe();
		Fvector forward_world;
		forward_world.mad(m_W_svpcam.c, camera_forward, 1.f);
		Fmatrix svp_vpm;
		svp_vpm.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		Fvector4 forward_clip;
		svp_vpm.transform(forward_clip,
			{ forward_world.x, forward_world.y, forward_world.z, 1.f });
		Fvector2 forward_ndc = {};
		const bool forward_valid = forward_clip.w > EPS
			&& _valid(forward_clip.x) && _valid(forward_clip.y)
			&& _valid(forward_clip.z) && _valid(forward_clip.w);
		if (forward_valid)
		{
			forward_ndc.x = forward_clip.x / forward_clip.w;
			forward_ndc.y = forward_clip.y / forward_clip.w;
		}
		Fvector rendered_local;
		rendered_local.set(forward_ndc.x * params.eyepiece.radius,
			forward_ndc.y * params.eyepiece.radius, 0.f);
		Fvector rendered_world;
		params.eyepiece.m_W.transform_tiny(rendered_world, rendered_local);
		Fvector2 lens_screen;
		Fvector2 objective_screen;
		Fvector2 mapped_screen;
		Fvector2 rendered_screen;
		if (project_screen(params.eyepiece.m_W.c, lens_screen)
			&& project_screen(params.objective.m_W.c, objective_screen)
			&& project_screen(mapped_world, mapped_screen)
			&& forward_valid && project_screen(rendered_world, rendered_screen))
		{
			const Fvector2 target = {
				objective_screen.x - lens_screen.x,
				objective_screen.y - lens_screen.y
			};
			const Fvector2 applied = {
				mapped_screen.x - lens_screen.x,
				mapped_screen.y - lens_screen.y
			};
			const Fvector2 residual = {
				mapped_screen.x - objective_screen.x,
				mapped_screen.y - objective_screen.y
			};
			const Fvector2 rendered_residual = {
				rendered_screen.x - objective_screen.x,
				rendered_screen.y - objective_screen.y
			};
			PipMsg("[SVP-AIM] lens=(%.1f,%.1f) objective=(%.1f,%.1f) target=(%.1f,%.1f)px mapped=(%.1f,%.1f) applied=(%.1f,%.1f)px geometricResidual=(%.2f,%.2f)px forwardNdc=(%.5f,%.5f) rendered=(%.1f,%.1f) renderResidual=(%.2f,%.2f)px jitter=(%.3f,%.3f)px raw=(%.2f,%.2f)mm principal=(%.5f,%.5f) valid=%d mode=%d session=%u opticEpoch=%u cameraEpoch=%u frame=%u",
				lens_screen.x, lens_screen.y,
				objective_screen.x, objective_screen.y,
				target.x, target.y,
				mapped_screen.x, mapped_screen.y,
				applied.x, applied.y,
				residual.x, residual.y,
				forward_ndc.x, forward_ndc.y,
				rendered_screen.x, rendered_screen.y,
				rendered_residual.x, rendered_residual.y,
				Device.m_SecondViewport.svp_jitter_px.x,
				Device.m_SecondViewport.svp_jitter_px.y,
				eye_sample.raw_mm.x, eye_sample.raw_mm.y,
				principal_ndc.x, principal_ndc.y,
				objective_registration.valid ? 1 : 0, entrance_ray_mode,
				camera_session, params.svp_optic_epoch, params.svp_camera_epoch,
				Device.dwFrame);
		}
	}

	// pip [3DB] overlay, 1 = markers + fire (green) camera (blue) sight (red) rays,
	// 2 = zeroed departure ray (white), 3 = fading shot tracers
	{
		extern int ps_r__3db_debug;
		if (ps_r__3db_debug > 0 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		{
			auto& vp = Device.m_SecondViewport;
			// the sight line from the stable published copy, else the eyepiece axis
			Fvector s_org, saxis;
			CSecondVPParams::SightSnapshot sight;
			const bool sight_ok = vp.ReadSight(sight)
				&& vp.SnapshotExact(sight.frame, sight.session, Device.dwFrame)
				&& sight.optic_epoch == vp.svp_optic_epoch;
			if (sight_ok)
			{
				s_org.set(sight.position);
				saxis.set(sight.direction);
			}
			else
			{
				s_org.set(params.eyepiece.m_W.c);
				saxis.set(params.eyepiece.m_W.k);
			}
			saxis.normalize_safe();
			const bool has_obj = (params.objective.radius > EPS || params.dbg_objective_r > EPS);
			svp_3db_overlay(fNearPlane, true, s_org, saxis, true, params.eyepiece.m_W.c, has_obj, params.objective.m_W.c);
		}
	}

	// pip one-shot config fingerprint on the first scoped frame so any tester log diffs against ours
	{
		extern int ps_r__svp_report;
		static bool s_cfg_logged = false;
		if (!s_cfg_logged || ps_r__svp_report)
		{
			s_cfg_logged = true;
			ps_r__svp_report = 0;
			// build fingerprint header first, testers diff their log against this
			PipMsg("[SVP-CFG] build %s mode=%d", __DATE__, scope_svp_enabled);
			// generate the cvar list from the console registry so it never drifts from the registered
			// set, every r__svp_/g_svp_/s3ds_ command with its live value grouped about eight per line
			if (Console)
			{
				char line[1024]; line[0] = 0; u32 grp = 0;
				for (const auto& c : Console->Commands)
				{
					const char* nm = c.second->Name();
					if (!(0 == strncmp(nm, "r__svp_", 7) || 0 == strncmp(nm, "g_svp_", 6) || 0 == strncmp(nm, "s3ds_", 5)))
						continue;
					IConsole_Command::TStatus st; st[0] = 0; c.second->Status(st);
					char tok[288]; xr_sprintf(tok, "%s%s=%s", grp ? " " : "", nm, st);
					xr_strcat(line, tok);
					if (++grp == 8) { PipMsg("[SVP-CFG] %s", line); line[0] = 0; grp = 0; }
				}
				if (line[0]) PipMsg("[SVP-CFG] %s", line);
				// aa state read the same registry way, ssfx_taa is the script-driven taa control
				IConsole_Command::TStatus a_taa, a_aa, a_ker; a_taa[0] = a_aa[0] = a_ker[0] = 0;
				auto st_of = [](const char* name, IConsole_Command::TStatus& out) {
					auto it = Console->Commands.find(name);
					if (it != Console->Commands.end()) it->second->Status(out);
				};
				st_of("ssfx_taa", a_taa); st_of("r2_aa", a_aa); st_of("r2_aa_kernel", a_ker);
				PipMsg("[SVP-CFG] aa ssfx_taa=%s r2_aa=%s r2_aa_kernel=%s", a_taa, a_aa, a_ker);
				// present pipeline isn't an r__svp_/g_svp_/s3ds_ cvar so the walk above never prints it
				extern int ps_framelimiter;
				extern int ps_r__swapchain_buffers;
				PipMsg("[SVP-CFG] swapchain_buffers=%d present_interval=%d framelimit=%d",
					ps_r__swapchain_buffers, psDeviceFlags.test(rsVSync) ? 1 : 0, ps_framelimiter);
			}

			// pip [SVP-FILES] shader source truth: where each scope file resolves from (loose or which
			// archive), size and source crc, plus the precompiled-blob and mounted-archive taints
			{
				static const char* s_scope_files[] = {
					"scope_color_write.ps", "scope_3dss_common.h", "scope_common.h",
					"scope_custom_image.h", "scope_custom_reticle.h", "scope_custom_shadow.h",
					"scope_custom_lens.h", "mark_adjust.h",
					"svp_hooks_common.h", "svp_hooks_image.h",
					"svp_hooks_reticle.h", "svp_hooks_lens.h", "svp_hooks_shadow.h",
					"models_scope_reticle.ps", "models_scope_reticle_precise.ps",
					"models_reflex_reticle.ps", "models_reflex_reticle_3db.ps",
					"models_reflex_reticle_simple.ps", "models_reflex_reticle_simple_3db.ps",
					"models_reflex_lens.ps", "models_scope_zwrite.ps",
					"models_scope_reticle.s", "models_scope_reticle_precise.s",
					"models_reflex_reticle.s", "models_reflex_reticle_3db.s",
					"models_reflex_reticle_simple.s", "models_reflex_reticle_simple_3db.s",
					"models_reflex_lens.s", "models_scope_zwrite.s", "models_scope_back.s",
					"models_scope_reticle.vs", "models_reflex_reticle.vs", "models_reflex_lens.vs",
					"scope_vertex.vs", "scope_defines.h", "svp_nearblur.ps",
					"scope_depth_write.ps", "svp_taa_stamp.ps",
					"gbuffer_stage.h", "nv_utils.h", "thermal_utils.h",
					"night_vision.h", "nightvision_gen_1.ps", "nightvision_gen_2.ps",
					"nightvision_gen_3.ps", "combine_1.ps", "combine_2_naa.ps",
					"pp_blur.ps", "sky2.ps" };
				for (const char* fn : s_scope_files)
				{
					string_path rel;
					strconcat(sizeof(rel), rel, "r3\\", fn);
					const CLocatorAPI::file* f = FS.exist("$game_shaders$", rel);
					if (!f)
					{
						PipMsg("[SVP-FILES] %-32s MISSING", fn);
						continue;
					}
					u32 crc = 0;
					char ver[96] = "unmarked";
					if (IReader* r = FS.r_open("$game_shaders$", rel))
					{
						crc = crc32(r->pointer(), r->length());
						// the resolved source carries our PIP_COMPAT_PATCH tag, a foreign shader that
						// shadowed ours or a stale pre-marker file reads unmarked
						const char* p = (const char*)r->pointer();
						const u32 n = r->length();
						const char* key = "PIP_COMPAT_PATCH";
						const u32 klen = (u32)xr_strlen(key);
						for (u32 i = 0; n > klen && i <= n - klen; ++i)
							if (0 == strncmp(p + i, key, klen))
							{
								u32 j = i + klen, k = 0;
								while (j < n && p[j] != '\r' && p[j] != '\n' && k < sizeof(ver) - 1)
									ver[k++] = p[j++];
								ver[k] = 0;
								break;
							}
						FS.r_close(r);
					}
					const char* src = "loose";
					if (f->vfs != 0xffffffff && f->vfs < FS.m_archives.size())
						src = FS.m_archives[f->vfs].path.c_str();
					PipMsg("[SVP-FILES] %-32s %6u bytes crc %08x ver[%s] %s", fn, f->size_real, crc, ver, src);
				}
				// shipped precompiled blobs load with no source crc and shadow every source edit
				FS_FileSet blobs;
				string_path bdir;
				FS.update_path(bdir, "$game_shaders$", "r3\objects\r4\scope_color_write.ps\\");
				FS.file_list(blobs, bdir, FS_ListFiles | FS_RootOnly, "*");
				PipMsg("[SVP-FILES] dispatcher precompiled blobs %u, rs_precompiled_shaders %d",
					(u32)blobs.size(), psDeviceFlags2.test(rsPrecompiledShaders) ? 1 : 0);
				u32 n_arch = 0;
				for (const auto& A : FS.m_archives)
					if (strstr(A.path.c_str(), "mods"))
					{
						PipMsg("[SVP-FILES] mounted mods archive %s", A.path.c_str());
						++n_arch;
					}
				PipMsg("[SVP-FILES] mods archives mounted %u", n_arch);
			}
		}
	}

	// pip [SVP-LEDGER] once-per-session inert-path sweep, after enough scoped time to exercise the
	// gated paths, flags any instrumented counter still zero while its gate cvar is on
	{
		extern void svp_ledger_sweep();
		const float SVP_LEDGER_SETTLE_S = 60.f; // diagnostic patience window in scoped seconds, not a rendering threshold
		static float s_ledger_scoped_s = 0.f;
		static bool s_ledger_swept = false;
		if (!s_ledger_swept)
		{
			s_ledger_scoped_s += Device.fTimeDelta;
			if (s_ledger_scoped_s >= SVP_LEDGER_SETTLE_S)
			{
				s_ledger_swept = true;
				svp_ledger_sweep();
			}
		}
	}

	static bool s_camera_valid = false;
	static u32 s_camera_frame = u32(-1);
	static u32 s_camera_session = 0;
	static u32 s_camera_epoch = 0;
	static CSecondVPParams::ECameraDomain s_camera_domain = CSecondVPParams::camera_main_eye;
	static int s_camera_ray_mode = 0;
	static float s_camera_parity = 0.f;
	const bool first_camera = !s_camera_valid;
	const bool frame_gap = s_camera_valid && Device.dwFrame != s_camera_frame + 1;
	const bool session_change = s_camera_valid && camera_session != s_camera_session;
	const bool epoch_change = s_camera_valid && params.svp_camera_epoch != s_camera_epoch;
	const bool domain_change = s_camera_valid && params.svp_camera_domain != s_camera_domain;
	const bool ray_change = s_camera_valid && entrance_ray_mode != s_camera_ray_mode;
	const bool parity_change = s_camera_valid && _abs(entrance_parity_state - s_camera_parity) > 0.001f;
	const bool history_reset = first_camera || frame_gap || session_change || epoch_change
		|| domain_change || ray_change || parity_change;
	if (history_reset)
	{
		extern int ps_r__svp_cop_diag;
		if (ps_r__svp_cop_diag)
		{
			LPCSTR reason = first_camera ? "initial" : (session_change ? "session"
				: (frame_gap ? "gap" : (epoch_change ? "camera"
					: (domain_change ? "domain" : (ray_change ? "ray" : "parity")))));
			PipMsg("[SVP-CAM] history=reset reason=%s previous=%s current=%s frame=%u session=%u",
				reason, svp_camera_domain_name(s_camera_domain),
				svp_camera_domain_name(params.svp_camera_domain),
				Device.dwFrame, camera_session);
		}
	}
	s_camera_valid = true;
	s_camera_frame = Device.dwFrame;
	s_camera_session = camera_session;
	s_camera_epoch = params.svp_camera_epoch;
	s_camera_domain = params.svp_camera_domain;
	s_camera_ray_mode = entrance_ray_mode;
	s_camera_parity = entrance_parity_state;
	return history_reset;
}

// pip front/second focal-plane world points (scope_w_ffp/sfp); the scope shader projects the SVP image through them
void ffp_sfp()
{
	auto e = Device.m_SecondViewport.eyepiece;
	auto o = Device.m_SecondViewport.objective;

	Fvector p_e = {0, 0, 0}; e.m_W.transform(p_e);
	Fvector p_o = {0, 0, 0}; o.m_W.transform(p_o);

	if (o.radius < EPS)
	{
		// no objective captured, place one at the authored-set front distance in eyepiece radii
		// (mod_system_3dss_objective_lenses median 12.7 mean 14.0, same 14r as the geomscan fallback)
		float distance = e.radius * 14.0f;
		o.radius = e.radius;
		p_o = {0, 0, distance};
		e.m_W.transform(p_o);
	}

	{
		// reproject the objective directly in front of the eyepiece (not all scopes are inline)
		float distance = p_o.distance_to(p_e);
		Fvector dir = {0, 0, 1};
		o.m_W.transform_dir(dir);
		p_o.set(dir.mul(distance).add(p_e));
	}

	Fvector p_d = Fvector(p_o).sub(p_e);

	// each focal anchor sits one eyepiece focal length inside its end of the tube (f_e ~ the
	// authored eye relief, symmetric focal model), the old 0.2/0.8 split stays as the cvar fallback
	float t1 = 0.2f, t2 = 0.8f;
	{
		extern int ps_r__svp_focal_derive;
		extern Fvector4 ps_s3ds_param_1;
		const float L = p_d.magnitude();
		const float fe = ps_s3ds_param_1.y * 0.01f;
		if (ps_r__svp_focal_derive && L > EPS && fe > 0.01f)
		{
			t1 = clampr(fe / L, 0.05f, 0.45f);
			t2 = clampr(1.f - fe / L, 0.55f, 0.95f);
		}
	}
	Device.m_SecondViewport.w_ffp = Fvector(p_d).mul(t1).add(p_e);
	Device.m_SecondViewport.w_sfp = Fvector(p_d).mul(t2).add(p_e);
}

// pip derive the eyepiece/objective from the captured scope-lens meshes, consumed by
// svpCamera and the activation gate (GetSVPCameraMatrix)
static xr_vector<Fvector4> g_pip_hud_geom; // pip diag: snapshot of HUD geometry centers (xyz) + radius (w), captured before render_hud clears the lists

// pip snapshot HUD geometry centers before render_hud clears the lists, deriveScopeLens
// geomscans them after the clear, the call site keeps its gate inline
void svp_snapshot_hud_geom()
{
	g_pip_hud_geom.clear();
	auto snap = [](auto& lst) { for (auto& H : lst) { if (!H.pVisual || !H.pMatrix) continue; auto& VV = H.pVisual->getVisData(); Fvector w; RImplementation.GMBase.svp_pose_of(H.pMatrix)->transform_tiny(w, VV.sphere.P); Fvector4 e; e.set(w.x, w.y, w.z, VV.sphere.R); g_pip_hud_geom.push_back(e); } };
	snap(RImplementation.GMBase.RGraph.mapHUDSorted.Sorted);
	snap(RImplementation.GMBase.RGraph.mapHUD);
}

// pip keeps optic identity separate from physical camera input changes
// target sizing follows identity while camera history follows both
static void svp_epoch_update(const void* lens_visual, float lens_radius,
	CSecondVPParams& params)
{
	// relative eyepiece-radius jump marking a different optic, migrated from the ratio reseed seed
	const float optic_change = 0.05f;
	struct SCameraInputs
	{
		Fvector4 offset = {};
		float objective_mm = 0.f;
		float eye_relief_low_mm = 0.f;
		float eye_relief_high_mm = 0.f;
		float exit_pupil_low_mm = 0.f;
		float exit_pupil_high_mm = 0.f;
		u32 config_generation = 0;
	};
	extern float ps_s3ds_eye_relief_low_mm, ps_s3ds_eye_relief_high_mm;
	extern float ps_s3ds_exit_pupil_low_mm, ps_s3ds_exit_pupil_high_mm;
	const auto& optic_config = params.RenderOpticConfig();
	SCameraInputs current;
	current.offset = params.svp_opt_offset;
	current.objective_mm = params.svp_opt_obj_mm;
	current.eye_relief_low_mm = optic_config.typed_route
		? optic_config.eye_relief_low_mm : ps_s3ds_eye_relief_low_mm;
	current.eye_relief_high_mm = optic_config.typed_route
		? optic_config.eye_relief_high_mm : ps_s3ds_eye_relief_high_mm;
	current.exit_pupil_low_mm = optic_config.typed_route
		? optic_config.exit_pupil_low_mm : ps_s3ds_exit_pupil_low_mm;
	current.exit_pupil_high_mm = optic_config.typed_route
		? optic_config.exit_pupil_high_mm : ps_s3ds_exit_pupil_high_mm;
	current.config_generation = optic_config.typed_route ? optic_config.generation : 0;
	static const void* s_prev_vis = nullptr;
	static float s_prev_r = 0.f;
	static SCameraInputs s_prev_inputs;
	static u32 s_optic_epoch = 0;
	static u32 s_camera_epoch = 0;
	if (lens_radius <= EPS)
		return;
	const bool have_prev = (s_prev_vis != nullptr) && (s_prev_r > EPS);
	const bool vis_change = have_prev && (lens_visual != s_prev_vis);
	const bool r_change = have_prev && (_abs(lens_radius - s_prev_r) > s_prev_r * optic_change);
	auto differs = [](float a, float b, float epsilon)
	{
		return !_valid(a) || !_valid(b) || _abs(a - b) > epsilon;
	};
	const bool input_change = have_prev
		&& (differs(current.offset.x, s_prev_inputs.offset.x, 0.0001f)
			|| differs(current.offset.y, s_prev_inputs.offset.y, 0.0001f)
			|| differs(current.offset.z, s_prev_inputs.offset.z, 0.0001f)
			|| differs(current.offset.w, s_prev_inputs.offset.w, 0.0001f)
			|| differs(current.objective_mm, s_prev_inputs.objective_mm, 0.01f)
			|| differs(current.eye_relief_low_mm, s_prev_inputs.eye_relief_low_mm, 0.01f)
			|| differs(current.eye_relief_high_mm, s_prev_inputs.eye_relief_high_mm, 0.01f)
			|| differs(current.exit_pupil_low_mm, s_prev_inputs.exit_pupil_low_mm, 0.01f)
			|| differs(current.exit_pupil_high_mm, s_prev_inputs.exit_pupil_high_mm, 0.01f)
			|| current.config_generation != s_prev_inputs.config_generation);
	const bool optic_change_now = vis_change || r_change;
	if (optic_change_now)
		++s_optic_epoch;
	if (optic_change_now || input_change)
	{
		++s_camera_epoch;
		extern int ps_r__svp_cop_diag;
		if (ps_r__svp_cop_diag)
			PipMsg("[SVP-EPOCH] optic=%u camera=%u reason=%s r %.2f->%.2fcm",
				s_optic_epoch, s_camera_epoch,
				vis_change ? (r_change ? "visual-radius" : "visual")
					: (r_change ? "radius" : "inputs"),
				s_prev_r * 100.f, lens_radius * 100.f);
	}
	s_prev_vis = lens_visual;
	s_prev_r = lens_radius;
	s_prev_inputs = current;
	params.svp_optic_epoch = s_optic_epoch;
	params.svp_camera_epoch = s_camera_epoch;
}

// pip the optics bus, resolve the per-optic inputs once from the fresh eyepiece so one precedence
// and one eps gate feed every consumer, authored_optics folds into the offset, mm from spec then w
static void svp_optics_resolve(CSecondVPParams* p, float er)
{
	extern int ps_r__svp_authored_optics;
	extern Fvector4 scope_objective_lens_offset;
	extern float ps_s3ds_objective_mm;
	const auto& config = p->RenderOpticConfig();
	Fvector4 off;
	if (ps_r__svp_authored_optics)
	{
		if (config.typed_route)
			off = config.has_objective_offset ? config.objective_offset : Fvector4{};
		else
			off = scope_objective_lens_offset;
	}
	else off.set(0.f, 0.f, 0.f, 0.f);
	p->svp_opt_offset = off;
	float mm = 0.f;
	const float authored_mm = config.typed_route
		? (config.has_objective_mm ? config.objective_mm : 0.f)
		: ps_s3ds_objective_mm;
	if (authored_mm > EPS)
		mm = authored_mm;
	else if (off.w > EPS)
		mm = 2000.f * off.w * er; // authored w radius in eyepiece radii to mm
	p->svp_opt_obj_mm = mm;
	extern int ps_r__svp_stats; extern u32 svp_stats_optic_resolve;
	if (ps_r__svp_stats) ++svp_stats_optic_resolve;
	extern int ps_r__svp_cop_diag;
	if (ps_r__svp_cop_diag)
	{
		static u32 s_opt_ms = 0;
		if (Device.dwTimeGlobal - s_opt_ms > 1000)
		{
			s_opt_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-OPTICS] off %.2f,%.2f,%.2f,%.2f mm %.1f src %s",
				off.x, off.y, off.z, off.w, mm,
				(authored_mm > EPS) ? (config.typed_route
					? (config.source[CSecondVPParams::optic_objective_mm][0]
						? config.source[CSecondVPParams::optic_objective_mm] : "typed_default")
					: "legacy") : ((off.w > EPS) ? "w" : "none"));
		}
	}
}

// pip a detected objective offset only means anything around the lens it was measured on, drop it
// when the detected eye disc sits further than this many eyepiece radii from the runtime eyepiece
static const float SVP_DETECT_EYE_TOL_R = 1.0f;

// pip the applied offset is the detector's own when all four components match, identifies the
// detection lane by value so no source label or push route has to be trusted
static bool svp_offset_is_detected(const Fvector4& off, const Fvector4& det)
{
	return _abs(off.x - det.x) < 0.01f && _abs(off.y - det.y) < 0.01f
		&& _abs(off.z - det.z) < 0.01f && _abs(off.w - det.w) < 0.01f;
}

// pip both centres are bind pose model space so the compare needs no runtime pose, authored ltx and
// typed spec offsets carry different numbers and a different label so neither can be dropped here
static bool svp_detected_offset_off_frame(CSecondVPParams* p, CSkeletonX* sk,
	const Fvector& eye_bind, float er, dxRender_Visual* visual)
{
	extern int ps_r__svp_lens_reject;
	if (!ps_r__svp_lens_reject || !sk || er <= EPS)
		return false;
	SLensDetection d;
	if (!sk->SVP_GetLensDetection(d) || !d.ok)
		return false;
	const float radii = d.eye_center.distance_to(eye_bind) / er;
	// a transition frame can hand over garbage geometry, skip the verdict and retry next frame
	if (!_valid(radii))
		return false;
	const auto& config = p->RenderOpticConfig();
	LPCSTR src = config.source[CSecondVPParams::optic_objective_offset];
	const bool labelled = src && 0 == xr_strcmp(src, "engine_detection");
	const bool matches = svp_offset_is_detected(p->svp_opt_offset, d.offset);
	// lateral magnitude of the applied offset in eyepiece radii, off axis objectives read high here
	const Fvector4& applied = p->svp_opt_offset;
	const float lat_r = sqrtf(applied.x * applied.x + applied.y * applied.y);
	const bool reject = (labelled || matches) && radii > SVP_DETECT_EYE_TOL_R;
	// one latched line per optic carrying every gate value so a miss reads straight from the log,
	// the raw centres and the eyepiece texture name identify which mesh each side actually holds
	static const void* s_gate_last = nullptr;
	if (visual != s_gate_last)
	{
		s_gate_last = visual;
		auto tx = visual ? visual->GetTexture() : nullptr;
		PipMsg("[SVP-EYEDIV] %s label=%s match=%d lat_r=%.2f off=(%.2f,%.2f,%.2f) dist_r=%.2f er_cm=%.2f tol_r=%.2f typed=%d has_off=%d detsrc=%d",
			reject ? "REJECT detected offset" : "keep offset",
			(src && src[0]) ? src : "none", matches ? 1 : 0,
			lat_r, applied.x, applied.y, applied.z,
			radii, er * 100.f, SVP_DETECT_EYE_TOL_R,
			config.typed_route ? 1 : 0, config.has_objective_offset ? 1 : 0, d.source);
		// det_lat_r is the objective offset perpendicular to the fitted eye normal, the quantity a
		// coaxiality bound at the objective search would test, logged for every optic used or not
		const float det_lat_r = sqrtf(d.offset.x * d.offset.x + d.offset.y * d.offset.y);
		PipMsg("[SVP-EYEDIV] operands eye_bind=(%.4f,%.4f,%.4f) det_eye=(%.4f,%.4f,%.4f) det_obj=(%.4f,%.4f,%.4f) det_r=%.4f has_obj=%d eyetex=%s",
			eye_bind.x, eye_bind.y, eye_bind.z,
			d.eye_center.x, d.eye_center.y, d.eye_center.z,
			d.obj_center.x, d.obj_center.y, d.obj_center.z,
			d.eye_radius, d.has_objective ? 1 : 0,
			tx ? tx->cName.c_str() : "?");
		PipMsg("[SVP-EYEDIV] detected off=(%.2f,%.2f,%.2f,%.2f) det_lat_r=%.2f eye_n=(%.4f,%.4f,%.4f) detsrc=%d",
			d.offset.x, d.offset.y, d.offset.z, d.offset.w, det_lat_r,
			d.eye_normal.x, d.eye_normal.y, d.eye_normal.z, d.source);
	}
	return reject;
}

void CRender::deriveScopeLens()
{
	auto& viewport = Device.m_SecondViewport;
	viewport.svp_lens_root = nullptr;
	viewport.svp_lens_visual = nullptr;
	viewport.svp_lens_owner = nullptr;
	viewport.svp_lens_frame = Device.dwFrame;
	extern int ps_r__svp_diag;
	if (ps_r__svp_diag)
	{
		const auto& config = viewport.RenderOpticConfig();
		static u32 s_latch_generation = u32(-1);
		static u32 s_latch_token = u32(-1);
		if (config.generation != s_latch_generation || config.context_token != s_latch_token)
		{
			s_latch_generation = config.generation;
			s_latch_token = config.context_token;
			PipMsg("[SVP-CONFIG] latch frame=%u session=%u typed=%d token=%u gen=%u weapon_id=%u context=%s valid=%d",
				Device.dwFrame, viewport.GetSVPSession(), config.typed_route,
				config.context_token, config.generation, config.weapon_id, config.context, config.valid);
		}
	}

	// pip clear the flat-panel publish, re-set below when a flat window lens is the eyepiece
	viewport.svp_panel_flat = false;

	// multi-lens weapons carry several scope-lens meshes, pick the aimed one, a visible
	// lens bone nearest the camera ray
	const void* best = nullptr;
	{
		extern int ps_r__svp_cop_diag;
		static u32 s_lens_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_lens_diag_ms > 3000);
		if (diag)
			s_lens_diag_ms = Device.dwTimeGlobal;
		float best_score = 1e9f;
		const Fvector cam_p = Device.vCameraPosition;
		const Fvector cam_f = Device.vCameraDirection;
		for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
		{
			if (!N.pVisual || !N.pMatrix)
				continue;
			Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
			bool bone_vis = true;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			if (sk)
			{
				bone_vis = sk->SVP_LensBoneVisible();
				Fmatrix boneR;
				if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
					lensX.mulB_43(boneR);
			}
			auto& V = N.pVisual->getVisData();
			Fvector c; V.box.getcenter(c);
			Fvector cw; lensX.transform_tiny(cw, c);
			Fvector d; d.sub(cw, cam_p);
			const float dist = d.magnitude();
			if (!_valid(dist) || dist < 0.01f)
				continue;
			d.div(dist);
			const float fwd = d.dotproduct(cam_f);
			if (diag)
			{
				auto tx = N.pVisual->GetTexture();
				Fvector bd; V.box.getsize(bd);
				PipMsg("[SVP-LENS] %s dist=%.1fcm fwd=%.3f r=%.1fcm box=%.1fx%.1fx%.1fcm %s",
					bone_vis ? "vis " : "HIDE", dist * 100.f, fwd, V.sphere.R * 100.f,
					bd.x * 100.f, bd.y * 100.f, bd.z * 100.f,
					tx ? tx->cName.c_str() : "?");
			}
			if (!bone_vis)
				continue;
			if (dist < best_score)
			{
				best_score = dist;
				best = &N;
			}
		}
	}

	// pip diag, an aimed frame that derives no lens (hip frames also derive, gate on the raise)
	if (!best)
	{
		extern int ps_r__svp_diag;
		if (ps_r__svp_diag && g_pGamePersistent
			&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_nb_ms = 0;
			if (Device.dwTimeGlobal - s_nb_ms > 500)
			{
				s_nb_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-LENS] no aimed lens picked, nodes=%u stale_r=%.1fcm",
					(u32)GMBase.RGraph.mapScopeHUDSorted.size(),
					Device.m_SecondViewport.dbg_eyepiece_r * 100.f);
			}
		}
	}

	for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
	{
		if (&N != best)
			continue;

		// a skinned scope lens is positioned by its bone, the captured matrix is only the kinematics
		// root, fold in the lens bone skinning matrix so the eyepiece follows the glass on ADS and sway
		Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		bool lens_bone = false;
		if (sk)
		{
			Fmatrix boneR;
			if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
			{
				lensX.mulB_43(boneR);
				lens_bone = true;
			}
		}

		auto& V = N.pVisual->getVisData();
		Fvector c;
		V.box.getcenter(c); // AABB center fits a flat lens disc tighter than the bounding sphere center
		Fmatrix m_W = lensX;
		m_W.mulB_43(Fmatrix().translate(c));
		float radius = V.sphere.R;

		auto* p = &Device.m_SecondViewport;

		p->eyepiece.m_W = m_W;
		p->eyepiece.radius = radius;
		p->svp_lens_root = N.pMatrix;
		p->svp_lens_visual = N.pVisual;
		p->svp_lens_owner = sk ? sk->SVP_SkeletonOwner() : nullptr;

		// resolve the optics bus from the fresh eyepiece, the consumers below and downstream read
		// the record instead of the raw cvars
		svp_optics_resolve(p, radius);
		svp_epoch_update(N.pVisual, radius, *p);

		{
			extern int ps_r__svp_cop_diag;
			static const void* s_root = nullptr;
			static const void* s_visual = nullptr;
			static u32 s_session = 0;
			static u32 s_epoch = 0;
			static u32 s_log_ms = 0;
			static Fvector s_relative_c = {};
			static Fvector s_relative_i = {};
			static Fvector s_relative_j = {};
			static Fvector s_relative_k = {};
			static bool s_valid = false;
			if (ps_r__svp_cop_diag >= 2)
			{
				Fmatrix root_inverse;
				root_inverse.invert(*GMBase.svp_pose_of(N.pMatrix));
				Fmatrix relative;
				relative.mul_43(root_inverse, m_W);
				Fvector relative_i = relative.i;
				Fvector relative_j = relative.j;
				Fvector relative_k = relative.k;
				relative_i.normalize_safe();
				relative_j.normalize_safe();
				relative_k.normalize_safe();
				const u32 session = p->GetSVPSession();
				const bool same = s_valid && s_root == N.pMatrix && s_visual == N.pVisual
					&& s_session == session && s_epoch == p->svp_camera_epoch;
				const bool linked = same && Device.dwTimeGlobal - s_log_ms <= 500;
				if (!linked || Device.dwTimeGlobal - s_log_ms > 250)
				{
					Fvector relative_delta = {};
					float angle_delta = 0.f;
					if (linked)
					{
						relative_delta.sub(relative.c, s_relative_c);
						const float trace = relative_i.dotproduct(s_relative_i)
							+ relative_j.dotproduct(s_relative_j)
							+ relative_k.dotproduct(s_relative_k);
						angle_delta = rad2deg(acosf(std::clamp((trace - 1.f) * 0.5f, -1.f, 1.f)));
					}
					PipMsg("[SVP-LENSREL] seed=%d dt_ms=%u relC=(%.5f,%.5f,%.5f) dRelC=(%.5f,%.5f,%.5f) relK=(%.5f,%.5f,%.5f) dAngleDeg=%.5f bone=%d root=%p visual=%p owner=%p session=%u epoch=%u frame=%u",
						linked ? 0 : 1, linked ? Device.dwTimeGlobal - s_log_ms : 0,
						relative.c.x, relative.c.y, relative.c.z,
						relative_delta.x, relative_delta.y, relative_delta.z,
						relative_k.x, relative_k.y, relative_k.z, angle_delta,
						lens_bone ? 1 : 0, N.pMatrix, N.pVisual, p->svp_lens_owner,
						session, p->svp_camera_epoch, Device.dwFrame);
					s_root = N.pMatrix;
					s_visual = N.pVisual;
					s_session = session;
					s_epoch = p->svp_camera_epoch;
					s_log_ms = Device.dwTimeGlobal;
					s_relative_c = relative.c;
					s_relative_i = relative_i;
					s_relative_j = relative_j;
					s_relative_k = relative_k;
					s_valid = true;
				}
			}
		}

		// ballistics sight line publishes as one record
		if (radius > EPS)
		{
			CSecondVPParams::SightSnapshot sight;
			sight.position.set(m_W.c);
			Fvector sk;
			sk.set(m_W.k);
			sk.normalize_safe();
			sight.direction.set(sk);
			sight.lens_radius = radius;
			sight.frame = Device.dwFrame;
			sight.session = p->GetSVPSession();
			sight.optic_epoch = p->svp_optic_epoch;
			const auto& config = p->RenderOpticConfig();
			sight.optic_typed = config.typed_route;
			sight.optic_config_valid = config.valid;
			sight.optic_context_token = config.context_token;
			sight.optic_config_generation = config.generation;
			sight.optic_route_epoch = config.route_epoch;
			p->PublishSight(sight);
		}

		// panel aspect from the lens AABB, the two largest extents are the plane W:H (the ~0 axis is
		// the depth), ~1 for a round lens so a conventional scope keeps its square SVP
		Fvector bsz; V.box.getsize(bsz);
		float ea[3] = { _abs(bsz.x), _abs(bsz.y), _abs(bsz.z) };
		if (ea[0] < ea[1]) { float t = ea[0]; ea[0] = ea[1]; ea[1] = t; }
		if (ea[1] < ea[2]) { float t = ea[1]; ea[1] = ea[2]; ea[2] = t; }
		if (ea[0] < ea[1]) { float t = ea[0]; ea[0] = ea[1]; ea[1] = t; }
		p->svp_panel_aspect = (ea[1] > EPS) ? (ea[0] / ea[1]) : 1.f;

		// pip flat-panel on-screen quad for the binocular brackets, plane half-extent world vectors
		// (mesh local x = width, y = height, z = normal)
		{
			extern int ps_r__svp_flat_window;
			extern Fvector4 ps_s3ds_param_3;
			if (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8)
			{
				Fvector we, he;
				m_W.transform_tiny(we, Fvector().set(bsz.x * 0.5f, 0.f, 0.f));
				m_W.transform_tiny(he, Fvector().set(0.f, bsz.y * 0.5f, 0.f));
				p->svp_panel_ax_w.sub(we, m_W.c);
				p->svp_panel_ax_h.sub(he, m_W.c);
				p->svp_panel_flat = true;
			}
		}

		if (p->eyepiece.radius > EPS)
		{
			// pip objective, prefer the real captured front lens on the optical axis, the
			// geomscan finds the forward-most on-axis node as the fallback plane
			float geom_front = -1.f;
			{
				const Fvector eye = p->eyepiece.m_W.c;
				Fvector axis; axis.set(p->eyepiece.m_W.k); axis.normalize();
				const float rr = p->eyepiece.radius;
				if (rr > EPS)
				{
					float fr = -1e9f;
					for (auto& g : g_pip_hud_geom)
					{
						Fvector wc; wc.set(g.x, g.y, g.z);
						Fvector d; d.sub(wc, eye);
						const float fwd = d.dotproduct(axis);
						if (fwd <= 0.f) continue;
						Fvector proj; proj.mad(eye, axis, fwd);
						const float perp = wc.distance_to(proj);
						if (perp < rr * 2.0f && (fwd + g.w) > fr) fr = fwd + g.w;
					}
					if (fr > 0.f) { geom_front = fr / rr; if (geom_front < 4.f) geom_front = 4.f; else if (geom_front > 30.f) geom_front = 30.f; }
				}
			}

			bool have_obj = false;
			float dbg_cand_dist = -1.f; // objective-capture distance, used by the capture gate below
			// authored objective wins, place the front lens at the resolved z along the optical axis
			// with the lateral x/y offset and w radius, all in eyepiece radii
			const Fvector4& off = p->svp_opt_offset;
			// an offset measured around some other lens falls through to the mesh then geometric objective
			const bool off_off_frame = (off.z > EPS)
				&& svp_detected_offset_off_frame(p, sk, c, p->eyepiece.radius, N.pVisual);
			if (off.z > EPS && !off_off_frame)
			{
				const float er = p->eyepiece.radius;
				Fvector fwd; fwd.set(p->eyepiece.m_W.k); fwd.normalize();
				Fvector ri;  ri.set(p->eyepiece.m_W.i);  ri.normalize();
				Fvector up;  up.set(p->eyepiece.m_W.j);  up.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				p->objective.m_W.c.mad(fwd, off.z * er);
				p->objective.m_W.c.mad(ri,  off.x * er);
				p->objective.m_W.c.mad(up,  off.y * er);
				p->objective.radius = (off.w > EPS)
					? off.w * er : p->eyepiece.radius * ps_r__svp_obj_size;
				have_obj = true;
			}
			for (auto& N : GMBase.RGraph.mapScopeHUDObjective)
			{
				if (have_obj)
					break; // authored objective already placed, mesh detection is the next fallback
				if (!N.pVisual || !N.pMatrix)
					break;
				Fmatrix oX = *GMBase.svp_pose_of(N.pMatrix);
				if (CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual))
				{
					Fmatrix boneR;
					if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
						oX.mulB_43(boneR);
				}
				auto& OV = N.pVisual->getVisData();
				Fvector oc; OV.box.getcenter(oc);
				Fvector ow; oX.transform_tiny(ow, oc);
				dbg_cand_dist = ow.distance_to(p->eyepiece.m_W.c);
				// distinct from the eyepiece (a single-lens scope captures the same disc for both)
				if (OV.sphere.R > EPS && dbg_cand_dist > p->eyepiece.radius * 0.5f)
				{
					p->objective.m_W = p->eyepiece.m_W; // stabilized optical axis
					p->objective.m_W.c.set(ow);          // real front-lens world position
					p->objective.radius = OV.sphere.R;
					have_obj = true;
				}
				break;
			}
			if (!have_obj)
			{
				// no distinct objective mesh, derive it geometrically along the optical axis,
				// the eyepiece radius is the only mesh-scale-robust unit
				Fvector fwd; fwd.set(p->eyepiece.m_W.k); fwd.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				// 14r fallback = the authored-set front distance (mod_system_3dss_objective_lenses mean, median 12.7)
				const float dist_r = (geom_front > 0.f ? geom_front : 14.0f) * ps_r__svp_obj_dist;
				p->objective.m_W.c.mad(fwd, p->eyepiece.radius * dist_r);
				p->objective.radius = p->eyepiece.radius * ps_r__svp_obj_size; // eyepiece-relative objective radius (one global knob): the front-node geomscan swung ~6x conflating glass vs bell housing, eyepiece radius is the only mesh-scale-stable unit
			}
			ffp_sfp(); // focal-plane points for the scope shader

			// pip [SVP-DETECT]/[SVP-EYEDIV] one-shot a/b, measured mesh geometry vs authored ltx vs
			// the live derivation, off unless r__svp_diag, latched per aimed optic
			{
				extern int ps_r__svp_diag;
				if (ps_r__svp_diag)
				{
					static const void* s_detect_last = nullptr;
					if (N.pVisual != s_detect_last)
					{
						extern int ps_r__svp_measured_optics;
						const float er = p->eyepiece.radius;
						// authored objective resolved by the optics bus, no second precedence copy
						const float auth_w = p->svp_opt_offset.w;
						const float auth_mm = p->svp_opt_obj_mm;
						// live objective this frame, forward distance and radius in eyepiece radii
						Fvector le; le.sub(p->objective.m_W.c, p->eyepiece.m_W.c);
						const float live_z = (er > EPS) ? le.magnitude() / er : 0.f;
						const float live_w = (er > EPS) ? p->objective.radius / er : 0.f;
						// a weapon transition frame can carry a garbage pose, nan or inf skips the
						// latch so the next frame retries with a real one
						if (_valid(er) && _valid(live_z) && _valid(live_w))
						{
							s_detect_last = N.pVisual;
							SLensDetection d;
							const bool have = sk && sk->SVP_GetLensDetection(d);
							PipMsg("[SVP-DETECT] switch=%d er=%.4f | authored z=%.2f w=%.3f mm=%.1f | live z=%.2f w=%.3f | detected ok=%d z=%.2f w=%.3f mm=%.1f src=%d",
								ps_r__svp_measured_optics, er,
								p->svp_opt_offset.z, auth_w, auth_mm,
								live_z, live_w,
								(int)(have && d.ok),
								(have && d.ok) ? d.offset.z : 0.f,
								(have && d.ok) ? d.offset.w : 0.f,
								(have && d.ok) ? d.mm : 0.f,
								(have && d.ok) ? d.source : -1);
							// live vs detected eyepiece radius, surfaces the near-bone contamination that skews
							// the analytic zoom ratio, read-only, the override stays deferred
							const float det_r = (have && d.ok) ? d.eye_radius : 0.f;
							const float eratio = (det_r > EPS && er > EPS) ? er / det_r : 0.f;
							PipMsg("[SVP-EYEDIV] live_r=%.4f detected_r=%.4f ratio=%.2f src=%d %s",
								er, det_r, eratio, (have && d.ok) ? d.source : -1,
								(eratio > 1.4f || (eratio > EPS && eratio < 0.7f)) ? "DIVERGE" : "agree");
						}
					}
				}
			}
		}
		break; // the first captured lens is the eyepiece
	}

	// pip DLSS reset when the lens first becomes valid (active can flip a frame before the capture),
	// render-thread edge state, inert at gate 0
	bool lens_valid = (Device.m_SecondViewport.eyepiece.radius > EPS);
	if (lens_valid && !Device.m_SecondViewport.m_lens_prev_valid)
		Device.m_SecondViewport.dlss_reset_next = true;
	Device.m_SecondViewport.m_lens_prev_valid = lens_valid;
}
