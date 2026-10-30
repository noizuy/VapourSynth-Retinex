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


#ifndef MSRCP_H_
#define MSRCP_H_


#include "Helper.h"
#include "MSR.h"
#include "Simd.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const struct MSRCPPara
{
    double chroma_protect = 1.2;
} MSRCPDefault {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRCPData
    : public MSRData
{
public:
    double chroma_protect = MSRCPDefault.chroma_protect;

private:
    virtual void getChannels(std::vector<std::pair<IntensityMode, int>> &channels) const override
    {
        if (vi->format.colorFamily == cfRGB)
            channels = { { IntensityMode::RGBAvg, 0 } };
        else
            channels = { { IntensityMode::Y, 0 } };
    }

protected:
    int parseChromaProtect(const VSMap *in, VSMap *out)
    {
        int error;

        chroma_protect = vsapi->mapGetFloat(in, "chroma_protect", 0, &error);
        if (error)
            chroma_protect = MSRCPDefault.chroma_protect;
        if (chroma_protect < 1)
        {
            setError(out, "Invalid \"chroma_protect\" assigned, must be float number ranges in [1, +inf)");
            return 1;
        }

        return 0;
    }

public:
    MSRCPData(const VSAPI *_vsapi = nullptr, std::string _FunctionName = "MSRCP")
        : MSRData(_vsapi, _FunctionName) {}

    ~MSRCPData() {}

    virtual int arguments_process(const VSMap *in, VSMap *out) override
    {
        if (MSRData::arguments_process(in, out))
            return 1;
        return parseChromaProtect(in, out);
    }
};


void VS_CC MSRCPCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);

