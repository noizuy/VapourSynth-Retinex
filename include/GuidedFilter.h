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


#ifndef GUIDEDFILTER_H_
#define GUIDEDFILTER_H_


#include "Helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Self-guided Fast Guided Filter (He & Sun, arXiv:1505.00996) of a GrayS node, used as an edge-aware
// illumination estimate. radius/eps are in full-resolution pixels / [0,1] intensity units; the box
// statistics run at 1/subsample resolution (1 = full resolution). Returns a new node reference.
VSNode *CreateGuidedFilterNode(VSNode *src, int radius, double eps, int subsample,
    VSCore *core, const VSAPI *vsapi);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
