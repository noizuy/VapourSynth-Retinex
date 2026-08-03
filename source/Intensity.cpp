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


#include "Intensity.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

struct IntensityData
{
    VSNode *node = nullptr;
    VSVideoInfo vi{};
    bool fulls = true;
    IntensityMode mode = IntensityMode::Y;
    int channel = 0;
};

template < typename T >
void IntensityProcess(const IntensityData *d, const VSFrame *src, VSFrame *dst, int bps, const VSAPI *vsapi)
{
    const int height = vsapi->getFrameHeight(src, 0);
    const int width = vsapi->getFrameWidth(src, 0);
    const int srcStride = static_cast<int>(vsapi->getStride(src, 0) / sizeof(T));
    const int dstStride = static_cast<int>(vsapi->getStride(dst, 0) / sizeof(float));

    // Defaults correspond to a full-range source; a limited-range source overrides these
    // below with the actual per-frame minimum/maximum, exactly as MSRCP/MSRCR did inline.
    T sFloor = 0;
    T sCeil = static_cast<T>((1 << bps) - 1);

    float *dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));

    if (d->mode == IntensityMode::Y)
    {
        const T *srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 0));

        if (!d->fulls)
        {
            T min = sCeil, max = sFloor;
            for (int j = 0; j < height; j++)
            {
                const T *row = srcp + j * srcStride;
                for (int x = 0; x < width; x++)
                {
                    min = Min(min, row[x]);
                    max = Max(max, row[x]);
                }
            }
            if (max > min) { sFloor = min; sCeil = max; }
        }

        const FLType gain = 1 / static_cast<FLType>(sCeil - sFloor);
        const FLType offset = -static_cast<FLType>(sFloor);

        for (int j = 0; j < height; j++)
        {
            const T *srow = srcp + j * srcStride;
            float *drow = dstp + j * dstStride;
            for (int x = 0; x < width; x++)
                drow[x] = static_cast<float>((srow[x] + offset) * gain);
        }
    }
    else // RGBAvg or RGBChannel: both need the combined R/G/B floor/ceil
    {
        const T *Rsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 0));
        const T *Gsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 1));
        const T *Bsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 2));

        if (!d->fulls)
        {
            T min = sCeil, max = sFloor;
            for (int j = 0; j < height; j++)
            {
                const T *rrow = Rsrcp + j * srcStride;
                const T *grow = Gsrcp + j * srcStride;
                const T *brow = Bsrcp + j * srcStride;
                for (int x = 0; x < width; x++)
                {
                    min = Min(min, Min(rrow[x], Min(grow[x], brow[x])));
                    max = Max(max, Max(rrow[x], Max(grow[x], brow[x])));
                }
            }
            if (max > min) { sFloor = min; sCeil = max; }
        }

        if (d->mode == IntensityMode::RGBAvg)
        {
            const FLType gain = 1 / (static_cast<FLType>(sCeil - sFloor) * 3);
            const FLType offset = -static_cast<FLType>(sFloor) * 3;

            for (int j = 0; j < height; j++)
            {
                const T *rrow = Rsrcp + j * srcStride;
                const T *grow = Gsrcp + j * srcStride;
                const T *brow = Bsrcp + j * srcStride;
                float *drow = dstp + j * dstStride;
                for (int x = 0; x < width; x++)
                    drow[x] = static_cast<float>((rrow[x] + grow[x] + brow[x] + offset) * gain);
            }
        }
        else // RGBChannel
        {
            const T *Csrcp = d->channel == 0 ? Rsrcp : d->channel == 1 ? Gsrcp : Bsrcp;

            const FLType gain = 1 / static_cast<FLType>(sCeil - sFloor);
            const FLType offset = -static_cast<FLType>(sFloor);

            for (int j = 0; j < height; j++)
            {
                const T *srow = Csrcp + j * srcStride;
                float *drow = dstp + j * dstStride;
                for (int x = 0; x < width; x++)
                    drow[x] = static_cast<float>((srow[x] + offset) * gain);
            }
        }
    }
}

const VSFrame *VS_CC IntensityGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const IntensityData *d = reinterpret_cast<const IntensityData *>(instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);

        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, src, core);

        if (fi->bytesPerSample == 1)
            IntensityProcess<uint8_t>(d, src, dst, fi->bitsPerSample, vsapi);
        else
            IntensityProcess<uint16_t>(d, src, dst, fi->bitsPerSample, vsapi);

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC IntensityFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    IntensityData *d = reinterpret_cast<IntensityData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

} // namespace


VSNode *CreateIntensityNode(VSNode *src, bool fulls, IntensityMode mode, int channel,
    VSCore *core, const VSAPI *vsapi)
{
    IntensityData *d = new IntensityData();
    d->node = vsapi->addNodeRef(src);
    d->fulls = fulls;
    d->mode = mode;
    d->channel = channel;

    d->vi = *vsapi->getVideoInfo(src);
    vsapi->queryVideoFormat(&d->vi.format, cfGray, stFloat, 32, 0, 0, core);

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2("RetinexIntensity", &d->vi, IntensityGetFrame, IntensityFree,
        fmParallel, &dep, 1, d, core);

    if (!node)
    {
        vsapi->freeNode(d->node);
        delete d;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