// Shared with GuidedRCPCreate.
const VSFrame *VS_CC MSRCPGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
void VS_CC MSRCPFree(void *instanceData, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRCPProcess
    : public MSRProcess
{
private:
    const MSRCPData &d;

private:
    template < typename T >
    void process_core();

protected:
    virtual void process_core8() override { process_core<uint8_t>(); }
    virtual void process_core16() override { process_core<uint16_t>(); }

public:
    MSRCPProcess(const MSRCPData &_d, int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *_vsapi)
        : MSRProcess(_d, n, frameCtx, core, _vsapi), d(_d) {}

    virtual ~MSRCPProcess() {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arguments of the YUV output loop: Y from the balanced intensity, U/V scaled by its gain over the input intensity.
template < typename T >
struct MSRCPYuvArgs
{
    T *Ydstp, *Udstp, *Vdstp;
    const T *Usrcp, *Vsrcp;
    const float *odata, *idata;
    int width, height, stride;
    int sNeutral;
    float sRangeC2, scale, dRange, offsetY, offsetC, peak;
    float protect_mul1, protect_mul2;
};

template < typename T, bool Protect >
RETINEX_FORCEINLINE void MSRCPYuvOutputImpl(const MSRCPYuvArgs<T> &a)
{
    for (int j = 0; j < a.height; j++)
    {
        const int r = a.stride * j;
        for (int x = 0; x < a.width; x++)
        {
            const int i = r + x;
            const int Uval = a.Usrcp[i] - a.sNeutral;
            const int Vval = a.Vsrcp[i] - a.sNeutral;

            // gain = O / I (chroma protect: log(O / I * (cp - 1) + 1) / log(cp)), 1 where I <= 0.
            // The ratio is computed for every pixel (with I replaced by 1 where I <= 0) and selected afterwards,
            // so the loop has no branch around the log and vectorizes.
            const bool valid = a.idata[i] > 0;
            const float ratio = a.odata[i] / (valid ? a.idata[i] : float(1));
            float gain = Protect ? FastLog(ratio * a.protect_mul1 + 1) * a.protect_mul2 : ratio;
            gain = valid ? gain : float(1);

            // Limit the gain so that U/V stay in range (division by 0 gives +inf, which Min ignores)
            gain = Min(a.sRangeC2 / static_cast<float>(Max(Abs(Uval), Abs(Vval))), gain) * a.scale;

            // Capped at the format's peak: the full range chroma offset (neutral + 0.499999) rounds to neutral + 0.5 in
            // float, so the largest U/V reach peak + 1. Through int: float -> int has a vector instruction,
            // float -> uint8/uint16 does not (same result in range).
            a.Ydstp[i] = static_cast<T>(static_cast<int>(Min(a.odata[i] * a.dRange + a.offsetY, a.peak)));
            a.Udstp[i] = static_cast<T>(static_cast<int>(Min(Uval * gain + a.offsetC, a.peak)));
            a.Vdstp[i] = static_cast<T>(static_cast<int>(Min(Vval * gain + a.offsetC, a.peak)));
        }
    }
}

#if RETINEX_AVX2_DISPATCH
template < typename T, bool Protect >
RETINEX_TARGET_AVX2 void MSRCPYuvOutputAvx2(const MSRCPYuvArgs<T> &a)
{
    MSRCPYuvOutputImpl<T, Protect>(a);
}
#endif

template < typename T >
void MSRCPYuvOutput(const MSRCPYuvArgs<T> &a, bool protect)
{
    if (protect)
        RETINEX_DISPATCH((MSRCPYuvOutputAvx2<T, true>(a)), (MSRCPYuvOutputImpl<T, true>(a)));
    else
        RETINEX_DISPATCH((MSRCPYuvOutputAvx2<T, false>(a)), (MSRCPYuvOutputImpl<T, false>(a)));
}


template < typename T >
void MSRCPProcess::process_core()
{
    int i, j, upper;
    float offset, scale;
    float omin, omax;
    T *Ydstp;

    // Calculate quantization parameters according to bit per sample and limited/full range
    // Floor and Ceil for limited range src will be determined later according to minimum and maximum value in the frame
    T sFloor = 0;
    //T sFloorC = 0;
    int sNeutral = 128 << (bps - 8);
    T sCeil = (1 << bps) - 1;
    //T sCeilC = (1 << bps) - 1;
    T sRange = d.fulls ? (1 << bps) - 1 : 219 << (bps - 8);
    T sRangeC = d.fulls ? (1 << bps) - 1 : 224 << (bps - 8);
    T dFloor = d.fulld ? 0 : 16 << (bps - 8);
    //T dFloorC = d.fulld ? 0 : 16 << (bps - 8);
    int dNeutral = 128 << (bps - 8);
    //T dCeil = d.fulld ? (1 << bps) - 1 : 235 << (bps - 8);
    //T dCeilC = d.fulld ? (1 << bps) - 1 : 240 << (bps - 8);
    T dRange = d.fulld ? (1 << bps) - 1 : 219 << (bps - 8);
    T dRangeC = d.fulld ? (1 << bps) - 1 : 224 << (bps - 8);
    //float sFloorCFL = static_cast<float>(sFloorC);
    //float sNeutralFL = static_cast<float>(sNeutral);
    //float sCeilFL = static_cast<float>(sCeil);
    //float sCeilCFL = static_cast<float>(sCeilC);
    float sRangeFL = static_cast<float>(sRange);
    float sRangeCFL = static_cast<float>(sRangeC);
    float sRangeC2FL = static_cast<float>(sRangeC) / 2.;
    float dFloorFL = static_cast<float>(dFloor);
    //float dFloorCFL = static_cast<float>(dFloorC);
    float dNeutralFL = static_cast<float>(dNeutral);
    //float dCeilFL = static_cast<float>(dCeil);
    //float dCeilCFL = static_cast<float>(dCeilC);
    float dRangeFL = static_cast<float>(dRange);
    float dRangeCFL = static_cast<float>(dRangeC);

    // Allocate floating point data buff
    float *idata = Scratch<float>(ScratchFrame, size_t(2) * pcount);
    float *odata = idata + pcount;

    if (fi->colorFamily == cfGray) // Procedure for Gray color family
    {
        // Get write pointer for dst
        Ydstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 0));

        // Intensity channel in [0,1], from the Intensity node
        CopyIntensity(idata, 0);

        // Apply MSR to floating point intensity channel
        MSRKernel(odata, idata, 0, omin, omax);
        // Simplest color balance with pixel clipping on either side of the dynamic range
        SimplestColorBalance(odata, idata, omin, omax);

        // Convert floating point intensity channel to integer Y channel
        offset = dFloorFL + float(0.5);
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
                Ydstp[i] = static_cast<T>(odata[i] * dRangeFL + offset);
        }
    }
    else if (fi->colorFamily == cfRGB) // Procedure for RGB color family
    {
        // Get read and write pointer for src and dst
        const T *Rsrcp, *Gsrcp, *Bsrcp;
        T *Rdstp, *Gdstp, *Bdstp;

        Rsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 0));
        Rdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 0));
        Gsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 1));
        Gdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 1));
        Bsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 2));
        Bdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 2));

        // sFloor/sCeil are needed to reconstruct RGB (the Intensity node detects them separately)
        if (!d.fulls)
        {
            // If src is of limited range, determine the Floor and Ceil by the minimum and maximum value in the frame
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
                //sCeilFL = static_cast<float>(sCeil);
            }
        }

        // Intensity channel in [0,1], from the Intensity node
        CopyIntensity(idata, 0);

        // Apply MSR to floating point intensity channel
        MSRKernel(odata, idata, 0, omin, omax);
        // Simplest color balance with pixel clipping on either side of the dynamic range
        SimplestColorBalance(odata, idata, omin, omax);

        // Adjust integer RGB channel according to filtering result in floating point intensity channel
        // (for full range in and out, sFloor is 0 and scale is 1, which leave the values unchanged)
        scale = dRangeFL / sRangeFL;
        offset = dFloorFL + float(0.5);

        for (j = 0; j < height; j++)
        {
            const int r = stride * j;
            for (int x = 0; x < width; x++)
            {
                const int k = r + x;
                const float Rval = static_cast<float>(Rsrcp[k] - sFloor);
                const float Gval = static_cast<float>(Gsrcp[k] - sFloor);
                const float Bval = static_cast<float>(Bsrcp[k] - sFloor);
                // O / I, 1 where I <= 0: computed for every pixel and selected afterwards, so the loop vectorizes
                float g = odata[k] / (idata[k] <= 0 ? 1 : idata[k]);
                g = idata[k] <= 0 ? 1 : g;
                g = Min(sRangeFL / Max(Rval, Max(Gval, Bval)), g) * scale;
                // Through int: float -> int has a vector instruction, float -> uint8/uint16 does not (same result in range)
                Rdstp[k] = static_cast<T>(static_cast<int>(Rval * g + offset));
                Gdstp[k] = static_cast<T>(static_cast<int>(Gval * g + offset));
                Bdstp[k] = static_cast<T>(static_cast<int>(Bval * g + offset));
            }
        }
    }
    else // Procedure for YUV color family
    {
        // Get read and write pointer for src and dst
        const T *Usrcp, *Vsrcp;
        T *Udstp, *Vdstp;

        Ydstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 0));
        Usrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 1));
        Udstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 1));
        Vsrcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, 2));
        Vdstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, 2));

        // Intensity channel in [0,1], from the Intensity node
        CopyIntensity(idata, 0);

        // Apply MSR to floating point intensity channel
        MSRKernel(odata, idata, 0, omin, omax);
        // Simplest color balance with pixel clipping on either side of the dynamic range
        SimplestColorBalance(odata, idata, omin, omax);

        // Convert floating point intensity channel to integer Y channel
        // Adjust integer UV channel according to filtering result in floating point intensity channel
        // Chroma protect uses log function to attenuate the adjustment in UV channel
        float chroma_protect_mul1 = static_cast<float>(d.chroma_protect - 1);
        float chroma_protect_mul2 = static_cast<float>(1 / log(d.chroma_protect));

        MSRCPYuvArgs<T> a;
        a.Ydstp = Ydstp; a.Udstp = Udstp; a.Vdstp = Vdstp;
        a.Usrcp = Usrcp; a.Vsrcp = Vsrcp;
        a.odata = odata; a.idata = idata;
        a.width = width; a.height = height; a.stride = stride;
        a.sNeutral = sNeutral;
        a.sRangeC2 = sRangeC2FL;
        a.scale = dRangeCFL / sRangeCFL;
        a.dRange = dRangeFL;
        a.offsetY = dFloorFL + float(0.5);
        a.offsetC = dNeutralFL + (d.fulld ? float(0.499999) : float(0.5));
        a.peak = static_cast<float>((1 << bps) - 1);
        a.protect_mul1 = chroma_protect_mul1;
        a.protect_mul2 = chroma_protect_mul2;
        MSRCPYuvOutput(a, d.chroma_protect > 1);
    }

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
