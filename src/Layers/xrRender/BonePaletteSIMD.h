#pragma once

#include <cstddef>
#include <cstdint>

namespace BonePaletteSIMD
{
	// Cached runtime CPUID + OSXSAVE check for AVX2 support.
	bool HasAVX2();

	// Bulk-transpose `count` contiguous 4x4 row-major matrices (16 floats each)
	// into packed 3x4 Fvector4 rows (12 floats per bone) for HLSL sbones_array.
	void TransposeContiguous4x3(float* dst, const float* srcMatrices, std::size_t count);

	// Bulk-transpose `count` strided 4x4 row-major matrices (`strideBytes` apart)
	// into packed 3x4 Fvector4 rows (12 floats per bone).
	void TransposeStrided4x3(float* dst, const void* srcBase, std::size_t strideBytes, std::size_t count);
}
