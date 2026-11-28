#include "BonePaletteSIMD.h"
#include <intrin.h>
#include <xmmintrin.h>

namespace BonePaletteSIMD
{
	void TransposeContiguous4x3_AVX2(float* dst, const float* src, std::size_t count);
	void TransposeStrided4x3_AVX2(float* dst, const void* srcBase, std::size_t strideBytes, std::size_t count);

	static bool DetectAVX2()
	{
		int info[4] = {};
		__cpuid(info, 0);
		if (info[0] < 7)
			return false;

		__cpuid(info, 1);
		const bool hasOSXSAVE = (info[2] & (1 << 27)) != 0;
		const bool hasAVX = (info[2] & (1 << 28)) != 0;
		if (!hasOSXSAVE || !hasAVX)
			return false;

		const unsigned long long xcr0 = _xgetbv(0);
		if ((xcr0 & 0x6ull) != 0x6ull)
			return false;

		__cpuidex(info, 7, 0);
		return (info[1] & (1 << 5)) != 0;
	}

	static inline void TransposeOne_SSE(float* dst, const float* m)
	{
		__m128 r0 = _mm_loadu_ps(m + 0);
		__m128 r1 = _mm_loadu_ps(m + 4);
		__m128 r2 = _mm_loadu_ps(m + 8);
		__m128 r3 = _mm_loadu_ps(m + 12);

		__m128 t0 = _mm_unpacklo_ps(r0, r1);
		__m128 t1 = _mm_unpacklo_ps(r2, r3);
		__m128 t2 = _mm_unpackhi_ps(r0, r1);
		__m128 t3 = _mm_unpackhi_ps(r2, r3);

		_mm_storeu_ps(dst + 0, _mm_movelh_ps(t0, t1));
		_mm_storeu_ps(dst + 4, _mm_movehl_ps(t1, t0));
		_mm_storeu_ps(dst + 8, _mm_movelh_ps(t2, t3));
	}

	static void TransposeContiguous4x3_SSE(float* dst, const float* src, std::size_t count)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			TransposeOne_SSE(dst, src);
			src += 16;
			dst += 12;
		}
	}

	static void TransposeStrided4x3_SSE(float* dst, const void* srcBase, std::size_t strideBytes, std::size_t count)
	{
		const std::uint8_t* p = static_cast<const std::uint8_t*>(srcBase);
		for (std::size_t i = 0; i < count; ++i)
		{
			TransposeOne_SSE(dst, reinterpret_cast<const float*>(p));
			p += strideBytes;
			dst += 12;
		}
	}

	bool HasAVX2()
	{
		static const bool s_hasAVX2 = DetectAVX2();
		return s_hasAVX2;
	}

	void TransposeContiguous4x3(float* dst, const float* srcMatrices, std::size_t count)
	{
		if (count == 0)
			return;
		if (HasAVX2())
			TransposeContiguous4x3_AVX2(dst, srcMatrices, count);
		else
			TransposeContiguous4x3_SSE(dst, srcMatrices, count);
	}

	void TransposeStrided4x3(float* dst, const void* srcBase, std::size_t strideBytes, std::size_t count)
	{
		if (count == 0)
			return;
		if (HasAVX2())
			TransposeStrided4x3_AVX2(dst, srcBase, strideBytes, count);
		else
			TransposeStrided4x3_SSE(dst, srcBase, strideBytes, count);
	}
}
