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


#include "GuidedRCP.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VS_CC GuidedRCPCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    GuidedRCPData *d = new GuidedRCPData(vsapi);

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

    // Create filter. MSRCPGetFrame/MSRCPFree operate generically on a MSRCPData reference,
    // so they work unchanged for GuidedRCPData too.
    std::vector<VSFilterDependency> deps;
    d->fillDependencies(deps);
    vsapi->createVideoFilter(out, "Guided_RCP", d->vi, MSRCPGetFrame, MSRCPFree, fmParallel,
        deps.data(), static_cast<int>(deps.size()), d, core);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
