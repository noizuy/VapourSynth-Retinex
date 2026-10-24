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


#include "GuidedFilter.h"
#include "BoxFilter.h"
#include "PlaneFilter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// 2D box filter of a packed (stride == w) buffer; dst may alias src (scratch holds the H pass).
void BoxFilter2D(const float *src, float *dst, int w, int h, int radius, float *scratch)
{
    BoxFilterHorizontal(src, w, scratch, w, w, h, radius);
    BoxFilterVertical(scratch, w, dst, w, w, h, radius);
}

// Block-average downsample by factor (edge blocks may be partial); factor == 1 is an exact copy.
void BoxDownsample(const float *src, int srcStride, int width, int height,
    float *dst, int dw, int dh, int factor)
{
    for (int oy = 0; oy < dh; oy++)
    {
        const int y0 = oy * factor;
        const int y1 = Min(y0 + factor, height);
        float *drow = dst + oy * dw;

        for (int ox = 0; ox < dw; ox++)
        {
            const int x0 = ox * factor;
            const int x1 = Min(x0 + factor, width);

            float sum = 0;
            for (int y = y0; y < y1; y++)
            {
                const float *srow = src + y * srcStride;
                for (int x = x0; x < x1; x++)
                    sum += srow[x];
            }
            drow[ox] = sum / static_cast<float>((y1 - y0) * (x1 - x0));
        }
    }
}

// Bilinear sample positions aligned with the factor x factor blocks BoxDownsample averaged: output i
// lies between low-res samples i0[i] and i1[i] (clamped) with weight w[i] on i1. factor == 1 is exact.
void BilinearTaps(int n, int dn, int factor, int *i0, int *i1, float *w)
{
    for (int i = 0; i < n; i++)
    {
        const float f = (i + 0.5f) / factor - 0.5f;
        const int k = static_cast<int>(std::floor(f));
        w[i] = f - k;
        i0[i] = Clip(k, 0, dn - 1);
        i1[i] = Clip(k + 1, 0, dn - 1);
    }
}


struct GuidedFilterKernel
{
    int radius;
    float eps;
    int subsample;

    void operator()(const float *srcp, int srcStride, float *dstp, int dstStride, int width, int height) const
    {
        const int dw = (width + subsample - 1) / subsample;
        const int dh = (height + subsample - 1) / subsample;
        const int rLow = Max(radius / subsample, 1);

        // low-resolution statistics (He & Sun, "Fast Guided Filter")
        const size_t lowSize = size_t(dw) * dh;
        float *lowI = Scratch<float>(ScratchFrame, lowSize * 5 + width + height
            + (sizeof(int) * 2 * (width + height) + sizeof(float) - 1) / sizeof(float));
        float *meanI = lowI + lowSize;
        float *sq = meanI + lowSize;
        float *a = sq + lowSize;
        float *b = a + lowSize;
        float *wx = b + lowSize;
        float *wy = wx + width;
        int *x0 = reinterpret_cast<int *>(wy + height);
        int *x1 = x0 + width;
        int *y0 = x1 + width;
        int *y1 = y0 + height;

        BoxDownsample(srcp, srcStride, width, height, lowI, dw, dh, subsample);

        for (size_t i = 0; i < lowSize; i++)
            sq[i] = lowI[i] * lowI[i];
        BoxFilter2D(lowI, meanI, dw, dh, rLow, a); // mean_I (a as scratch)
        BoxFilter2D(sq, sq, dw, dh, rLow, a); // corr_I (in place)

        // self-guided (I == p): a = var_I / (var_I + eps), b = mean_I * (1 - a)
        for (size_t i = 0; i < lowSize; i++)
        {
            const float varI = Max(sq[i] - meanI[i] * meanI[i], 0.f); // clamp rounding below 0
            const float ai = varI / (varI + eps);
            a[i] = ai;
            b[i] = meanI[i] * (1.f - ai);
        }

        BoxFilter2D(a, a, dw, dh, rLow, lowI); // mean_a (in place, lowI as scratch)
        BoxFilter2D(b, b, dw, dh, rLow, lowI); // mean_b (in place)

        // q = mean_a .* I + mean_b, with mean_a/mean_b bilinearly upsampled on the fly
        BilinearTaps(width, dw, subsample, x0, x1, wx);
        BilinearTaps(height, dh, subsample, y0, y1, wy);

        for (int y = 0; y < height; y++)
        {
            const float *a0 = a + y0[y] * dw, *a1 = a + y1[y] * dw;
            const float *b0 = b + y0[y] * dw, *b1 = b + y1[y] * dw;
            const float fy = wy[y];
            const float *Irow = srcp + y * srcStride;
            float *orow = dstp + y * dstStride;

            for (int x = 0; x < width; x++)
            {
                const int i0 = x0[x], i1 = x1[x];
                const float fx = wx[x];
                const float av0 = a0[i0] + (a0[i1] - a0[i0]) * fx;
                const float av1 = a1[i0] + (a1[i1] - a1[i0]) * fx;
                const float bv0 = b0[i0] + (b0[i1] - b0[i0]) * fx;
                const float bv1 = b1[i0] + (b1[i1] - b1[i0]) * fx;
                orow[x] = (av0 + (av1 - av0) * fy) * Irow[x] + (bv0 + (bv1 - bv0) * fy);
            }
        }
    }
};

} // namespace


VSNode *CreateGuidedFilterNode(VSNode *src, int radius, double eps, int subsample,
    VSCore *core, const VSAPI *vsapi)
{
    const GuidedFilterKernel k = { radius, static_cast<float>(eps), subsample };
    return CreatePlaneFilterNode("RetinexGuidedFilter", src, k, core, vsapi);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
