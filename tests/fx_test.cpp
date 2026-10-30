// Tests of the built-in effects (src/core/Effects): echo timing and ping-pong, feedback stability, reverb decay time and level,
// width (mono, unity, extra wide), dry level, buffer-size independence, tempo changes, hostile input, reset.
// JUCE-free.  Run: build/fx_test
#include "core/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace dub;

static int failures = 0;
static long long checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; if (failures <= 40) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } } while (0)

struct Out { std::vector<float> l, r; };

static Out run (DubFx& fx, const std::vector<float>& in, const FxParams& p, int block)
{
    Out o; o.l.resize (in.size()); o.r.resize (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        fx.process (in.data() + pos, o.l.data() + pos, o.r.data() + pos, n, p);
    }
    return o;
}

static FxParams dry()
{
    FxParams p;
    p.echoMix = 0; p.reverbMix = 0; p.echoFeedback = 0; p.width = 1; p.bpm = 120;
    return p;
}

static std::vector<float> impulse (size_t n) { std::vector<float> v (n, 0.0f); v[0] = 1.0f; return v; }

static uint32_t rngState = 12345;
static float noise() { rngState = rngState * 1664525u + 1013904223u; return (float) ((rngState >> 8) & 0xffff) / 32768.0f - 1.0f; }

static size_t argmax (const std::vector<float>& v, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min (to, v.size()); ++i) if (std::fabs (v[i]) > std::fabs (v[best])) best = i;
    return best;
}

static double energy (const std::vector<float>& v, size_t from, size_t to)
{
    double e = 0;
    for (size_t i = from; i < std::min (to, v.size()); ++i) e += (double) v[i] * v[i];
    return e;
}

static bool finiteAndBounded (const Out& o, double limit, double* peakOut = nullptr)
{
    double peak = 0;
    bool ok = true;
    for (size_t i = 0; i < o.l.size(); ++i)
    {
        if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) ok = false;
        peak = std::max (peak, (double) std::max (std::fabs (o.l[i]), std::fabs (o.r[i])));
    }
    if (peakOut) *peakOut = peak;
    return ok && peak <= limit;
}

// Time for the reverb tail to fall by 60 dB, from the -5 .. -35 dB part of the backward-integrated decay (Schroeder).
static double rt60 (const Out& o, double sr)
{
    std::vector<double> e (o.l.size());
    for (size_t i = 0; i < e.size(); ++i) e[i] = (double) o.l[i] * o.l[i] + (double) o.r[i] * o.r[i];
    for (size_t i = e.size() - 1; i-- > 0;) e[i] += e[i + 1];
    const double e0 = e[0];
    double t1 = -1, t2 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (1e-30, e[i] / e0));
        if (t1 < 0 && db <= -5.0) t1 = (double) i / sr;
        if (t2 < 0 && db <= -35.0) { t2 = (double) i / sr; break; }
    }
    return t1 < 0 || t2 < 0 ? -1.0 : 2.0 * (t2 - t1);
}

static void testEchoTiming()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double bpm : { 90.0, 120.0, 140.0 })
            for (int t = 0; t < kNumEchoTimes; ++t)
            {
                DubFx fx; fx.prepare (sr, 512);
                FxParams p = dry(); p.echoMix = 1; p.echoTime = t; p.bpm = bpm;
                const size_t total = (size_t) (3.0 * sr);
                const auto o = run (fx, impulse (total), p, 480);
                const double want = echoTimeBeats (t) * 60.0 / bpm * sr;
                const size_t at = argmax (o.l, 5, total);
                CHECK (std::fabs ((double) at - want) <= 1.0, "echo %s at %.0f BPM, %.0f Hz: first repeat at %zu, wanted %.2f", echoTimeName (t), bpm, sr, at, want);
                CHECK (std::fabs (o.l[at]) > 0.25, "echo %s: repeat is too quiet (%.3f)", echoTimeName (t), o.l[at]);
                CHECK (energy (o.r, 5, (size_t) want - 2) < 1e-9, "echo %s: right side is not silent before the second repeat", echoTimeName (t));
                CHECK (std::fabs (o.l[0] - 1.0f) < 1e-6, "echo: the dry impulse is not at full level (%.4f)", o.l[0]);
            }
}

