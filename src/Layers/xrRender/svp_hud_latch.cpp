#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

// pip per visual table of posed vert angles for the bones the sight axis passes through, their
// boxes lie near the axis so the verts carry the visible depth instead
struct SSvpMeshNearEntry { float tan_theta; float axial_min; };
struct SSvpMeshNearTable
{
	u32 session = 0;
	u32 epoch = u32(-1);
	bool built = false;
	xr_vector<SSvpMeshNearEntry> entries; // sorted by angle, axial_min is the running minimum
};
static xr_map<const dxRender_Visual*, SSvpMeshNearTable> s_svp_mesh_near;

// pip object-space render (skinning) matrix of the lens bone, the SVP eyepiece uses it so a skinned
// scope lens follows its bone through ADS and sway instead of the kinematics root
bool CSkeletonX::SVP_LensBoneXform(Fmatrix& out)
{
	if (!Parent)
		return false;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	out = Parent->LL_GetBoneInstance(bone).mRenderTransform;
	return true;
}

u16 CSkeletonX::SVP_MeasureBoneCount() const
{
	return Parent ? (u16)Parent->LL_BoneCount() : 0;
}

// pip world corners of one bone's obb, posed from the same palette the drain renders with so the
// measurement and the draw cannot disagree
bool CSkeletonX::SVP_BoneCornersWorld(u16 bone, const Fmatrix& pose, Fvector* corners8)
{
	if (!Parent || bone >= Parent->LL_BoneCount())
		return false;
	Fmatrix bone_x;
	const bool snapped = SVP_BoneSnapshotXform(bone, bone_x);
	{
		xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
		if (!Parent->LL_GetBoneVisible(bone))
			return false;
		if (!snapped)
			bone_x = Parent->LL_GetBoneInstance(bone).mRenderTransform;
	}
	const Fobb& obb = Parent->LL_GetData(bone).obb;
	const Fvector& S = obb.m_halfsize;
	if (!_valid(S) || S.x <= EPS || S.y <= EPS || S.z <= EPS)
		return false;
	Fmatrix box_x;
	obb.xform_get(box_x);
	Fmatrix X = pose;
	X.mulB_43(bone_x);
	X.mulB_43(box_x);
	for (u32 c = 0; c < 8; ++c)
	{
		Fvector local;
		local.set((c & 1) ? S.x : -S.x, (c & 2) ? S.y : -S.y, (c & 4) ? S.z : -S.z);
		X.transform_tiny(corners8[c], local);
		if (!_valid(corners8[c]))
			return false;
	}
	return true;
}

// pip posed world verts of the flagged bones, the same palette composition as the bone corners
u32 CSkeletonX::SVP_MeshVertsWorld(const Fmatrix& pose, const xr_vector<u8>& through,
	xr_vector<Fvector>& out)
{
	if (!Parent || through.empty())
		return 0;
	xr_vector<Fvector> bind;
	xr_vector<u16> bone;
	SVP_GatherVerts(bind, bone);
	if (bind.empty())
		return 0;
	xr_vector<Fmatrix> palette(through.size());
	xr_vector<u8> ok(through.size(), 0);
	for (u16 b = 0; b < (u16)through.size(); ++b)
	{
		if (!through[b])
			continue;
		Fmatrix bone_x;
		const bool snapped = SVP_BoneSnapshotXform(b, bone_x);
		{
			xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
			if (b >= Parent->LL_BoneCount() || !Parent->LL_GetBoneVisible(b))
				continue;
			if (!snapped)
				bone_x = Parent->LL_GetBoneInstance(b).mRenderTransform;
		}
		palette[b].mul_43(pose, bone_x);
		ok[b] = 1;
	}
	u32 added = 0;
	for (u32 i = 0; i < bind.size(); ++i)
	{
		const u16 b = bone[i];
		if (b >= (u16)through.size() || !ok[b])
			continue;
		Fvector w;
		palette[b].transform_tiny(w, bind[i]);
		if (_valid(w))
		{
			out.push_back(w);
			++added;
		}
	}
	return added;
}

// pip lens bone visibility, hidden markswitch lens meshes must not win the eyepiece pick
bool CSkeletonX::SVP_LensBoneVisible()
{
	if (!Parent)
		return true;
	BOOL visible;
	if (SVP_BoneSnapshotVisible(visible))
		return !!visible;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return true;
	return !!Parent->LL_GetBoneVisible(bone);
}

// pip forward the owning kinematics measured lens fit, lets the render side query detection
// from a lens leaf visual, false when unparented
bool CSkeletonX::SVP_GetLensDetection(SLensDetection& out)
{
	if (!Parent)
		return false;
	return Parent->GetLensDetection(out);
}

// pip capture one HUD root sample for both viewports and every late lens draw
void CDSGraphManager::svp_latch_hud_poses()
{
	m_svp_pose.clear();
	m_svp_pose_frame = Device.dwFrame;
	auto grab = [this](Fmatrix* p)
	{
		if (!p)
			return;
		for (const auto& pose : m_svp_pose)
			if (pose.source == p)
				return;
		m_svp_pose.emplace_back();
		m_svp_pose.back().source = p;
		m_svp_pose.back().value = *p;
	};
	for (auto& n : RGraph.mapHUD)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Sorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Wmark)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Emissive)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Distort)
		grab(n.pMatrix);
