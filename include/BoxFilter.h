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


#ifndef BOXFILTER_H_
#define BOXFILTER_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Sliding-window box (mean) filter: O(width*height) regardless of radius, using clamp-to-edge
// boundary handling. src/dst may be the same buffer only if srcStride == dstStride.
// Shared by BoxBlur.cpp (as a building block for a multi-pass Gaussian approximation) and
// GuidedFilter.cpp (as the guided filter's core local-statistics primitive).
void BoxFilterHorizontal(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius);

void BoxFilterVertical(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
