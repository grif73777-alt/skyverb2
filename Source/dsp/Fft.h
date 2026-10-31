#pragma once
#include <vector>
#include <cmath>
#include <utility>

namespace sky
{
constexpr float kPi = 3.14159265358979323846f;

// Radix-2 complex FFT on split re/im arrays. No scaling in either direction.
class Fft
{
public:
    void setSize (int n)
    {
        size = n;
        int bits = 0;
        while ((1 << bits) < n) ++bits;

        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }

        twr.resize ((size_t) n / 2);
        twi.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * i / n;
            twr[(size_t) i] = (float) std::cos (a);
            twi[(size_t) i] = (float) std::sin (a);
        }
    }

    // inverse == false : e^{-i...}   inverse == true : e^{+i...}
    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        const float sgn = inverse ? -1.0f : 1.0f;
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;
            for (int i = 0; i < size; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = twr[(size_t) (k * step)];
                    const float wi = sgn * twi[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

    int getSize() const { return size; }

private:
    int size = 0;
    std::vector<int> rev;
    std::vector<float> twr, twi;
};
} // namespace sky
