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


#ifndef DEVICE_H_
#define DEVICE_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Which implementation/plugin performs the Gaussian blur for a given scale.
enum class BlurDevice
{
    CPU_BOX,  // 3-pass box blur (BoxBlur.cpp), no external dependency
    CPU_IIR,  // single-pass Young/van Vliet recursive filter (IirBlur.cpp), no external dependency
    CPU_ASS,  // libass's pyramid "Cascade Blur" (AssBlur.cpp), no external dependency
    GPU,      // com.julek.vszipcl (OpenCL)
    CUDA      // com.julek.vszipcu (CUDA)
};


// Returns false if str isn't a valid device name.
bool ParseDevice(const std::string &str, BlurDevice &device);

const char *DeviceName(BlurDevice device);

// The valid device names, quoted, for error messages: "cpu_box", ... or "cuda"
const std::string &DeviceNameList();


// Gaussian blur (sigma in pixels) of a GrayS node on the given device. Returns a new node
// reference, or nullptr with errMsg set (e.g. the backing plugin isn't installed).
// tune is only used by gpu; -1 keeps the plugin's default.
VSNode *BuildGaussianBlurNode(VSNode *src, double sigma, BlurDevice device,
    int64_t device_id, int64_t num_streams, int64_t tune,
    VSCore *core, const VSAPI *vsapi, std::string &errMsg);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