#if defined(USE_DX11)
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab(n.pMatrix);

	extern int scope_svp_enabled;
	extern int ps_r__svp_weapon_continuity;
	const bool exact_pose = scope_svp_enabled >= 2 && ps_r__svp_weapon_continuity
		&& Device.true_pip_on;
	if (exact_pose)
	{
		svp_hud_bone_snapshot_begin();
		auto capture = [](dxRender_Visual* v)
		{
			CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
			if (skeleton)
				skeleton->SVP_CaptureBoneSnapshot();
		};
		for (auto& n : RGraph.mapHUD)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Sorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Wmark)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Emissive)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Distort)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDSorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDObjective)
			capture(n.pVisual);
		for (auto& n : RGraph.mapReflexHUDSorted)
			capture(n.pVisual);
	}

	m_svp_bone.clear();
	m_svp_bone_frame = Device.dwFrame;
	auto grab_bone = [this](dxRender_Visual* v)
	{
		if (!v)
			return;
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
				return;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(v);
		Fmatrix b;
		if (sk && sk->SVP_LensBoneXform(b))
		{
			m_svp_bone.emplace_back();
			m_svp_bone.back().visual = v;
			m_svp_bone.back().value = b;
		}
	};
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab_bone(n.pVisual);
#endif
}

// the latched pose while this frame's latch is live, else the caller's pointer unchanged
Fmatrix* CDSGraphManager::svp_pose_of(Fmatrix* p)
{
	if (!p || p == &Fidentity) // world statics all push identity, none of them can be a latch source
		return p;
	if (m_svp_pose_frame == Device.dwFrame)
		for (auto& pose : m_svp_pose)
			if (pose.source == p)
				return &pose.value;
	return p;
}

// the latched lens bone while this frame's latch is live, false falls back to the live read
bool CDSGraphManager::svp_lens_bone_of(dxRender_Visual* v, Fmatrix& out)
{
	CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
	if (skeleton && skeleton->SVP_BoneSnapshotXform(out))
		return true;
	if (m_svp_bone_frame == Device.dwFrame)
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
			{
				out = bone.value;
				return true;
			}
	return false;
}

static LPCSTR svp_hud_role_name(u8 role)
{
	switch (role)
	{
	case IDSGraphManager::hud_hands: return "hands";
	case IDSGraphManager::hud_primary_item: return "primary";
	case IDSGraphManager::hud_offhand_item: return "offhand";
	case IDSGraphManager::hud_optic: return "optic";
	case IDSGraphManager::hud_prop: return "prop";
	default: return "unknown";
	}
}

static bool svp_basis_coords(const Fvector& origin, const Fvector& right,
	const Fvector& up, const Fvector& forward, const Fvector& point, Fvector& coords)
{
	Fvector r = right;
	Fvector u = up;
	Fvector f = forward;
	if (r.square_magnitude() <= EPS || u.square_magnitude() <= EPS
		|| f.square_magnitude() <= EPS)
		return false;
	r.normalize();
	u.normalize();
	f.normalize();
	Fvector delta;
	delta.sub(point, origin);
	coords.set(delta.dotproduct(r), delta.dotproduct(u), delta.dotproduct(f));
	return _valid(coords);
}

static bool svp_basis_direction(const Fvector& right, const Fvector& up,
	const Fvector& forward, const Fvector& direction, Fvector& coords)
{
	Fvector r = right;
	Fvector u = up;
	Fvector f = forward;
	Fvector d = direction;
	if (r.square_magnitude() <= EPS || u.square_magnitude() <= EPS
		|| f.square_magnitude() <= EPS || d.square_magnitude() <= EPS)
		return false;
	r.normalize();
	u.normalize();
	f.normalize();
	d.normalize();
	coords.set(d.dotproduct(r), d.dotproduct(u), d.dotproduct(f));
	return _valid(coords);
}

struct SSvpRelativeProbe
{
	dxRender_Visual* visual = nullptr;
	Fmatrix* root = nullptr;
	const void* owner = nullptr;
	Fvector camera = {};
	Fvector lens = {};
	Fvector root_lens = {};
	Fvector axis_i = {};
	Fvector axis_j = {};
	Fvector axis_k = {};
	float forward = -flt_max;
	u8 role = IDSGraphManager::hud_unknown;
	bool skinned = false;
	bool snapshot = false;
	bool valid = false;
};

static bool svp_axial_bounds(const Fbox& box, const Fmatrix& world,
	const Fvector& origin, const Fvector& axis, float& lower, float& upper, Fbox& box_w,
	Fvector* corners_w)
{
	lower = 1e9f;
	upper = -1e9f;
	box_w.invalidate();
	for (u32 corner = 0; corner < 8; ++corner)
	{
		Fvector local;
		local.set((corner & 1) ? box.max.x : box.min.x,
			(corner & 2) ? box.max.y : box.min.y,
			(corner & 4) ? box.max.z : box.min.z);
		Fvector point;
		world.transform_tiny(point, local);
		Fvector offset;
		offset.sub(point, origin);
		const float axial = offset.dotproduct(axis);
		if (!_valid(axial))
			return false;
		lower = _min(lower, axial);
		upper = _max(upper, axial);
		box_w.modify(point);
		corners_w[corner] = point;
	}
	return lower <= upper;
}

