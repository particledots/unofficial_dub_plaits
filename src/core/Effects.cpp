#include "Effects.h"
#include <algorithm>
#include <cmath>

namespace dub {

namespace {

const char* const kEchoNames[kNumEchoTimes] = { "1/16", "1/8T", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" };
const double kEchoBeats[kNumEchoTimes] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0 };

constexpr double kPi = 3.14159265358979323846;
constexpr double kSlowestBpm = 30.0;       // below this the echo time stops growing (the longest echo, 1/2, is then 4 s)
constexpr double kMaxEchoSeconds = 4.0;

inline float flush (float x) { return std::fabs (x) < 1e-20f ? 0.0f : x; }

// Smooth saturation: near-linear for small signals, bounded by +-1.
inline float softClip (float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float onePoleCoef (double hz, double sr)
{
    const double c = 1.0 - std::exp (-2.0 * kPi * hz / sr);
    return (float) std::min (0.99, std::max (0.0, c));
}

// A circular buffer read at a (fractional) distance behind the write position.
struct Delay
{
    std::vector<float> buf;
    unsigned mask = 0, w = 0;

    void init (size_t maxDelay)
    {
        size_t n = 8;
        while (n < maxDelay + 8) n <<= 1;
        buf.assign (n, 0.0f);
        mask = (unsigned) (n - 1);
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    // d >= 1: the sample written d samples before the one about to be written
    inline float read (double d) const
    {
        const unsigned i = (unsigned) d;
        const float f = (float) (d - (double) i);
        const float a = buf[(w - i) & mask], b = buf[(w - i - 1) & mask];
        return a + (b - a) * f;
    }
    inline float readInt (unsigned d) const { return buf[(w - d) & mask]; }
    inline void write (float x) { buf[w & mask] = x; ++w; }
};

// Schroeder all-pass (canonical form): y = -g*w + w[n-D], w = x + g*w[n-D]
struct AllPass
{
    Delay d;
    unsigned len = 1;
    float g = 0.5f;
    void init (unsigned length, float gain) { len = std::max (1u, length); g = gain; d.init (len + 2); }
    void clear() { d.clear(); }
    inline float process (float x)
    {
        const float old = d.readInt (len);
        const float w = flush (x + g * old);
        d.write (w);
        return old - g * w;
    }
};

// 8-line feedback delay network (Householder feedback matrix), damped, with slow delay modulation.
struct Reverb
{
    static constexpr int N = 8;
    Delay line[N];
    double base[N] {};                // nominal lengths in samples
    float damp[N] {};                 // low-pass state per line
    float gain[N] {};
    float lfo[N] {};                  // modulation phase 0..1
    float lfoInc[N] {};
    double modDepth = 3.0;            // samples
    float dampCoef = 0.5f;
    AllPass diff[4];
    float hpState = 0.0f, hpCoef = 0.01f;
    float lastDecay = -1.0f;
    double sr = 48000.0;
    double decayComp = 1.17;          // the damping makes the measured decay shorter than the gains alone give; calibrated in tests/fx_test

    void init (double sampleRate)
    {
        sr = sampleRate;
        decayComp = 1.17;
        const double s = sampleRate / 48000.0;
        static const int lens[N] = { 1087, 1283, 1427, 1559, 1693, 1873, 2017, 2203 };
        static const double rates[N] = { 0.23, 0.31, 0.41, 0.29, 0.53, 0.37, 0.61, 0.47 };   // modulation Hz
        modDepth = 3.0 * s;
        for (int i = 0; i < N; ++i)
        {
            base[i] = std::max (64.0, std::floor (lens[i] * s + 0.5));
            line[i].init ((size_t) (base[i] + modDepth + 8.0));
            lfo[i] = (float) i / (float) N;
            lfoInc[i] = (float) (rates[i] / sampleRate);
        }
        static const int dl[4] = { 113, 337, 191, 59 };
        for (int i = 0; i < 4; ++i) diff[i].init ((unsigned) std::max (8.0, std::floor (dl[i] * s + 0.5)), 0.6f);
        dampCoef = 1.0f - (float) std::exp (-2.0 * kPi * 5000.0 / sampleRate);
        hpCoef = onePoleCoef (120.0, sampleRate);
        clear();
    }

    void clear()
    {
        for (int i = 0; i < N; ++i) { line[i].clear(); damp[i] = 0.0f; }
        for (auto& a : diff) a.clear();
        hpState = 0.0f;
        lastDecay = -1.0f;
    }

    void setDecay (float seconds)
    {
        if (seconds == lastDecay) return;
        lastDecay = seconds;
        for (int i = 0; i < N; ++i)
            gain[i] = (float) std::pow (10.0, -3.0 * base[i] / (sr * (double) seconds * decayComp));
    }

    // One sample in, stereo out.
    inline void process (float in, float& outL, float& outR)
    {
        // low cut, then diffusion
        hpState += hpCoef * (in - hpState);
        float x = flush (in - hpState);
        for (auto& a : diff) x = a.process (x);

        float y[N], yd[N];
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            lfo[i] += lfoInc[i];
            if (lfo[i] >= 1.0f) lfo[i] -= 1.0f;
            const double d = base[i] + modDepth * std::sin (2.0 * kPi * (double) lfo[i]);
            const float v = line[i].read (d);
            damp[i] = flush (damp[i] + dampCoef * (v - damp[i]));
            y[i] = damp[i];
            yd[i] = damp[i] * gain[i];
            sum += yd[i];
        }
        static const float inj[N] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        const float half = sum * (2.0f / (float) N);
        for (int i = 0; i < N; ++i)
            line[i].write (flush (yd[i] - half + inj[i] * 0.3536f * x));
        // decorrelated taps: even lines left, odd lines right
        outL = 0.5f * (y[0] - y[2] + y[4] - y[6]);
        outR = 0.5f * (y[1] - y[3] + y[5] - y[7]);
    }
};

} // namespace

const char* echoTimeName (int index) { return index >= 0 && index < kNumEchoTimes ? kEchoNames[index] : "?"; }
double echoTimeBeats (int index) { return kEchoBeats[std::min (kNumEchoTimes - 1, std::max (0, index))]; }

struct DubFx::Impl
{
    double sr = 48000.0;
    bool first = true;

