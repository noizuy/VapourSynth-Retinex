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


#include "MSRCR.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const VSFrame *VS_CC MSRCRGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const MSRCRData *d = reinterpret_cast<MSRCRData *>(instanceData);

    if (activationReason == arInitial)
    {
        d->requestFrames(n, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        MSRCRProcess p(*d, n, frameCtx, core, vsapi);

        return p.process();
    }

    return nullptr;
}

void VS_CC MSRCRFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    MSRCRData *d = reinterpret_cast<MSRCRData *>(instanceData);

    delete d;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VS_CC MSRCRCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    MSRCRData *d = new MSRCRData(vsapi);

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
    vsapi->createVideoFilter(out, "MSRCR", d->vi, MSRCRGetFrame, MSRCRFree, fmParallel,
        deps.data(), static_cast<int>(deps.size()), d, core);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
