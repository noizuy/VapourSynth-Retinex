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


#ifndef ASSBLUR_H_
#define ASSBLUR_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Builds a node approximating a Gaussian blur (sigma in pixels) of a single-plane GrayS
// node via libass's "Cascade Blur" algorithm (libass/c/ass_blur.c, libass/c/blur_template.h):
// repeatedly halve the resolution with a fixed 6-tap [1,5,10,10,5,1] pyramid kernel, apply a
// small (4-8 tap) FIR filter whose coefficients are solved via least squares so that,
// combined with the pyramid's own smoothing, the net effect approximates the target
// Gaussian, then double the resolution back with two 3-tap kernels. Cost per pixel is
// effectively independent of sigma (the working resolution shrinks as sigma grows).
//
// libass grows its output canvas at each stage since it blurs isolated glyph bitmaps that
// are allowed to spread beyond their original bounds; here every stage instead uses
// clamp-to-edge boundaries and produces same-size output, since a VapourSynth frame plane
// can't grow. The returned node is a new reference the caller must free.
VSNode *CreateAssBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
