// Standalone DSP tests (no JUCE needed).
// Build: g++ -std=c++17 -O2 -I../Source/dsp dsp_tests.cpp -o dsp_tests && ./dsp_tests
#include "Fft.h"
#include "Mdct.h"
#include "LossyCodec.h"
#include "Reverb.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
#include <vector>
#include <complex>
#include <numeric>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace sky;

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        const bool ok_ = (cond);                          \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");   \
        std::printf (__VA_ARGS__);                        \
        std::printf ("\n");                               \
        if (! ok_) ++failures;                            \
    } while (0)

static double rms (const std::vector<float>& v, size_t a = 0, size_t b = (size_t) -1)
{
    b = std::min (b, v.size());
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}

static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

// ---------------------------------------------------------------- FFT
static void testFft()
{
    std::printf ("FFT\n");
    const int n = 64;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> d (-1, 1);
    std::vector<float> re (n), im (n), r0, i0;
    for (int i = 0; i < n; ++i) { re[(size_t) i] = d (rng); im[(size_t) i] = d (rng); }
    r0 = re; i0 = im;

    Fft fft; fft.setSize (n);
    fft.transform (re.data(), im.data(), false);

    double maxErr = 0;
    for (int k = 0; k < n; ++k)
    {
        std::complex<double> acc = 0;
        for (int t = 0; t < n; ++t)
            acc += std::complex<double> (r0[(size_t) t], i0[(size_t) t]) * std::polar (1.0, -2.0 * M_PI * k * t / n);
        maxErr = std::max (maxErr, std::abs (acc - std::complex<double> (re[(size_t) k], im[(size_t) k])));
    }
    CHECK (maxErr < 1e-4, "FFT matches naive DFT (max err %.2e)", maxErr);

    fft.transform (re.data(), im.data(), true);
    double e2 = 0;
    for (int i = 0; i < n; ++i)
        e2 = std::max (e2, (double) std::fabs (re[(size_t) i] / n - r0[(size_t) i]));
    CHECK (e2 < 1e-5, "inverse FFT round trip (max err %.2e)", e2);
}

// ---------------------------------------------------------------- MDCT
static void testMdct()
{
    std::printf ("MDCT\n");
    // 1) forward matches the textbook formula
    {
        const int N = 16;
        Mdct m; m.prepare (N);
        std::mt19937 rng (2);
        std::uniform_real_distribution<float> d (-1, 1);
        std::vector<float> x ((size_t) 2 * N), X ((size_t) N);
        for (auto& v : x) v = d (rng);
        m.forward (x.data(), X.data());

        double maxErr = 0;
        for (int k = 0; k < N; ++k)
        {
            double acc = 0;
            for (int n = 0; n < 2 * N; ++n)
            {
                const double w = std::sin (M_PI * (n + 0.5) / (2 * N));
                acc += x[(size_t) n] * w * std::cos (M_PI / N * (n + 0.5 + N / 2.0) * (k + 0.5));
            }
            maxErr = std::max (maxErr, std::fabs (acc - X[(size_t) k]));
        }
        CHECK (maxErr < 1e-4, "MDCT forward matches direct formula (max err %.2e)", maxErr);
    }

    // 2) TDAC perfect reconstruction
    {
        const int N = 512, blocks = 20;
        Mdct m; m.prepare (N);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> d (-1, 1);
        std::vector<float> x ((size_t) N * blocks), y (x.size(), 0.0f);
        for (auto& v : x) v = d (rng);

        std::vector<float> X ((size_t) N), t ((size_t) 2 * N);
        for (int b = 1; b < blocks; ++b)
        {
            m.forward (x.data() + (b - 1) * N, X.data());
            m.inverse (X.data(), t.data());
            for (int n = 0; n < 2 * N; ++n) y[(size_t) ((b - 1) * N + n)] += t[(size_t) n];
        }
        double maxErr = 0;
        for (int n = N; n < N * (blocks - 1); ++n)
            maxErr = std::max (maxErr, (double) std::fabs (y[(size_t) n] - x[(size_t) n]));
        CHECK (maxErr < 1e-4, "MDCT/IMDCT overlap-add reconstructs input (max err %.2e)", maxErr);
    }
}

// ---------------------------------------------------------------- Codec
static std::vector<float> makeMusicLike (int len, float fs, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0, 1);
    std::vector<float> x ((size_t) len);
    const float f0[] = { 220, 277.2f, 329.6f, 440, 880 };
    for (int i = 0; i < len; ++i)
    {
        float s = 0;
        for (int h = 0; h < 5; ++h)
            for (int k = 1; k <= 12; ++k)
                s += std::sin (2.0f * kPi * f0[h] * (float) k * (float) i / fs) / (float) (k * k) * 0.1f;
        const float env = 0.5f + 0.5f * std::sin (2.0f * kPi * 2.0f * (float) i / fs);
        s += 0.05f * g (rng) * env; // some broadband noise (hats-like)
        x[(size_t) i] = s * 0.5f;
    }
    return x;
}

