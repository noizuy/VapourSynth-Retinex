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

/*
* Portions (the Cascade Blur algorithm and its coefficient solver) are ported from libass
* (libass/ass_blur.c, libass/c/blur_template.h) and remain under the following license:
*
* Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
* Copyright (C) 2015-2022 libass contributors
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/


#include <utility>
#include "AssBlur.h"
#include "PlaneFilter.h"


namespace
{

const double Pi = 3.1415926535897932384626433832795;

struct AssBlurMethod
{
    int level = 0;
    int radius = 4;
    float coeff[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    float center = 1; // 1 - 2 * sum(coeff)
};

// Samples of the normalized Gaussian PDF with variance r2: res[i] = sqrt(alpha/pi)*exp(-alpha*i^2), alpha = 1/(2*r2).
void CalcGauss(double *res, int n, double r2)
{
    double alpha = 0.5 / r2;
    double mul = exp(-alpha), mul2 = mul * mul;
    double cur = sqrt(alpha / Pi);

    res[0] = cur;
    cur *= mul;
    res[1] = cur;
    for (int i = 2; i < n; i++)
    {
        mul *= mul2;
        cur *= mul;
        res[i] = cur;
    }
}

// In-place convolution of coeff[] with a symmetric kernel [k3,k2,k1,k0,k1,k2,k3], used to
// fold the pyramid downsampling filter's own frequency response into the least-squares fit.
void CoeffFilter(double *coeff, int n, const double kernel[4])
{
    double prev1 = coeff[1], prev2 = coeff[2], prev3 = coeff[3];
    for (int i = 0; i < n; i++)
    {
        double res = coeff[i + 0] * kernel[0] +
            (prev1 + coeff[i + 1]) * kernel[1] +
            (prev2 + coeff[i + 2]) * kernel[2] +
            (prev3 + coeff[i + 3]) * kernel[3];
        prev3 = prev2;
        prev2 = prev1;
        prev1 = coeff[i];
        coeff[i] = res;
    }
}

// Builds the (symmetric) least-squares normal-equations matrix from matFreq and inverts it
// in place via Gauss-Jordan elimination.
void CalcMatrix(double mat[8][8], const double *matFreq, int n)
{
    for (int i = 0; i < n; i++)
    {
        mat[i][i] = matFreq[2 * i + 2] + 3 * matFreq[0] - 4 * matFreq[i + 1];
        for (int j = i + 1; j < n; j++)
            mat[i][j] = mat[j][i] = matFreq[i + j + 2] + matFreq[j - i] +
                2 * (matFreq[0] - matFreq[i + 1] - matFreq[j + 1]);
    }

    for (int k = 0; k < n; k++)
    {
        double z = 1 / mat[k][k];
        mat[k][k] = 1;
        for (int i = 0; i < n; i++)
        {
            if (i == k) continue;
            double mul = mat[i][k] * z;
            mat[i][k] = 0;
            for (int j = 0; j < n; j++)
                mat[i][j] -= mat[k][j] * mul;
        }
        for (int j = 0; j < n; j++)
            mat[k][j] *= z;
    }
}

// Least-squares fit of the small parametric blur's n taps so that, combined with `level`
// rounds of 2x pyramid downsampling (mul = 0.25^level accounts for the variance those
// rounds already contribute), the net effect approximates a Gaussian of variance r2.
void CalcCoeff(double mu[8], int n, double r2, double mul)
{
    const double w = 12096.;
    double kernel[4] =
    {
        (((+3280. / w) * mul + 1092. / w) * mul + 2520. / w) * mul + 5204. / w,
        (((-2460. / w) * mul -  273. / w) * mul -  210. / w) * mul + 2943. / w,
        (((+ 984. / w) * mul -  546. / w) * mul -  924. / w) * mul +  486. / w,
        (((- 164. / w) * mul +  273. / w) * mul -  126. / w) * mul +   17. / w,
    };

    double matFreq[17] = { kernel[0], kernel[1], kernel[2], kernel[3] };
    CoeffFilter(matFreq, 7, kernel);

    double vecFreq[12];
    CalcGauss(vecFreq, n + 4, r2 * mul);
    CoeffFilter(vecFreq, n + 1, kernel);

    double mat[8][8];
    CalcMatrix(mat, matFreq, n);

    double vec[8];
    for (int i = 0; i < n; i++)
        vec[i] = matFreq[0] - matFreq[i + 1] - vecFreq[0] + vecFreq[i + 1];

    for (int i = 0; i < n; i++)
    {
        double res = 0;
        for (int j = 0; j < n; j++)
            res += mat[i][j] * vec[j];
        mu[i] = Max(0., res);
    }
}

// Picks the pyramid depth (level), the small blur's tap count (radius, 4-8) and its
// coefficients for a Gaussian of variance r2 = sigma^2.
void FindBestMethod(AssBlurMethod &m, double r2)
{
    double mu[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    if (r2 < 0.5)
    {
        m.level = 0;
        m.radius = 4;
        mu[1] = 0.085 * r2 * r2 * r2;
        mu[0] = 0.5 * r2 - 4 * mu[1];
        mu[2] = mu[3] = 0;
    }
    else
    {
        int level;
        double frac = frexp(sqrt(0.11569 * r2 + 0.20591047), &level);
        m.level = level;
        double mul = pow(0.25, level);
        m.radius = 8 - static_cast<int>((10.1525 + 0.8335 * mul) * (1 - frac));
        m.radius = Max(m.radius, 4);
        CalcCoeff(mu, m.radius, r2, mul);
    }

    double dc = 1;
    for (int i = 0; i < m.radius; i++)
    {
        m.coeff[i] = static_cast<float>(mu[i]);
        dc -= 2 * mu[i];
    }
    m.center = static_cast<float>(dc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Contract by 2 with kernel [1,5,10,10,5,1]/32; clamp-to-edge instead of libass's
// canvas-growing boundary handling.
void ShrinkHorz(const float *src, int w, int h, float *dst, int dw)
{
    // taps 2k-2..2k+3 need no clamping for k in [1, k1)
    const int k1 = Max(1, Min(dw, (w - 4) / 2 + 1));

    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * dw;
        auto clamped = [&](int k)
        {
            drow[k] = (1 * srow[Clip(2 * k - 2, 0, w - 1)] +
                5 * srow[Clip(2 * k - 1, 0, w - 1)] +
                10 * srow[Clip(2 * k + 0, 0, w - 1)] +
                10 * srow[Clip(2 * k + 1, 0, w - 1)] +
                5 * srow[Clip(2 * k + 2, 0, w - 1)] +
                1 * srow[Clip(2 * k + 3, 0, w - 1)]) / 32.f;
        };

        if (dw > 0) clamped(0);
        for (int k = 1; k < k1; k++)
        {
            const float *p = srow + 2 * k - 2;
            drow[k] = (1 * p[0] + 5 * p[1] + 10 * p[2] + 10 * p[3] + 5 * p[4] + 1 * p[5]) / 32.f;
        }
        for (int k = k1; k < dw; k++)
            clamped(k);
    }
}

void ShrinkVert(const float *src, int srcStride, int w, int h, float *dst, int dh)
{
    for (int k = 0; k < dh; k++)
    {
        const float *r0 = src + Clip(2 * k - 2, 0, h - 1) * srcStride;
        const float *r1 = src + Clip(2 * k - 1, 0, h - 1) * srcStride;
        const float *r2 = src + Clip(2 * k + 0, 0, h - 1) * srcStride;
        const float *r3 = src + Clip(2 * k + 1, 0, h - 1) * srcStride;
        const float *r4 = src + Clip(2 * k + 2, 0, h - 1) * srcStride;
        const float *r5 = src + Clip(2 * k + 3, 0, h - 1) * srcStride;
        float *drow = dst + k * w;
        for (int x = 0; x < w; x++)
            drow[x] = (1 * r0[x] + 5 * r1[x] + 10 * r2[x] + 10 * r3[x] + 5 * r4[x] + 1 * r5[x]) / 32.f;
    }
}

// Expand by 2 with kernel [5,10,1]/16 for even outputs, [1,10,5]/16 for odd outputs, from inputs
// k-1, k, k+1 (shrink output k is centred at 2k+0.5; libass reads k-2..k as its canvas grows).
// Writes only the first dw/dh outputs, cropping back to the pre-shrink size.
void ExpandHorz(const float *src, int w, int h, float *dst, int dw)
{
    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * dw;
        auto clamped = [&](int k)
        {
            const float p1 = srow[Clip(k - 1, 0, w - 1)];
            const float z0 = srow[k];
            const float n1 = srow[Clip(k + 1, 0, w - 1)];
            if (2 * k < dw)
                drow[2 * k] = (5 * p1 + 10 * z0 + 1 * n1) / 16.f;
            if (2 * k + 1 < dw)
                drow[2 * k + 1] = (1 * p1 + 10 * z0 + 5 * n1) / 16.f;
        };

        // dw >= 2*w - 1, so only the first and last k need clamping/cropping
        clamped(0);
        for (int k = 1; k < w - 1; k++)
        {
            const float p1 = srow[k - 1], z0 = srow[k], n1 = srow[k + 1];
            drow[2 * k] = (5 * p1 + 10 * z0 + 1 * n1) / 16.f;
            drow[2 * k + 1] = (1 * p1 + 10 * z0 + 5 * n1) / 16.f;
        }
        if (w > 1)
            clamped(w - 1);
    }
}

void ExpandVert(const float *src, int w, int h, float *dst, int dstStride, int dh)
{
    for (int k = 0; k < h; k++)
    {
        const float *rp = src + Clip(k - 1, 0, h - 1) * w;
        const float *rz = src + k * w;
        const float *rn = src + Clip(k + 1, 0, h - 1) * w;
        if (2 * k < dh)
        {
            float *drow = dst + 2 * k * dstStride;
            for (int x = 0; x < w; x++)
                drow[x] = (5 * rp[x] + 10 * rz[x] + 1 * rn[x]) / 16.f;
        }
        if (2 * k + 1 < dh)
        {
            float *drow = dst + (2 * k + 1) * dstStride;
            for (int x = 0; x < w; x++)
                drow[x] = (1 * rp[x] + 10 * rz[x] + 5 * rn[x]) / 16.f;
        }
    }
}

// Symmetric FIR at the reduced resolution: centre weight d, coeff[i-1] at offsets +-i.
void AssBlurHorz(const float *src, int srcStride, int w, int h, float *dst, int radius, const float *coeff, float d)
{
    const int x0 = Min(radius, w);
    const int x1 = Max(x0, w - radius);

    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * srcStride;
        float *drow = dst + y * w;

        for (int x = 0; x < w; x++)
            drow[x] = d * srow[x];
        for (int i = 1; i <= radius; i++)
        {
            const float c = coeff[i - 1];
            for (int x = 0; x < x0; x++)
                drow[x] += c * (srow[Max(x - i, 0)] + srow[Min(x + i, w - 1)]);
            for (int x = x0; x < x1; x++)
                drow[x] += c * (srow[x - i] + srow[x + i]);
            for (int x = x1; x < w; x++)
                drow[x] += c * (srow[Max(x - i, 0)] + srow[Min(x + i, w - 1)]);
        }
    }
}

void AssBlurVert(const float *src, int w, int h, float *dst, int dstStride, int radius, const float *coeff, float d)
{
    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * dstStride;

        for (int x = 0; x < w; x++)
            drow[x] = d * srow[x];
        for (int i = 1; i <= radius; i++)
        {
            const float c = coeff[i - 1];
            const float *rm = src + Max(y - i, 0) * w;
            const float *rp = src + Min(y + i, h - 1) * w;
            for (int x = 0; x < w; x++)
                drow[x] += c * (rm[x] + rp[x]);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssBlurKernel
{
    AssBlurMethod method;

    void operator()(const float *srcp, int srcStride, float *dstp, int dstStride, int width, int height) const
    {
        const AssBlurMethod &m = method;

        // sizes after each shrink step, for the expand steps to crop back to
        std::vector<int> wDims = { width }, hDims = { height };
        for (int i = 0; i < m.level; i++) wDims.push_back((wDims.back() + 1) / 2);
        for (int i = 0; i < m.level; i++) hDims.push_back((hDims.back() + 1) / 2);

        // ping-pong buffers; the first step reads src and the last one writes dst
        const size_t frameSize = size_t(width) * height;
        float *cur = Scratch<float>(ScratchFrame, 2 * frameSize);
        float *next = cur + frameSize;
        int curW = width, curH = height;

        // same order as libass: shrink V then H, expand H then V
        const float *in = srcp;
        int inStride = srcStride;
        for (int i = 0; i < m.level; i++)
        {
            const int newH = hDims[i + 1];
            ShrinkVert(in, inStride, curW, curH, next, newH);
            std::swap(cur, next);
            in = cur;
            inStride = curW;
            curH = newH;
        }
        for (int i = 0; i < m.level; i++)
        {
            const int newW = wDims[i + 1];
            ShrinkHorz(cur, curW, curH, next, newW);
            std::swap(cur, next);
            curW = newW;
        }

        AssBlurHorz(m.level ? cur : srcp, m.level ? curW : srcStride, curW, curH, next, m.radius, m.coeff, m.center);
        std::swap(cur, next);
        if (m.level == 0)
        {
            AssBlurVert(cur, curW, curH, dstp, dstStride, m.radius, m.coeff, m.center);
        }
        else
        {
            AssBlurVert(cur, curW, curH, next, curW, m.radius, m.coeff, m.center);
            std::swap(cur, next);

            for (int i = 0; i < m.level; i++)
            {
                const int newW = wDims[m.level - 1 - i];
                ExpandHorz(cur, curW, curH, next, newW);
                std::swap(cur, next);
                curW = newW;
            }
            for (int i = 0; i < m.level; i++)
            {
                const int newH = hDims[m.level - 1 - i];
                const bool last = i == m.level - 1;
                ExpandVert(cur, curW, curH, last ? dstp : next, last ? dstStride : curW, newH);
                std::swap(cur, next);
                curH = newH;
            }
        }
    }
};

} // namespace


VSNode *CreateAssBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    AssBlurKernel k;
    FindBestMethod(k.method, sigma * sigma);
    return CreatePlaneFilterNode("RetinexAssBlur", src, k, core, vsapi);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
