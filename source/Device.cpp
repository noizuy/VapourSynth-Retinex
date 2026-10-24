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


#include "Device.h"
#include "BoxBlur.h"
#include "IirBlur.h"
#include "AssBlur.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

const struct
{
    const char *name;
    BlurDevice device;
} DeviceTable[] =
{
    { "cpu_box", BlurDevice::CPU_BOX },
    { "cpu_iir", BlurDevice::CPU_IIR },
    { "cpu_ass", BlurDevice::CPU_ASS },
    { "gpu", BlurDevice::GPU },
    { "cuda", BlurDevice::CUDA }
};

} // namespace


bool ParseDevice(const std::string &str, BlurDevice &device)
{
    for (const auto &e : DeviceTable)
    {
        if (str == e.name)
        {
            device = e.device;
            return true;
        }
    }
    return false;
}

const char *DeviceName(BlurDevice device)
{
    for (const auto &e : DeviceTable)
    {
        if (e.device == device)
            return e.name;
    }
    return "";
}

const std::string &DeviceNameList()
{
    static const std::string list = []
    {
        const size_t n = sizeof(DeviceTable) / sizeof(DeviceTable[0]);
        std::string str;
        for (size_t i = 0; i < n; i++)
        {
            if (i > 0)
                str += i + 1 < n ? ", " : " or ";
            str += std::string("\"") + DeviceTable[i].name + "\"";
        }
        return str;
    }();
    return list;
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
        node = CreateBoxBlurNode(src, sigma, core, vsapi);
        if (!node)
            errMsg = "failed to build the box blur node for the \"cpu_box\" device";
        break;

    case BlurDevice::CPU_IIR:
        node = CreateIirBlurNode(src, sigma, core, vsapi);
        if (!node)
            errMsg = "failed to build the IIR blur node for the \"cpu_iir\" device";
        break;

    case BlurDevice::CPU_ASS:
        node = CreateAssBlurNode(src, sigma, core, vsapi);
        if (!node)
            errMsg = "failed to build the ASS cascade blur node for the \"cpu_ass\" device";
        break;

    default:
        node = BuildGpuCudaBlurNode(src, sigma, device, device_id, num_streams, tune, core, vsapi, errMsg);
        break;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
