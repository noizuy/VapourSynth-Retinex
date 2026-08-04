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


#include "IirBlur.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// I.T. Young, L.J. van Vliet, "Recursive implementation of the Gaussian filter", 1995:
// a single application of this 4th-order recursive filter (run forward+backward per
// dimension) closely approximates a true Gaussian, at O(1) cost per pixel regardless
// of sigma.
void Recursive_Gaussian_Parameters(const double sigma, FLType &B, FLType &B1, FLType &B2, FLType &B3)
{
    const double q = sigma < 2.5 ? 3.97156 - 4.14554 * sqrt(1 - 0.26891 * sigma) : 0.98711 * sigma - 0.96330;

    const double b0 = 1.57825 + 2.44413 * q + 1.4281 * q * q + 0.422205 * q * q * q;
    const double b1 = 2.44413 * q + 2.85619 * q * q + 1.26661 * q * q * q;
    const double b2 = -(1.4281 * q * q + 1.26661 * q * q * q);
    const double b3 = 0.422205 * q * q * q;

    B = static_cast<FLType>(1 - (b1 + b2 + b3) / b0);
    B1 = static_cast<FLType>(b1 / b0);
    B2 = static_cast<FLType>(b2 / b0);
    B3 = static_cast<FLType>(b3 / b0);
}

void Recursive_Gaussian2D_Vertical(FLType *output, const FLType *input, int height, int width, int stride,
    const FLType B, const FLType B1, const FLType B2, const FLType B3)
{
    int i0, i1, i2, i3, j, lower, upper;
    FLType P0, P1, P2, P3;

    if (output != input)
        memcpy(output, input, sizeof(FLType) * width);

    for (j = 0; j < height; j++)
    {
        lower = stride * j;
        upper = lower + width;

        i0 = lower;
        i1 = j < 1 ? i0 : i0 - stride;
        i2 = j < 2 ? i1 : i1 - stride;
        i3 = j < 3 ? i2 : i2 - stride;

        for (; i0 < upper; i0++, i1++, i2++, i3++)
        {
            P3 = output[i3];
            P2 = output[i2];
            P1 = output[i1];
            P0 = input[i0];
            output[i0] = B * P0 + B1 * P1 + B2 * P2 + B3 * P3;
        }
    }

    for (j = height - 1; j >= 0; j--)
    {
        lower = stride * j;
        upper = lower + width;

        i0 = lower;
        i1 = j >= height - 1 ? i0 : i0 + stride;
        i2 = j >= height - 2 ? i1 : i1 + stride;
        i3 = j >= height - 3 ? i2 : i2 + stride;

        for (; i0 < upper; i0++, i1++, i2++, i3++)
        {
            P3 = output[i3];
            P2 = output[i2];
            P1 = output[i1];
            P0 = output[i0];
            output[i0] = B * P0 + B1 * P1 + B2 * P2 + B3 * P3;
        }
    }
}

void Recursive_Gaussian2D_Horizontal(FLType *output, const FLType *input, int height, int width, int stride,
    const FLType B, const FLType B1, const FLType B2, const FLType B3)
{
    int i, j, lower, upper;
    FLType P0, P1, P2, P3;

    for (j = 0; j < height; j++)
    {
        lower = stride * j;
        upper = lower + width;

        i = lower;
        output[i] = P3 = P2 = P1 = input[i];

        for (i++; i < upper; i++)
        {
            P0 = B * input[i] + B1 * P1 + B2 * P2 + B3 * P3;
            P3 = P2;
            P2 = P1;
            P1 = P0;
            output[i] = P0;
        }

        i--;
        P3 = P2 = P1 = output[i];

        for (i--; i >= lower; i--)
        {
            P0 = B * output[i] + B1 * P1 + B2 * P2 + B3 * P3;
            P3 = P2;
            P2 = P1;
            P1 = P0;
            output[i] = P0;
        }
    }
}


struct IirBlurData
{
    VSNode *node = nullptr;
    VSVideoInfo vi{};
    FLType B = 0, B1 = 0, B2 = 0, B3 = 0;
};

const VSFrame *VS_CC IirBlurGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const IirBlurData *d = reinterpret_cast<const IirBlurData *>(instanceData);

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

        // The recursive filter needs a single consistent stride for its input/output
        // buffer, so work in a tightly packed (stride == width) FLType buffer.
        FLType *buf1 = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType) * width * height, Alignment);
        FLType *buf2 = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType) * width * height, Alignment);

        for (int y = 0; y < height; y++)
        {
            const float *srow = srcp + y * srcStride;
            FLType *drow = buf1 + y * width;
            for (int x = 0; x < width; x++)
                drow[x] = static_cast<FLType>(srow[x]);
        }

        Recursive_Gaussian2D_Horizontal(buf2, buf1, height, width, width, d->B, d->B1, d->B2, d->B3);
        Recursive_Gaussian2D_Vertical(buf2, buf2, height, width, width, d->B, d->B1, d->B2, d->B3);

        for (int y = 0; y < height; y++)
        {
            const FLType *srow = buf2 + y * width;
            float *drow = dstp + y * dstStride;
            for (int x = 0; x < width; x++)
                drow[x] = static_cast<float>(srow[x]);
        }

        vsh::vsh_aligned_free(buf1);
        vsh::vsh_aligned_free(buf2);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC IirBlurFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    IirBlurData *d = reinterpret_cast<IirBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

} // namespace


VSNode *CreateIirBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    IirBlurData *d = new IirBlurData();
    d->node = vsapi->addNodeRef(src);
    d->vi = *vsapi->getVideoInfo(src);
    Recursive_Gaussian_Parameters(sigma, d->B, d->B1, d->B2, d->B3);

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2("RetinexIirBlur", &d->vi, IirBlurGetFrame, IirBlurFree,
        fmParallel, &dep, 1, d, core);

    if (!node)
    {
        vsapi->freeNode(d->node);
        delete d;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
