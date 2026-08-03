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


#ifndef INTENSITY_H_
#define INTENSITY_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class IntensityMode
{
    Y,          // plane 0 (Gray or YUV luma)
    RGBAvg,     // (R+G+B)/3-style average, used by MSRCP for RGB input
    RGBChannel  // a single R/G/B channel, normalized using the shared RGB floor/ceil (MSRCR)
};


// Builds a node producing a single-plane GrayS clip (values in [0,1] for full-range input)
// derived from src, matching the intensity-channel derivation historically inlined in
// MSRCP/MSRCR: for limited-range (fulls == false) input, the floor/ceil are auto-detected
// per frame from the actual min/max sample value(s), exactly as before.
//
// channel selects which plane (0=R,1=G,2=B) is written out when mode is RGBChannel; ignored
// otherwise. Returns nullptr on failure (only possible if src's own format is unsupported).
VSNode *CreateIntensityNode(VSNode *src, bool fulls, IntensityMode mode, int channel,
    VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
