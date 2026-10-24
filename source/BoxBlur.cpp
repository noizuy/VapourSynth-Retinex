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


#include "BoxBlur.h"
#include "BoxFilter.h"
#include "PlaneFilter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// P. Kovesi, "Fast Almost-Gaussian Filtering", DICTA 2010: 3 box widths (two adjacent odd sizes)
// whose combined variance matches the Gaussian.
void BoxRadiiForSigma(double sigma, int radii[3])
{
    const int n = 3;

    double wIdeal = sqrt((12. * sigma * sigma / n) + 1.);
    int wl = static_cast<int>(std::floor(wIdeal));
    if (wl < 1) wl = 1;
    if (wl % 2 == 0) wl -= 1;
    if (wl < 1) wl = 1;
    int wu = wl + 2;

    double mIdeal = (12. * sigma * sigma - n * wl * wl - 4. * n * wl - 3. * n) / (-4. * wl - 4.);
    int m = static_cast<int>(std::round(mIdeal));
    m = Clip(m, 0, n);

    for (int i = 0; i < n; i++)
    {
        int width = i < m ? wl : wu;
        radii[i] = Max((width - 1) / 2, 0);
    }
}


struct BoxBlurKernel
{
    int radii[3];

    void operator()(const float *srcp, int srcStride, float *dstp, int dstStride, int width, int height) const
    {
        // Boxes along different axes commute: 3 H passes per row, then 3 V passes per column strip
        const int strip = 128;
        const size_t frameSize = size_t(width) * height;
        float *bufH = Scratch<float>(ScratchFrame, frameSize + 2 * width + size_t(2) * strip * height);
        float *row1 = bufH + frameSize;
        float *row2 = row1 + width;
        float *col1 = row2 + width;
        float *col2 = col1 + size_t(strip) * height;

        for (int y = 0; y < height; y++)
        {
            BoxFilterHorizontal(srcp + y * srcStride, 0, row1, 0, width, 1, radii[0]);
            BoxFilterHorizontal(row1, 0, row2, 0, width, 1, radii[1]);
            BoxFilterHorizontal(row2, 0, bufH + y * width, 0, width, 1, radii[2]);
        }

        for (int x = 0; x < width; x += strip)
        {
            const int sw = Min(strip, width - x);
            BoxFilterVertical(bufH + x, width, col1, sw, sw, height, radii[0]);
            BoxFilterVertical(col1, sw, col2, sw, sw, height, radii[1]);
            BoxFilterVertical(col2, sw, dstp + x, dstStride, sw, height, radii[2]);
        }
    }
};

} // namespace


VSNode *CreateBoxBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    BoxBlurKernel k;
    BoxRadiiForSigma(sigma, k.radii);
    return CreatePlaneFilterNode("RetinexBoxBlur", src, k, core, vsapi);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