static void runCodec (LossyCodec& c, const std::vector<float>& in, std::vector<float>& coded, std::vector<float>& dry)
{
    coded.assign (in.size(), 0.0f);
    dry.assign (in.size(), 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
    {
        float cl, cr, dl, dr;
        c.processSample (in[i], in[i] * 0.8f, cl, cr, dl, dr);
        coded[i] = cl; dry[i] = dl;
    }
}

static double snrDb (const std::vector<float>& ref, const std::vector<float>& test, int lat)
{
    double num = 0, den = 0;
    for (size_t i = (size_t) lat + 8192; i < ref.size() - 2048; ++i)
    {
        const double r = ref[i - (size_t) lat], t = test[i];
        num += r * r; den += (r - t) * (r - t);
    }
    return 10.0 * std::log10 (num / std::max (den, 1e-20));
}

static void testCodec()
{
    std::printf ("Lossy codec\n");
    const float fs = 44100.0f;
    const auto x = makeMusicLike ((int) (fs * 3), fs, 7);
    std::vector<float> coded, dry;

    // transparent path: exact reconstruction + delay == kLatency
    {
        LossyCodec c; c.prepare (fs); c.setTransparent (true);
        runCodec (c, x, coded, dry);
        const double s = snrDb (x, coded, LossyCodec::kLatency);
        CHECK (s > 80.0, "transparent mode is bit-exact up to float rounding, latency = %d samples (SNR %.1f dB)", LossyCodec::kLatency, s);

        double dryErr = 0;
        for (size_t i = (size_t) LossyCodec::kLatency; i < x.size(); ++i)
            dryErr = std::max (dryErr, (double) std::fabs (dry[i] - x[i - (size_t) LossyCodec::kLatency]));
        CHECK (dryErr == 0.0, "delayed dry output is exactly the input delayed by %d samples", LossyCodec::kLatency);
    }

    // bitrate sweep
    std::printf ("  bitrate -> SNR (in-band quantisation noise), cutoff\n");
    const float rates[] = { 8, 16, 32, 48, 64, 96, 128, 192 };
    double prev = -1e9; bool mono = true; double snr192 = 0;
    for (float r : rates)
    {
        LossyCodec c; c.prepare (fs); c.setBitrate (r);
        runCodec (c, x, coded, dry);
        const double s = snrDb (x, coded, LossyCodec::kLatency);
        std::printf ("    %5.0f kbps  SNR %6.2f dB   lowpass %5.0f Hz\n", r, s, LossyCodec::cutoffForBitrate (r));
        if (s < prev - 0.5) mono = false;
        prev = s; if (r == 192) snr192 = s;
    }
    CHECK (mono, "SNR rises with bitrate");
    CHECK (snr192 > 12.0, "192 kbps is clearly higher quality than 8 kbps (SNR %.1f dB)", snr192);

    // bandwidth: 32 kbps must remove everything above ~8.2 kHz
    {
        // white-ish noise input
        std::mt19937 rng (5);
        std::normal_distribution<float> g (0, 0.2f);
        std::vector<float> n ((size_t) fs * 2);
        for (auto& v : n) v = g (rng);

        LossyCodec c; c.prepare (fs); c.setBitrate (32);
        runCodec (c, n, coded, dry);

        const int fftN = 4096;
        Fft fft; fft.setSize (fftN);
        std::vector<float> re ((size_t) fftN), im ((size_t) fftN, 0.0f);
        double lo = 0, hi = 0; int frames = 0;
        for (size_t start = 20000; start + fftN < coded.size(); start += fftN, ++frames)
        {
            for (int i = 0; i < fftN; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) fftN);
                re[(size_t) i] = coded[start + (size_t) i] * w; im[(size_t) i] = 0;
            }
            fft.transform (re.data(), im.data(), false);
            for (int k = 1; k < fftN / 2; ++k)
            {
                const double f = k * fs / fftN, p = re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
                if (f < 7000) lo += p; else if (f > 9500) hi += p;
            }
        }
        const double ratio = 10 * std::log10 (hi / lo);
        CHECK (ratio < -50.0, "32 kbps: energy above 9.5 kHz is %.1f dB relative to <7 kHz", ratio);
    }

    // sample-rate independence of band layout (no crash / finite)
    for (float sr : { 22050.0f, 48000.0f, 96000.0f, 192000.0f })
    {
        LossyCodec c; c.prepare (sr); c.setBitrate (64);
        const auto y = makeMusicLike ((int) sr, sr, 9);
        runCodec (c, y, coded, dry);
        bool finite = true; for (float v : coded) if (! std::isfinite (v) || std::fabs (v) > 8.0f) finite = false;
        CHECK (finite, "codec finite and bounded at %.0f Hz", sr);
    }

    // silence / tiny signals do not produce NaNs
    {
        LossyCodec c; c.prepare (fs); c.setBitrate (32);
        std::vector<float> z ((size_t) 20000, 0.0f);
        runCodec (c, z, coded, dry);
        bool finite = true; for (float v : coded) if (! std::isfinite (v)) finite = false;
        CHECK (finite && rms (coded) == 0.0, "digital silence stays silent");
    }
}

