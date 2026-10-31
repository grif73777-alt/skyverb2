#pragma once
#include "Fft.h"
#include <vector>

namespace sky
{
// MDCT with N coefficients per frame (frame length 2N, hop N), sine window.
// forward(): 2N time samples -> N coefficients (window applied inside).
// inverse(): N coefficients -> 2N windowed samples, scaled so that
//            overlap-add of consecutive frames reconstructs the input exactly.
class Mdct
{
public:
    void prepare (int numCoefs)
    {
        N = numCoefs;
        const int M = 2 * N;
        fft.setSize (M);

        win.resize ((size_t) M);
        preC.resize ((size_t) M);  preS.resize ((size_t) M);
        postC.resize ((size_t) N); postS.resize ((size_t) N);
        ipreC.resize ((size_t) N); ipreS.resize ((size_t) N);
        ipostC.resize ((size_t) M); ipostS.resize ((size_t) M);
        re.assign ((size_t) M, 0.0f);
        im.assign ((size_t) M, 0.0f);

        const double pi = 3.14159265358979323846;
        const double n0 = 0.5 + N / 2.0;

        for (int n = 0; n < M; ++n)
        {
            win[(size_t) n]   = (float) std::sin (pi * (n + 0.5) / M);
            preC[(size_t) n]  = (float) std::cos (-pi * n / M);
            preS[(size_t) n]  = (float) std::sin (-pi * n / M);
            ipostC[(size_t) n] = (float) std::cos (pi * (n + n0) / M);
            ipostS[(size_t) n] = (float) std::sin (pi * (n + n0) / M);
        }
        for (int k = 0; k < N; ++k)
        {
            postC[(size_t) k] = (float) std::cos (-pi * n0 * (k + 0.5) / N);
            postS[(size_t) k] = (float) std::sin (-pi * n0 * (k + 0.5) / N);
            ipreC[(size_t) k] = (float) std::cos (pi * n0 * k / N);
            ipreS[(size_t) k] = (float) std::sin (pi * n0 * k / N);
        }
    }

    int size() const { return N; }

    void forward (const float* in2N, float* outN)
    {
        const int M = 2 * N;
        for (int n = 0; n < M; ++n)
        {
            const float v = in2N[n] * win[(size_t) n];
            re[(size_t) n] = v * preC[(size_t) n];
            im[(size_t) n] = v * preS[(size_t) n];
        }
        fft.transform (re.data(), im.data(), false);
        for (int k = 0; k < N; ++k)
            outN[k] = re[(size_t) k] * postC[(size_t) k] - im[(size_t) k] * postS[(size_t) k];
    }

    void inverse (const float* inN, float* out2N)
    {
        const int M = 2 * N;
        for (int k = 0; k < N; ++k)
        {
            re[(size_t) k] = inN[k] * ipreC[(size_t) k];
            im[(size_t) k] = inN[k] * ipreS[(size_t) k];
        }
        for (int k = N; k < M; ++k) { re[(size_t) k] = 0.0f; im[(size_t) k] = 0.0f; }

        fft.transform (re.data(), im.data(), true);

        const float scale = 2.0f / (float) N;
        for (int n = 0; n < M; ++n)
        {
            const float v = re[(size_t) n] * ipostC[(size_t) n] - im[(size_t) n] * ipostS[(size_t) n];
            out2N[n] = v * win[(size_t) n] * scale;
        }
    }

private:
    int N = 0;
    Fft fft;
    std::vector<float> win, preC, preS, postC, postS, ipreC, ipreS, ipostC, ipostS, re, im;
};
} // namespace sky