static void testPingPong()
{
    const double sr = 48000;
    DubFx fx; fx.prepare (sr, 512);
    FxParams p = dry(); p.echoMix = 1; p.echoTime = 2; p.echoFeedback = 0.6f; p.echoTone = 1.0f; p.bpm = 120;   // 1/8 = 0.25 s
    const size_t D = 12000;
    const auto o = run (fx, impulse ((size_t) (4 * sr)), p, 512);
    const double eL1 = energy (o.l, D - 40, D + 400), eR1 = energy (o.r, D - 40, D + 400);
    const double eL2 = energy (o.l, 2 * D - 40, 2 * D + 400), eR2 = energy (o.r, 2 * D - 40, 2 * D + 400);
    const double eL3 = energy (o.l, 3 * D - 40, 3 * D + 400), eR3 = energy (o.r, 3 * D - 40, 3 * D + 400);
    CHECK (eL1 > 0.1 && eR1 < 1e-9, "ping-pong: first repeat should be left only (L %.4g, R %.4g)", eL1, eR1);
    CHECK (eR2 > 1e-4 && eL2 < eR2 * 0.01, "ping-pong: second repeat should be right only (L %.4g, R %.4g)", eL2, eR2);
    CHECK (eL3 > 1e-5 && eR3 < eL3 * 0.01, "ping-pong: third repeat should be left only (L %.4g, R %.4g)", eL3, eR3);
    CHECK (eR2 < eL1 && eL3 < eR2, "ping-pong: repeats should get quieter (%.4g %.4g %.4g)", eL1, eR2, eL3);
}

static void testFeedbackStability()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        DubFx fx; fx.prepare (sr, 512);
        FxParams p; p.echoMix = 1; p.echoFeedback = 0.95f; p.echoTime = 0; p.reverbMix = 1; p.reverbDecay = 12; p.width = 2; p.echoTone = 1;
        std::vector<float> in ((size_t) (20 * sr));
        for (auto& v : in) v = noise();
        const auto o = run (fx, in, p, 512);
        double peak = 0;
        const bool ok = finiteAndBounded (o, 30.0, &peak);
        CHECK (ok, "worst case at %.0f Hz: not finite or too loud (peak %.2f)", sr, peak);
        std::printf ("worst case (feedback 95 %%, reverb 12 s, width 200 %%, full-scale noise, %.0f Hz): peak %.2f\n", sr, peak);
    }
}

static void testTailsDie()
{
    const double sr = 48000;
    DubFx fx; fx.prepare (sr, 512);
    FxParams p;      // the defaults
    const auto o = run (fx, impulse ((size_t) (40 * sr)), p, 512);
    const double tail = std::max (energy (o.l, (size_t) (30 * sr), o.l.size()), energy (o.r, (size_t) (30 * sr), o.r.size()));
    CHECK (tail < 1e-10, "defaults: still sounding after 30 s (energy %.3g)", tail);
    // exact silence in -> exact silence out, forever
    DubFx fx2; fx2.prepare (sr, 512);
    const auto z = run (fx2, std::vector<float> ((size_t) (5 * sr), 0.0f), p, 512);
    CHECK (energy (z.l, 0, z.l.size()) == 0.0 && energy (z.r, 0, z.r.size()) == 0.0, "silence in must give silence out");
}

