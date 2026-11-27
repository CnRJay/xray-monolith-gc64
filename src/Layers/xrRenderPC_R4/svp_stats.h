#ifndef svp_statsH
#define svp_statsH
#pragma once

// pip per-viewport render-stats overlay, gated by r__svp_stats
// every call self-gates, at r__svp_stats 0 no queries exist and no work runs

namespace svp_stats
{
	enum section_e
	{
		SEC_MAIN_GBUFFER = 0,
		SEC_MAIN_LIGHTS,
		SEC_MAIN_EMISSIVE,
		SEC_MAIN_COMBINE,
		SEC_SVP_GBUFFER,
		SEC_SVP_LIGHTS,
		SEC_SVP_EMISSIVE,
		SEC_SVP_COMBINE,
		SEC_FRAME,
		// combine sub sections, timed on the main pass only, they nest inside SEC_MAIN_COMBINE
		SEC_C_AO,
		SEC_C_IL,
		SEC_C_SKY,
		SEC_C_COMBINE1,
		SEC_C_SSR,
		SEC_C_WATER,
		SEC_C_RAIN,
		SEC_C_FWD,
		SEC_C_BLOOM,
		SEC_C_SUNSHAFT,
		SEC_C_FOG,
		SEC_C_TAA,
		SEC_C_BLUR,
		SEC_C_DOF,
		SEC_C_LUT,
		SEC_C_COMBINE2,
		// tracked copy cost by category, each accumulates across every copy site of its kind
		SEC_CP_HIST,
		SEC_CP_TAIL,
		SEC_CP_SCENE,
		SEC_CP_PASS,
		// per sun cascade shadow-map render, gpu only, read by the fps audit overlay
		SEC_SUN_C0,
		SEC_SUN_C1,
		SEC_SUN_C2,
		// sun sub sections, accumulated across all three cascades, vol nests inside accum
		SEC_SUN_SMAP,
		SEC_SUN_GRASS,
		SEC_SUN_MINMAX,
		SEC_SUN_ACCUM,
		SEC_SUN_VOL,
		SEC_SUN_SVP,
		SEC_COUNT
	};

	// scoped-only cpu brackets, wall clock only so they spend no timestamp pair
	enum cpu_e
	{
		CPU_REPLAY = 0, // svp target switches around the scope gbuffer
		CPU_LIGHTS,     // dual-accum target switches plus the shadow atlas re-point
		CPU_HUD,        // hud pose latch, lens derive, objective weapon drain
		CPU_LENS,       // main-view 3dss lens composite
		CPU_MISC,       // svp target alloc, history reset, svp capture tail
		CPU_COUNT
	};

	// lean post gate skips, phase_combine ors these into svp_stats_lean_flags per frame
	enum lean_bit_e
	{
		LEAN_LUT = 1 << 0,
		LEAN_WATER = 1 << 1,
		LEAN_GLASS = 1 << 2,
	};

	// frame boundary, disjoint query + ring advance, render thread inside Render() only
	void frame_begin();
	void frame_end(bool svp_active);

	// timing window, cpu qpc + gpu timestamp pair + rcache deltas, multiple pairs per frame accumulate
	void section_begin(section_e s);
	void section_end(section_e s);

	// cpu-only window, no query, multiple pairs per frame accumulate into one bucket
	void cpu_begin(cpu_e c);
	void cpu_end(cpu_e c);

	// light and shadow tallies at the accumulation sites
	void note_main_lights(u32 total, u32 shadowed);
	void note_svp_blend();
	void note_sun();

	// two-column panel, main viewport, drawn onto the bound ldr target
	void draw_overlay();

	// release the query pool + font, device reset or disable
	void release();
}

#endif