    // echo
    Delay dl, dr;
    double delayCur = 0.0;
    float hpL = 0, hpR = 0, lpL = 0, lpR = 0;
    float hpCoef = 0.01f;
    float slew = 0.0005f;

    Reverb rev;

    // stereo spread of the dry sound (width above 100 %)
    AllPass spreadL[3], spreadR[3];

    // smoothed parameters
    float sEchoMix = 0, sEchoFb = 0, sEchoTone = 0, sRevMix = 0, sRevDecay = 2.5f, sWidth = 1;
    float smooth = 0.002f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const size_t maxDelay = (size_t) (kMaxEchoSeconds * sampleRate) + 16;
        dl.init (maxDelay);
        dr.init (maxDelay);
        hpCoef = onePoleCoef (150.0, sampleRate);
        slew = (float) (1.0 / (0.04 * sampleRate));        // the delay time glides in about 40 ms (tape-like)
        smooth = (float) (1.0 / (0.01 * sampleRate));
        rev.init (sampleRate);
        const double ms = sampleRate / 1000.0;
        static const double l[3] = { 2.3, 5.9, 11.3 }, r[3] = { 3.1, 7.9, 13.7 };
        for (int i = 0; i < 3; ++i)
        {
            spreadL[i].init ((unsigned) std::max (4.0, std::floor (l[i] * ms + 0.5)), 0.45f);
            spreadR[i].init ((unsigned) std::max (4.0, std::floor (r[i] * ms + 0.5)), 0.45f);
        }
        reset();
    }

    void reset()
    {
        dl.clear(); dr.clear();
        hpL = hpR = lpL = lpR = 0.0f;
        delayCur = 0.0;
        rev.clear();
        for (auto& a : spreadL) a.clear();
        for (auto& a : spreadR) a.clear();
        first = true;
    }

