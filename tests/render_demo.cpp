// Renders a short audio demo of the Skyverb DSP (no JUCE needed):
//   1. dry   2. reverb   3. reverb + lossy 64 kbps   4. + lossy 24 kbps   5. + lossy 10 kbps
// Build: g++ -std=c++17 -O2 -I../Source/dsp render_demo.cpp -o render_demo && ./render_demo demo.wav
#include "Reverb.h"
#include "LossyCodec.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <random>

using namespace sky;

static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path, "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) L.size();
    const uint32_t dataBytes = n * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (float v : { L[i], R[i] })
        {
            const float c = std::fmax (-1.0f, std::fmin (1.0f, v));
            const int16_t s = (int16_t) std::lrintf (c * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    }
    std::fclose (f);
}

// plucked-string-ish notes: a few decaying partials + a little noise click
static void makeMelody (std::vector<float>& L, std::vector<float>& R, int sr)
{
    struct Note { float t, hz, len; };
    const Note notes[] = { { 0.00f, 523.25f, 0.9f }, { 0.50f, 659.25f, 0.9f }, { 1.00f, 783.99f, 0.9f }, { 1.50f, 987.77f, 1.4f },
                           { 2.40f, 880.00f, 0.9f }, { 2.90f, 659.25f, 0.9f }, { 3.40f, 587.33f, 0.9f }, { 3.90f, 523.25f, 1.8f } };
    const int len = (int) (6.0f * (float) sr);
    L.assign ((size_t) len, 0.0f);
    R.assign ((size_t) len, 0.0f);
    std::mt19937 rng (4);
    std::normal_distribution<float> g (0, 1);
    for (const auto& nt : notes)
    {
        const int s0 = (int) (nt.t * (float) sr);
        const int nl = (int) (nt.len * (float) sr);
        for (int i = 0; i < nl && s0 + i < len; ++i)
        {
            const float t = (float) i / (float) sr;
            const float env = std::exp (-t * 3.2f) * (1.0f - std::exp (-t * 600.0f));
            float v = 0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (2.0f * kPi * nt.hz * (float) h * t) / (float) (h * h) * std::exp (-t * 1.5f * (float) h);
            v = v * env * 0.35f + (i < 300 ? g (rng) * 0.05f * (1.0f - (float) i / 300.0f) : 0.0f);
            L[(size_t) (s0 + i)] += v;
            R[(size_t) (s0 + i)] += v * 0.95f;
        }
    }
}

struct Chain
{
    Reverb rev; LossyCodec codec;
    void init (int sr, float decay, float lossyKbps)
    {
        rev.prepare (sr);
        ReverbParams p; p.decay = decay; p.size = 0.6f; p.predelayMs = 18; p.damping = 0.45f; p.diffusion = 0.75f; p.modulation = 0.35f;
        rev.setParams (p);
        codec.prepare (sr);
        codec.setBitrate (lossyKbps);
    }
};

int main (int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "skyverb_demo.wav";
    const int sr = 44100;
    std::vector<float> dL, dR;
    makeMelody (dL, dR, sr);
    const int tail = sr * 4; // let the tail ring out
    dL.resize (dL.size() + (size_t) tail, 0.0f);
    dR.resize (dR.size() + (size_t) tail, 0.0f);

    std::vector<float> oL, oR;
    auto append = [&] (const std::vector<float>& l, const std::vector<float>& r)
    {
        oL.insert (oL.end(), l.begin(), l.end());
        oR.insert (oR.end(), r.begin(), r.end());
        oL.insert (oL.end(), (size_t) sr, 0.0f);
        oR.insert (oR.end(), (size_t) sr, 0.0f);
    };

    append (dL, dR); // 1. dry

    const float kbps[] = { 0, 64, 24, 10 }; // 0 = reverb only
    for (float kb : kbps)
    {
        Chain c; c.init (sr, 3.5f, kb > 0 ? kb : 128.0f);
        std::vector<float> wL (dL.size()), wR (dL.size());
        c.rev.process (dL.data(), dR.data(), wL.data(), wR.data(), (int) dL.size());

        std::vector<float> rL (dL.size()), rR (dL.size());
        const float mix = 0.4f;
        const float dg = std::cos (mix * 1.5707963f), wg = std::sin (mix * 1.5707963f);
        for (size_t i = 0; i < dL.size(); ++i)
        {
            const float inL = dL[i] * dg + wL[i] * wg, inR = dR[i] * dg + wR[i] * wg;
            if (kb > 0)
            {
                float cl, cr, pl, pr;
                c.codec.processSample (inL, inR, cl, cr, pl, pr);
                rL[i] = cl; rR[i] = cr;
            }
            else { rL[i] = inL; rR[i] = inR; }
        }
        // drop the codec latency so every section lines up with the dry one
        if (kb > 0) { rL.erase (rL.begin(), rL.begin() + LossyCodec::kLatency); rR.erase (rR.begin(), rR.begin() + LossyCodec::kLatency); rL.resize (dL.size()); rR.resize (dL.size()); }
        append (rL, rR);
    }

    writeWav (out, oL, oR, sr);
    std::printf ("wrote %s (%.1f s)\n", out, (double) oL.size() / sr);
    return 0;
}
