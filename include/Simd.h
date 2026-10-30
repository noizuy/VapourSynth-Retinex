/*
* Retinex filter - VapourSynth plugin
* Copyright (C) 2026  noizuy
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef SIMD_H_
#define SIMD_H_


#include <cfloat>
#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define RETINEX_SSE2 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hot per-pixel loops are written as plain loops (no intrinsics) that the compiler auto-vectorizes. With GCC/Clang
// on x86 they are compiled a second time for AVX2 and chosen at runtime:
//
//     RETINEX_FORCEINLINE void FooImpl(...) { loop }
//     #if RETINEX_AVX2_DISPATCH
//     RETINEX_TARGET_AVX2 void FooAvx2(...) { FooImpl(...); }
//     #endif
//     void Foo(...) { RETINEX_DISPATCH(FooAvx2(...), FooImpl(...)); }
//
// FMA is deliberately not enabled for the AVX2 copy, so both copies round identically and the output does not
// depend on the CPU. Vectorizing the loops' selects needs -fno-trapping-math (set in meson.build).
// Define RETINEX_DISABLE_AVX2 to build without the AVX2 copies (e.g. to compare both paths).

#if defined(_MSC_VER) && !defined(__clang__)
#define RETINEX_FORCEINLINE __forceinline
#else
#define RETINEX_FORCEINLINE inline __attribute__((always_inline))
#endif

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__)) && !defined(RETINEX_DISABLE_AVX2)
#define RETINEX_AVX2_DISPATCH 1
#define RETINEX_TARGET_AVX2 __attribute__((target("avx2")))

inline bool CpuHasAvx2()
{
    static const bool has = __builtin_cpu_supports("avx2");
    return has;
}

#define RETINEX_DISPATCH(avx2Call, defaultCall) do { if (CpuHasAvx2()) avx2Call; else defaultCall; } while (0)
#else
#define RETINEX_AVX2_DISPATCH 0
#define RETINEX_DISPATCH(avx2Call, defaultCall) do { defaultCall; } while (0)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Natural logarithm for float, written as plain arithmetic so that it vectorizes inside loops.
// Algorithm and coefficients are from Cephes logf.c (Stephen L. Moshier), as also used by Julien Pommier's
// sse_mathfun.h: x = m * 2^e with m in [sqrt(1/2), sqrt(2)), log(m) by a degree-8 polynomial in m-1, and e*log(2)
// added in two parts for accuracy.
// Valid for finite x >= FLT_MIN (positive normal): max error 0.83 ulp over all of them, against log() in double,
// about the same worst case as glibc's logf (exhaustive check: tests/fastlog_test.cpp, run by `meson test`).
RETINEX_FORCEINLINE float FastLog(float x)
{
    int32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    int32_t ei = (bits >> 23) - 126;
    bits = (bits & 0x007fffff) | 0x3f000000;
    float m;
    std::memcpy(&m, &bits, sizeof(m)); // m in [0.5, 1)

    // m < sqrt(1/2): use 2m-1 and e-1, else m-1 (written without branches)
    const int32_t lo = m < 0.707106781186547524f;
    ei -= lo;
    m = m + m * static_cast<float>(lo) - 1.f;

    const float e = static_cast<float>(ei);
    const float z = m * m;
    float y = 7.0376836292E-2f;
    y = y * m - 1.1514610310E-1f;
    y = y * m + 1.1676998740E-1f;
    y = y * m - 1.2420140846E-1f;
    y = y * m + 1.4249322787E-1f;
    y = y * m - 1.6668057665E-1f;
    y = y * m + 2.0000714765E-1f;
    y = y * m - 2.4999993993E-1f;
    y = y * m + 3.3333331174E-1f;
    y = y * m * z;
    y += -2.12194440e-4f * e;
    y += -0.5f * z;
    return m + y + 0.693359375f * e;
}


// Updates min/max with the range of row[0, width). Compilers do not vectorize a NaN-correct min/max reduction,
// so this uses SSE2 explicitly (rows are still in L1 when this runs right after producing them).
RETINEX_FORCEINLINE void RowMinMax(const float *row, int width, float &min, float &max)
{
    int x = 0;
    float lmin = min, lmax = max;
#if RETINEX_SSE2
    if (width >= 4)
    {
        __m128 vmin = _mm_set1_ps(lmin);
        __m128 vmax = _mm_set1_ps(lmax);
        for (; x + 4 <= width; x += 4)
        {
            const __m128 v = _mm_loadu_ps(row + x);
            vmin = _mm_min_ps(vmin, v);
            vmax = _mm_max_ps(vmax, v);
        }
        alignas(16) float a[4], b[4];
        _mm_store_ps(a, vmin);
        _mm_store_ps(b, vmax);
        for (int k = 0; k < 4; k++)
        {
            lmin = a[k] < lmin ? a[k] : lmin;
            lmax = b[k] > lmax ? b[k] : lmax;
        }
    }
#endif
    for (; x < width; x++)
    {
        lmin = row[x] < lmin ? row[x] : lmin;
        lmax = row[x] > lmax ? row[x] : lmax;
    }
    min = lmin;
    max = lmax;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
