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


#include "GaussBlur.h"
#include "Device.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VS_CC GaussBlurCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    VSNode *src = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(src);

    auto fail = [&](const std::string &msg)
    {
        vsapi->mapSetError(out, ("retinex.GaussBlur: " + msg).c_str());
        vsapi->freeNode(src);
    };

    if (vi->format.colorFamily != cfGray || vi->format.sampleType != stFloat || vi->format.bitsPerSample != 32)
        return fail("clip must be 32-bit float Gray (GrayS) with a constant format");

    const double sigma = vsapi->mapGetFloat(in, "sigma", 0, nullptr);
    if (!std::isfinite(sigma) || sigma < 0)
        return fail("\"sigma\" must be a finite non-negative number");

    int error;
    const char *devStr = vsapi->mapGetData(in, "device", 0, &error);
    BlurDevice device = BlurDevice::CPU_BOX;
    if (!error && !ParseDevice(devStr, device))
        return fail("invalid \"device\", must be " + DeviceNameList());

    if (sigma == 0)
    {
        vsapi->mapConsumeNode(out, "clip", src, maReplace);
        return;
    }

    std::string errMsg;
    VSNode *node = BuildGaussianBlurNode(src, sigma, device, 0, 1, -1, core, vsapi, errMsg);
    if (!node)
        return fail(errMsg);

    vsapi->freeNode(src);
    vsapi->mapConsumeNode(out, "clip", node, maReplace);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
