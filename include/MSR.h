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


#ifndef MSR_H_
#define MSR_H_


#include "Helper.h"
#include "Device.h"
#include "Intensity.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const struct MSRPara
{
    std::vector<double> sigma;

    double lower_thr = 0.001;
    double upper_thr = 0.001;
    int HistBins = 4096;

    bool fulls = true;
    bool fulld = fulls;

    MSRPara() : sigma({ 25, 80, 250 }) {}
} MSRDefault;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRData
    : public VSData
{
public:
    std::vector<double> sigma;

    double lower_thr = MSRDefault.lower_thr;
    double upper_thr = MSRDefault.upper_thr;
    int HistBins = MSRDefault.HistBins;

    bool fulls = MSRDefault.fulls;
    bool fulld = MSRDefault.fulld;

    // One entry per sigma scale (expanded to sigma.size(), repeating the last given entry).
    std::vector<BlurDevice> device;
    std::vector<int64_t> device_id;
    std::vector<int64_t> num_streams;
    std::vector<int64_t> tune; // -1 means "not specified" (gpu/OpenCL only)

    // Built by buildGraph() once arguments are known: one intensity node per channel
    // (1 for MSRCP, 3 - R/G/B - for MSRCR), and per channel one blurred node per active
    // (sigma > 0) scale, in the same order as sigma itself.
    int channelCount = 0;
    VSNode *intensityNode[VSMaxPlaneCount] = { nullptr, nullptr, nullptr };
    std::vector<VSNode *> blurNode[VSMaxPlaneCount];

private:
    void fulls_select()
    {
        if (vi->format.colorFamily == cfGray || vi->format.colorFamily == cfYUV)
            fulls = false;
        else if (vi->format.colorFamily == cfRGB)
            fulls = true;
    }

    // channels[c] == (intensity mode, plane/channel index used when mode is RGBChannel).
    virtual void getChannels(std::vector<std::pair<IntensityMode, int>> &channels) const = 0;

protected:
    // Input format/range validation + lower_thr/upper_thr/fulls/fulld parsing: shared by
    // every MSRData subclass regardless of how they estimate illumination per scale.
    int parseCommon(const VSMap *in, VSMap *out)
    {
        int error;

        node = vsapi->mapGetNode(in, "input", 0, nullptr);
        vi = vsapi->getVideoInfo(node);

        if (!vsh::isConstantVideoFormat(vi))
        {
            setError(out, "Invalid input clip, only constant format input supported");
            return 1;
        }
        if (vi->format.sampleType != stInteger || (vi->format.bytesPerSample != 1 && vi->format.bytesPerSample != 2))
        {
            setError(out, "Invalid input clip, only 8-16 bit int formats supported");
            return 1;
        }
        if (vi->format.subSamplingH || vi->format.subSamplingW)
        {
            setError(out, "sub-sampled format is not supported, convert it to YUV444 or RGB first");
            return 1;
        }

        lower_thr = vsapi->mapGetFloat(in, "lower_thr", 0, &error);
        if (error)
            lower_thr = MSRDefault.lower_thr;
        if (lower_thr < 0)
        {
            setError(out, "Invalid \"lower_thr\" assigned, must be float number ranges in [0, 1)");
            return 1;
        }

        upper_thr = vsapi->mapGetFloat(in, "upper_thr", 0, &error);
        if (error)
            upper_thr = MSRDefault.upper_thr;
        if (upper_thr < 0)
        {
            setError(out, "Invalid \"upper_thr\" assigned, must be float number ranges in [0, 1)");
            return 1;
        }

        if (lower_thr + upper_thr >= 1)
        {
            setError(out, "Invalid \"lower_thr\" and \"upper_thr\" assigned, the sum of which mustn't equal or exceed 1");
            return 1;
        }

        fulls = vsapi->mapGetInt(in, "fulls", 0, &error) == 0 ? false : true;
        if (error)
            fulls_select();

        fulld = vsapi->mapGetInt(in, "fulld", 0, &error) == 0 ? false : true;
        if (error)
            fulld = fulls;

        return 0;
    }

    // sigma + device/device_id/num_streams/tune parsing, for Gaussian-blur-backed
    // subclasses (MSRCPData/MSRCRData) only; a non-Gaussian subclass (e.g. GuidedRCPData)
    // calls parseCommon() instead and parses its own scale parameters.
    int parseGaussianScales(const VSMap *in, VSMap *out)
    {
        int i, m;

        m = vsapi->mapNumElements(in, "sigma");
        if (m > 0)
        {
            for (i = 0; i < m; i++)
            {
                sigma.push_back(vsapi->mapGetFloat(in, "sigma", i, nullptr));

                if (sigma[i] < 0)
                {
                    setError(out, "Invalid \"sigma\" assigned, must be non-negative float number");
                    return 1;
                }
            }
        }
        else
        {
            sigma = MSRDefault.sigma;
        }

        size_t s, scount = sigma.size();

        for (s = 0; s < scount; s++)
        {
            if (sigma[s] > 0) break;
        }
        if (s >= scount)
        {
            for (i = 0; i < VSMaxPlaneCount; i++)
                process[i] = 0;
        }

        // device / device_id / num_streams / tune: one entry per sigma scale. Fewer entries
        // than sigma are allowed; the last given entry is repeated for the remaining scales.
        m = vsapi->mapNumElements(in, "device");
        for (i = 0; i < m; i++)
        {
            const char *devStr = vsapi->mapGetData(in, "device", i, nullptr);
            BlurDevice dev;
            if (!ParseDevice(devStr, dev))
            {
                setError(out, "Invalid \"device\" assigned, must be \"cpu_box\", \"cpu_iir\", \"gpu\" or \"cuda\"");
                return 1;
            }
            device.push_back(dev);
        }
        if (device.empty())
            device.push_back(BlurDevice::CPU_BOX);
        while (device.size() < scount)
            device.push_back(device.back());

        m = vsapi->mapNumElements(in, "device_id");
        for (i = 0; i < m; i++)
        {
            int64_t id = vsapi->mapGetInt(in, "device_id", i, nullptr);
            if (id < 0)
            {
                setError(out, "Invalid \"device_id\" assigned, must be a non-negative integer");
                return 1;
            }
            device_id.push_back(id);
        }
        if (device_id.empty())
            device_id.push_back(0);
        while (device_id.size() < scount)
            device_id.push_back(device_id.back());

        m = vsapi->mapNumElements(in, "num_streams");
        for (i = 0; i < m; i++)
        {
            int64_t ns = vsapi->mapGetInt(in, "num_streams", i, nullptr);
            if (ns < 1)
            {
                setError(out, "Invalid \"num_streams\" assigned, must be a positive integer");
                return 1;
            }
            num_streams.push_back(ns);
        }
        if (num_streams.empty())
            num_streams.push_back(1);
        while (num_streams.size() < scount)
            num_streams.push_back(num_streams.back());

        m = vsapi->mapNumElements(in, "tune");
        for (i = 0; i < m; i++)
            tune.push_back(vsapi->mapGetInt(in, "tune", i, nullptr));
        if (tune.empty())
            tune.push_back(-1);
        while (tune.size() < scount)
            tune.push_back(tune.back());

        return 0;
    }

public:
    MSRData(const VSAPI *_vsapi = nullptr, std::string _FunctionName = "MSR", std::string _NameSpace = "retinex")
        : VSData(_vsapi, _FunctionName, _NameSpace) {}

    virtual ~MSRData()
    {
        for (int c = 0; c < VSMaxPlaneCount; c++)
        {
            if (intensityNode[c]) vsapi->freeNode(intensityNode[c]);
            for (VSNode *n : blurNode[c])
                vsapi->freeNode(n);
        }
    }

    virtual int arguments_process(const VSMap *in, VSMap *out)
    {
        if (parseCommon(in, out))
            return 1;
        if (parseGaussianScales(in, out))
            return 1;
        return 0;
    }

    // Number of illumination-estimate "scales" combined per channel. Default: one per
    // sigma entry; a non-Gaussian subclass overrides this along with scaleActive() and
    // buildScaleNode() below to key off its own per-scale parameters instead.
    virtual size_t scaleCount() const { return sigma.size(); }

    // Whether scale s contributes an actual filtered estimate, vs. the trivial "skip"
    // case (sigma/radius <= 0) that contributes a constant factor instead.
    virtual bool scaleActive(size_t s) const { return sigma[s] > 0; }

    // Builds the illumination-estimate node for scale s of a given channel's intensity
    // node. Only called when scaleActive(s) is true.
    virtual VSNode *buildScaleNode(VSNode *channelNode, size_t s, VSCore *core, const VSAPI *vsapi, std::string &errMsg) const
    {
        return BuildGaussianBlurNode(channelNode, sigma[s], device[s], device_id[s], num_streams[s], tune[s], core, vsapi, errMsg);
    }

    // Builds the intensity-extraction and per-scale illumination-estimate node graph. Must
    // be called after arguments_process() succeeds and before the filter node itself is
    // created. Returns false (and sets an error on out) on failure, e.g. a required backend
    // plugin is missing.
    bool buildGraph(VSCore *core, VSMap *out)
    {
        std::vector<std::pair<IntensityMode, int>> channels;
        getChannels(channels);
        channelCount = static_cast<int>(channels.size());

        for (int c = 0; c < channelCount; c++)
        {
            intensityNode[c] = CreateIntensityNode(node, fulls, channels[c].first, channels[c].second, core, vsapi);
            if (!intensityNode[c])
            {
                setError(out, "failed to build the intensity extraction node");
                return false;
            }

            for (size_t s = 0; s < scaleCount(); s++)
            {
                if (!scaleActive(s)) continue;

                std::string errMsg;
                VSNode *blurred = buildScaleNode(intensityNode[c], s, core, vsapi, errMsg);
                if (!blurred)
                {
                    setError(out, errMsg.c_str());
                    return false;
                }
                blurNode[c].push_back(blurred);
            }
        }

        return true;
    }

    // Requests (arInitial) or must be paired with a matching getFrameFilter (arAllFramesReady)
    // for every auxiliary node this instance built, in addition to the main input node.
    void requestFrames(int n, VSFrameContext *frameCtx) const
    {
        vsapi->requestFrameFilter(n, node, frameCtx);
        for (int c = 0; c < channelCount; c++)
        {
            vsapi->requestFrameFilter(n, intensityNode[c], frameCtx);
            for (VSNode *bn : blurNode[c])
                vsapi->requestFrameFilter(n, bn, frameCtx);
        }
    }

    // Fills dependencies with every node this instance built (main input, intensity nodes,
    // blurred nodes), for use with createVideoFilter.
    void fillDependencies(std::vector<VSFilterDependency> &dependencies) const
    {
        dependencies.push_back({ node, rpStrictSpatial });
        for (int c = 0; c < channelCount; c++)
        {
            dependencies.push_back({ intensityNode[c], rpStrictSpatial });
            for (VSNode *bn : blurNode[c])
                dependencies.push_back({ bn, rpStrictSpatial });
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MSRProcess
    : public VSProcess
{
private:
    const MSRData &d;

protected:
    // Fetched once per GetFrame call, in the constructor, for every node d built; freed in
    // the destructor. Indexed the same way as d.intensityNode / d.blurNode.
    const VSFrame *intensityFrame[VSMaxPlaneCount] = { nullptr, nullptr, nullptr };
    std::vector<const VSFrame *> blurFrame[VSMaxPlaneCount];

private:
    template < typename T >
    void process_core();

protected:
    virtual void process_core8() = 0;
    virtual void process_core16() = 0;

public:
    MSRProcess(const MSRData &_d, int n, VSFrameContext *frameCtx, VSCore *core, const VSAPI *_vsapi)
        : VSProcess(_d, n, frameCtx, core, _vsapi), d(_d)
    {
        for (int c = 0; c < d.channelCount; c++)
        {
            intensityFrame[c] = vsapi->getFrameFilter(n, d.intensityNode[c], frameCtx);
            for (VSNode *bn : d.blurNode[c])
                blurFrame[c].push_back(vsapi->getFrameFilter(n, bn, frameCtx));
        }
    }

    virtual ~MSRProcess()
    {
        for (int c = 0; c < d.channelCount; c++)
        {
            if (intensityFrame[c]) vsapi->freeFrame(intensityFrame[c]);
            for (const VSFrame *f : blurFrame[c])
                vsapi->freeFrame(f);
        }
    }

    // Copies the pre-computed [0,1] intensity channel for the given channel (as built by the
    // Intensity node) into idata, using this process's own buffer stride.
    void CopyIntensity(FLType *idata, int channel) const;

    // Multi Scale Retinex process kernel for floating point data: combines the pre-blurred
    // per-scale frames for the given channel with idata into odata (log-average form).
    int MSRKernel(FLType *odata, const FLType *idata, int channel) const;

    // Simplest color balance with pixel clipping on either side of the dynamic range
    int SimplestColorBalance(FLType *odata, const FLType *idata) const; // odata as input and output, idata as source
    template < typename T >
    int SimplestColorBalance(T *dst, FLType *odata, const T *src, T dFloor, T dCeil) const; // odata as input, dst as output, src as source
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template < typename T >
int MSRProcess::SimplestColorBalance(T *dst, FLType *odata, const T *src, T dFloor, T dCeil) const
{
    int i, j, upper;

    FLType offset, gain;
    FLType min = FLType_MAX;
    FLType max = -FLType_MAX;

    FLType dFloorFL = static_cast<FLType>(dFloor);
    FLType dCeilFL = static_cast<FLType>(dCeil);
    FLType dRangeFL = dCeilFL - dFloorFL;

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
        memcpy(dst, src, sizeof(T)*pcount);
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

    gain = dRangeFL / (max - min);
    offset = -min * gain + dFloorFL + FLType(0.5);

    if (d.lower_thr > 0 || d.upper_thr > 0)
    {
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
                dst[i] = static_cast<T>(Clip(odata[i] * gain + offset, dFloorFL, dCeilFL));
        }
    }
    else
    {
        for (j = 0; j < height; j++)
        {
            i = stride * j;
            for (upper = i + width; i < upper; i++)
                dst[i] = static_cast<T>(odata[i] * gain + offset);
        }
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
