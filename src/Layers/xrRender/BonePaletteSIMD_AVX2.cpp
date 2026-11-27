#include "BonePaletteSIMD.h"
#include <immintrin.h>

namespace BonePaletteSIMD
{
	namespace detail
	{
		static inline void TransposeOne_128(float* dst, const float* m)
		{
			__m128 r0 = _mm_loadu_ps(m + 0);
			__m128 r1 = _mm_loadu_ps(m + 4);
			__m128 r2 = _mm_loadu_ps(m + 8);
			__m128 r3 = _mm_loadu_ps(m + 12);

			__m128 t0 = _mm_unpacklo_ps(r0, r1); // _11, _21, _12, _22
			__m128 t1 = _mm_unpacklo_ps(r2, r3); // _31, _41, _32, _42
			__m128 t2 = _mm_unpackhi_ps(r0, r1); // _13, _23, _14, _24
			__m128 t3 = _mm_unpackhi_ps(r2, r3); // _33, _43, _34, _44

			_mm_storeu_ps(dst + 0, _mm_movelh_ps(t0, t1)); // _11, _21, _31, _41
			_mm_storeu_ps(dst + 4, _mm_movehl_ps(t1, t0)); // _12, _22, _32, _42
			_mm_storeu_ps(dst + 8, _mm_movelh_ps(t2, t3)); // _13, _23, _33, _43
		}

		static inline void TransposePair_AVX2(float* dst, const float* m0, const float* m1)
		{
			__m256 a01 = _mm256_loadu_ps(m0 + 0);
			__m256 a23 = _mm256_loadu_ps(m0 + 8);
			__m256 b01 = _mm256_loadu_ps(m1 + 0);
			__m256 b23 = _mm256_loadu_ps(m1 + 8);

			__m256 r0 = _mm256_permute2f128_ps(a01, b01, 0x20); // [M0.r0 | M1.r0]
			__m256 r1 = _mm256_permute2f128_ps(a01, b01, 0x31); // [M0.r1 | M1.r1]
			__m256 r2 = _mm256_permute2f128_ps(a23, b23, 0x20); // [M0.r2 | M1.r2]
			__m256 r3 = _mm256_permute2f128_ps(a23, b23, 0x31); // [M0.r3 | M1.r3]

			__m256 t0 = _mm256_unpacklo_ps(r0, r1);
			__m256 t1 = _mm256_unpacklo_ps(r2, r3);
			__m256 t2 = _mm256_unpackhi_ps(r0, r1);
			__m256 t3 = _mm256_unpackhi_ps(r2, r3);

			__m256 c0 = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(1, 0, 1, 0)); // [M0.c0 | M1.c0]
			__m256 c1 = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(3, 2, 3, 2)); // [M0.c1 | M1.c1]
			__m256 c2 = _mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(1, 0, 1, 0)); // [M0.c2 | M1.c2]

			__m256 out0 = _mm256_permute2f128_ps(c0, c1, 0x20); // [M0.c0 | M0.c1]
			__m256 out1 = _mm256_permute2f128_ps(c2, c0, 0x30); // [M0.c2 | M1.c0]
			__m256 out2 = _mm256_permute2f128_ps(c1, c2, 0x31); // [M1.c1 | M1.c2]

			_mm256_storeu_ps(dst + 0, out0);
			_mm256_storeu_ps(dst + 8, out1);
			_mm256_storeu_ps(dst + 16, out2);
		}
	}

	void TransposeContiguous4x3_AVX2(float* dst, const float* src, std::size_t count)
	{
		std::size_t i = 0;
		for (; i + 1 < count; i += 2)
		{
			detail::TransposePair_AVX2(dst, src, src + 16);
			src += 32;
			dst += 24;
		}
		if (i < count)
			detail::TransposeOne_128(dst, src);
	}

	void TransposeStrided4x3_AVX2(float* dst, const void* srcBase, std::size_t strideBytes, std::size_t count)
	{
		const std::uint8_t* p = static_cast<const std::uint8_t*>(srcBase);
		std::size_t i = 0;
		for (; i + 1 < count; i += 2)
		{
			const float* m0 = reinterpret_cast<const float*>(p);
			const float* m1 = reinterpret_cast<const float*>(p + strideBytes);
			detail::TransposePair_AVX2(dst, m0, m1);
			p += strideBytes * 2;
			dst += 24;
		}
		if (i < count)
			detail::TransposeOne_128(dst, reinterpret_cast<const float*>(p));
	}
}
