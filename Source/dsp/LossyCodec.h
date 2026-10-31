#pragma once
#include "Mdct.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sky
{
// A small perceptual-style transform codec used purely as an effect.
// It does what a real low-bitrate codec does, in real time:
//   MDCT (512 bins, sine window) -> joint M/S -> bark-band energy weighting
//   -> power-law quantiser (x^0.75, like MP3/AAC) with a global step found by
//   bisection so the frame fits the bit budget of the chosen bitrate
//   -> bandwidth lowpass that depends on bitrate -> inverse MDCT.
// The audible results are the familiar ones: lost highs, "birdies"/spectral
// holes, pre-echo smearing, watery stereo image.
//
// Latency is exactly kLatency samples. The dry signal delayed by the same amount
// is returned too so the host can crossfade coded/uncoded without comb filtering.
class LossyCodec
{
public:
    static constexpr int N = 512;
    static constexpr int kLatency = 2 * N;

    LossyCodec() { mdct.prepare (N); reset(); }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        buildBands();
        reset();
    }

    void reset()
    {
        for (auto* a : { &curL, &curR, &prevL, &prevR, &outL, &outR, &ovL, &ovR })
            a->fill (0.0f);
        ringL.fill (0.0f);
        ringR.fill (0.0f);
        pos = 0;
        ringPos = 0;
    }

    void setBitrate (float kbps) { bitrate = std::clamp (kbps, 2.0f, 512.0f); }
    void setTransparent (bool t) { transparent = t; } // test hook: skip quantiser

    inline void processSample (float inL, float inR,
                               float& codedL, float& codedR,
                               float& delayedL, float& delayedR)
    {
        codedL = outL[(size_t) pos];
        codedR = outR[(size_t) pos];
        delayedL = ringL[(size_t) ringPos];
        delayedR = ringR[(size_t) ringPos];

        ringL[(size_t) ringPos] = inL;
        ringR[(size_t) ringPos] = inR;
        ringPos = (ringPos + 1) & (2 * N - 1);

        curL[(size_t) pos] = inL;
        curR[(size_t) pos] = inR;

        if (++pos == N)
        {
            processFrame();
            pos = 0;
        }
    }

    // Bandwidth (Hz) the codec keeps at a given bitrate.
    static float cutoffForBitrate (float kbps)
    {
        static const float tb[][2] = { { 8, 2600 },  { 16, 4600 },  { 24, 6600 },  { 32, 8200 },
                                       { 48, 10800 }, { 64, 12800 }, { 96, 15200 }, { 128, 16400 },
                                       { 160, 18200 }, { 192, 19600 }, { 320, 20500 }, { 512, 22000 } };
        constexpr int n = (int) (sizeof (tb) / sizeof (tb[0]));
        if (kbps <= tb[0][0]) return tb[0][1];
        for (int i = 1; i < n; ++i)
        {
            if (kbps <= tb[i][0])
            {
                const float t = std::log2 (kbps / tb[i - 1][0]) / std::log2 (tb[i][0] / tb[i - 1][0]);
                return tb[i - 1][1] + t * (tb[i][1] - tb[i - 1][1]);
            }
        }
        return tb[n - 1][1];
    }

private:
    static constexpr int kMaxBands = 32;

    void buildBands()
    {
        static const float edgesHz[] = { 0, 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720,
                                         2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000,
                                         15500, 1e9f };
        numBands = 0;
        int last = 0;
        for (size_t i = 1; i < sizeof (edgesHz) / sizeof (edgesHz[0]); ++i)
        {
            int bin = (int) std::lround (std::min (edgesHz[i], fs * 0.5f) / (fs * 0.5f) * N);
            bin = std::min (bin, N);
            if (bin <= last) continue;
            bandLo[(size_t) numBands] = last;
            bandHi[(size_t) numBands] = bin;
            ++numBands;
            last = bin;
            if (last >= N) break;
        }
        if (last < N) bandHi[(size_t) numBands - 1] = N;
    }

    static float bitCost (int q)
    {
        static const std::array<float, 256> tab = []
        {
            std::array<float, 256> t {};
            t[0] = 0.08f; // zeros are run-length coded, nearly free
            for (int i = 1; i < 256; ++i) t[(size_t) i] = 2.0f + 1.7f * std::log2 ((float) i);
            return t;
        }();
        return q < 256 ? tab[(size_t) q] : 2.0f + 1.7f * std::log2 ((float) q);
    }

    // x^0.75 without pow()
    static inline float pow075 (float x)
    {
        const float s = std::sqrt (x);
        return s * std::sqrt (s);
    }

    void quantise (float* m, float* s)
    {
        // bandwidth
        const float cutoff = std::min (cutoffForBitrate (bitrate), fs * 0.5f);
        const int bw = std::clamp ((int) (cutoff / (fs * 0.5f) * N), 8, N);
        for (int k = bw; k < N; ++k) { m[k] = 0.0f; s[k] = 0.0f; }

        // band weights from combined energy (step grows with band level -> "masking")
        std::array<float, kMaxBands> w {};
        int active = 0;
        for (int b = 0; b < numBands; ++b)
        {
            const int lo = bandLo[(size_t) b];
            const int hi = std::min (bandHi[(size_t) b], bw);
            if (lo >= hi) { w[(size_t) b] = 0.0f; continue; }
            float e = 0.0f;
            for (int k = lo; k < hi; ++k) e += m[k] * m[k] + s[k] * s[k];
            const float rms = std::sqrt (e / (float) (hi - lo));
            w[(size_t) b] = std::pow (std::max (rms, 1.0e-6f), 0.55f);
            ++active;
        }

        const float frameBits = bitrate * 1000.0f * (float) N / fs;
        const float budget = std::max (16.0f, frameBits - (6.0f + 1.5f * (float) active));

        auto bitsFor = [&] (float delta)
        {
            float bits = 0.0f;
            for (int b = 0; b < numBands; ++b)
            {
                const int lo = bandLo[(size_t) b];
                const int hi = std::min (bandHi[(size_t) b], bw);
                if (lo >= hi) continue;
                const float inv = 1.0f / (delta * w[(size_t) b]);
                for (int k = lo; k < hi; ++k)
                {
                    const int qm = (int) (pow075 (std::fabs (m[k]) * inv) + 0.4054f);
                    const int qs = (int) (pow075 (std::fabs (s[k]) * inv) + 0.4054f);
                    bits += bitCost (qm) + bitCost (qs);
                }
            }
            return bits;
        };

        float lo = -30.0f, hi = 14.0f; // log2 of global step
        for (int it = 0; it < 18; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            if (bitsFor (std::exp2 (mid)) > budget) lo = mid; else hi = mid;
        }
        const float delta = std::exp2 (hi);

        for (int b = 0; b < numBands; ++b)
        {
            const int bl = bandLo[(size_t) b];
            const int bh = std::min (bandHi[(size_t) b], bw);
            if (bl >= bh) continue;
            const float step = delta * w[(size_t) b];
            const float inv = 1.0f / step;
            for (int k = bl; k < bh; ++k)
            {
                for (float* c : { &m[k], &s[k] })
                {
                    const float v = *c;
                    const int q = std::min (8191, (int) (pow075 (std::fabs (v) * inv) + 0.4054f));
                    const float mag = q == 0 ? 0.0f : (float) q * std::cbrt ((float) q) * step;
                    *c = v < 0.0f ? -mag : mag;
                }
            }
        }
    }

    void processFrame()
    {
        float frame[2 * N];
        float cL[N], cR[N], tmp[2 * N];

        std::memcpy (frame, prevL.data(), N * sizeof (float));
        std::memcpy (frame + N, curL.data(), N * sizeof (float));
        mdct.forward (frame, cL);
        std::memcpy (frame, prevR.data(), N * sizeof (float));
        std::memcpy (frame + N, curR.data(), N * sizeof (float));
        mdct.forward (frame, cR);

        if (! transparent)
        {
            float m[N], s[N];
            for (int k = 0; k < N; ++k)
            {
                m[k] = 0.5f * (cL[k] + cR[k]);
                s[k] = 0.5f * (cL[k] - cR[k]);
            }
            quantise (m, s);
            for (int k = 0; k < N; ++k)
            {
                cL[k] = m[k] + s[k];
                cR[k] = m[k] - s[k];
            }
        }

        mdct.inverse (cL, tmp);
        for (int n = 0; n < N; ++n) { outL[(size_t) n] = ovL[(size_t) n] + tmp[n]; ovL[(size_t) n] = tmp[N + n]; }
        mdct.inverse (cR, tmp);
        for (int n = 0; n < N; ++n) { outR[(size_t) n] = ovR[(size_t) n] + tmp[n]; ovR[(size_t) n] = tmp[N + n]; }

        prevL = curL;
        prevR = curR;
    }

    Mdct mdct;
    float fs = 44100.0f;
    float bitrate = 128.0f;
    bool transparent = false;

    int numBands = 0;
    std::array<int, kMaxBands> bandLo {}, bandHi {};

    std::array<float, N> curL {}, curR {}, prevL {}, prevR {}, outL {}, outR {}, ovL {}, ovR {};
    std::array<float, 2 * N> ringL {}, ringR {};
    int pos = 0, ringPos = 0;
};
} // namespace sky
