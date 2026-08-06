/*
* Retinex filter - VapourSynth plugin
* Copyright (C) 2014  mawen1250
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// Applies a 2D box filter (horizontal then vertical) to a tightly packed (stride == w)
// buffer. dst may alias src.
void BoxFilter2D(const float *src, float *dst, int w, int h, int radius, float *scratch)
{
    BoxFilterHorizontal(src, w, scratch, w, w, h, radius);
    BoxFilterVertical(scratch, w, dst, w, w, h, radius);
}

// Block-average downsample by an integer factor (the last row/column of blocks may be
// smaller than factor x factor if width/height aren't a multiple of it). factor == 1
// degenerates to an exact copy.
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

// Upsamples a dw x dh (tightly packed) grid to width x height via bilinear interpolation,
// aligning sample centers with the factor x factor blocks BoxDownsample averaged. factor
// == 1 degenerates to an exact copy.
void BilinearUpsample(const float *src, int dw, int dh, float *dst, int dstStride,
    int width, int height, int factor)
{
    for (int y = 0; y < height; y++)
    {
        const float fy = (y + 0.5f) / factor - 0.5f;
        const int y0 = static_cast<int>(std::floor(fy));
        const float wy = fy - y0;
        const int y0c = Clip(y0, 0, dh - 1);
        const int y1c = Clip(y0 + 1, 0, dh - 1);

        float *drow = dst + y * dstStride;

        for (int x = 0; x < width; x++)
        {
            const float fx = (x + 0.5f) / factor - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx));
            const float wx = fx - x0;
            const int x0c = Clip(x0, 0, dw - 1);
            const int x1c = Clip(x0 + 1, 0, dw - 1);

            const float v00 = src[y0c * dw + x0c];
            const float v01 = src[y0c * dw + x1c];
            const float v10 = src[y1c * dw + x0c];
            const float v11 = src[y1c * dw + x1c];

            const float v0 = v00 + (v01 - v00) * wx;
            const float v1 = v10 + (v11 - v10) * wx;
            drow[x] = v0 + (v1 - v0) * wy;
        }
    }
}


struct GuidedFilterData
{
    VSNode *node = nullptr;
    VSVideoInfo vi{};
    int radius = 0;
    float eps = 0;
    int subsample = 1;
};

const VSFrame *VS_CC GuidedFilterGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const GuidedFilterData *d = reinterpret_cast<const GuidedFilterData *>(instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const int width = vsapi->getFrameWidth(src, 0);
        const int height = vsapi->getFrameHeight(src, 0);
        const int srcStride = static_cast<int>(vsapi->getStride(src, 0) / sizeof(float));
        const float *srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 0));

        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, width, height, src, core);
        const int dstStride = static_cast<int>(vsapi->getStride(dst, 0) / sizeof(float));
        float *dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));

        const int subsample = d->subsample;
        const int dw = (width + subsample - 1) / subsample;
        const int dh = (height + subsample - 1) / subsample;
        const int rLow = Max(d->radius / subsample, 1);
        const float eps = d->eps;

        // Low-resolution working buffers (statistics computed here, per He & Sun's "Fast
        // Guided Filter": box-filter cost is independent of radius, so the only speedup
        // comes from shrinking the number of pixels processed).
        float *lowI = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);
        float *meanI = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);
        float *sq = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);
        float *a = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);
        float *b = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);
        float *scratchLow = vsh::vsh_aligned_malloc<float>(sizeof(float) * dw * dh, Alignment);

        BoxDownsample(srcp, srcStride, width, height, lowI, dw, dh, subsample);

        BoxFilter2D(lowI, meanI, dw, dh, rLow, scratchLow); // mean_I

        for (int i = 0; i < dw * dh; i++)
            sq[i] = lowI[i] * lowI[i];
        BoxFilter2D(sq, sq, dw, dh, rLow, scratchLow); // corr_I (in place)

        // Self-guided (I == p): cov_Ip == var_I and mean_p == mean_I, which simplifies
        // a = cov_Ip/(var_I+eps) and b = mean_p - a*mean_I down to just var_I and mean_I.
        for (int i = 0; i < dw * dh; i++)
        {
            const float varI = sq[i] - meanI[i] * meanI[i];
            const float ai = varI / (varI + eps);
            a[i] = ai;
            b[i] = meanI[i] * (1.f - ai);
        }

        BoxFilter2D(a, a, dw, dh, rLow, scratchLow); // mean_a (in place)
        BoxFilter2D(b, b, dw, dh, rLow, scratchLow); // mean_b (in place)

        float *meanAFull = vsh::vsh_aligned_malloc<float>(sizeof(float) * width * height, Alignment);
        float *meanBFull = vsh::vsh_aligned_malloc<float>(sizeof(float) * width * height, Alignment);
        BilinearUpsample(a, dw, dh, meanAFull, width, width, height, subsample);
        BilinearUpsample(b, dw, dh, meanBFull, width, width, height, subsample);

        for (int y = 0; y < height; y++)
        {
            const float *Irow = srcp + y * srcStride;
            const float *aRow = meanAFull + y * width;
            const float *bRow = meanBFull + y * width;
            float *orow = dstp + y * dstStride;
            for (int x = 0; x < width; x++)
                orow[x] = aRow[x] * Irow[x] + bRow[x]; // q = mean_a .* I + mean_b
        }

        vsh::vsh_aligned_free(meanAFull);
        vsh::vsh_aligned_free(meanBFull);
        vsh::vsh_aligned_free(lowI);
        vsh::vsh_aligned_free(meanI);
        vsh::vsh_aligned_free(sq);
        vsh::vsh_aligned_free(a);
        vsh::vsh_aligned_free(b);
        vsh::vsh_aligned_free(scratchLow);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC GuidedFilterFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    GuidedFilterData *d = reinterpret_cast<GuidedFilterData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

} // namespace


VSNode *CreateGuidedFilterNode(VSNode *src, int radius, double eps, int subsample,
    VSCore *core, const VSAPI *vsapi)
{
    GuidedFilterData *d = new GuidedFilterData();
    d->node = vsapi->addNodeRef(src);
    d->vi = *vsapi->getVideoInfo(src);
    d->radius = radius;
    d->eps = static_cast<float>(eps);
    d->subsample = subsample;

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2("RetinexGuidedFilter", &d->vi, GuidedFilterGetFrame, GuidedFilterFree,
        fmParallel, &dep, 1, d, core);

    if (!node)
    {
        vsapi->freeNode(d->node);
        delete d;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
