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


#include "AssBlur.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This file ports the numerical core of libass's "Cascade Blur" algorithm
// (libass/c/ass_blur.c and libass/c/blur_template.h, Copyright (C) 2015 Vabishchevich
// Nikolay, ISC-licensed) from fixed-point/SIMD-striped bitmaps to plain clamped-boundary
// float buffers. See CreateAssBlurNode's declaration for how this adaptation differs from
// the original.


namespace
{

const double Pi = 3.1415926535897932384626433832795;

struct AssBlurMethod
{
    int level = 0;
    int radius = 4;
    double coeff[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
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

    for (int i = 0; i < m.radius; i++)
        m.coeff[i] = mu[i];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Contract by 2 with kernel [1,5,10,10,5,1]/32; clamp-to-edge instead of libass's
// canvas-growing boundary handling.
void ShrinkHorz(const float *src, int w, int h, float *dst, int dw)
{
    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * dw;
        for (int k = 0; k < dw; k++)
        {
            drow[k] = (1 * srow[Clip(2 * k - 2, 0, w - 1)] +
                5 * srow[Clip(2 * k - 1, 0, w - 1)] +
                10 * srow[Clip(2 * k + 0, 0, w - 1)] +
                10 * srow[Clip(2 * k + 1, 0, w - 1)] +
                5 * srow[Clip(2 * k + 2, 0, w - 1)] +
                1 * srow[Clip(2 * k + 3, 0, w - 1)]) / 32.f;
        }
    }
}

void ShrinkVert(const float *src, int w, int h, float *dst, int dh)
{
    for (int k = 0; k < dh; k++)
    {
        const float *r0 = src + Clip(2 * k - 2, 0, h - 1) * w;
        const float *r1 = src + Clip(2 * k - 1, 0, h - 1) * w;
        const float *r2 = src + Clip(2 * k + 0, 0, h - 1) * w;
        const float *r3 = src + Clip(2 * k + 1, 0, h - 1) * w;
        const float *r4 = src + Clip(2 * k + 2, 0, h - 1) * w;
        const float *r5 = src + Clip(2 * k + 3, 0, h - 1) * w;
        float *drow = dst + k * w;
        for (int x = 0; x < w; x++)
            drow[x] = (1 * r0[x] + 5 * r1[x] + 10 * r2[x] + 10 * r3[x] + 5 * r4[x] + 1 * r5[x]) / 32.f;
    }
}

// Expand by 2 with kernel [5,10,1]/16 for even outputs, [1,10,5]/16 for odd outputs.
// Writes only the first dw/dh outputs (dw/dh may be less than 2*w/2*h, to crop back to a
// target size instead of libass's canvas growth).
void ExpandHorz(const float *src, int w, int h, float *dst, int dw)
{
    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * dw;
        for (int k = 0; k < w; k++)
        {
            const float p1 = srow[Clip(k - 2, 0, w - 1)];
            const float z0 = srow[Clip(k - 1, 0, w - 1)];
            const float n1 = srow[k];
            if (2 * k < dw)
                drow[2 * k] = (5 * p1 + 10 * z0 + 1 * n1) / 16.f;
            if (2 * k + 1 < dw)
                drow[2 * k + 1] = (1 * p1 + 10 * z0 + 5 * n1) / 16.f;
        }
    }
}

void ExpandVert(const float *src, int w, int h, float *dst, int dh)
{
    for (int k = 0; k < h; k++)
    {
        const float *rp = src + Clip(k - 2, 0, h - 1) * w;
        const float *rz = src + Clip(k - 1, 0, h - 1) * w;
        const float *rn = src + k * w;
        if (2 * k < dh)
        {
            float *drow = dst + 2 * k * w;
            for (int x = 0; x < w; x++)
                drow[x] = (5 * rp[x] + 10 * rz[x] + 1 * rn[x]) / 16.f;
        }
        if (2 * k + 1 < dh)
        {
            float *drow = dst + (2 * k + 1) * w;
            for (int x = 0; x < w; x++)
                drow[x] = (1 * rp[x] + 10 * rz[x] + 5 * rn[x]) / 16.f;
        }
    }
}

// The small (radius <= 8 tap) parametric blur applied at the reduced resolution: a
// symmetric FIR filter with center weight d = 1 - 2*sum(coeff) and side weights coeff[i-1]
// at offsets +-i, per blur_template.h's "Main Parametric Filters" comment.
void AssBlurHorz(const float *src, int w, int h, float *dst, int radius, const double *coeff)
{
    double d = 1.;
    for (int i = 0; i < radius; i++) d -= 2 * coeff[i];

    for (int y = 0; y < h; y++)
    {
        const float *srow = src + y * w;
        float *drow = dst + y * w;
        for (int x = 0; x < w; x++)
        {
            double acc = d * srow[x];
            for (int i = 1; i <= radius; i++)
                acc += coeff[i - 1] * (srow[Clip(x - i, 0, w - 1)] + srow[Clip(x + i, 0, w - 1)]);
            drow[x] = static_cast<float>(acc);
        }
    }
}

void AssBlurVert(const float *src, int w, int h, float *dst, int radius, const double *coeff)
{
    double d = 1.;
    for (int i = 0; i < radius; i++) d -= 2 * coeff[i];

    for (int y = 0; y < h; y++)
    {
        float *drow = dst + y * w;
        for (int x = 0; x < w; x++)
        {
            double acc = d * src[y * w + x];
            for (int i = 1; i <= radius; i++)
                acc += coeff[i - 1] * (src[Clip(y - i, 0, h - 1) * w + x] + src[Clip(y + i, 0, h - 1) * w + x]);
            drow[x] = static_cast<float>(acc);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssBlurData
{
    VSNode *node = nullptr;
    VSVideoInfo vi{};
    AssBlurMethod method;
};

const VSFrame *VS_CC AssBlurGetFrame(int n, int activationReason, void *instanceData, void **frameData,
    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const AssBlurData *d = reinterpret_cast<const AssBlurData *>(instanceData);

    if (activationReason == arInitial)
    {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady)
    {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const int width = vsapi->getFrameWidth(src, 0);
        const int height = vsapi->getFrameHeight(src, 0);
        const int srcStride = static_cast<int>(vsapi->getStride(src, 0) / sizeof(float));
        const float *srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 0));

        VSFrame *dst = vsapi->newVideoFrame(&d->vi.format, width, height, src, core);
        const int dstStride = static_cast<int>(vsapi->getStride(dst, 0) / sizeof(float));
        float *dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));

        const AssBlurMethod &m = d->method;

        // Dimensions after each of the `level` shrink steps, so the matching expand steps
        // can crop back to precisely the original width/height (see ExpandHorz/Vert).
        std::vector<int> wDims = { width }, hDims = { height };
        for (int i = 0; i < m.level; i++) wDims.push_back((wDims.back() + 1) / 2);
        for (int i = 0; i < m.level; i++) hDims.push_back((hDims.back() + 1) / 2);

        float *cur = vsh::vsh_aligned_malloc<float>(sizeof(float) * width * height, Alignment);
        for (int y = 0; y < height; y++)
            memcpy(cur + y * width, srcp + y * srcStride, sizeof(float) * width);

        int curW = width, curH = height;

        // libass shrinks vertically then horizontally, and expands horizontally then
        // vertically; kept in the same order here for fidelity (mostly immaterial since
        // sigma is isotropic, but matches the reference exactly).
        for (int i = 0; i < m.level; i++)
        {
            const int newH = hDims[i + 1];
            float *next = vsh::vsh_aligned_malloc<float>(sizeof(float) * curW * newH, Alignment);
            ShrinkVert(cur, curW, curH, next, newH);
            vsh::vsh_aligned_free(cur);
            cur = next;
            curH = newH;
        }
        for (int i = 0; i < m.level; i++)
        {
            const int newW = wDims[i + 1];
            float *next = vsh::vsh_aligned_malloc<float>(sizeof(float) * newW * curH, Alignment);
            ShrinkHorz(cur, curW, curH, next, newW);
            vsh::vsh_aligned_free(cur);
            cur = next;
            curW = newW;
        }

        {
            float *tmp = vsh::vsh_aligned_malloc<float>(sizeof(float) * curW * curH, Alignment);
            AssBlurHorz(cur, curW, curH, tmp, m.radius, m.coeff);
            vsh::vsh_aligned_free(cur);
            cur = tmp;

            tmp = vsh::vsh_aligned_malloc<float>(sizeof(float) * curW * curH, Alignment);
            AssBlurVert(cur, curW, curH, tmp, m.radius, m.coeff);
            vsh::vsh_aligned_free(cur);
            cur = tmp;
        }

        for (int i = 0; i < m.level; i++)
        {
            const int newW = wDims[m.level - 1 - i];
            float *next = vsh::vsh_aligned_malloc<float>(sizeof(float) * newW * curH, Alignment);
            ExpandHorz(cur, curW, curH, next, newW);
            vsh::vsh_aligned_free(cur);
            cur = next;
            curW = newW;
        }
        for (int i = 0; i < m.level; i++)
        {
            const int newH = hDims[m.level - 1 - i];
            float *next = vsh::vsh_aligned_malloc<float>(sizeof(float) * curW * newH, Alignment);
            ExpandVert(cur, curW, curH, next, newH);
            vsh::vsh_aligned_free(cur);
            cur = next;
            curH = newH;
        }

        for (int y = 0; y < height; y++)
            memcpy(dstp + y * dstStride, cur + y * width, sizeof(float) * width);

        vsh::vsh_aligned_free(cur);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

void VS_CC AssBlurFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    AssBlurData *d = reinterpret_cast<AssBlurData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

} // namespace


VSNode *CreateAssBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    AssBlurData *d = new AssBlurData();
    d->node = vsapi->addNodeRef(src);
    d->vi = *vsapi->getVideoInfo(src);
    FindBestMethod(d->method, sigma * sigma);

    VSFilterDependency dep = { src, rpStrictSpatial };
    VSNode *node = vsapi->createVideoFilter2("RetinexAssBlur", &d->vi, AssBlurGetFrame, AssBlurFree,
        fmParallel, &dep, 1, d, core);

    if (!node)
    {
        vsapi->freeNode(d->node);
        delete d;
    }

    return node;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
