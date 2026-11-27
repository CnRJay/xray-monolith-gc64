// occ_engine_moc.cpp: Intel masked occlusion culling behind the engine seam

#include "stdafx.h"
#include "occ_engine.h"
#include "occRasterizer.h"
#include "svp_console.h"
#include "../../3rd party/MaskedOcclusionCulling/MaskedOcclusionCulling.h"

// the batch is packed Fvector, the vertex layout below depends on it
static_assert(sizeof(Fvector) == 12, "moc vertex layout expects a packed 3 float Fvector");

namespace
{
	const u32 s_preset_w[4] = { 256, 384, 512, 768 };
	const u32 s_preset_h[4] = { 144, 216, 288, 432 };

	ICF int clamp_preset(int preset)
	{
		if (preset < 0) return 0;
		if (preset > 3) return 3;
		return preset;
	}

	class occ_moc final : public IOccEngine
	{
	public:
		void begin_frame(const Fmatrix& full, const Fvector& cop, float near_w) override;
		bool emit(occTri& T, const Fvector* verts) override;
		void end_frame() override;

		BOOL test_box(const Fbox& world) override;
		BOOL test_poly(const Fvector* v, u32 n) override;

		bool wants_skip_filter() override { return false; }

		void reserve(u32 tris);
		void set_res(int preset) { m_res_want = clamp_preset(preset); }
		void release();

	private:
		void grow_index(u32 verts);

		MaskedOcclusionCulling* m_moc = nullptr;
		Fmatrix m_full;
		float m_near = 0.f;
		int m_res_want = 1;
		int m_res_applied = -1;
		bool m_ready = false; // an end_frame has filled the buffer since the level loaded
		u32 m_probe_countdown = 0;
		xr_vector<Fvector> m_batch;
		xr_vector<u32> m_index;
		xr_vector<float> m_probe;
	};

	occ_moc& instance()
	{
		static occ_moc s_moc;
		return s_moc;
	}

	void occ_moc::reserve(u32 tris)
	{
		m_batch.reserve(size_t(tris) * 3);
		grow_index(tris * 3);
		m_ready = false;
	}

	void occ_moc::release()
	{
		if (m_moc)
		{
			MaskedOcclusionCulling::Destroy(m_moc);
			m_moc = nullptr;
		}
		m_res_applied = -1;
		m_ready = false;
		m_batch.clear();
	}

	void occ_moc::grow_index(u32 verts)
	{
		const size_t had = m_index.size();
		if (verts <= had)
			return;
		m_index.resize(verts);
		for (size_t i = had; i < m_index.size(); ++i)
			m_index[i] = u32(i);
	}

	void occ_moc::begin_frame(const Fmatrix& full, const Fvector& cop, float near_w)
	{
		(void)cop;

		if (!m_moc)
		{
			// picks the best implementation the running cpu supports
			m_moc = MaskedOcclusionCulling::Create();
			m_res_applied = -1;
			m_ready = false;
		}
		if (!m_moc)
			return;

		if (m_res_applied != m_res_want)
		{
			m_res_applied = m_res_want;
			m_moc->SetResolution(s_preset_w[m_res_applied], s_preset_h[m_res_applied]);
			m_ready = false;
		}

		m_full.set(full);
		m_near = near_w;
		m_moc->SetNearClipPlane(near_w);
		m_moc->ClearBuffer();
		m_batch.clear();
	}

	bool occ_moc::emit(occTri& T, const Fvector* verts)
	{
		(void)T;
		m_batch.push_back(verts[0]);
		m_batch.push_back(verts[1]);
		m_batch.push_back(verts[2]);
		return true;
	}

	void occ_moc::end_frame()
	{
		if (!m_moc)
			return;

		const u32 nverts = u32(m_batch.size());
		if (nverts >= 3)
		{
			grow_index(nverts);
			const MaskedOcclusionCulling::CullingResult ret =
				m_moc->RenderTriangles((const float*)&m_batch[0], &m_index[0], int(nverts / 3),
					(const float*)&m_full,
					MaskedOcclusionCulling::BACKFACE_NONE,
					MaskedOcclusionCulling::CLIP_PLANE_ALL,
					MaskedOcclusionCulling::VertexLayout(12, 4, 8));
			svp_stats_moc_ret = u32(ret) + 1;
		}
		m_ready = true;

		// throttled occupancy probe, answers whether the occluders ever reach the buffer
		if (ps_r__svp_stats >= 2 && nverts >= 3)
		{
			if (0 == m_probe_countdown)
			{
				m_probe_countdown = 300;
				const u32 w = s_preset_w[m_res_applied];
				const u32 h = s_preset_h[m_res_applied];
				m_probe.resize(size_t(w) * h);
				m_moc->ComputePixelDepthBuffer(&m_probe[0], false);
				u32 filled = 0;
				for (size_t i = 0; i < m_probe.size(); ++i)
					if (m_probe[i] > 0.f) ++filled;
				svp_stats_moc_fill_pct = u32(filled * 100ull / m_probe.size());
				Msg("* [MOC] ret %u tris %u filled %u of %u px", svp_stats_moc_ret - 1, nverts / 3, filled, u32(m_probe.size()));
			}
			else
				--m_probe_countdown;
		}
	}

