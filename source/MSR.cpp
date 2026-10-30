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
#include "Simd.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MSRProcess::CopyIntensity(float *idata, int channel) const
{
    const VSFrame *frame = intensityFrame[channel];
    const int srcStride = static_cast<int>(vsapi->getStride(frame, 0) / sizeof(float));
    const float *srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(frame, 0));

    for (int j = 0; j < height; j++)
        memcpy(idata + j * stride, srcp + j * srcStride, sizeof(float) * width);
}


namespace
{

// Per-row product over scales of (I / L + 1), then log averaged over scales, and the range of the result.
// Inactive scales (sigma 0) multiply by 2, so they still count in the average.
RETINEX_FORCEINLINE void MSRKernelImpl(float *odata, const float *idata, int width, int height, int stride,
    const float *const *illump, const int *illumStride, const char *active, size_t scount, float &min, float &max)
{
    float omin = FLT_MAX;
    float omax = -FLT_MAX;

    for (int j = 0; j < height; j++)
    {
        float *orow = odata + j * stride;
        const float *irow = idata + j * stride;

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
                    const float lv = lrow[x];
                    orow[x] *= lv <= 0 ? 1 : irow[x] / lv + 1;
                }
            }
            else
            {
                for (int x = 0; x < width; x++)
                    orow[x] *= float(2);
            }
        }

        // The product is >= 1; capping it at FLT_MAX (a tiny illumination can overflow it) keeps FastLog in range
        for (int x = 0; x < width; x++)
            orow[x] = FastLog(Min(orow[x], FLT_MAX)) / static_cast<float>(scount);

        RowMinMax(orow, width, omin, omax);
    }

    min = omin;
    max = omax;
}

#if RETINEX_AVX2_DISPATCH
RETINEX_TARGET_AVX2 void MSRKernelAvx2(float *odata, const float *idata, int width, int height, int stride,
    const float *const *illump, const int *illumStride, const char *active, size_t scount, float &min, float &max)
{
    MSRKernelImpl(odata, idata, width, height, stride, illump, illumStride, active, scount, min, max);
}
#endif

} // namespace


int MSRProcess::MSRKernel(float *odata, const float *idata, int channel, float &min, float &max) const
{
    const size_t scount = d.scaleCount();

    // Active scales' blurred frames, in blurFrame[channel]'s order
    std::vector<const float *> illump;
    std::vector<int> illumStride;
    std::vector<char> active(scount);
    size_t blurIdx = 0;
    for (size_t s = 0; s < scount; s++)
    {
        active[s] = d.scaleActive(s);
        if (!active[s]) continue;
        const VSFrame *bf = blurFrame[channel][blurIdx++];
        illump.push_back(reinterpret_cast<const float *>(vsapi->getReadPtr(bf, 0)));
        illumStride.push_back(static_cast<int>(vsapi->getStride(bf, 0) / sizeof(float)));
    }

    RETINEX_DISPATCH(
        MSRKernelAvx2(odata, idata, width, height, stride, illump.data(), illumStride.data(), active.data(), scount, min, max),
        MSRKernelImpl(odata, idata, width, height, stride, illump.data(), illumStride.data(), active.data(), scount, min, max));
    return 0;
}


void MSRProcess::HistogramClipRange(const float *odata, float &min, float &max) const
{
    int h, HistBins = d.HistBins;
    int Count, MaxCount;

    // 4 interleaved sub-histograms, summed afterwards: consecutive pixels usually fall into the
    // same bin, and a single histogram serializes those increments through one memory location
    int *Histogram = vsh::vsh_aligned_malloc<int>(sizeof(int)*HistBins * 4, Alignment);
    memset(Histogram, 0, sizeof(int)*HistBins * 4);
    int *Histogram1 = Histogram + HistBins;
    int *Histogram2 = Histogram1 + HistBins;
    int *Histogram3 = Histogram2 + HistBins;

    float gain = (HistBins - 1) / (max - min);
    float offset = -min * gain;

    for (int j = 0; j < height; j++)
    {
        const float *row = odata + stride * j;
        int x = 0;
        for (; x + 4 <= width; x += 4)
        {
            Histogram[static_cast<int>(row[x] * gain + offset)]++;
            Histogram1[static_cast<int>(row[x + 1] * gain + offset)]++;
            Histogram2[static_cast<int>(row[x + 2] * gain + offset)]++;
            Histogram3[static_cast<int>(row[x + 3] * gain + offset)]++;
        }
        for (; x < width; x++)
            Histogram[static_cast<int>(row[x] * gain + offset)]++;
    }

    for (h = 0; h < HistBins; h++)
        Histogram[h] += Histogram1[h] + Histogram2[h] + Histogram3[h];

    gain = (max - min) / (HistBins - 1);
    offset = min;

    Count = 0;
    MaxCount = static_cast<int>(width*height*d.lower_thr + 0.5);

    for (h = 0; h < HistBins; h++)
    {
        Count += Histogram[h];
        if (Count > MaxCount) break;
    }

    const float lower = h * gain + offset;

    Count = 0;
    MaxCount = static_cast<int>(width*height*d.upper_thr + 0.5);

    for (h = HistBins - 1; h >= 0; h--)
    {
        Count += Histogram[h];
        if (Count > MaxCount) break;
    }

    max = h * gain + offset;
    min = lower;

    vsh::vsh_aligned_free(Histogram);
}


int MSRProcess::SimplestColorBalance(float *odata, const float *idata, float min, float max) const
{
    int i, j, upper;

    float offset, gain;

    float FloorFL = 0;
    float CeilFL = 1;

    if (max <= min)
    {
        memcpy(odata, idata, sizeof(float)*pcount);
        return 1;
    }

    if (d.lower_thr > 0 || d.upper_thr > 0)
        HistogramClipRange(odata, min, max);

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
