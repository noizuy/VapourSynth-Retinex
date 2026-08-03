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


#include "Device.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseDevice(const std::string &str, BlurDevice &device)
{
    if (str == "cpu") { device = BlurDevice::CPU; return true; }
    if (str == "gpu") { device = BlurDevice::GPU; return true; }
    if (str == "cuda") { device = BlurDevice::CUDA; return true; }
    return false;
}

const char *DeviceName(BlurDevice device)
{
    switch (device)
    {
    case BlurDevice::CPU: return "cpu";
    case BlurDevice::GPU: return "gpu";
    case BlurDevice::CUDA: return "cuda";
    default: return "";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

const double Pi = 3.1415926535897932384626433832795;

// Userdata carried by the VSFunction passed to resize2.Custom as custom_kernel.
struct GaussianKernelData
{
    double sigma;
};

void VS_CC FreeGaussianKernelData(void *userData)
{
    delete reinterpret_cast<GaussianKernelData *>(userData);
}

// Implements resize2's custom_kernel protocol: reads "x" from in, writes "val" to out.
void VS_CC GaussianKernelFunc(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    const GaussianKernelData *data = reinterpret_cast<const GaussianKernelData *>(userData);
    const double sigma = data->sigma;

    const double x = vsapi->mapGetFloat(in, "x", 0, nullptr);
    const double value = 1. / (sigma * sqrt(2 * Pi)) * exp(-(x * x) / (2 * sigma * sigma));

    vsapi->mapSetFloat(out, "val", value, maReplace);
}

// Invokes plugin.function(args), consuming args, and extracts the returned "clip".
VSNode *InvokeGetClip(VSMap *args, VSPlugin *plugin, const char *function, const VSAPI *vsapi, std::string &errMsg)
{
    VSMap *out = vsapi->invoke(plugin, function, args);
    vsapi->freeMap(args);

    const char *err = vsapi->mapGetError(out);
    if (err)
    {
        errMsg = err;
        vsapi->freeMap(out);
        return nullptr;
    }

    int error;
    VSNode *node = vsapi->mapGetNode(out, "clip", 0, &error);
    vsapi->freeMap(out);

    if (error || !node)
    {
        errMsg = std::string(function) + " did not return a clip";
        return nullptr;
    }

    return node;
}

VSNode *BuildGpuCudaBlurNode(VSNode *src, double sigma, BlurDevice device,
    int64_t device_id, int64_t num_streams, int64_t tune,
    VSCore *core, const VSAPI *vsapi, std::string &errMsg)
{
    const char *identifier = device == BlurDevice::CUDA ? "com.julek.vszipcu" : "com.julek.vszipcl";
    const char *packageName = device == BlurDevice::CUDA ? "vapoursynth-zipcu" : "vapoursynth-zipcl";

    VSPlugin *plugin = vsapi->getPluginByID(identifier, core);
    if (!plugin)
    {
        errMsg = std::string("the \"") + DeviceName(device) + "\" device requires the " + packageName +
            " plugin (" + identifier + "), which is not installed";
        return nullptr;
    }

    VSMap *args = vsapi->createMap();
    vsapi->mapConsumeNode(args, "clip", vsapi->addNodeRef(src), maReplace);
    vsapi->mapSetFloat(args, "sigma", sigma, maReplace);
    vsapi->mapSetInt(args, "device_id", device_id, maReplace);
    vsapi->mapSetInt(args, "num_streams", num_streams, maReplace);
    if (device == BlurDevice::GPU && tune >= 0)
        vsapi->mapSetInt(args, "tune", tune, maReplace);

    return InvokeGetClip(args, plugin, "GaussBlur", vsapi, errMsg);
}

VSNode *BuildCpuBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi, std::string &errMsg)
{
    VSPlugin *plugin = vsapi->getPluginByID("dev.setsugen.resize2", core);
    if (!plugin)
    {
        errMsg = "the \"cpu\" device requires the vapoursynth-resize2 plugin (dev.setsugen.resize2), which is not installed";
        return nullptr;
    }

    const VSVideoInfo *vi = vsapi->getVideoInfo(src);

    // Support radius covering ~99.7% of the Gaussian's mass, matching the taps auto-derived
    // by vszipcu/vszipcl's own Gaussian kernels (roughly 3*sigma on either side of center).
    int64_t taps = static_cast<int64_t>(std::ceil(sigma * 3));
    if (taps < 1) taps = 1;

    GaussianKernelData *kernelData = new GaussianKernelData{ sigma };
    VSFunction *func = vsapi->createFunction(GaussianKernelFunc, kernelData, FreeGaussianKernelData, core);

    VSMap *args = vsapi->createMap();
    vsapi->mapConsumeNode(args, "clip", vsapi->addNodeRef(src), maReplace);
    vsapi->mapConsumeFunction(args, "custom_kernel", func, maReplace);
    vsapi->mapSetInt(args, "taps", taps, maReplace);
    vsapi->mapSetInt(args, "width", vi->width, maReplace);
    vsapi->mapSetInt(args, "height", vi->height, maReplace);
    // Without this, resize2 shortcuts same-size/no-shift resizes to a plain frame copy,
    // skipping the convolution (and our custom kernel) entirely.
    vsapi->mapSetInt(args, "force", 1, maReplace);

    return InvokeGetClip(args, plugin, "Custom", vsapi, errMsg);
}

} // namespace


VSNode *BuildGaussianBlurNode(VSNode *src, double sigma, BlurDevice device,
    int64_t device_id, int64_t num_streams, int64_t tune,
    VSCore *core, const VSAPI *vsapi, std::string &errMsg)
{
    if (device == BlurDevice::CPU)
        return BuildCpuBlurNode(src, sigma, core, vsapi, errMsg);
    else
        return BuildGpuCudaBlurNode(src, sigma, device, device_id, num_streams, tune, core, vsapi, errMsg);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
