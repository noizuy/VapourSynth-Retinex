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


#ifndef DEVICE_H_
#define DEVICE_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Which implementation/plugin performs the Gaussian blur for a given scale.
enum class BlurDevice
{
    CPU_BOX,  // 3-pass box blur (BoxBlur.cpp), no external dependency
    CPU_IIR,  // single-pass Young/van Vliet recursive filter (IirBlur.cpp), no external dependency
    GPU,      // com.julek.vszipcl (OpenCL)
    CUDA      // com.julek.vszipcu (CUDA)
};


// Returns false if str isn't one of "cpu_box", "cpu_iir", "gpu", "cuda".
bool ParseDevice(const std::string &str, BlurDevice &device);

const char *DeviceName(BlurDevice device);


// Builds a node that outputs a Gaussian blur (sigma in pixels) of a single-plane GrayS node,
// dispatching to the plugin backing the requested device. The returned node is a new
// reference the caller must free. Returns nullptr and fills errMsg on failure (e.g. the
// backing plugin isn't installed).
//
// tune is only used for the gpu device; pass -1 to leave it at the plugin's own default.
VSNode *BuildGaussianBlurNode(VSNode *src, double sigma, BlurDevice device,
    int64_t device_id, int64_t num_streams, int64_t tune,
    VSCore *core, const VSAPI *vsapi, std::string &errMsg);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
