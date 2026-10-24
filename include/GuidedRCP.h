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


#ifndef GUIDEDRCP_H_
#define GUIDEDRCP_H_


#include "Helper.h"
#include "MSRCP.h"
#include "GuidedFilter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Guided_RCP: MSRCP's chromaticity-preserving reconstruction with the multi-scale Gaussian
// illumination estimate replaced by a single Fast Guided Filter pass (edge-aware, so a single
// scale is enough to avoid halos). scaleCount() is always 1.
class GuidedRCPData
    : public MSRCPData
{
public:
    std::vector<int> radius;   // single element; kept as a vector purely to reuse MSRData's scale machinery
    std::vector<double> eps;
    int subsample = 4;

public:
    GuidedRCPData(const VSAPI *_vsapi = nullptr)
        : MSRCPData(_vsapi, "Guided_RCP") {}

    virtual size_t scaleCount() const override { return radius.size(); }
    virtual bool scaleActive(size_t s) const override { return radius[s] > 0; }

    virtual VSNode *buildScaleNode(VSNode *channelNode, size_t s, VSCore *core, const VSAPI *vsapi, std::string &errMsg) const override
    {
        VSNode *node = CreateGuidedFilterNode(channelNode, radius[s], eps[s], subsample, core, vsapi);
        if (!node)
            errMsg = "failed to build the guided filter node";
        return node;
    }

    virtual int arguments_process(const VSMap *in, VSMap *out) override
    {
        if (parseCommon(in, out))
            return 1;

        int error;

        int64_t r = vsapi->mapGetInt(in, "radius", 0, &error);
        if (error)
            r = 20;
        if (r < 0)
        {
            setError(out, "Invalid \"radius\" assigned, must be a non-negative integer");
            return 1;
        }
        radius.push_back(static_cast<int>(r));
        if (r <= 0)
        {
            for (int i = 0; i < VSMaxPlaneCount; i++)
                process[i] = 0;
        }

        double e = vsapi->mapGetFloat(in, "eps", 0, &error);
        if (error)
            e = 0.01;
        if (e <= 0)
        {
            setError(out, "Invalid \"eps\" assigned, must be a positive float number");
            return 1;
        }
        eps.push_back(e);

        int64_t ss = vsapi->mapGetInt(in, "subsample", 0, &error);
        if (error)
            ss = 4;
        if (ss < 1)
        {
            setError(out, "Invalid \"subsample\" assigned, must be a positive integer");
            return 1;
        }
        subsample = static_cast<int>(ss);

        return parseChromaProtect(in, out);
    }
};


void VS_CC GuidedRCPCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