// ---------------------------------------------------------------- Reverb
static double estimateRt60 (const std::vector<float>& ir, float fs)
{
    // Schroeder backward integration, fit between -5 and -35 dB, extrapolate to -60
    std::vector<double> edc (ir.size());
    double acc = 0;
    for (size_t i = ir.size(); i-- > 0;) { acc += (double) ir[i] * ir[i]; edc[i] = acc; }
    const double e0 = edc[0];
    long t5 = -1, t35 = -1;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double d = 10 * std::log10 (edc[i] / e0);
        if (t5 < 0 && d <= -5) t5 = (long) i;
        if (t35 < 0 && d <= -35) { t35 = (long) i; break; }
    }
    if (t5 < 0 || t35 < 0) return -1;
    return 2.0 * (double) (t35 - t5) / fs;
}

static void runReverb (Reverb& r, const std::vector<float>& inL, const std::vector<float>& inR,
                       std::vector<float>& oL, std::vector<float>& oR)
{
    oL.assign (inL.size(), 0.0f);
    oR.assign (inL.size(), 0.0f);
    const int blk = 480;
    for (size_t s = 0; s < inL.size(); s += (size_t) blk)
    {
        const int n = (int) std::min<size_t> ((size_t) blk, inL.size() - s);
        r.process (inL.data() + s, inR.data() + s, oL.data() + s, oR.data() + s, n);
    }
}

