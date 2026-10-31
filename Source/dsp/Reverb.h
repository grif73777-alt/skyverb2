#pragma once
#include "Fft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sky
{
inline int nextPow2 (int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

inline int popcount4 (int v)
{
    int c = 0;
    for (int b = 0; b < 4; ++b) c += (v >> b) & 1;
    return c;
}

// Power-of-two circular buffer. read() is "read before push": read (d) returns the
// sample that was pushed d samples ago. Fractional reads use 4-point Hermite.
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        size = nextPow2 (maxDelay + 8);
        mask = size - 1;
        buf.assign ((size_t) size, 0.0f);
        w = 0;
    }

    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    inline float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }

    inline float read (float d) const // d >= 2
    {
        const int di = (int) d;
        const float f = d - (float) di;
        const float ym1 = buf[(size_t) ((w - (di - 1)) & mask)];
        const float y0  = buf[(size_t) ((w - di) & mask)];
        const float y1  = buf[(size_t) ((w - (di + 1)) & mask)];
        const float y2  = buf[(size_t) ((w - (di + 2)) & mask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

private:
    std::vector<float> buf;
    int size = 0, mask = 0, w = 0;
};

class Allpass
{
public:
    void prepare (int length) { len = std::max (4, length); dl.prepare (len + 8); }
    void clear() { dl.clear(); }
    void setGain (float gain) { g = gain; }

    inline float process (float x)
    {
        const float d = dl.readInt (len);
        const float v = x - g * d;
        dl.push (v);
        return g * v + d;
    }

private:
    DelayLine dl;
    int len = 4;
    float g = 0.5f;
};

class Biquad
{
public:
    void reset() { z1 = z2 = 0.0f; }

    void setLowpass (float fs, float fc) { design (fs, fc, false); }
    void setHighpass (float fs, float fc) { design (fs, fc, true); }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (float fs, float fc, bool highpass)
    {
        fc = std::clamp (fc, 5.0f, fs * 0.47f);
        const float w0 = 2.0f * kPi * fc / fs;
        const float cw = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        if (highpass)
        {
            b0 = (1.0f + cw) * 0.5f / a0;
            b1 = -(1.0f + cw) / a0;
            b2 = b0;
        }
        else
        {
            b0 = (1.0f - cw) * 0.5f / a0;
            b1 = (1.0f - cw) / a0;
            b2 = b0;
        }
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

struct ReverbParams
{
    float size = 0.5f;        // 0..1
    float decay = 2.5f;       // RT60, seconds
    float predelayMs = 15.0f; // 0..250
    float damping = 0.4f;     // 0..1 (high-frequency absorption)
    float diffusion = 0.7f;   // 0..1
    float modulation = 0.3f;  // 0..1
    float lowCutHz = 100.0f;
    float highCutHz = 14000.0f;
    float width = 1.0f;       // 0..2
};

// 16-line feedback delay network (Hadamard feedback matrix) with
//  - pre-delay,
//  - 4-stage stereo allpass input diffusion,
//  - per-line RT60 gains and one-pole high-frequency damping in the loop,
//  - slow modulation of the delay lengths (kills metallic ringing),
//  - decorrelated stereo taps, output EQ and width.
class Reverb
{
public:
    static constexpr int kLines = 16;
    static constexpr int kCtl = 16; // control-rate interval in samples

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float srScale = fs / 44100.0f;

        const int maxLine = (int) std::ceil (2593.0f * 2.0f * srScale + 24.0f * srScale) + 16;
        for (auto& l : lines) l.prepare (maxLine);

        static const int apL[4] = { 142, 107, 379, 277 };
        static const int apR[4] = { 149, 113, 401, 293 };
        for (int i = 0; i < 4; ++i)
        {
            allL[(size_t) i].prepare ((int) std::lround ((float) apL[i] * srScale));
            allR[(size_t) i].prepare ((int) std::lround ((float) apR[i] * srScale));
        }

        preL.prepare ((int) (0.26f * fs) + 16);
        preR.prepare ((int) (0.26f * fs) + 16);

        for (int j = 0; j < kLines; ++j)
        {
            phase[(size_t) j] = 2.0f * kPi * (float) j / (float) kLines;
            phaseInc[(size_t) j] = 2.0f * kPi * (0.31f + 0.083f * (float) j) / fs;
            sL[(size_t) j] = (popcount4 (j & 3) & 1) ? -1.0f : 1.0f;
            sR[(size_t) j] = (popcount4 (j & 12) & 1) ? -1.0f : 1.0f;
            oL[(size_t) j] = (popcount4 (j & 5) & 1) ? -1.0f : 1.0f;
            oR[(size_t) j] = (popcount4 (j & 10) & 1) ? -1.0f : 1.0f;
        }

        snap = true;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& a : allL) a.clear();
        for (auto& a : allR) a.clear();
        preL.clear();
        preR.clear();
        lp.fill (0.0f);
        hpL.reset(); hpR.reset(); lpfL.reset(); lpfR.reset();
        ctl = 0;
    }

    void setParams (const ReverbParams& p) { target = p; }

    // dry in -> wet out (100% wet, EQ'd, width applied)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ctl == 0) { updateControl(); ctl = kCtl; }
            --ctl;

            // pre-delay
            const float pdL = preL.read (preSamples);
            const float pdR = preR.read (preSamples);
            preL.push (inL[i]);
            preR.push (inR[i]);

            // input diffusion
            float dl = pdL, dr = pdR;
            for (int a = 0; a < 4; ++a)
            {
                dl = allL[(size_t) a].process (dl);
                dr = allR[(size_t) a].process (dr);
            }

            // FDN
            std::array<float, kLines> y;
            for (int j = 0; j < kLines; ++j)
            {
                const float d = curLen[(size_t) j] + modDepth * std::sin (phase[(size_t) j]);
                phase[(size_t) j] += phaseInc[(size_t) j];
                if (phase[(size_t) j] > 2.0f * kPi) phase[(size_t) j] -= 2.0f * kPi;

                const float raw = lines[(size_t) j].read (d);
                lp[(size_t) j] = (1.0f - dampA) * raw + dampA * lp[(size_t) j];
                y[(size_t) j] = lp[(size_t) j] * gain[(size_t) j];
            }

            float wl = 0.0f, wr = 0.0f;
            for (int j = 0; j < kLines; ++j)
            {
                wl += y[(size_t) j] * oL[(size_t) j];
                wr += y[(size_t) j] * oR[(size_t) j];
            }
            wl *= 0.25f * outGain;
            wr *= 0.25f * outGain;

            // Hadamard feedback (orthonormal, 1/4 scaling)
            for (int len = 1; len < kLines; len <<= 1)
                for (int a = 0; a < kLines; a += 2 * len)
                    for (int b = a; b < a + len; ++b)
                    {
                        const float u = y[(size_t) b], v = y[(size_t) (b + len)];
                        y[(size_t) b] = u + v;
                        y[(size_t) (b + len)] = u - v;
                    }

            for (int j = 0; j < kLines; ++j)
                lines[(size_t) j].push (y[(size_t) j] * 0.25f + 0.7071f * (dl * sL[(size_t) j] + dr * sR[(size_t) j]));

            // output EQ
            wl = lpfL.process (hpL.process (wl));
            wr = lpfR.process (hpR.process (wr));

            // width
            const float mid = 0.5f * (wl + wr);
            const float side = 0.5f * (wl - wr) * widthS;
            outL[i] = mid + side;
            outR[i] = mid - side;
        }
    }

private:
    static constexpr float tcFast = 0.03f;

    static float smoothCoef (float seconds, float fs_)
    {
        return 1.0f - std::exp (-(float) kCtl / (seconds * fs_));
    }

    void updateControl()
    {
        const float srScale = fs / 44100.0f;
        if (snap)
        {
            sizeS = target.size; decayS = target.decay; preMsS = target.predelayMs;
            dampS = target.damping; diffS = target.diffusion; modS = target.modulation;
            lowS = target.lowCutHz; highS = target.highCutHz; widthS = target.width;
        }
        else
        {
            const float slow = smoothCoef (0.12f, fs);
            const float fast = smoothCoef (tcFast, fs);
            sizeS += slow * (target.size - sizeS);
            decayS += slow * (target.decay - decayS);
            preMsS += slow * (target.predelayMs - preMsS);
            dampS += fast * (target.damping - dampS);
            diffS += fast * (target.diffusion - diffS);
            modS += fast * (target.modulation - modS);
            lowS += fast * (target.lowCutHz - lowS);
            highS += fast * (target.highCutHz - highS);
            widthS += fast * (target.width - widthS);
        }

        static const float base[kLines] = { 887, 997, 1103, 1217, 1327, 1439, 1553, 1667,
                                            1783, 1901, 2017, 2131, 2251, 2357, 2477, 2593 };
        const float scale = (0.35f + 1.65f * std::clamp (sizeS, 0.0f, 1.0f)) * srScale;
        const float rt60 = std::max (0.05f, decayS);
        float gSum = 0.0f;
        for (int j = 0; j < kLines; ++j)
        {
            const float len = base[j] * scale;
            curLen[(size_t) j] = len;
            const float g = std::min (0.99995f, std::exp (-6.907755f * len / (fs * rt60)));
            gain[(size_t) j] = g;
            gSum += g;
        }
        const float gMean = gSum / (float) kLines;
        outGain = 1.8f * std::sqrt (std::max (1.0e-4f, 1.0f - gMean * gMean));

        const float fcDamp = std::min(20000.0f * std::pow (0.06f, std::clamp (dampS, 0.0f, 1.0f)), fs * 0.45f);
        dampA = std::exp (-2.0f * kPi * fcDamp / fs);

        modDepth = std::clamp (modS, 0.0f, 1.0f) * 20.0f * srScale;

        const float apG = 0.72f * std::clamp (diffS, 0.0f, 1.0f);
        for (int a = 0; a < 4; ++a) { allL[(size_t) a].setGain (apG); allR[(size_t) a].setGain (apG); }

        preSamples = std::max (4.0f, preMsS * 0.001f * fs);

        hpL.setHighpass (fs, lowS);  hpR.setHighpass (fs, lowS);
        lpfL.setLowpass (fs, highS); lpfR.setLowpass (fs, highS);

        snap = false;
    }

    float fs = 44100.0f;
    ReverbParams target;
    bool snap = true;
    int ctl = 0;

    std::array<DelayLine, kLines> lines;
    std::array<Allpass, 4> allL, allR;
    DelayLine preL, preR;
    Biquad hpL, hpR, lpfL, lpfR;

    std::array<float, kLines> lp {}, gain {}, curLen {}, phase {}, phaseInc {}, sL {}, sR {}, oL {}, oR {};

    float sizeS = 0.5f, decayS = 2.5f, preMsS = 15.0f, dampS = 0.4f, diffS = 0.7f, modS = 0.3f;
    float lowS = 100.0f, highS = 14000.0f, widthS = 1.0f;
    float dampA = 0.3f, outGain = 0.3f, modDepth = 0.0f, preSamples = 700.0f;
};
} // namespace sky
