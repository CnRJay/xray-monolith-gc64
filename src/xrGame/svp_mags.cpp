#if defined(SVP_TEST_CLIENT)
#include "../xrCore/xrCore.h"
#else
#include "stdafx.h"
#endif
#include "svp_mags.h"
#include "../xrEngine/svp_gameplay_cvars.h"

float svp_magnification_scroll_multiplier(float scroll_multiplier)
{
	return _valid(scroll_multiplier) && scroll_multiplier > 0.f
		? scroll_multiplier : 1.f;
}

float svp_magnification_to_runtime_factor(float magnification)
{
	if (!_valid(magnification) || magnification <= 0.f)
		return 0.f;
	const float factor = (SVP_ZOOM_BASE_FOV / 0.75f) / magnification;
	return _valid(factor) && factor > 0.f ? factor : 0.f;
}

float svp_runtime_factor_to_magnification(float runtime_factor)
{
	if (!_valid(runtime_factor) || runtime_factor <= 0.f)
		return 0.f;
	const float magnification = (SVP_ZOOM_BASE_FOV / 0.75f) / runtime_factor;
	return _valid(magnification) && magnification > 0.f ? magnification : 0.f;
}

float svp_magnification_to_weapon_factor(float magnification, float scroll_multiplier)
{
	return svp_magnification_to_runtime_factor(magnification) /
		svp_magnification_scroll_multiplier(scroll_multiplier);
}

float svp_weapon_factor_to_magnification(float weapon_factor, float scroll_multiplier)
{
	return svp_runtime_factor_to_magnification(weapon_factor *
		svp_magnification_scroll_multiplier(scroll_multiplier));
}

bool svp_magnification_ladder_valid(const svp_mags_data& ladder)
{
	const bool count_valid =
		(ladder.mode == svp_mag_fixed && ladder.count == 1) ||
		(ladder.mode == svp_mag_continuous && ladder.count == 2) ||
		(ladder.mode == svp_mag_detent && ladder.count >= 2 &&
			ladder.count <= _countof(ladder.values));
	if (!count_valid)
		return false;
	for (u32 i = 0; i < ladder.count; ++i)
		if (!_valid(ladder.values[i]) || ladder.values[i] <= 0.f ||
			ladder.values[i] > SVP_MAG_LIMIT ||
			svp_magnification_to_runtime_factor(ladder.values[i]) <= 0.f ||
			(i && !(ladder.values[i] > ladder.values[i - 1])))
			return false;
	return true;
}

static u64 svp_mag_hash(u64 hash, const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

u64 svp_magnification_fingerprint(const svp_mags_data& ladder)
{
	u64 hash = 14695981039346656037ull;
	const u8 mode = static_cast<u8>(ladder.mode);
	hash = svp_mag_hash(hash, &mode, sizeof(mode));
	hash = svp_mag_hash(hash, &ladder.count, sizeof(ladder.count));
	for (u32 i = 0; i < ladder.count && i < _countof(ladder.values); ++i)
		hash = svp_mag_hash(hash, &ladder.values[i], sizeof(ladder.values[i]));
	return hash;
}

int svp_magnification_exact_index(const svp_mags_data& ladder, float magnification)
{
	for (u32 i = 0; i < ladder.count && i < _countof(ladder.values); ++i)
		if (ladder.values[i] == magnification)
			return static_cast<int>(i);
	return -1;
}

int svp_magnification_nearest_index(const svp_mags_data& ladder, float magnification)
{
	if (!ladder.count)
		return -1;
	u32 best = 0;
	float distance = _abs(ladder.values[0] - magnification);
	for (u32 i = 1; i < ladder.count && i < _countof(ladder.values); ++i)
	{
		const float candidate = _abs(ladder.values[i] - magnification);
		if (candidate < distance)
		{
			best = i;
			distance = candidate;
		}
	}
	return static_cast<int>(best);
}

float svp_magnification_adjacent(const svp_mags_data& ladder, float magnification, int direction)
{
	if (ladder.mode != svp_mag_detent ||
		!svp_magnification_ladder_valid(ladder))
		return 0.f;
	int index = svp_magnification_exact_index(ladder, magnification);
	if (index < 0)
		index = svp_magnification_nearest_index(ladder, magnification);
	if (direction)
		index += direction > 0 ? 1 : -1;
	clamp(index, 0, static_cast<int>(ladder.count) - 1);
	return ladder.values[index];
}
