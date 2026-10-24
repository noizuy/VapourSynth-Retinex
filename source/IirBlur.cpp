/*
* Retinex filter - VapourSynth plugin
* Copyright (C) 2014  mawen1250
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


#include "IirBlur.h"
#include "PlaneFilter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace
{

// I.T. Young, L.J. van Vliet, "Recursive implementation of the Gaussian filter", 1995
void Recursive_Gaussian_Parameters(const double sigma, FLType &B, FLType &B1, FLType &B2, FLType &B3)
{
    double q = sigma < 2.5 ? 3.97156 - 4.14554 * sqrt(1 - 0.26891 * sigma) : 0.98711 * sigma - 0.96330;
    // q < 0 below sigma ~0.305 gives negative lobes; q == 0 is the identity
    if (!(q > 0)) q = 0;

    const double b0 = 1.57825 + 2.44413 * q + 1.4281 * q * q + 0.422205 * q * q * q;
    const double b1 = 2.44413 * q + 2.85619 * q * q + 1.26661 * q * q * q;
    const double b2 = -(1.4281 * q * q + 1.26661 * q * q * q);
    const double b3 = 0.422205 * q * q * q;

    B = static_cast<FLType>(1 - (b1 + b2 + b3) / b0);
    B1 = static_cast<FLType>(b1 / b0);
    B2 = static_cast<FLType>(b2 / b0);
    B3 = static_cast<FLType>(b3 / b0);
}

// Backward pass initialisation for replicate edges (B. Triggs, M. Sdika, "Boundary conditions for
// Young-van Vliet recursive filtering", IEEE TSP 2006): with u the last input and e[j] = y[N-1-j] - u,
//     v[N+k] = u + sum_j M[k][j] * e[j],   k = 0, 1, 2.
// M is computed numerically by running the homogeneous recursions past the edge until they decay.
void Recursive_Gaussian_Boundary(FLType B, FLType B1, FLType B2, FLType B3, double M[3][3])
{
    std::vector<double> e, dv;

    for (int j = 0; j < 3; j++)
    {
        // forward pass beyond the edge (input == u, so deviation is homogeneous)
        e.assign(3, 0.0);
        e[2 - j] = 1; // e holds y[N-3], y[N-2], y[N-1] in order
        for (size_t n = 3; n < 1000000; n++)
        {
            const double v = B1 * e[n - 1] + B2 * e[n - 2] + B3 * e[n - 3];
            e.push_back(v);
            if (n > 16 && std::abs(v) < 1e-13 && std::abs(e[n - 1]) < 1e-13 && std::abs(e[n - 2]) < 1e-13)
                break;
        }

        // backward pass from far beyond the edge (deviation 0) back to index N
        const size_t K = e.size(); // e[3] corresponds to index N
        dv.assign(K + 3, 0.0);
        for (size_t n = K; n-- > 3;)
            dv[n] = B * e[n] + B1 * dv[n + 1] + B2 * dv[n + 2] + B3 * dv[n + 3];

        for (int k = 0; k < 3; k++)
            M[k][j] = 3 + k < K ? dv[3 + k] : 0;
    }
}

// In place
void Recursive_Gaussian2D_Vertical(FLType *data, int stride, int width, int height,
    const FLType B, const FLType B1, const FLType B2, const FLType B3, const double M[3][3])
{
    int i0, i1, i2, i3, j, lower, upper;
    FLType P0, P1, P2, P3;

    // last input row (the replicated edge value), overwritten by the forward pass
    std::vector<FLType> last(data + stride * (height - 1), data + stride * (height - 1) + width);

    for (j = 0; j < height; j++)
    {
        lower = stride * j;
        upper = lower + width;

        i0 = lower;
        i1 = j < 1 ? i0 : i0 - stride;
        i2 = j < 2 ? i1 : i1 - stride;
        i3 = j < 3 ? i2 : i2 - stride;

        for (; i0 < upper; i0++, i1++, i2++, i3++)
        {
            P3 = data[i3];
            P2 = data[i2];
            P1 = data[i1];
            P0 = data[i0];
            data[i0] = B * P0 + B1 * P1 + B2 * P2 + B3 * P3;
        }
    }

    // backward state for rows height, height+1, height+2
    std::vector<FLType> tail(3 * width);
    {
        const FLType *e0 = data + stride * (height - 1);
        const FLType *e1 = data + stride * Max(height - 2, 0);
        const FLType *e2 = data + stride * Max(height - 3, 0);
        for (int x = 0; x < width; x++)
        {
            const double u = last[x];
            const double d0 = e0[x] - u, d1 = e1[x] - u, d2 = e2[x] - u;
            for (int k = 0; k < 3; k++)
                tail[k * width + x] = static_cast<FLType>(u + M[k][0] * d0 + M[k][1] * d1 + M[k][2] * d2);
        }
    }

    for (j = height - 1; j >= 0; j--)
    {
        FLType *row0 = data + stride * j;
        const FLType *row1 = j + 1 < height ? data + stride * (j + 1) : tail.data() + width * (j + 1 - height);
        const FLType *row2 = j + 2 < height ? data + stride * (j + 2) : tail.data() + width * (j + 2 - height);
        const FLType *row3 = j + 3 < height ? data + stride * (j + 3) : tail.data() + width * (j + 3 - height);

        for (int x = 0; x < width; x++)
            row0[x] = B * row0[x] + B1 * row1[x] + B2 * row2[x] + B3 * row3[x];
    }
}

// G rows interleaved so their serial recursions overlap
template < int G, typename T >
void Recursive_Gaussian_Rows(const T *input, int inStride, FLType *output, int outStride, int width,
    const FLType B, const FLType B1, const FLType B2, const FLType B3, const double M[3][3])
{
    FLType P0, P1[G], P2[G], P3[G];

    for (int k = 0; k < G; k++)
        P3[k] = P2[k] = P1[k] = input[k * inStride];

    for (int i = 0; i < width; i++)
    {
        for (int k = 0; k < G; k++)
        {
            P0 = B * input[k * inStride + i] + B1 * P1[k] + B2 * P2[k] + B3 * P3[k];
            P3[k] = P2[k];
            P2[k] = P1[k];
            P1[k] = P0;
            output[k * outStride + i] = P0;
        }
    }

    // P1, P2, P3 now hold the last 3 forward outputs
    for (int k = 0; k < G; k++)
    {
        const double u = input[k * inStride + width - 1];
        const double d0 = P1[k] - u, d1 = P2[k] - u, d2 = P3[k] - u;
        P1[k] = static_cast<FLType>(u + M[0][0] * d0 + M[0][1] * d1 + M[0][2] * d2);
        P2[k] = static_cast<FLType>(u + M[1][0] * d0 + M[1][1] * d1 + M[1][2] * d2);
        P3[k] = static_cast<FLType>(u + M[2][0] * d0 + M[2][1] * d1 + M[2][2] * d2);
    }

    for (int i = width - 1; i >= 0; i--)
    {
        for (int k = 0; k < G; k++)
        {
            FLType &out = output[k * outStride + i];
            P0 = B * out + B1 * P1[k] + B2 * P2[k] + B3 * P3[k];
            P3[k] = P2[k];
            P2[k] = P1[k];
            P1[k] = P0;
            out = P0;
        }
    }
}

template < typename T >
void Recursive_Gaussian2D_Horizontal(const T *input, int inStride, FLType *output, int outStride, int width, int height,
    const FLType B, const FLType B1, const FLType B2, const FLType B3, const double M[3][3])
{
    const int G = 8;
    int j = 0;
    for (; j + G <= height; j += G)
        Recursive_Gaussian_Rows<G>(input + j * inStride, inStride, output + j * outStride, outStride, width, B, B1, B2, B3, M);
    for (; j < height; j++)
        Recursive_Gaussian_Rows<1>(input + j * inStride, inStride, output + j * outStride, outStride, width, B, B1, B2, B3, M);
}


struct IirBlurKernel
{
    FLType B, B1, B2, B3;
    double M[3][3];

    void operator()(const float *srcp, int srcStride, float *dstp, int dstStride, int width, int height) const
    {
        // packed (stride == width) buffer for the in-place vertical pass
        FLType *buf = Scratch<FLType>(ScratchFrame, size_t(width) * height);

        Recursive_Gaussian2D_Horizontal(srcp, srcStride, buf, width, width, height, B, B1, B2, B3, M);
        const int strip = 128;
        for (int x = 0; x < width; x += strip)
            Recursive_Gaussian2D_Vertical(buf + x, width, Min(strip, width - x), height, B, B1, B2, B3, M);

        for (int y = 0; y < height; y++)
        {
            const FLType *srow = buf + y * width;
            float *drow = dstp + y * dstStride;
            for (int x = 0; x < width; x++)
                drow[x] = static_cast<float>(srow[x]);
        }
    }
};

} // namespace


VSNode *CreateIirBlurNode(VSNode *src, double sigma, VSCore *core, const VSAPI *vsapi)
{
    IirBlurKernel k;
    Recursive_Gaussian_Parameters(sigma, k.B, k.B1, k.B2, k.B3);
    Recursive_Gaussian_Boundary(k.B, k.B1, k.B2, k.B3, k.M);
    return CreatePlaneFilterNode("RetinexIirBlur", src, k, core, vsapi);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