	BOOL occ_moc::test_box(const Fbox& world)
	{
		if (!m_moc || !m_ready)
			return TRUE;

		// pass one, every corner in clip space, one at or behind the near plane answers visible
		float cw[8];
		bool behind = false;
		for (int i = 0; i < 8; ++i)
		{
			const float x = (i & 1) ? world.max.x : world.min.x;
			const float y = (i & 2) ? world.max.y : world.min.y;
			const float z = (i & 4) ? world.max.z : world.min.z;
			cw[i] = x * m_full._14 + y * m_full._24 + z * m_full._34 + m_full._44;
			behind |= (cw[i] <= m_near + EPS);
		}
		if (behind)
			return TRUE;

		// pass two, the ndc rect over all eight corners
		float xmin = flt_max, xmax = -flt_max, ymin = flt_max, ymax = -flt_max, wmin = flt_max;
		for (int i = 0; i < 8; ++i)
		{
			const float x = (i & 1) ? world.max.x : world.min.x;
			const float y = (i & 2) ? world.max.y : world.min.y;
			const float z = (i & 4) ? world.max.z : world.min.z;
			const float iw = 1.f / cw[i];
			const float nx = (x * m_full._11 + y * m_full._21 + z * m_full._31 + m_full._41) * iw;
			const float ny = (x * m_full._12 + y * m_full._22 + z * m_full._32 + m_full._42) * iw;
			xmin = _min(xmin, nx); xmax = _max(xmax, nx);
			ymin = _min(ymin, ny); ymax = _max(ymax, ny);
			wmin = _min(wmin, cw[i]);
		}

		// clear of the ndc box on either axis, the tile math never reports that
		if (xmax < -1.f || xmin > 1.f || ymax < -1.f || ymin > 1.f)
			return TRUE;

		return (MaskedOcclusionCulling::OCCLUDED == m_moc->TestRect(xmin, ymin, xmax, ymax, wmin)) ? FALSE : TRUE;
	}

	BOOL occ_moc::test_poly(const Fvector* v, u32 n)
	{
		if (!m_moc || !m_ready || 0 == n)
			return TRUE;

		bool behind = false;
		for (u32 i = 0; i < n; ++i)
		{
			const float w = v[i].x * m_full._14 + v[i].y * m_full._24 + v[i].z * m_full._34 + m_full._44;
			behind |= (w <= m_near + EPS);
		}
		if (behind)
			return TRUE;

		float xmin = flt_max, xmax = -flt_max, ymin = flt_max, ymax = -flt_max, wmin = flt_max;
		for (u32 i = 0; i < n; ++i)
		{
			const float w = v[i].x * m_full._14 + v[i].y * m_full._24 + v[i].z * m_full._34 + m_full._44;
			const float iw = 1.f / w;
			const float nx = (v[i].x * m_full._11 + v[i].y * m_full._21 + v[i].z * m_full._31 + m_full._41) * iw;
			const float ny = (v[i].x * m_full._12 + v[i].y * m_full._22 + v[i].z * m_full._32 + m_full._42) * iw;
			xmin = _min(xmin, nx); xmax = _max(xmax, nx);
			ymin = _min(ymin, ny); ymax = _max(ymax, ny);
			wmin = _min(wmin, w);
		}

		if (xmax < -1.f || xmin > 1.f || ymax < -1.f || ymin > 1.f)
			return TRUE;

		return (MaskedOcclusionCulling::OCCLUDED == m_moc->TestRect(xmin, ymin, xmax, ymax, wmin)) ? FALSE : TRUE;
	}
}

IOccEngine& occ_engine_moc()
{
	return instance();
}

void occ_engine_moc_reserve(u32 tris)
{
	instance().reserve(tris);
}

void occ_engine_moc_set_res(int preset)
{
	instance().set_res(preset);
}

void occ_engine_moc_preset_size(int preset, u32& w, u32& h)
{
	const int p = clamp_preset(preset);
	w = s_preset_w[p];
	h = s_preset_h[p];
}

void occ_engine_moc_release()
{
	instance().release();
}