static void testReverb()
{
    std::printf ("Reverb\n");
    const float fs = 48000.0f;

    // RT60 vs. Decay knob
    for (float decay : { 0.8f, 2.0f, 5.0f })
    {
        Reverb r; r.prepare (fs);
        ReverbParams p; p.decay = decay; p.damping = 0.0f; p.predelayMs = 0; p.size = 0.5f;
        p.lowCutHz = 20; p.highCutHz = 20000; p.modulation = 0.0f;
        r.setParams (p);

        const size_t len = (size_t) (fs * (decay * 2.6f + 1.0f));
        std::vector<float> iL (len, 0.0f), iR (len, 0.0f), oL, oR;
        iL[0] = iR[0] = 1.0f;
        runReverb (r, iL, iR, oL, oR);

        const double rt = estimateRt60 (oL, fs);
        const bool ok = rt > decay * 0.7 && rt < decay * 1.3;
        CHECK (ok, "Decay %.1f s -> measured RT60 %.2f s", decay, rt);
    }

    // damping shortens the high-frequency tail only
    {
        double hf[2] = { 0, 0 }, lf[2] = { 0, 0 };
        int idx = 0;
        for (float damp : { 0.0f, 0.9f })
        {
            Reverb r; r.prepare (fs);
            ReverbParams p; p.decay = 3.0f; p.damping = damp; p.predelayMs = 0; p.lowCutHz = 20; p.highCutHz = 20000;
            p.modulation = 0.0f;
            r.setParams (p);
            std::vector<float> iL ((size_t) fs * 9, 0.0f), iR (iL.size(), 0.0f), oL, oR;
            iL[0] = iR[0] = 1.0f;
            runReverb (r, iL, iR, oL, oR);

            std::vector<float> hi = oL, lo = oL;
            Biquad h1, h2, l1, l2;
            h1.setHighpass (fs, 6000); h2.setHighpass (fs, 6000);
            l1.setLowpass (fs, 400);   l2.setLowpass (fs, 400);
            for (auto& v : hi) v = h2.process (h1.process (v));
            for (auto& v : lo) v = l2.process (l1.process (v));
            hf[idx] = estimateRt60 (hi, fs);
            lf[idx] = estimateRt60 (lo, fs);
            ++idx;
        }
        CHECK (hf[1] < hf[0] * 0.5, "Damping 0 -> HF RT60 %.2f s, damping 0.9 -> %.2f s", hf[0], hf[1]);
        CHECK (std::fabs (lf[1] - lf[0]) < lf[0] * 0.25, "low frequencies keep their decay (%.2f s vs %.2f s)", lf[0], lf[1]);
    }

    // stereo decorrelation and level on noise
    {
        Reverb r; r.prepare (fs);
        ReverbParams p; p.decay = 2.5f; r.setParams (p);
        std::mt19937 rng (11);
        std::normal_distribution<float> g (0, 0.25f);
        std::vector<float> iL ((size_t) fs * 8), iR;
        for (auto& v : iL) v = g (rng);
        iR = iL; // mono source
        std::vector<float> oL, oR;
        runReverb (r, iL, iR, oL, oR);

        const size_t a = (size_t) fs * 3;
        double cLR = 0, eL = 0, eR = 0;
        for (size_t i = a; i < oL.size(); ++i) { cLR += (double) oL[i] * oR[i]; eL += (double) oL[i] * oL[i]; eR += (double) oR[i] * oR[i]; }
        const double corr = cLR / std::sqrt (eL * eR);
        const double lvl = db (rms (oL, a) / rms (iL, a));
        CHECK (std::fabs (corr) < 0.35, "mono in -> L/R wet correlation %.2f (wide)", corr);
        CHECK (lvl > -9.0 && lvl < 6.0, "wet level vs. input on noise: %.1f dB", lvl);
    }

    // level should stay comparable across Decay values (normalisation)
    {
        double lv[3]; int idx = 0;
        for (float decay : { 0.5f, 3.0f, 15.0f })
        {
            Reverb r; r.prepare (fs);
            ReverbParams p; p.decay = decay; r.setParams (p);
            std::mt19937 rng (12);
            std::normal_distribution<float> g (0, 0.25f);
            std::vector<float> iL ((size_t) fs * 25), oL, oR;
            for (auto& v : iL) v = g (rng);
            std::vector<float> iR = iL;
            runReverb (r, iL, iR, oL, oR);
            lv[idx++] = db (rms (oL, (size_t) fs * 18) / rms (iL, (size_t) fs * 18));
        }
        CHECK (std::fabs (lv[0] - lv[2]) < 6.0, "wet level at decay 0.5/3/15 s: %.1f / %.1f / %.1f dB", lv[0], lv[1], lv[2]);
    }

    // stress: random parameter jumps, max decay, loud input -> must stay finite and bounded
    for (float sr : { 44100.0f, 96000.0f })
    {
        Reverb r; r.prepare (sr);
        std::mt19937 rng (13);
        std::uniform_real_distribution<float> u (0, 1);
        std::normal_distribution<float> g (0, 0.5f);
        double peak = 0; bool finite = true;
        std::vector<float> iL (256), iR (256), oL (256), oR (256);
        for (int blk = 0; blk < (int) (sr * 15 / 256); ++blk)
        {
            ReverbParams p;
            p.size = u (rng); p.decay = 0.1f + 29.9f * u (rng); p.predelayMs = 250 * u (rng);
            p.damping = u (rng); p.diffusion = u (rng); p.modulation = u (rng);
            p.lowCutHz = 20 + 980 * u (rng); p.highCutHz = 1000 + 19000 * u (rng); p.width = 2 * u (rng);
            if (blk % 7 == 0) r.setParams (p);
            for (int i = 0; i < 256; ++i) { iL[(size_t) i] = g (rng); iR[(size_t) i] = g (rng); }
            r.process (iL.data(), iR.data(), oL.data(), oR.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                if (! std::isfinite (oL[(size_t) i]) || ! std::isfinite (oR[(size_t) i])) finite = false;
                peak = std::max ({ peak, (double) std::fabs (oL[(size_t) i]), (double) std::fabs (oR[(size_t) i]) });
            }
        }
        CHECK (finite && peak < 12.0, "%.0f Hz random-parameter stress: finite, peak %.2f", sr, peak);
    }

    // long decay must actually die out when input stops
    {
        Reverb r; r.prepare (fs);
        ReverbParams p; p.decay = 4.0f; p.damping = 0.3f; r.setParams (p);
        std::mt19937 rng (14);
        std::normal_distribution<float> g (0, 0.3f);
        std::vector<float> iL ((size_t) fs * 20, 0.0f), oL, oR;
        for (size_t i = 0; i < (size_t) fs; ++i) iL[i] = g (rng);
        std::vector<float> iR = iL;
        runReverb (r, iL, iR, oL, oR);
        const double early = rms (oL, (size_t) fs / 2, (size_t) fs);
        const double late = rms (oL, (size_t) fs * 12, (size_t) fs * 13);
        CHECK (db (late / early) < -60.0, "tail 12 s after the input is %.0f dB below the early level (decay 4 s)", db (late / early));
    }
}

int main()
{
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);

    testFft();
    testMdct();
    testCodec();
    testReverb();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
