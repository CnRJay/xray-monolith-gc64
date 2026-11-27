// occ_engine.h: the occlusion engine seam CHOM renders occluders into and answers queries from
#pragma once

class occTri;

class IOccEngine
{
public:
	virtual ~IOccEngine()
	{
	}

	virtual void begin_frame(const Fmatrix& full, const Fvector& cop, float near_w) = 0;

	// world space occluder triangle, false means the tri contributed nothing and earns a skip stamp
	virtual bool emit(occTri& T, const Fvector* verts) = 0;
	virtual void end_frame() = 0;

	virtual BOOL test_box(const Fbox& world) = 0;
	virtual BOOL test_poly(const Fvector* v, u32 n) = 0;

	// true only where per-tri pixel feedback exists to drive the skip stamp
	virtual bool wants_skip_filter() = 0;
};

// the stock hierarchical raster, one instance wrapping the process wide Raster
IOccEngine& occ_engine_legacy();

// the masked rasterizer, its buffer is allocated on the first frame that latches it on
IOccEngine& occ_engine_moc();

// level occluder budget, sizes the vertex batch and the identity index once
void occ_engine_moc_reserve(u32 tris);

// resolution preset for the coming frame, 0 = 256x144, 1 = 384x216, 2 = 512x288
void occ_engine_moc_set_res(int preset);

// pixel size of a preset, read by the overlay and the level audit line
void occ_engine_moc_preset_size(int preset, u32& w, u32& h);

// drops the instance and its buffer
void occ_engine_moc_release();

#ifdef DEBUG
// tris the legacy near clip passed this frame, the HOM debug panel reads it
u32 occ_legacy_dbg_visible();
#endif
