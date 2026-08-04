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
#include "BoxBlur.h"
#include "IirBlur.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseDevice(const std::string &str, BlurDevice &device)
{
    if (str == "cpu_box") { device = BlurDevice::CPU_BOX; return true; }
    if (str == "cpu_iir") { device = BlurDevice::CPU_IIR; return true; }
    if (str == "gpu") { device = BlurDevice::GPU; return true; }
    if (str == "cuda") { device = BlurDevice::CUDA; return true; }
    return false;
}

const char *DeviceName(BlurDevice device)
{
    switch (device)
    {
    case BlurDevice::CPU_BOX: return "cpu_box";
    case BlurDevice::CPU_IIR: return "cpu_iir";
    case BlurDevice::GPU: return "gpu";
    case BlurDevice::CUDA: return "cuda";
    default: return "";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

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

} // namespace


VSNode *BuildGaussianBlurNode(VSNode *src, double sigma, BlurDevice device,
    int64_t device_id, int64_t num_streams, int64_t tune,
    VSCore *core, const VSAPI *vsapi, std::string &errMsg)
{
    VSNode *node = nullptr;

    switch (device)
    {
    case BlurDevice::CPU_BOX:
        // No external plugin needed: a 3-pass box blur approximates the Gaussian in O(1)
        // time per pixel regardless of sigma (unlike a literal windowed convolution).
        node = CreateBoxBlurNode(src, sigma, core, vsapi);
        if (!node)
            errMsg = "failed to build the box blur node for the \"cpu_box\" device";
        break;

    case BlurDevice::CPU_IIR:
        // No external plugin needed: a single-pass recursive (IIR) filter, O(1) per pixel.
        node = CreateIirBlurNode(src, sigma, core, vsapi);
        if (!node)
            errMsg = "failed to build the IIR blur node for the \"cpu_iir\" device";
        break;

    default:
        node = BuildGpuCudaBlurNode(src, sigma, device, device_id, num_streams, tune, core, vsapi, errMsg);
        break;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