static void testReverb()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float decay : { 1.0f, 2.5f, 6.0f })
        {
            DubFx fx; fx.prepare (sr, 512);
            FxParams p = dry(); p.reverbMix = 1; p.reverbDecay = decay;
            auto o = run (fx, impulse ((size_t) ((decay * 2.5 + 2) * sr)), p, 512);
            o.l[0] = o.r[0] = 0.0f;                                  // the dry impulse is not part of the tail
            const double t = rt60 (o, sr);
            std::printf ("reverb decay %.1f s at %.0f Hz: measured %.2f s (%.0f %% of the setting)\n", decay, sr, t, 100.0 * t / decay);
            CHECK (t > 0.45 * decay && t < 1.3 * decay, "reverb decay %.1f s at %.0f Hz measured %.2f s", decay, sr, t);
            CHECK (finiteAndBounded (o, 5.0), "reverb: not finite or too loud");
        }
    // level: stationary noise through the reverb alone comes out at a similar loudness
    for (float decay : { 0.5f, 2.5f, 8.0f })
    {
        DubFx fx; fx.prepare (48000, 512);
        FxParams p = dry(); p.reverbMix = 1; p.reverbDecay = decay;
        std::vector<float> in (48000 * 12);
        for (auto& v : in) v = 0.3f * noise();
        const auto o = run (fx, in, p, 512);
        const size_t from = 48000 * 6;
        const double rin = std::sqrt (energy (in, from, in.size()) / (double) (in.size() - from));
        const double rout = std::sqrt (0.5 * (energy (o.l, from, o.l.size()) + energy (o.r, from, o.r.size())) / (double) (o.l.size() - from)) ;
        const double wet = std::sqrt (std::max (0.0, rout * rout - rin * rin));
        std::printf ("reverb level, decay %.1f s: dry RMS %.3f, total RMS %.3f, wet RMS %.3f (%.2f x the input)\n", decay, rin, rout, wet, wet / rin);
        CHECK (wet / rin > 0.4 && wet / rin < 2.0, "reverb level at decay %.1f s: wet is %.2f x the input", decay, wet / rin);
    }
}