// axial depth where a posed box first clears every side plane forward of the apex, a lower bound
// on the nearest visible point, negative when no forward part of the box satisfies some plane
static float svp_cone_entry(const CFrustum& F, const Fvector* corner,
	const Fvector& origin, const Fvector& axis, float apex)
{
	// box corner bits are x y z, an edge joins the two corners differing in one bit
	static const u8 edge[12][2] = {
		{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
		{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};
	float axial[8];
	float entry = -1.f;
	for (int pi = 0; pi < F.p_count; ++pi)
	{
		const CFrustum::fplane& P = F.planes[pi];
		float cls[8];
		float best = flt_max;
		for (u32 c = 0; c < 8; ++c)
		{
			cls[c] = P.classify(corner[c]);
			Fvector o;
			o.sub(corner[c], origin);
			axial[c] = o.dotproduct(axis);
			// the aabb test treats a positive classify as outside, behind the apex every plane
			// alone admits mirror points that are not visibility so those are clamped out
			if (cls[c] <= 0.f && axial[c] >= apex)
				best = _min(best, axial[c]);
		}
		for (u32 e = 0; e < 12; ++e)
		{
			const u32 c0 = edge[e][0], c1 = edge[e][1];
			// an inside region crossing the apex plane is visible exactly at the apex
			if ((axial[c0] < apex) != (axial[c1] < apex))
			{
				const float span = axial[c1] - axial[c0];
				if (_abs(span) > EPS_S)
				{
					const float s = (apex - axial[c0]) / span;
					if (cls[c0] + s * (cls[c1] - cls[c0]) <= 0.f)
						best = _min(best, apex);
				}
			}
			const float a = cls[c0];
			const float b = cls[c1];
			if ((a <= 0.f) == (b <= 0.f))
				continue;
			const float den = a - b;
			if (_abs(den) < EPS_S)
				continue;
			Fvector d;
			d.sub(corner[c1], corner[c0]);
			Fvector x;
			x.mad(corner[c0], d, a / den);
			Fvector o;
			o.sub(x, origin);
			const float t = o.dotproduct(axis);
			if (t >= apex)
				best = _min(best, t);
		}
		if (best >= flt_max)
			return -1.f; // no forward part satisfies this plane, the box is not visible
		entry = _max(entry, best); // the deepest per-plane entry bounds the real one from below
	}
	return _valid(entry) ? entry : -1.f;
}

static bool svp_objective_hud_filter_ready()
{
	extern int scope_svp_enabled;
	extern int ps_r__svp_optic_body_suppress;
	auto& vp = Device.m_SecondViewport;
	return ps_r__svp_optic_body_suppress && scope_svp_enabled >= 2
		&& Device.true_pip_on && vp.IsSVPActive()
		&& vp.svp_camera_domain == CSecondVPParams::camera_objective
		&& vp.SnapshotExact(vp.svp_camera_frame, vp.svp_camera_session, Device.dwFrame)
		&& vp.eyepiece.radius > EPS && vp.objective.radius > EPS;
}

void CDSGraphManager::svp_classify_objective_hud(dxRender_Visual* visual, Fmatrix* matrix,
	u8 role, SSvpHudAdmission& admission)
{
	admission = SSvpHudAdmission();
	if (!svp_objective_hud_filter_ready() || !visual || !matrix)
		return;

	auto& vp = Device.m_SecondViewport;
	const Fvector origin = vp.eyepiece.m_W.c;
	Fvector axis;
	axis.sub(vp.objective.m_W.c, origin);
	const float tube = axis.magnitude();
	if (!_valid(tube) || tube <= EPS)
	{
		admission.reason = "tube";
		return;
	}
	axis.div(tube);
	admission.objective = tube;

	Fmatrix world = *svp_pose_of(matrix);
	if (CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(visual))
	{
		Fmatrix bone;
		if (svp_lens_bone_of(visual, bone) || skeleton->SVP_LensBoneXform(bone))
			world.mulB_43(bone);
	}

	Fvector center;
	world.transform_tiny(center, visual->vis.sphere.P);
	Fvector offset;
	offset.sub(center, origin);
	admission.axial = offset.dotproduct(axis);
	Fvector on_axis;
	on_axis.mad(origin, axis, admission.axial);
	admission.radial = on_axis.distance_to(center);
	admission.radius = visual->vis.sphere.R;
	// screen frame lateral offsets name each candidate clock direction in the dump
	{
		Fvector sr;
		sr.crossproduct(Device.vCameraTop, axis);
		if (sr.square_magnitude() > EPS_S)
		{
			sr.normalize();
			Fvector su;
			su.crossproduct(axis, sr);
			su.normalize_safe();
			Fvector lat;
			lat.sub(center, on_axis);
			admission.dir_right = lat.dotproduct(sr);
			admission.dir_up = lat.dotproduct(su);
		}
	}

	const bool role_ok = role == IDSGraphManager::hud_primary_item
		|| role == IDSGraphManager::hud_optic;
	const bool size_ok = _valid(admission.radius) && admission.radius > EPS
		&& admission.radius < tube * 1.75f;
	const float body_radius = std::max(vp.eyepiece.radius, vp.objective.radius) * 1.75f + 0.01f;
	const bool wrap = admission.radial < admission.radius * 0.6f
		&& admission.radius < tube * 0.3f;
	const bool axis_overlap = _valid(admission.radial)
		&& (admission.radial < body_radius || wrap);
	const bool bounds_ok = svp_axial_bounds(visual->vis.box, world, origin, axis,
		admission.axial_lo, admission.axial_hi, admission.box_w, admission.corners_w);
	admission.box_valid = bounds_ok;
	const float plane_epsilon = 0.001f;
	const bool contains_objective = bounds_ok
		&& admission.axial_lo <= tube + plane_epsilon
		&& admission.axial_hi >= tube - plane_epsilon;
	const bool local = admission.axial > -0.6f * tube
		&& admission.axial < 1.4f * tube;

	admission.candidate = role_ok && size_ok && axis_overlap && bounds_ok;
	admission.reject = admission.candidate && local && contains_objective;
	admission.forward = admission.candidate
		&& admission.axial_lo > tube + plane_epsilon;
	if (admission.reject)
		admission.reason = "objective-body";
	else if (admission.forward)
		admission.reason = "forward";
	else if (!role_ok)
		admission.reason = "role";
	else if (!size_ok)
		admission.reason = "size";
	else if (!axis_overlap)
		admission.reason = "axis";
	else if (!bounds_ok)
		admission.reason = "bounds";
	else if (!local)
		admission.reason = "range";
	else
		admission.reason = "plane-miss";
}

// pip drain only the weapon list into the scope image, the caller owns the view/projection
void CDSGraphManager::r_dsgraph_render_hud_svp()
{
	PROF_EVENT("r_dsgraph_render_hud_svp");
	// same camera and projection as the world, real depth so the scene occludes the weapon
	// sliver and vice versa (rmNear depth-fronting belonged to the retired separate camera)
	RImplementation.rmNormal();
	auto& graph = RGraph.mapHUD;
	// an empty weapon list still has to publish, otherwise the camera reads stale and floors itself
	if (!graph.empty() || svp_objective_hud_filter_ready())
	{
		std::sort(graph.begin(), graph.end());
		const auto& vp = Device.m_SecondViewport;
		Fvector ax;
		ax.sub(vp.objective.m_W.c, vp.eyepiece.m_W.c);
		const float len2 = ax.square_magnitude();
		const float tube = _sqrt(len2);
		// the near measure runs from the live camera plane, a clip-on front sits past the objective
		const float measure_front = _max(tube, vp.svp_front_use_m);
		const bool objective_filter = svp_objective_hud_filter_ready();
		u32 admit_candidates = 0;
		u32 admit_suppressed = 0;
		u32 admit_clipon_unresolved = 0;
		float clipon_axial = -1.f;
		extern int ps_r__svp_cop_diag;
		static u32 s_hud_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_hud_diag_ms > 3000);
		const bool relative_diag = ps_r__svp_cop_diag >= 2
			&& vp.svp_lens_frame == Device.dwFrame
			&& vp.SnapshotExact(vp.svp_camera_frame, vp.svp_camera_session, Device.dwFrame)
			&& vp.svp_lens_root && vp.svp_lens_visual;
		SSvpRelativeProbe relative_probe;
		if (diag)
		{
			s_hud_diag_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-HUD] tube=%.1fcm items=%u objectiveFilter=%d",
				tube * 100.f, (u32)graph.size(), objective_filter ? 1 : 0);
		}
		extern int ps_r__svp_stats;
		extern u32 svp_stats_hud_cull_reject;
		// nearest drawn weapon extent in front of the objective, the near-plane derive reads it next frame
		// svp_cull_reject exempts posed items so the scope frustum is tested here or the barrel never leaves
		// -1 is the sentinel for nothing ahead, 0 means geometry reaches the objective plane
		float min_axial = -1.f;
		CFrustum cone;
		bool cone_ok = false;
		if (objective_filter
			&& Device.m_SecondViewport.SnapshotExact(Device.m_SecondViewport.svp_camera_frame,
				Device.m_SecondViewport.svp_camera_session, Device.dwFrame))
		{
			Fmatrix full;
			full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
			cone.CreateFromMatrix(full, FRUSTUM_P_LRTB);
			cone_ok = true;
		}
		// any part ahead of the plane measures, and the depth used is where the cone actually
		// reaches the geometry, so a barrel under the optical axis measures further out as zoom rises
		const Fvector cone_origin = vp.eyepiece.m_W.c;
		Fvector cone_axis = ax;
		if (tube > EPS)
			cone_axis.div(tube);
		u32 measured_bones = 0;
		u32 axis_bones = 0;
		static u32 s_neardump_ms = 0;
		const bool near_dump = ps_r__svp_cop_diag >= 2 && cone_ok
			&& Device.dwTimeGlobal - s_neardump_ms > 1000;
		if (near_dump)
			s_neardump_ms = Device.dwTimeGlobal;
		auto fold_near = [&](const Fvector* corners, float lo, float hi)
		{
			if (hi <= measure_front)
				return -1.f;
			const float ahead = _max(lo - measure_front, 0.f);
			// a negative entry means no forward part of the box is visible, it constrains nothing
			const float entry = svp_cone_entry(cone, corners, cone_origin, cone_axis, measure_front);
			if (entry < 0.f)
				return -1.f;
			const float v = _max(ahead, entry - measure_front);
			if (!_valid(v))
				return -1.f;
			min_axial = (min_axial < 0.f) ? v : _min(min_axial, v);
			return v;
		};
		// pip a bone box the sight axis passes through ahead of the plane is housing around the
		// camera not image content, it must not floor the near measure
		auto axis_through_box = [&](const Fvector* c8)
		{
			Fmatrix box;
			box.identity();
			box.i.sub(c8[1], c8[0]); box.i.mul(0.5f);
			box.j.sub(c8[2], c8[0]); box.j.mul(0.5f);
			box.k.sub(c8[4], c8[0]); box.k.mul(0.5f);
			box.c.add(c8[0], c8[7]); box.c.mul(0.5f);
			Fmatrix inv;
			if (!inv.invert_b(box))
				return true;
			Fvector o, d;
			inv.transform_tiny(o, cone_origin);
			inv.transform_dir(d, cone_axis);
			float t0 = measure_front, t1 = flt_max;
			for (u32 i = 0; i < 3; ++i)
			{
				const float oc = (&o.x)[i];
				const float dc = (&d.x)[i];
				if (_abs(dc) < 1e-6f)
				{
					if (_abs(oc) > 1.f)
						return false;
					continue;
				}
				float ta = (-1.f - oc) / dc;
				float tb = (1.f - oc) / dc;
				if (ta > tb)
				{
					const float tt = ta;
					ta = tb;
					tb = tt;
				}
				t0 = _max(t0, ta);
				t1 = _min(t1, tb);
				if (t0 > t1)
					return false;
			}
			return true;
		};
		// verts stand in for the boxes the axis disqualifies, the hud assembly is rigid to the
		// scope for a session so the angles hold between rebuilds
		auto mesh_near_table = [&](CSkeletonX* sk, dxRender_Visual* V, const Fmatrix& pose,
			const xr_vector<u8>& through) -> const SSvpMeshNearTable&
		{
			static u32 s_mesh_session = 0;
			if (s_mesh_session != vp.GetSVPSession())
			{
				s_mesh_session = vp.GetSVPSession();
				s_svp_mesh_near.clear();
			}
			SSvpMeshNearTable& t = s_svp_mesh_near[V];
			if (t.built && t.session == vp.GetSVPSession() && t.epoch == vp.svp_optic_epoch)
				return t;
			t.session = vp.GetSVPSession();
			t.epoch = vp.svp_optic_epoch;
			t.built = true;
			t.entries.clear();
			xr_vector<Fvector> verts;
			sk->SVP_MeshVertsWorld(pose, through, verts);
			t.entries.reserve(verts.size());
			const Fvector apex = vp.objective.m_W.c;
			for (const Fvector& w : verts)
			{
				Fvector o;
				o.sub(w, apex);
				const float axial = o.dotproduct(cone_axis);
				if (!(axial > EPS))
					continue;
				Fvector radial;
				radial.mad(o, cone_axis, -axial);
				const float tan_t = radial.magnitude() / axial;
				if (_valid(tan_t) && _valid(axial))
					t.entries.push_back({ tan_t, axial });
			}
			std::sort(t.entries.begin(), t.entries.end(),
				[](const SSvpMeshNearEntry& l, const SSvpMeshNearEntry& r) { return l.tan_theta < r.tan_theta; });
			float run = flt_max;
			for (SSvpMeshNearEntry& e : t.entries)
			{
				run = _min(run, e.axial_min);
				e.axial_min = run;
			}
			return t;
		};
		// the running minimum at the cone edge is the nearest vert the current fov can see
		auto mesh_near_query = [&](const SSvpMeshNearTable& t) -> float
		{
			if (t.entries.empty())
				return -1.f;
			const Fmatrix& P = Device.matrices[1].mProject;
			if (!(P._11 > EPS) || !(P._22 > EPS))
				return -1.f;
			const float tx = 1.f / P._11;
			const float ty = 1.f / P._22;
			const float tan_half = _sqrt(tx * tx + ty * ty);
			u32 lo2 = 0, hi2 = (u32)t.entries.size();
			while (lo2 < hi2)
			{
				const u32 mid = (lo2 + hi2) / 2;
				if (t.entries[mid].tan_theta < tan_half)
					lo2 = mid + 1;
				else
					hi2 = mid;
			}
			if (lo2 == 0)
				return -1.f;
			const float best = t.entries[lo2 - 1].axial_min;
			return (_valid(best) && best < flt_max) ? _max(best, 0.f) : -1.f;
		};
		auto measure_near = [&](dxRender_Visual* V, Fmatrix* M, const SSvpHudAdmission& a, u8 role)
		{
			if (!a.box_valid || !cone_ok || a.axial_hi <= tube)
				return;
			LPCSTR rn = near_dump ? svp_hud_role_name(role) : "";
			float bb[6] = { a.box_w.min.x, a.box_w.min.y, a.box_w.min.z,
				a.box_w.max.x, a.box_w.max.y, a.box_w.max.z };
			u32 mask = 0xff;
			if (cone.testAABB(bb, mask) == fcvNone)
				return;
			// a one piece weapon aabb contains the optical axis so it always measures zero, its
			// bones do not, the on-axis body sits behind the lens plane and drains away
			CSkeletonX* sk = (V && M) ? fast_dynamic_cast<CSkeletonX*>(V) : nullptr;
			u32 used = 0;
			if (sk)
			{
				const Fmatrix& pose = *svp_pose_of(M);
				const u16 count = sk->SVP_MeasureBoneCount();
				Fvector bc[8];
				xr_vector<u8> through;
				for (u16 b = 0; b < count; ++b)
				{
					if (!sk->SVP_BoneCornersWorld(b, pose, bc))
						continue;
					float lo = flt_max, hi = -flt_max;
					for (u32 c = 0; c < 8; ++c)
					{
						Fvector o;
						o.sub(bc[c], cone_origin);
						const float d = o.dotproduct(cone_axis);
						lo = _min(lo, d);
						hi = _max(hi, d);
					}
					if (hi <= tube)
					{
						// a behind verdict from a lying obb hides forward children, the verts arbitrate
						if (through.empty())
							through.resize(count, 0);
						through[b] = 1;
						continue;
					}
					if (axis_through_box(bc))
					{
						++axis_bones;
						if (through.empty())
							through.resize(count, 0);
						through[b] = 1;
						if (near_dump)
							PipMsg("[SVP-NEARDUMP] %s vis=%p bone=%u lo=%.3f hi=%.3f axis mesh", rn, (void*)V, b, lo - tube, hi - tube);
						continue;
					}
					const float fv = fold_near(bc, lo, hi);
					if (fv >= 0.f)
						++used;
					else
					{
						// a bone obb can undercover the rigid children riding it, the verts arbitrate
						if (through.empty())
							through.resize(count, 0);
						through[b] = 1;
					}
					if (near_dump)
						PipMsg("[SVP-NEARDUMP] %s vis=%p bone=%u lo=%.3f hi=%.3f v=%.3f", rn, (void*)V, b, lo - tube, hi - tube, fv);
				}
				if (!through.empty())
				{
					const SSvpMeshNearTable& t = mesh_near_table(sk, V, pose, through);
					const float mv = mesh_near_query(t);
					if (mv >= 0.f)
					{
						min_axial = (min_axial < 0.f) ? mv : _min(min_axial, mv);
						++used;
					}
					if (near_dump)
						PipMsg("[SVP-NEARDUMP] %s vis=%p mesh verts=%u v=%.3f", rn, (void*)V, (u32)t.entries.size(), mv);
				}
			}
			// a posed skeleton speaks through its bones, the whole box serves boneless visuals only
			// and never when the sight axis passes through it
			if (!sk && !axis_through_box(a.corners_w))
			{
				const float fv = fold_near(a.corners_w, a.axial_lo, a.axial_hi);
				if (near_dump)
					PipMsg("[SVP-NEARDUMP] %s vis=%p wholebox lo=%.3f hi=%.3f v=%.3f", rn, (void*)V, a.axial_lo - tube, a.axial_hi - tube, fv);
			}
			measured_bones += used;
		};
		for (auto& item : graph)
		{
			if (svp_cull_reject(item.pVisual, svp_pose_of(item.pMatrix)))
			{
				if (ps_r__svp_stats)
					++svp_stats_hud_cull_reject;
				continue;
			}
			dxRender_Visual* V = item.pVisual;
			VERIFY(V && V->shader._get());
			bool drop = false;
			float t = 0.f, rad = -1.f;
			float axial_lo = 0.f, axial_hi = 0.f;
			SSvpHudAdmission objective_admission;
			LPCSTR admission = "outside";
			if (objective_filter)
			{
				svp_classify_objective_hud(V, item.pMatrix, item.hud_role, objective_admission);
				t = tube > EPS ? objective_admission.axial / tube : 0.f;
				rad = objective_admission.radial;
				axial_lo = objective_admission.axial_lo;
				axial_hi = objective_admission.axial_hi;
				drop = objective_admission.reject;
				admission = objective_admission.reason;
				if (objective_admission.candidate)
					++admit_candidates;
				if (objective_admission.reject)
					++admit_suppressed;
				if (objective_admission.forward)
				{
					++admit_clipon_unresolved;
					if (objective_admission.axial_hi > clipon_axial)
						clipon_axial = objective_admission.axial_hi;
				}
			}
			if (diag)
			{
				auto tx = V->GetTexture();
				if (objective_filter)
					PipMsg("[SVP-ADMIT] %s role=%s t=%.2f rad=%.1fcm dir=(%.1f,%.1f)cm R=%.1fcm axial=[%.1f,%.1f]cm objective=%.1fcm reason=%s visual=%p root=%p lensVisual=%p lensRoot=%p %s",
						drop ? "SUPPRESS" : "retain", svp_hud_role_name(item.hud_role), t,
						rad * 100.f, objective_admission.dir_right * 100.f,
						objective_admission.dir_up * 100.f,
						V->vis.sphere.R * 100.f, axial_lo * 100.f, axial_hi * 100.f,
						tube * 100.f, admission, (void*)V, (void*)item.pMatrix,
						vp.svp_lens_visual, vp.svp_lens_root,
						tx ? tx->cName.c_str() : "?");
				else
					PipMsg("[SVP-HUD] keep t=%.2f rad=%.1fcm R=%.1fcm %s",
						t, rad * 100.f, V->vis.sphere.R * 100.f, tx ? tx->cName.c_str() : "?");
			}
			if (drop) continue;
			// only geometry the scope can actually see may raise the near plane, and only the part
			// ahead of the objective, anything behind it is past the camera already
			measure_near(V, item.pMatrix, objective_admission, item.hud_role);
			if (relative_diag && item.hud_role == hud_primary_item
				&& V != vp.svp_lens_visual)
			{
				Fmatrix root_world = *svp_pose_of(item.pMatrix);
				Fmatrix point_world = root_world;
				CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(V);
				bool snapshot = false;
				bool comparable = skeleton != nullptr;
				if (comparable)
				{
					Fmatrix bone;
					if (skeleton->SVP_RigidBoneSnapshotXform(bone))
					{
						point_world.mulB_43(bone);
						snapshot = true;
					}
					else
						comparable = false;
				}
				Fvector point;
				point_world.transform_tiny(point, V->vis.sphere.P);
				Fvector camera;
				if (comparable && svp_basis_coords(vp.svp_cam_pos, vp.svp_right, vp.svp_up,
					vp.svp_fwd, point, camera) && camera.z > EPS
					&& camera.z > relative_probe.forward)
				{
					Fvector lens;
					Fvector root_lens;
					if (svp_basis_coords(vp.eyepiece.m_W.c, vp.eyepiece.m_W.i,
						vp.eyepiece.m_W.j, vp.eyepiece.m_W.k, point, lens)
						&& svp_basis_coords(vp.eyepiece.m_W.c, vp.eyepiece.m_W.i,
							vp.eyepiece.m_W.j, vp.eyepiece.m_W.k, root_world.c, root_lens)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.i, relative_probe.axis_i)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.j, relative_probe.axis_j)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.k, relative_probe.axis_k))
					{
						relative_probe.visual = V;
						relative_probe.root = item.pMatrix;
						relative_probe.owner = skeleton ? skeleton->SVP_SkeletonOwner() : nullptr;
						relative_probe.camera = camera;
						relative_probe.lens = lens;
						relative_probe.root_lens = root_lens;
						relative_probe.forward = camera.z;
						relative_probe.role = item.hud_role;
						relative_probe.skinned = skeleton != nullptr;
						relative_probe.snapshot = snapshot;
						relative_probe.valid = true;
					}
				}
			}
			RCache.set_Element(item.pSE);
			RCache.set_xform_world(*svp_pose_of(item.pMatrix));
			RImplementation.apply_object(item.pObject);
			RImplementation.apply_lmaterial();
			V->Render(svp_drain_lod(item.ssa, V->vis.sphere.R));
