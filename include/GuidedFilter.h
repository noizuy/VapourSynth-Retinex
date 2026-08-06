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


#ifndef GUIDEDFILTER_H_
#define GUIDEDFILTER_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Builds a node applying the (self-guided, i.e. guide == input) Fast Guided Filter
// (He & Sun, "Fast Guided Filter", arXiv:1505.00996) to a single-plane GrayS node: an
// edge-aware smoothing filter used here as the illumination estimate in place of a
// Gaussian/box/IIR blur. radius/eps are the guided filter's usual window radius and
// regularization (in the same [0,1] units as the intensity channel); subsample is the
// downsampling factor the "fast" variant runs its box-filter statistics at before
// upsampling the resulting linear coefficients back to full resolution (1 disables the
// speedup and runs at full resolution). The returned node is a new reference the caller
// must free.
VSNode *CreateGuidedFilterNode(VSNode *src, int radius, double eps, int subsample,
    VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