    void run (const float* dry, float* outL, float* outR, int n, const FxParams& p)
    {
        const float tMix = std::min (1.0f, std::max (0.0f, p.echoMix));
        const float tFb = std::min (0.95f, std::max (0.0f, p.echoFeedback));
        const float tTone = std::min (1.0f, std::max (0.0f, p.echoTone));
        const float tRev = std::min (1.0f, std::max (0.0f, p.reverbMix));
        const float tDecay = std::min (12.0f, std::max (0.3f, p.reverbDecay));
        const float tWidth = std::min (2.0f, std::max (0.0f, p.width));
        const double bpm = std::max (kSlowestBpm, std::isfinite (p.bpm) ? p.bpm : 120.0);
        const double targetDelay = std::min ((double) (dl.buf.size() - 8),
                                             std::max (32.0, echoTimeBeats (p.echoTime) * 60.0 / bpm * sr));
        if (first)
        {
            sEchoMix = tMix; sEchoFb = tFb; sEchoTone = tTone; sRevMix = tRev; sRevDecay = tDecay; sWidth = tWidth;
            delayCur = targetDelay;
            first = false;
        }
        const float sm = smooth;
        for (int i = 0; i < n; ++i)
        {
            const float in = std::isfinite (dry[i]) ? dry[i] : 0.0f;
            sEchoMix += (tMix - sEchoMix) * sm;
            sEchoFb += (tFb - sEchoFb) * sm;
            sEchoTone += (tTone - sEchoTone) * sm;
            sRevMix += (tRev - sRevMix) * sm;
            sRevDecay += (tDecay - sRevDecay) * sm;
            sWidth += (tWidth - sWidth) * sm;
            delayCur += (targetDelay - delayCur) * (double) slew;

            // ---- echo (ping-pong: the input goes to the left line, the repeats bounce between the two) ----
            const float eL = dl.read (delayCur), eR = dr.read (delayCur);
            const double toneHz = 800.0 * std::pow (11.25, (double) sEchoTone);       // 800 Hz .. 9 kHz
            const float lpc = onePoleCoef (toneHz, sr);
            hpL += hpCoef * (eR - hpL); hpR += hpCoef * (eL - hpR);
            const float fL = eR - hpL, fR = eL - hpR;                                 // high-passed cross feedback
            lpL = flush (lpL + lpc * (fL - lpL)); lpR = flush (lpR + lpc * (fR - lpR));
            dl.write (flush (in + softClip (lpL * sEchoFb)));
            dr.write (flush (softClip (lpR * sEchoFb)));
            const float oL = eL * sEchoMix, oR = eR * sEchoMix;

            // ---- reverb (fed by the dry sound and the echoes) ----
            if (rev.lastDecay < 0.0f || std::fabs (sRevDecay - rev.lastDecay) > 0.002f * rev.lastDecay) rev.setDecay (sRevDecay);
            float rL, rR;
            rev.process (0.5f * (in + 0.5f * (oL + oR)), rL, rR);
            const float rg = sRevMix * 4.5f;                                          // calibrated in tests/fx_test
            float L = in + oL + rg * rL;
            float R = in + oR + rg * rR;

            // ---- width: mid/side; above 100 % the dry sound also gets a decorrelated spread ----
            float a = in, b = in;
            for (auto& ap : spreadL) a = ap.process (a);
            for (auto& ap : spreadR) b = ap.process (b);
            const float M = 0.5f * (L + R);
            float S = 0.5f * (L - R) * sWidth;
            const float spread = std::min (1.0f, std::max (0.0f, sWidth - 1.0f));
            S += 0.5f * (a - b) * spread * 0.8f;
            outL[i] = M + S;
            outR[i] = M - S;
        }
    }
};

DubFx::DubFx() : impl_ (new Impl()) {}
DubFx::~DubFx() = default;
void DubFx::prepare (double sampleRate, int) { impl_->prepare (sampleRate); }
void DubFx::reset() { impl_->reset(); }
void DubFx::process (const float* dry, float* outL, float* outR, int n, const FxParams& p) { impl_->run (dry, outL, outR, n, p); }
double DubFx::maxTailSeconds() { return 20.0; }

} // namespace dub
