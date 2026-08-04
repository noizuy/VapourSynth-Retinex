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


#ifndef BOXBLUR_H_
#define BOXBLUR_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Builds a node approximating a Gaussian blur (sigma in pixels) of a single-plane GrayS
// node via three passes of box blur (horizontal + vertical sliding-window sums), whose
// radii are chosen following Kovesi's "Fast Almost-Gaussian Filtering". Cost per pixel is
// O(1) regardless of sigma, unlike a literal windowed convolution. The returned node is a
// new reference the caller must free.
VSNode *CreateBoxBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
