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


#include "BoxBlur.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// P. Kovesi, "Fast Almost-Gaussian Filtering", DICTA 2010: pick 3 box widths whose combined
// variance matches a Gaussian of the given sigma. Using two slightly different odd widths
// (rather than the same width 3 times) gets much closer to a true Gaussian.
void BoxRadiiForSigma(double sigma, int radii[3])
{
    const int n = 3;

    double wIdeal = sqrt((12. * sigma * sigma / n) + 1.);
    int wl = static_cast<int>(std::floor(wIdeal));
    if (wl < 1) wl = 1;
    if (wl % 2 == 0) wl -= 1;
    if (wl < 1) wl = 1;
    int wu = wl + 2;

    double mIdeal = (12. * sigma * sigma - n * wl * wl - 4. * n * wl - 3. * n) / (-4. * wl - 4.);
    int m = static_cast<int>(std::round(mIdeal));
    m = Clip(m, 0, n);

    for (int i = 0; i < n; i++)
    {
        int width = i < m ? wl : wu;
        radii[i] = Max((width - 1) / 2, 0);
    }
}

// Sliding-window box blur: O(width*height) regardless of radius. src/dst may be the same
// buffer only if srcStride == dstStride (in-place row processing is safe since each output
// row only depends on already-consumed input values within that same row).
void BoxBlurHorizontal(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius)
{
    if (radius <= 0)
    {
        for (int y = 0; y < height; y++)
            memcpy(dst + y * dstStride, src + y * srcStride, sizeof(float) * width);
        return;
    }

    const float norm = 1.f / static_cast<float>(2 * radius + 1);

    for (int y = 0; y < height; y++)
    {
        const float *srow = src + y * srcStride;
        float *drow = dst + y * dstStride;

        float sum = 0;
        for (int x = -radius; x <= radius; x++)
            sum += srow[Clip(x, 0, width - 1)];

        for (int x = 0; x < width; x++)
        {
            drow[x] = sum * norm;
            sum += srow[Clip(x + radius + 1, 0, width - 1)] - srow[Clip(x - radius, 0, width - 1)];
        }
    }
}

void BoxBlurVertical(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius)
{
    if (radius <= 0)
    {
        for (int y = 0; y < height; y++)
            memcpy(dst + y * dstStride, src + y * srcStride, sizeof(float) * width);
        return;
    }

    const float norm = 1.f / static_cast<float>(2 * radius + 1);

    float *sum = vsh::vsh_aligned_malloc<float>(sizeof(float) * width, Alignment);
    memset(sum, 0, sizeof(float) * width);

    for (int y = -radius; y <= radius; y++)
    {
        const float *srow = src + Clip(y, 0, height - 1) * srcStride;
        for (int x = 0; x < width; x++)
            sum[x] += srow[x];
    }

    for (int y = 0; y < height; y++)
    {
        float *drow = dst + y * dstStride;
        for (int x = 0; x < width; x++)
            drow[x] = sum[x] * norm;

        const float *addRow = src + Clip(y + radius + 1, 0, height - 1) * srcStride;
        const float *subRow = src + Clip(y - radius, 0, height - 1) * srcStride;
        for (int x = 0; x < width; x++)
            sum[x] += addRow[x] - subRow[x];
    }

    vsh::vsh_aligned_free(sum);
}


struct BoxBlurData
{
    VSNode *node = nullptr;
    VSVideoInfo vi{};
    int radii[3] = { 0, 0, 0 };
};

const VSFrame *VS_CC BoxBlurGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const BoxBlurData *d = reinterpret_cast<const BoxBlurData *>(instanceData);

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

        float *bufA = vsh::vsh_aligned_malloc<float>(sizeof(float) * width * height, Alignment);
        float *bufB = vsh::vsh_aligned_malloc<float>(sizeof(float) * width * height, Alignment);

        BoxBlurHorizontal(srcp, srcStride, bufA, width, width, height, d->radii[0]);
        BoxBlurVertical(bufA, width, bufB, width, width, height, d->radii[0]);
        BoxBlurHorizontal(bufB, width, bufA, width, width, height, d->radii[1]);
        BoxBlurVertical(bufA, width, bufB, width, width, height, d->radii[1]);
        BoxBlurHorizontal(bufB, width, bufA, width, width, height, d->radii[2]);
        BoxBlurVertical(bufA, width, dstp, dstStride, width, height, d->radii[2]);

        vsh::vsh_aligned_free(bufA);
        vsh::vsh_aligned_free(bufB);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC BoxBlurFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    BoxBlurData *d = reinterpret_cast<BoxBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

} // namespace


VSNode *CreateBoxBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    BoxBlurData *d = new BoxBlurData();
    d->node = vsapi->addNodeRef(src);
    d->vi = *vsapi->getVideoInfo(src);
    BoxRadiiForSigma(sigma, d->radii);

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2("RetinexBoxBlur", &d->vi, BoxBlurGetFrame, BoxBlurFree,
        fmParallel, &dep, 1, d, core);

    if (!node)
    {
        vsapi->freeNode(d->node);
        delete d;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
