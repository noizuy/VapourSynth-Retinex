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


#include "MSRCP.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const VSFrame *VS_CC MSRCPGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const MSRCPData *d = reinterpret_cast<MSRCPData *>(instanceData);

    if (activationReason == arInitial)
    {
        d->requestFrames(n, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        MSRCPProcess p(*d, n, frameCtx, core, vsapi);

        return p.process();
    }

    return nullptr;
}

void VS_CC MSRCPFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    MSRCPData *d = reinterpret_cast<MSRCPData *>(instanceData);

    delete d;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VS_CC MSRCPCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    MSRCPData *d = new MSRCPData(vsapi);

    if (d->arguments_process(in, out))
    {
        delete d;
        return;
    }

    if (!d->buildGraph(core, out))
    {
        delete d;
        return;
    }

    // Create filter
    std::vector<VSFilterDependency> deps;
    d->fillDependencies(deps);
    vsapi->createVideoFilter(out, "MSRCP", d->vi, MSRCPGetFrame, MSRCPFree, fmParallel,
        deps.data(), static_cast<int>(deps.size()), d, core);
    if (vsapi->mapGetError(out)) // the core doesn't free d on failure
        delete d;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