#if RENDER == R_R4
			extern void svp_objective_hud_note_draw(u8);
			svp_objective_hud_note_draw(item.hud_role);
#endif
		}
		// reflex lenses project through matrices[1] too, they draw later in the frame from a map the
		// main combine clears at its tail, so this walk still sees them
#ifdef USE_DX11
		if (cone_ok)
		{
			for (auto& n : RGraph.mapReflexHUDSorted)
			{
				if (!n.pVisual || !n.pMatrix)
					continue;
				if (svp_cull_reject(n.pVisual, svp_pose_of(n.pMatrix)))
					continue;
				SSvpHudAdmission ra;
				svp_classify_objective_hud(n.pVisual, n.pMatrix, n.hud_role, ra);
				// the same objective body suppression the main walk applies, a rejected lens is the
				// on axis thing that must never floor the measure
				if (ra.reject)
					continue;
				measure_near(n.pVisual, n.pMatrix, ra, n.hud_role);
			}
		}
#endif
		if (near_dump)
			PipMsg("[SVP-NEARDUMP] frame min=%.3f bones=%u skip=%u", min_axial, measured_bones, axis_bones);
		// publish for the next frame's objective camera, stamped so a gap or a new optic fails safe
		// -1 means the drain ran and saw nothing ahead of the objective, not missing data
		if (objective_filter && cone_ok)
		{
			auto& bus = Device.m_SecondViewport;
			bus.svp_hud_min_axial = min_axial;
			bus.svp_clipon_axial = clipon_axial;
			bus.svp_hud_min_bones = measured_bones;
			bus.svp_hud_axis_skip = axis_bones;
			bus.svp_hud_min_frame = Device.dwFrame;
			bus.svp_hud_min_session = bus.GetSVPSession();
			bus.svp_hud_min_epoch = bus.svp_optic_epoch;
		}
		if (relative_diag && relative_probe.valid)
		{
			static dxRender_Visual* s_visual = nullptr;
			static const void* s_root = nullptr;
			static const void* s_owner = nullptr;
			static const void* s_lens_root = nullptr;
			static const void* s_lens_visual = nullptr;
			static const void* s_lens_owner = nullptr;
			static u32 s_session = 0;
			static u32 s_epoch = 0;
			static u32 s_log_ms = 0;
			static Fvector s_camera = {};
			static Fvector s_lens = {};
			static Fvector s_root_lens = {};
			static Fvector s_axis_i = {};
			static Fvector s_axis_j = {};
			static Fvector s_axis_k = {};
			static bool s_valid = false;

			const bool same = s_valid && s_visual == relative_probe.visual
				&& s_root == relative_probe.root
				&& s_owner == relative_probe.owner
				&& s_lens_root == vp.svp_lens_root
				&& s_lens_visual == vp.svp_lens_visual
				&& s_lens_owner == vp.svp_lens_owner
				&& s_session == vp.GetSVPSession()
				&& s_epoch == vp.svp_camera_epoch;
			const bool linked = same && Device.dwTimeGlobal - s_log_ms <= 500;
			if (!linked || Device.dwTimeGlobal - s_log_ms > 250)
			{
				const u32 dt_ms = linked ? Device.dwTimeGlobal - s_log_ms : 0;
				Fvector camera_delta = {};
				Fvector lens_delta = {};
				Fvector root_delta = {};
				float angle_delta = 0.f;
				if (linked)
				{
					camera_delta.sub(relative_probe.camera, s_camera);
					lens_delta.sub(relative_probe.lens, s_lens);
					root_delta.sub(relative_probe.root_lens, s_root_lens);
					const float relative_trace =
						relative_probe.axis_i.dotproduct(s_axis_i)
						+ relative_probe.axis_j.dotproduct(s_axis_j)
						+ relative_probe.axis_k.dotproduct(s_axis_k);
					const float angle_cos = (relative_trace - 1.f) * 0.5f;
					angle_delta = rad2deg(acosf(std::clamp(angle_cos, -1.f, 1.f)));
				}
				const int same_owner = relative_probe.owner == vp.svp_lens_owner ? 1 : 0;
				auto texture = relative_probe.visual->GetTexture();
				PipMsg("[SVP-REL] probe=forward-primary seed=%d dt_ms=%u pointLens=(%.5f,%.5f,%.5f) dPoint=(%.5f,%.5f,%.5f) axisK=(%.5f,%.5f,%.5f) dAngleDeg=%.5f rootLens=(%.5f,%.5f,%.5f) dRoot=(%.5f,%.5f,%.5f) camera=(%.5f,%.5f,%.5f) dCamera=(%.5f,%.5f,%.5f) sameRoot=%d sameOwner=%d skinned=%d snapshot=%d root=%p lensRoot=%p visual=%p lensVisual=%p role=%s texture=%s session=%u epoch=%u frame=%u",
					linked ? 0 : 1, dt_ms,
					relative_probe.lens.x, relative_probe.lens.y, relative_probe.lens.z,
					lens_delta.x, lens_delta.y, lens_delta.z,
					relative_probe.axis_k.x, relative_probe.axis_k.y, relative_probe.axis_k.z,
					angle_delta,
					relative_probe.root_lens.x, relative_probe.root_lens.y, relative_probe.root_lens.z,
					root_delta.x, root_delta.y, root_delta.z,
					relative_probe.camera.x, relative_probe.camera.y, relative_probe.camera.z,
					camera_delta.x, camera_delta.y, camera_delta.z,
					relative_probe.root == vp.svp_lens_root ? 1 : 0, same_owner,
					relative_probe.skinned ? 1 : 0, relative_probe.snapshot ? 1 : 0,
					(void*)relative_probe.root, vp.svp_lens_root,
					(void*)relative_probe.visual, vp.svp_lens_visual,
					svp_hud_role_name(relative_probe.role),
					texture ? texture->cName.c_str() : "?",
					vp.GetSVPSession(), vp.svp_camera_epoch, Device.dwFrame);
				s_log_ms = Device.dwTimeGlobal;
				s_visual = relative_probe.visual;
				s_root = relative_probe.root;
				s_owner = relative_probe.owner;
				s_lens_root = vp.svp_lens_root;
				s_lens_visual = vp.svp_lens_visual;
				s_lens_owner = vp.svp_lens_owner;
				s_session = vp.GetSVPSession();
				s_epoch = vp.svp_camera_epoch;
				s_camera = relative_probe.camera;
				s_lens = relative_probe.lens;
				s_root_lens = relative_probe.root_lens;
				s_axis_i = relative_probe.axis_i;
				s_axis_j = relative_probe.axis_j;
				s_axis_k = relative_probe.axis_k;
				s_valid = true;
			}
		}
		else if (relative_diag)
		{
			static u32 s_unavailable_ms = 0;
			if (Device.dwTimeGlobal - s_unavailable_ms > 1000)
			{
				s_unavailable_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-REL] probe=forward-primary unavailable reason=no-comparable-rigid-primary root=%p owner=%p session=%u epoch=%u frame=%u",
					vp.svp_lens_root, vp.svp_lens_owner,
					vp.GetSVPSession(), vp.svp_camera_epoch, Device.dwFrame);
			}
		}
		if (diag && objective_filter)
			PipMsg("[SVP-ADMIT] summary candidates=%u suppressed=%u clipon=%u cliponExtent=%.1fcm front=%.1fcm frame=%u session=%u",
				admit_candidates, admit_suppressed, admit_clipon_unresolved,
				clipon_axial * 100.f,
				vp.svp_front_use_m * 100.f, vp.svp_camera_frame, vp.svp_camera_session);
		graph.clear();
	}
	RCache.set_xform_world(Fidentity);
	RImplementation.rmNormal();
}
