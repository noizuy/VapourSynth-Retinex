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

    virtual int arguments_process(const VSMap *in, VSMap *out)
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
    virtual void process_core8() { process_core<uint8_t>(); }
    virtual void process_core16() { process_core<uint16_t>(); }

public:
    MSRCRProcess(const MSRCRData &_d, int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *_vsapi)
        : MSRProcess(_d, n, frameCtx, core, _vsapi), d(_d) {}

    virtual ~MSRCRProcess() {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template < typename T >
void MSRCRProcess::process_core()
{
    int i, j, upper;

    // Calculate quantization parameters according to bit per sample and limited/full range
    // Floor and Ceil for limited range src will be determined later according to minimum and maximum value in the frame
    T sFloor = 0;
    T sCeil = (1 << bps) - 1;
    T dFloor = d.fulld ? 0 : 16 << (bps - 8);
    T dCeil = d.fulld ? (1 << bps) - 1 : 235 << (bps - 8);

    // Allocate floating point data buff
    FLType *idata = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType)*pcount, Alignment);
    FLType *odataR = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType)*pcount, Alignment);
    FLType *odataG = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType)*pcount, Alignment);
    FLType *odataB = vsh::vsh_aligned_malloc<FLType>(sizeof(FLType)*pcount, Alignment);

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

    // R/G/B intensity channels (already derived, normalized to [0,1], and blurred per scale
    // by the Intensity/blur nodes built at filter creation)

    // Apply MSR to floating point R channel
    CopyIntensity(idata, 0);
    MSRKernel(odataR, idata, 0);

    // Apply MSR to floating point G channel
    CopyIntensity(idata, 1);
    MSRKernel(odataG, idata, 1);

    // Apply MSR to floating point B channel
    CopyIntensity(idata, 2);
    MSRKernel(odataB, idata, 2);

    // Color restoration
    FLType RvalFL, GvalFL, BvalFL;
    FLType temp;

    for (j = 0; j < height; j++)
    {
        i = stride * j;
        for (upper = i + width; i < upper; i++)
        {
            RvalFL = Rsrcp[i] - sFloor;
            GvalFL = Gsrcp[i] - sFloor;
            BvalFL = Bsrcp[i] - sFloor;
            temp = RvalFL + GvalFL + BvalFL;
            temp = temp <= 0 ? 0 : d.restore / temp;
            odataR[i] *= log(RvalFL * temp + 1);
            odataG[i] *= log(GvalFL * temp + 1);
            odataB[i] *= log(BvalFL * temp + 1);
        }
    }

    // Simplest color balance with pixel clipping on either side of the dynamic range
    SimplestColorBalance(Rdstp, odataR, Rsrcp, dFloor, dCeil);
    SimplestColorBalance(Gdstp, odataG, Gsrcp, dFloor, dCeil);
    SimplestColorBalance(Bdstp, odataB, Bsrcp, dFloor, dCeil);

    // Free floating point data buff
    vsh::vsh_aligned_free(idata);
    vsh::vsh_aligned_free(odataR);
    vsh::vsh_aligned_free(odataG);
    vsh::vsh_aligned_free(odataB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif