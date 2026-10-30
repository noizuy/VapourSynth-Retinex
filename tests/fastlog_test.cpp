// Exhaustive accuracy test of FastLog (include/Simd.h): every positive normal float is compared with log() evaluated
// in double, and the error is measured in ulp of the float result. Run with `meson test -C build`.

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Simd.h"


namespace
{

// |got - exact| in units of the float spacing at the correctly rounded result
double UlpError(float got, double exact)
{
    const float rounded = static_cast<float>(exact);
    const float next = std::nextafter(std::fabs(rounded), FLT_MAX);
    const double ulp = static_cast<double>(next) - std::fabs(rounded);
    return std::fabs(static_cast<double>(got) - exact) / ulp;
}

struct Result
{
    double maxUlp = 0;
    float worstX = 0;
    uint64_t count = 0;
};

// All floats with bit patterns in [first, last]; log(1) is exactly 0 (no ulp scale) and is checked separately
Result Scan(uint32_t first, uint32_t last)
{
    Result r;
    for (uint32_t b = first; ; b++)
    {
        float x;
        std::memcpy(&x, &b, sizeof(x));
        if (x != 1.f)
        {
            const double e = UlpError(FastLog(x), std::log(static_cast<double>(x)));
            if (e > r.maxUlp)
            {
                r.maxUlp = e;
                r.worstX = x;
            }
            r.count++;
        }
        if (b == last) break;
    }
    return r;
}

} // namespace


int main()
{
    // MSRCP/MSRCR only take the log of values >= 1; the bound must also hold for the documented range down to FLT_MIN
    const double bound = 1.0;
    int failed = 0;

    if (FastLog(1.f) != 0.f)
    {
        std::printf("FAIL: FastLog(1) = %.9g, expected 0\n", FastLog(1.f));
        failed = 1;
    }

    const struct { const char *name; uint32_t first, last; } ranges[] =
    {
        { "[FLT_MIN, 1)", 0x00800000u, 0x3f7fffffu },
        { "(1, FLT_MAX]", 0x3f800001u, 0x7f7fffffu },
    };

    for (const auto &range : ranges)
    {
        const Result r = Scan(range.first, range.last);
        const bool ok = r.maxUlp <= bound;
        std::printf("%s %s: %llu values, max error %.3f ulp at x = %.9g\n", ok ? "ok  " : "FAIL", range.name,
            static_cast<unsigned long long>(r.count), r.maxUlp, r.worstX);
        if (!ok) failed = 1;
    }

    return failed;
}
