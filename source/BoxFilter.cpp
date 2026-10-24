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


#include "BoxFilter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BoxFilterHorizontal(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius)
{
    if (radius <= 0)
    {
        for (int y = 0; y < height; y++)
            memcpy(dst + y * dstStride, src + y * srcStride, sizeof(float) * width);
        return;
    }

    const float norm = 1.f / static_cast<float>(2 * radius + 1);

    for (int y = 0; y < height; y++)
    {
        const float *srow = src + y * srcStride;
        float *drow = dst + y * dstStride;

        float sum = 0;
        for (int x = -radius; x <= radius; x++)
            sum += srow[Clip(x, 0, width - 1)];

        // Clamped borders, unclamped interior [x0, x1) where x - radius >= 0 and x + radius + 1 <= width - 1
        const int x0 = Min(radius, width);
        const int x1 = Max(x0, width - radius - 1);
        int x = 0;
        for (; x < x0; x++)
        {
            drow[x] = sum * norm;
            sum += srow[Min(x + radius + 1, width - 1)] - srow[0];
        }
        for (; x < x1; x++)
        {
            drow[x] = sum * norm;
            sum += srow[x + radius + 1] - srow[x - radius];
        }
        for (; x < width; x++)
        {
            drow[x] = sum * norm;
            sum += srow[width - 1] - srow[Max(x - radius, 0)];
        }
    }
}

void BoxFilterVertical(const float *src, int srcStride, float *dst, int dstStride,
    int width, int height, int radius)
{
    if (radius <= 0)
    {
        for (int y = 0; y < height; y++)
            memcpy(dst + y * dstStride, src + y * srcStride, sizeof(float) * width);
        return;
    }

    const float norm = 1.f / static_cast<float>(2 * radius + 1);

    float *sum = Scratch<float>(ScratchBoxSum, width);
    memset(sum, 0, sizeof(float) * width);

    for (int y = -radius; y <= radius; y++)
    {
        const float *srow = src + Clip(y, 0, height - 1) * srcStride;
        for (int x = 0; x < width; x++)
            sum[x] += srow[x];
    }

    for (int y = 0; y < height; y++)
    {
        float *drow = dst + y * dstStride;
        const float *addRow = src + Min(y + radius + 1, height - 1) * srcStride;
        const float *subRow = src + Max(y - radius, 0) * srcStride;
        for (int x = 0; x < width; x++)
        {
            drow[x] = sum[x] * norm;
            sum[x] += addRow[x] - subRow[x];
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
