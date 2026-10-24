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


#ifndef ASSBLUR_H_
#define ASSBLUR_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Gaussian blur (sigma in pixels) of a GrayS node by libass's Cascade Blur: shrink by 2 `level` times
// with a [1,5,10,10,5,1] kernel, apply a 4-8 tap FIR fitted by least squares, expand back with 3-tap
// kernels. Cost per pixel is roughly independent of sigma. Unlike libass (whose canvas grows), every
// stage clamps at the edges and keeps the frame size. Returns a new node reference.
VSNode *CreateAssBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
