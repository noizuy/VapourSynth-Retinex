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

        for (int x = 0; x < width; x++)
        {
            drow[x] = sum * norm;
            sum += srow[Clip(x + radius + 1, 0, width - 1)] - srow[Clip(x - radius, 0, width - 1)];
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

    float *sum = vsh::vsh_aligned_malloc<float>(sizeof(float) * width, Alignment);
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
        for (int x = 0; x < width; x++)
            drow[x] = sum[x] * norm;

        const float *addRow = src + Clip(y + radius + 1, 0, height - 1) * srcStride;
        const float *subRow = src + Clip(y - radius, 0, height - 1) * srcStride;
        for (int x = 0; x < width; x++)
            sum[x] += addRow[x] - subRow[x];
    }

    vsh::vsh_aligned_free(sum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
