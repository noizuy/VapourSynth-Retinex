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


#ifndef PLANEFILTER_H_
#define PLANEFILTER_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Single-plane GrayS -> GrayS node wrapper. Kernel is a copyable functor holding the filter's
// parameters, called once per frame as kernel(srcp, srcStride, dstp, dstStride, width, height)
// with strides in floats.
template < typename Kernel >
struct PlaneFilterData
{
    VSNode *node;
    VSVideoInfo vi;
    Kernel kernel;
};

template < typename Kernel >
const VSFrame *VS_CC PlaneFilterGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const PlaneFilterData<Kernel> *d = reinterpret_cast<const PlaneFilterData<Kernel> *>(instanceData);

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

        d->kernel(srcp, srcStride, dstp, dstStride, width, height);

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

template < typename Kernel >
void VS_CC PlaneFilterFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    PlaneFilterData<Kernel> *d = reinterpret_cast<PlaneFilterData<Kernel> *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

// Returns a new node reference, or nullptr on failure.
template < typename Kernel >
VSNode *CreatePlaneFilterNode(const char *name, VSNode *src, const Kernel &kernel, VSCore *core, const VSAPI *vsapi)
{
    PlaneFilterData<Kernel> *d = new PlaneFilterData<Kernel>{ vsapi->addNodeRef(src), *vsapi->getVideoInfo(src), kernel };

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2(name, &d->vi, PlaneFilterGetFrame<Kernel>, PlaneFilterFree<Kernel>,
        fmParallel, &dep, 1, d, core);

    // the core doesn't call the free callback when node creation fails
    if (!node)
        PlaneFilterFree<Kernel>(d, core, vsapi);

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
