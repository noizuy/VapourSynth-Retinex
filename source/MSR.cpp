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


#include "Helper.h"
#include "MSR.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MSRProcess::CopyIntensity(FLType *idata, int channel) const
{
    const VSFrame *frame = intensityFrame[channel];
    const int srcStride = static_cast<int>(vsapi->getStride(frame, 0) / sizeof(float));
    const float *srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 0));

    for (int j = 0; j < height; j++)
    {
        const float *srow = srcp + j * srcStride;
        FLType *drow = idata + j * stride;
        for (int x = 0; x < width; x++)
            drow[x] = static_cast<FLType>(srow[x]);
    }
}


int MSRProcess::MSRKernel(FLType *odata, const FLType *idata, int channel) const
{
    const size_t scount = d.scaleCount();

    // Active scales' blurred frames, in blurFrame[channel]'s order; inactive scales multiply by 2
    std::vector<const float *> illump;
    std::vector<int> illumStride;
    std::vector<bool> active(scount);
    size_t blurIdx = 0;
    for (size_t s = 0; s < scount; s++)
    {
        active[s] = d.scaleActive(s);
        if (!active[s]) continue;
        const VSFrame *bf = blurFrame[channel][blurIdx++];
        illump.push_back(reinterpret_cast<const float *>(vsapi->getReadPtr(bf, 0)));
        illumStride.push_back(static_cast<int>(vsapi->getStride(bf, 0) / sizeof(float)));
    }

    // Row-fused: product over scales, then log, in one pass per row (same operation order per pixel)
    for (int j = 0; j < height; j++)
    {
        FLType *orow = odata + j * stride;
        const FLType *irow = idata + j * stride;

        for (int x = 0; x < width; x++)
            orow[x] = 1;

        for (size_t s = 0, g = 0; s < scount; s++)
        {
            if (active[s])
            {
                const float *lrow = illump[g] + j * illumStride[g];
                g++;
                for (int x = 0; x < width; x++)
                {
                    const FLType lv = static_cast<FLType>(lrow[x]);
                    orow[x] *= lv <= 0 ? 1 : irow[x] / lv + 1;
                }
            }
            else
            {
                for (int x = 0; x < width; x++)
                    orow[x] *= FLType(2);
            }
        }

        for (int x = 0; x < width; x++)
            orow[x] = log(orow[x]) / static_cast<FLType>(scount);
    }

    return 0;
}


int MSRProcess::SimplestColorBalance(FLType *odata, const FLType *idata) const
{
    int i, j, upper;

    FLType offset, gain;
    FLType min = FLType_MAX;
    FLType max = -FLType_MAX;

    FLType FloorFL = 0;
    FLType CeilFL = 1;

    for (j = 0; j < height; j++)
    {
        i = stride * j;
        for (upper = i + width; i < upper; i++)
        {
            min = Min(min, odata[i]);
            max = Max(max, odata[i]);
        }
    }

    if (max <= min)
    {
        memcpy(odata, idata, sizeof(FLType)*pcount);
        return 1;
    }

    if (d.lower_thr > 0 || d.upper_thr > 0)
    {
        int h, HistBins = d.HistBins;
        int Count, MaxCount;

        int *Histogram = vsh::vsh_aligned_malloc<int>(sizeof(int)*HistBins, Alignment);
        memset(Histogram, 0, sizeof(int)*HistBins);

        gain = (HistBins - 1) / (max - min);
        offset = -min * gain;

        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
            {
                Histogram[static_cast<int>(odata[i] * gain + offset)]++;
            }
        }

        gain = (max - min) / (HistBins - 1);
        offset = min;

        Count = 0;
        MaxCount = static_cast<int>(width*height*d.lower_thr + 0.5);

        for (h = 0; h < HistBins; h++)
        {
            Count += Histogram[h];
            if (Count > MaxCount) break;
        }

        min = h * gain + offset;

        Count = 0;
        MaxCount = static_cast<int>(width*height*d.upper_thr + 0.5);

        for (h = HistBins - 1; h >= 0; h--)
        {
            Count += Histogram[h];
            if (Count > MaxCount) break;
        }

        max = h * gain + offset;

        vsh::vsh_aligned_free(Histogram);
    }

    gain = (CeilFL - FloorFL) / (max - min);
    offset = -min * gain + FloorFL;

    if (d.lower_thr > 0 || d.upper_thr > 0)
    {
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
                odata[i] = Clip(odata[i] * gain + offset, FloorFL, CeilFL);
        }
    }
    else
    {
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
                odata[i] = odata[i] * gain + offset;
        }
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