static void testWidth()
{
    const double sr = 48000;
    std::vector<float> in ((size_t) (2 * sr));
    for (auto& v : in) v = 0.4f * noise();

    {   // everything off, width 100 %: the output is the input, exactly
        DubFx fx; fx.prepare (sr, 512);
        const auto o = run (fx, in, dry(), 512);
        double d = 0;
        for (size_t i = 0; i < in.size(); ++i) d = std::max (d, (double) std::max (std::fabs (o.l[i] - in[i]), std::fabs (o.r[i] - in[i])));
        CHECK (d < 1e-6, "dry at width 100 %% is not the input (max difference %.3g)", d);
    }
    {   // width 0: mono, with echo and reverb on
        DubFx fx; fx.prepare (sr, 512);
        FxParams p; p.width = 0;
        const auto o = run (fx, in, p, 512);
        double d = 0;
        for (size_t i = (size_t) (0.5 * sr); i < in.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - o.r[i]));
        CHECK (d < 1e-5, "width 0 %% is not mono (max L-R %.3g)", d);
        CHECK (energy (o.l, 0, o.l.size()) > 1.0, "width 0 %%: no sound");
    }
    {   // width 200 %: mid unchanged, sides added
        DubFx fx; fx.prepare (sr, 512);
        FxParams p = dry(); p.width = 2;
        const auto o = run (fx, in, p, 512);
        double dmid = 0, side = 0;
        for (size_t i = (size_t) (0.5 * sr); i < in.size(); ++i)
        {
            dmid = std::max (dmid, (double) std::fabs (0.5f * (o.l[i] + o.r[i]) - in[i]));
            side += (double) (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
        }
        CHECK (dmid < 1e-5, "width 200 %%: the mid (L+R) changed (%.3g)", dmid);
        CHECK (side > 1.0, "width 200 %%: the dry sound got no stereo spread (side energy %.3g)", side);
        std::printf ("width 200 %%, dry noise: side/mid energy ratio %.2f\n", side / (4.0 * energy (in, (size_t) (0.5 * sr), in.size())));
    }
    {   // width 150 %: between
        DubFx fx; fx.prepare (sr, 512);
        FxParams p = dry(); p.width = 1.5f;
        const auto o = run (fx, in, p, 512);
        double side = 0;
        for (size_t i = (size_t) (0.5 * sr); i < in.size(); ++i) side += (double) (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
        FxParams p2 = dry(); p2.width = 2;
        DubFx fx2; fx2.prepare (sr, 512);
        const auto o2 = run (fx2, in, p2, 512);
        double side2 = 0;
        for (size_t i = (size_t) (0.5 * sr); i < in.size(); ++i) side2 += (double) (o2.l[i] - o2.r[i]) * (o2.l[i] - o2.r[i]);
        CHECK (side > 0 && side < side2, "width 150 %% should be narrower than 200 %%");
    }
}

static void testBlockSizes()
{
    const double sr = 48000;
    std::vector<float> in ((size_t) (3 * sr));
    for (size_t i = 0; i < in.size(); ++i) in[i] = (i % 24000 < 2000) ? 0.5f * noise() : 0.0f;
    FxParams p; p.width = 1.6f;
    DubFx ref; ref.prepare (sr, 512);
    const auto a = run (ref, in, p, 512);
    for (int block : { 1, 7, 64, 480, 1000, 4096 })
    {
        DubFx fx; fx.prepare (sr, 512);
        const auto b = run (fx, in, p, block);
        const bool same = a.l == b.l && a.r == b.r;
        CHECK (same, "output differs between 512 and %d sample buffers", block);
    }
}

static void testTempoChange()
{
    const double sr = 48000;
    DubFx fx; fx.prepare (sr, 512);
    FxParams p = dry(); p.echoMix = 1; p.echoTime = 4; p.bpm = 120;       // 1/4 = 0.5 s
    run (fx, std::vector<float> ((size_t) sr, 0.0f), p, 512);
    p.bpm = 60;                                                            // now 1 s
    run (fx, std::vector<float> ((size_t) (0.5 * sr), 0.0f), p, 512);     // let the delay time glide
    const auto o = run (fx, impulse ((size_t) (3 * sr)), p, 512);
    const size_t at = argmax (o.l, 5, o.l.size());
    CHECK (std::fabs ((double) at - sr) <= 2.0, "after a tempo change to 60 BPM the 1/4 echo is at %zu samples, wanted %.0f", at, sr);
}

static void testHostile()
{
    const double sr = 48000;
    {
        DubFx fx; fx.prepare (sr, 512);
        FxParams p; p.echoMix = 5; p.echoFeedback = 9; p.echoTone = -3; p.echoTime = 99; p.reverbMix = 7; p.reverbDecay = 0; p.width = 9; p.bpm = 0;
        std::vector<float> in ((size_t) (6 * sr));
        for (auto& v : in) v = noise();
        const auto o = run (fx, in, p, 512);
        double peak = 0;
        CHECK (finiteAndBounded (o, 60.0, &peak), "hostile parameters: not finite or too loud (peak %.2f)", peak);
    }
    {
        DubFx fx; fx.prepare (sr, 512);
        FxParams p; p.bpm = std::nan ("");
        std::vector<float> in ((size_t) (6 * sr));
        for (auto& v : in) v = noise();
        in[1000] = std::nanf (""); in[2000] = INFINITY; in[3000] = -INFINITY;
        const auto o = run (fx, in, p, 512);
        CHECK (finiteAndBounded (o, 60.0), "NaN / infinity in the input poisoned the effects");
    }
    {   // reset clears the tails
        DubFx fx; fx.prepare (sr, 512);
        FxParams p;
        run (fx, impulse ((size_t) sr), p, 512);
        fx.reset();
        const auto z = run (fx, std::vector<float> ((size_t) sr, 0.0f), p, 512);
        CHECK (energy (z.l, 0, z.l.size()) == 0.0 && energy (z.r, 0, z.r.size()) == 0.0, "reset left a tail behind");
    }
    {   // a changing reverb time and mix while sound plays stays clean (no blow-up)
        DubFx fx; fx.prepare (sr, 512);
        FxParams p;
        Out all;
        for (int k = 0; k < 40; ++k)
        {
            p.reverbDecay = 0.3f + (float) (k % 7) * 1.7f; p.echoMix = (float) (k % 5) / 4.0f; p.reverbMix = (float) (k % 3) / 2.0f;
            p.width = (float) (k % 4) * 0.6f; p.echoFeedback = (float) (k % 6) * 0.18f; p.echoTime = k % kNumEchoTimes; p.bpm = 80 + 7 * k;
            std::vector<float> in (4800);
            for (auto& v : in) v = 0.5f * noise();
            const auto o = run (fx, in, p, 480);
            all.l.insert (all.l.end(), o.l.begin(), o.l.end()); all.r.insert (all.r.end(), o.r.begin(), o.r.end());
        }
        double peak = 0;
        CHECK (finiteAndBounded (all, 30.0, &peak), "changing parameters: not finite or too loud (peak %.2f)", peak);
    }
}

int main()
{
    testEchoTiming();
    testPingPong();
    testFeedbackStability();
    testTailsDie();
    testReverb();
    testWidth();
    testBlockSizes();
    testTempoChange();
    testHostile();
    if (failures == 0) std::printf ("fx_test: all %lld checks passed\n", checks);
    else std::printf ("fx_test: %d of %lld checks FAILED\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
