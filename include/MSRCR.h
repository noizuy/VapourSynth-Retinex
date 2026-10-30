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


#ifndef MSRCR_H_
#define MSRCR_H_


#include "Helper.h"
#include "MSR.h"
#include "Simd.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const struct MSRCRPara
{
    double restore = 125.0;
} MSRCRDefault {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRCRData
    : public MSRData
{
public:
    double restore = MSRCRDefault.restore;

private:
    virtual void getChannels(std::vector<std::pair<IntensityMode, int>> &channels) const override
    {
        channels = { { IntensityMode::RGBChannel, 0 }, { IntensityMode::RGBChannel, 1 }, { IntensityMode::RGBChannel, 2 } };
    }

public:
    MSRCRData(const VSAPI *_vsapi = nullptr, std::string _FunctionName = "MSRCR")
        : MSRData(_vsapi, _FunctionName) {}

    ~MSRCRData() {}

    virtual int arguments_process(const VSMap *in, VSMap *out) override
    {
        if (MSRData::arguments_process(in, out))
            return 1;

        int error;

        if (vi->format.colorFamily != cfRGB)
        {
            setError(out, "Invalid input clip, only RGB format input supported");
            return 1;
        }

        restore = vsapi->mapGetFloat(in, "restore", 0, &error);
        if (error)
            restore = MSRCRDefault.restore;
        if (restore < 0)
        {
            setError(out, "Invalid \"restore\" assigned, must be non-negative float number");
            return 1;
        }

        return 0;
    }
};


void VS_CC MSRCRCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRCRProcess
    : public MSRProcess
{
private:
    const MSRCRData &d;

private:
    template < typename T >
    void process_core();

protected:
    virtual void process_core8() override { process_core<uint8_t>(); }
    virtual void process_core16() override { process_core<uint16_t>(); }

public:
    MSRCRProcess(const MSRCRData &_d, int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *_vsapi)
        : MSRProcess(_d, n, frameCtx, core, _vsapi), d(_d) {}

    virtual ~MSRCRProcess() {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arguments of the color restoration loop: each MSR channel is multiplied by log(C * restore / (R + G + B) + 1).
template < typename T >
struct MSRCRRestoreArgs
{
    const T *Rsrcp, *Gsrcp, *Bsrcp;
    float *odataR, *odataG, *odataB;
    int width, height, stride;
    T sFloor;
    float restore;
};

template < typename T >
RETINEX_FORCEINLINE void MSRCRRestoreImpl(const MSRCRRestoreArgs<T> &a,
    float &minR, float &maxR, float &minG, float &maxG, float &minB, float &maxB)
{
    minR = minG = minB = FLT_MAX;
    maxR = maxG = maxB = -FLT_MAX;

    // Locals: the row stores could alias the struct's float members, which would force reloads inside the loop
    const float restore = a.restore;
    const T sFloor = a.sFloor;

    for (int j = 0; j < a.height; j++)
    {
        const int r = a.stride * j;
        float *orowR = a.odataR + r;
        float *orowG = a.odataG + r;
        float *orowB = a.odataB + r;

        for (int x = 0; x < a.width; x++)
        {
            const int i = r + x;
            const float RvalFL = static_cast<float>(a.Rsrcp[i] - sFloor);
            const float GvalFL = static_cast<float>(a.Gsrcp[i] - sFloor);
            const float BvalFL = static_cast<float>(a.Bsrcp[i] - sFloor);
            // restore / (R + G + B), 0 where the sum is 0 (divisor guarded so that the select needs no branch)
            const float sum = RvalFL + GvalFL + BvalFL;
            const float temp = sum <= 0 ? 0 : restore / (sum <= 0 ? 1 : sum);
            orowR[x] *= FastLog(RvalFL * temp + 1);
            orowG[x] *= FastLog(GvalFL * temp + 1);
            orowB[x] *= FastLog(BvalFL * temp + 1);
        }

        RowMinMax(orowR, a.width, minR, maxR);
        RowMinMax(orowG, a.width, minG, maxG);
        RowMinMax(orowB, a.width, minB, maxB);
    }
}

#if RETINEX_AVX2_DISPATCH
template < typename T >
RETINEX_TARGET_AVX2 void MSRCRRestoreAvx2(const MSRCRRestoreArgs<T> &a,
    float &minR, float &maxR, float &minG, float &maxG, float &minB, float &maxB)
{
    MSRCRRestoreImpl(a, minR, maxR, minG, maxG, minB, maxB);
}
#endif

template < typename T >
void MSRCRRestore(const MSRCRRestoreArgs<T> &a,
    float &minR, float &maxR, float &minG, float &maxG, float &minB, float &maxB)
{
    RETINEX_DISPATCH(MSRCRRestoreAvx2(a, minR, maxR, minG, maxG, minB, maxB),
        MSRCRRestoreImpl(a, minR, maxR, minG, maxG, minB, maxB));
}


template < typename T >
void MSRCRProcess::process_core()
{
    int i, j, upper;
    float minR, maxR, minG, maxG, minB, maxB;

    // Calculate quantization parameters according to bit per sample and limited/full range
    // Floor and Ceil for limited range src will be determined later according to minimum and maximum value in the frame
    T sFloor = 0;
    T sCeil = (1 << bps) - 1;
    T dFloor = d.fulld ? 0 : 16 << (bps - 8);
    T dCeil = d.fulld ? (1 << bps) - 1 : 235 << (bps - 8);

    // Allocate floating point data buff
    float *idata = Scratch<float>(ScratchFrame, size_t(4) * pcount);
    float *odataR = idata + pcount;
    float *odataG = odataR + pcount;
    float *odataB = odataG + pcount;

    // Get read and write pointer for src and dst
    const T *Rsrcp, *Gsrcp, *Bsrcp;
    T *Rdstp, *Gdstp, *Bdstp;

    Rsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 0));
    Rdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 0));
    Gsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 1));
    Gdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 1));
    Bsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 2));
    Bdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 2));

    // If src is not of full range, determine the Floor and Ceil by the maximum and minimum value in the frame
    if (!d.fulls)
    {
        T min, max;

        min = sCeil;
        max = sFloor;
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
            {
                min = Min(min, Min(Rsrcp[i], Min(Gsrcp[i], Bsrcp[i])));
                max = Max(max, Max(Rsrcp[i], Max(Gsrcp[i], Bsrcp[i])));
            }
        }
        if (max > min)
        {
            sFloor = min;
            sCeil = max;
        }
    }

    // R/G/B channels in [0,1], from the Intensity nodes

    // Apply MSR to floating point R channel
    CopyIntensity(idata, 0);
    MSRKernel(odataR, idata, 0, minR, maxR);

    // Apply MSR to floating point G channel
    CopyIntensity(idata, 1);
    MSRKernel(odataG, idata, 1, minG, maxG);

    // Apply MSR to floating point B channel
    CopyIntensity(idata, 2);
    MSRKernel(odataB, idata, 2, minB, maxB);

    // Color restoration, tracking the range of each restored channel for the color balance
    // (the kernel's ranges no longer apply after the per-pixel restoration factor)
    MSRCRRestoreArgs<T> a;
    a.Rsrcp = Rsrcp; a.Gsrcp = Gsrcp; a.Bsrcp = Bsrcp;
    a.odataR = odataR; a.odataG = odataG; a.odataB = odataB;
    a.width = width; a.height = height; a.stride = stride;
    a.sFloor = sFloor;
    a.restore = static_cast<float>(d.restore);
    MSRCRRestore(a, minR, maxR, minG, maxG, minB, maxB);

    // Simplest color balance with pixel clipping on either side of the dynamic range
    SimplestColorBalance(Rdstp, odataR, Rsrcp, dFloor, dCeil, minR, maxR);
    SimplestColorBalance(Gdstp, odataG, Gsrcp, dFloor, dCeil, minG, maxG);
    SimplestColorBalance(Bdstp, odataB, Bsrcp, dFloor, dCeil, minB, maxB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif