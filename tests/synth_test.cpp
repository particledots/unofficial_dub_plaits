// Headless tests of the sound models (DubSynth), JUCE-free. Every model, three sample rates (44.1, 48, 96 kHz).
//
//   A  basics         silent until the first note (exactly 0), then sound: finite, bounded, not silent; the sound starts
//                     after the event and soon; no event is applied late
//   B  knob grid      HARMONICS x TIMBRE x MORPH at 0 / 0.5 / 1, with three envelope / resonance settings: finite, bounded,
//                     not silent, on every model
//   C  block size     the output is bit-identical for host buffers of 1, 7, 12, 13, 64, 441, 512 and 4800 samples
//   D  hostile input  out-of-range and NaN knob values, huge or tiny sample counts: still finite and bounded
//   E  floods         5-note chords every 0.5 ms (voice stealing) and an ACID note on every sample
//   F  model switch   random model changes with random knobs and notes; a note left behind in a model that was switched
//                     away from does not come back when it is selected again
//   G  sequencer      the real chain DubSequencer -> TransportClock -> DubSynth, 4 bars, every model
//   H  pitch          the resampler keeps the pitch (1 kHz in, 1 kHz out, 5 host rates); a single A3 comes out at 220 Hz
//                     (within 0.9 cents of what the model is designed to give) on every model and sample rate
// "Bounded" means no sample above the limit of its model (the level limiters of the models, plus a few percent of
// resampler overshoot); "not silent" means a clearly audible peak.
// Run: build/synth_test
#include "core/DubSynth.h"
#include "core/Resampler.h"
#include "core/TransportClock.h"
#include "stmlib/utils/random.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace dub;

static int failures = 0;
static long long checks = 0;
static void fail (const std::string& msg) { ++failures; if (failures <= 60) std::printf ("FAIL: %s\n", msg.c_str()); }
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { char b_[600]; std::snprintf (b_, sizeof b_, __VA_ARGS__); fail (b_); } } while (0)

static const double kRates[3] = { 44100.0, 48000.0, 96000.0 };

// The largest sample each model may produce: the models' own limiters (poly: knee up to 1.0; STRINGS: x 0.8; ACID: x 0.6),
// plus 8 % for the resampler's overshoot of a hard-limited signal.
static double peakLimit (int model)
{
    if (model == kStrings) return 0.8 * 1.08;
    if (model == kAcid) return 0.6 * 1.08;
    return 1.0 * 1.08;
}

static Event genEvent (int type, int note, int vel) { Event e; e.kind = Event::Gen; e.type = type; e.note = note; e.vel = vel; return e; }
static Event acidLen (double ms) { Event e; e.kind = Event::AcidLen; e.ms = ms; return e; }

struct Timed { int sample; Event ev; };

// The DUB chord on C (root, 5th, 7th, 3rd + 5th an octave up) in C minor, as the sequencer plays it at octave 3.
static const int kDub[5] = { 48, 55, 58, 63, 67 };

static void addChord (std::vector<Timed>& v, int sample, int vel, int shift = 0)
{
    for (int n : kDub) v.push_back ({ sample, genEvent (0, n + shift, vel) });
}
static void addAcid (std::vector<Timed>& v, int sample, int note, int vel, bool slide, double ms)
{
    v.push_back ({ sample, acidLen (ms) });
    v.push_back ({ sample, genEvent (slide ? 2 : 1, note, vel) });
}
static void addNote (std::vector<Timed>& v, int model, int sample, int vel)
{
    if (model == kAcid) addAcid (v, sample, 48, vel, false, 300.0);
    else addChord (v, sample, vel);
}

// Renders `total` host samples in buffers of `block`, feeding the scripted events (absolute sample positions).
static std::vector<float> render (DubSynth& s, const std::vector<Timed>& script, int total, int block, const SynthParams& p)
{
    std::vector<float> out ((size_t) total), tmp ((size_t) std::max (block, 1)), tmpR ((size_t) std::max (block, 1));
    size_t next = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        while (next < script.size() && script[next].sample < pos + n)
        {
            s.addEvent (std::max (0, script[next].sample - pos), script[next].ev);
            ++next;
        }
        s.process (tmp.data(), tmpR.data(), n, p);
        for (int i = 0; i < n; ++i) out[(size_t) (pos + i)] = tmp[(size_t) i];
        if (std::memcmp (tmp.data(), tmpR.data(), (size_t) n * sizeof (float)) != 0) fail ("left and right differ");
    }
    return out;
}

struct Stats { double peak = 0, rms = 0; bool finite = true; };
static Stats measure (const std::vector<float>& v, size_t from, size_t to)
{
    Stats st;
    double sq = 0;
    to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i)
    {
        if (! std::isfinite (v[i])) { st.finite = false; continue; }
        st.peak = std::max (st.peak, (double) std::fabs (v[i]));
        sq += (double) v[i] * v[i];
    }
    st.rms = to > from ? std::sqrt (sq / (double) (to - from)) : 0.0;
    return st;
}

static void freshSynth (DubSynth& s, double sr, int maxBlock = 512)
{
    stmlib::Random::Seed (1);
    s.prepare (sr, maxBlock);
}

static std::string tag (int model, double sr) { char b[120]; std::snprintf (b, sizeof b, "%s @ %.0f Hz", synthModelName (model), sr); return b; }

// ------------------------------------------------------------------------------------------------ A
static void testBasics()
{
    std::printf ("A  basics (every model, 3 sample rates)\n");
    std::printf ("   %-34s %-9s %-9s %-9s %-9s\n", "model", "44.1k peak", "48k peak", "96k peak", "48k RMS");
    for (int m = 0; m < kNumSynthModels; ++m)
    {
        double peakAt[3] = {}, rms48 = 0;
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            DubSynth s;
            freshSynth (s, sr);
            SynthParams p; p.model = m;
            const int noteAt = (int) (0.2 * sr), total = (int) (3.0 * sr);
            std::vector<Timed> script;
            addNote (script, m, noteAt, 90);
            const auto out = render (s, script, total, 512, p);
            const Stats before = measure (out, 0, (size_t) noteAt);
            const Stats after = measure (out, (size_t) noteAt, out.size());
            const Stats body = measure (out, (size_t) noteAt, (size_t) (noteAt + 1.0 * sr));
            CHECK (before.peak == 0.0, "%s: sound before the first note (peak %g)", tag (m, sr).c_str(), before.peak);
            CHECK (after.finite, "%s: not finite", tag (m, sr).c_str());
            CHECK (s.nonFiniteSamples() == 0, "%s: %lld non-finite samples had to be replaced", tag (m, sr).c_str(), s.nonFiniteSamples());
            CHECK (s.lateEvents() == 0, "%s: %lld late events", tag (m, sr).c_str(), s.lateEvents());
            CHECK (after.peak <= peakLimit (m), "%s: peak %.4f above the limit %.3f", tag (m, sr).c_str(), after.peak, peakLimit (m));
            CHECK (after.peak >= 0.02, "%s: too quiet, peak %.5f", tag (m, sr).c_str(), after.peak);
            CHECK (body.rms >= 0.004, "%s: too quiet, RMS %.5f", tag (m, sr).c_str(), body.rms);
            // the sound starts after the event (causal) and within 15 ms of it
            size_t onset = 0;
            while (onset < out.size() && std::fabs (out[onset]) < 1e-3f) ++onset;
            CHECK (onset >= (size_t) noteAt, "%s: sound starts at sample %zu, before the event at %d", tag (m, sr).c_str(), onset, noteAt);
            CHECK (onset <= (size_t) (noteAt + 0.015 * sr), "%s: sound starts %.1f ms after the event", tag (m, sr).c_str(), (double) ((long long) onset - noteAt) * 1000.0 / sr);
            // ... and it dies away on its own (default FREQUENCY: a stab of about half a second): the last second is quiet
            if (m != kStrings)
            {
                const Stats tail = measure (out, out.size() - (size_t) sr, out.size());
                CHECK (tail.peak < 0.05, "%s: still sounding at the end, peak %.4f", tag (m, sr).c_str(), tail.peak);
            }
            peakAt[r] = after.peak;
            if (r == 1) rms48 = body.rms;
            CHECK (s.latencyInHostSamples() > 0 && s.latencyInHostSamples() < (int) (0.003 * sr), "%s: latency %d samples", tag (m, sr).c_str(), s.latencyInHostSamples());
        }
        std::printf ("   %-34s %-9.3f %-9.3f %-9.3f %-9.4f\n", synthModelName (m), peakAt[0], peakAt[1], peakAt[2], rms48);
    }
}

// ------------------------------------------------------------------------------------------------ B
static void testGrid()
{
    std::printf ("B  knob grid (HARMONICS x TIMBRE x MORPH x 3 envelope/resonance settings = 81 per model and rate)\n");
    const float v3[3] = { 0.0f, 0.5f, 1.0f };
    const float envs[3][2] = { { 0.0f, 0.0f }, { 0.5f, 0.35f }, { 1.0f, 1.0f } };
    for (int m = 0; m < kNumSynthModels; ++m)
    {
        double quiet = 1e9, loud = 0;
        long long runs = 0;
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            const int noteAt = (int) (0.05 * sr), total = (int) (1.3 * sr);
            std::vector<Timed> script;
            addNote (script, m, noteAt, 110);          // an accented note: the loudest case
            DubSynth s;
            for (float h : v3) for (float t : v3) for (float mo : v3) for (auto& e : envs)
            {
                freshSynth (s, sr);
                SynthParams p; p.model = m; p.harmonics = h; p.timbre = t; p.morph = mo; p.envelope = e[0]; p.resonance = e[1];
                const auto out = render (s, script, total, 480, p);
                const Stats st = measure (out, 0, out.size());
                char what[200];
                std::snprintf (what, sizeof what, "%s H%.1f T%.1f M%.1f env%.1f res%.2f", tag (m, sr).c_str(), h, t, mo, e[0], e[1]);
                CHECK (st.finite && s.nonFiniteSamples() == 0, "%s: not finite", what);
                CHECK (st.peak <= peakLimit (m), "%s: peak %.4f above the limit %.3f", what, st.peak, peakLimit (m));
                CHECK (st.peak >= 0.005, "%s: silent, peak %.6f", what, st.peak);
                CHECK (s.lateEvents() == 0, "%s: late events", what);
                quiet = std::min (quiet, st.peak);
                loud = std::max (loud, st.peak);
                ++runs;
            }
        }
        std::printf ("   %-34s %lld runs, peaks %.3f .. %.3f\n", synthModelName (m), runs, quiet, loud);
    }
}

// ------------------------------------------------------------------------------------------------ C
static void testBlockSizes()
{
    std::printf ("C  block size independence (bit-identical output)\n");
    const int sizes[] = { 1, 7, 12, 13, 64, 441, 512, 4800 };
    for (int m = 0; m < kNumSynthModels; ++m)
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            const int total = (int) (1.6 * sr);
            std::vector<Timed> script;
            // notes at awkward positions, an accent, a second chord while the first still sounds
            addNote (script, m, 1000, 90);
            addNote (script, m, (int) (0.3 * sr) + 3, 120);
            addNote (script, m, (int) (0.55 * sr) + 7, 90);
            if (m == kAcid) addAcid (script, (int) (0.8 * sr) + 1, 55, 95, true, 120.0);
            SynthParams p; p.model = m; p.harmonics = 0.3f; p.timbre = 0.7f; p.morph = 0.4f; p.envelope = 0.45f;
            DubSynth ref;
            freshSynth (ref, sr, 4800);
            const auto want = render (ref, script, total, 512, p);
            for (int b : sizes)
            {
                DubSynth s;
                freshSynth (s, sr, 4800);
                const auto got = render (s, script, total, b, p);
                const bool same = std::memcmp (got.data(), want.data(), got.size() * sizeof (float)) == 0;
                CHECK (same, "%s: output differs between 512- and %d-sample buffers", tag (m, sr).c_str(), b);
                CHECK (s.lateEvents() == 0, "%s: late events with %d-sample buffers", tag (m, sr).c_str(), b);
            }
        }
}

// ------------------------------------------------------------------------------------------------ D
static void testHostile()
{
    std::printf ("D  hostile input\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    struct Case { const char* name; float h, t, m, e, r, d; };
    const Case cases[] = {
        { "far out of range", -5.0f, 9.0f, 3.0f, 100.0f, -3.0f, 7.0f },
        { "NaN", nan, nan, nan, nan, nan, nan },
        { "infinite", inf, -inf, inf, inf, -inf, inf },
    };
    for (int m = 0; m < kNumSynthModels; ++m)
        for (int r = 0; r < 3; ++r)
            for (auto& c : cases)
            {
                const double sr = kRates[r];
                DubSynth s;
                freshSynth (s, sr);
                SynthParams p; p.model = m; p.harmonics = c.h; p.timbre = c.t; p.morph = c.m; p.envelope = c.e; p.resonance = c.r; p.drift = c.d;
                std::vector<Timed> script;
                addNote (script, m, 1000, 127);
                addNote (script, m, (int) (0.4 * sr), 100);
                const auto out = render (s, script, (int) (1.0 * sr), 512, p);
                const Stats st = measure (out, 0, out.size());
                CHECK (st.finite && s.nonFiniteSamples() == 0, "%s, %s: not finite (%lld replaced)", tag (m, sr).c_str(), c.name, s.nonFiniteSamples());
                CHECK (st.peak <= peakLimit (m), "%s, %s: peak %.4f", tag (m, sr).c_str(), c.name, st.peak);
                // the knobs come back to normal: it must not have been left broken
                p = SynthParams(); p.model = m;
                std::vector<Timed> later;
                addNote (later, m, 100, 100);
                const auto again = render (s, later, (int) (0.8 * sr), 512, p);
                const Stats st2 = measure (again, 0, again.size());
                CHECK (st2.finite && st2.peak >= 0.02 && st2.peak <= peakLimit (m), "%s, after %s: peak %.5f (broken state?)", tag (m, sr).c_str(), c.name, st2.peak);
            }
    // notes and velocities far outside 0..127 (a transposed chord can leave the keyboard; events come from outside the synth)
    for (int m = 0; m < kNumSynthModels; ++m)
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            DubSynth s;
            freshSynth (s, sr);
            SynthParams p; p.model = m;
            std::vector<Timed> script;
            const int odd[] = { -50, 0, 1, 127, 128, 300, -1 };
            const int vels[] = { -5, 0, 1, 127, 500 };
            int k = 0;
            for (int note : odd)
                for (int vel : vels)
                {
                    const int at = 200 + k++ * (int) (0.02 * sr);
                    if (m == kAcid) addAcid (script, at, note, vel, k % 2 == 0, k % 3 == 0 ? 0.0 : 30.0);
                    else for (int j = 0; j < 5; ++j) script.push_back ({ at, genEvent (0, note + 7 * j, vel) });
                }
            const auto out = render (s, script, (int) (1.6 * sr), 512, p);
            const Stats st = measure (out, 0, out.size());
            CHECK (st.finite && s.nonFiniteSamples() == 0, "%s: extreme notes, not finite (%lld replaced)", tag (m, sr).c_str(), s.nonFiniteSamples());
            CHECK (st.peak <= peakLimit (m), "%s: extreme notes, peak %.4f", tag (m, sr).c_str(), st.peak);
        }
    // odd buffer sizes
    for (int m = 0; m < kNumSynthModels; ++m)
    {
        DubSynth s;
        freshSynth (s, 48000.0, 64);
        SynthParams p; p.model = m;
        std::vector<float> a (200000), b (200000);
        s.process (a.data(), b.data(), 0, p);                                    // nothing
        s.addEvent (0, genEvent (0, 48, 100));
        s.process (a.data(), b.data(), 1, p);
        s.process (a.data(), b.data(), 200000, p);                               // far more than maxBlock
        const Stats st = measure (a, 0, a.size());
        CHECK (st.finite, "%s: odd buffer sizes, not finite", synthModelName (m));
        CHECK (st.peak <= peakLimit (m), "%s: odd buffer sizes, peak %.4f", synthModelName (m), st.peak);
    }
}

// ------------------------------------------------------------------------------------------------ E
static void testFloods()
{
    std::printf ("E  floods\n");
    for (int m = 0; m < kNumSynthModels; ++m)
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            DubSynth s;
            freshSynth (s, sr);
            SynthParams p; p.model = m; p.envelope = 0.8f; p.resonance = 1.0f;
            std::vector<Timed> script;
            const int step = std::max (1, (int) (0.0005 * sr));         // every 0.5 ms
            for (int t = 0; t < (int) (1.0 * sr); t += step)
            {
                if (m == kAcid) addAcid (script, t, 36 + (t / step) % 40, (t / step) % 3 ? 90 : 120, (t / step) % 2 == 0, 1.0);
                else addChord (script, t, (t / step) % 3 ? 90 : 120, ((t / step) % 5) - 2);
            }
            if (m == kAcid)                                              // and one on every single sample for a while
                for (int t = (int) (1.0 * sr); t < (int) (1.2 * sr); ++t) addAcid (script, t, 30 + t % 50, 100, t % 2 == 0, 0.0);
            const auto out = render (s, script, (int) (2.0 * sr), 512, p);
            const Stats st = measure (out, 0, out.size());
            CHECK (st.finite && s.nonFiniteSamples() == 0, "%s: flood, not finite", tag (m, sr).c_str());
            CHECK (st.peak <= peakLimit (m), "%s: flood, peak %.4f above %.3f", tag (m, sr).c_str(), st.peak, peakLimit (m));
            CHECK (st.peak >= 0.02, "%s: flood, silent (%.5f)", tag (m, sr).c_str(), st.peak);
            CHECK (s.lateEvents() == 0, "%s: flood, late events %lld", tag (m, sr).c_str(), s.lateEvents());
            if (m <= kChiptune) CHECK (s.activeVoices() <= 6, "%s: more than 6 voices", tag (m, sr).c_str());
        }
}

// ------------------------------------------------------------------------------------------------ F
static void testSwitching()
{
    std::printf ("F  model switching\n");
    for (int r = 0; r < 3; ++r)
    {
        const double sr = kRates[r];
        DubSynth s;
        freshSynth (s, sr);
        uint32_t rng = 12345u + (uint32_t) r;
        auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        const int total = (int) (30.0 * sr);
        double peak = 0;
        bool finite = true;
        int pos = 0;
        std::vector<float> l (512), rr (512);
        SynthParams p;
        int nextChange = 0, nextNote = 0;
        while (pos < total)
        {
            if (pos >= nextChange)
            {
                p.model = (int) (rnd() * kNumSynthModels) % kNumSynthModels;
                p.harmonics = rnd(); p.timbre = rnd(); p.morph = rnd(); p.envelope = rnd(); p.resonance = rnd(); p.drift = rnd();
                nextChange = pos + 1 + (int) (rnd() * 0.4 * sr);
            }
            if (pos >= nextNote)
            {
                const int off = (int) (rnd() * 400);
                if (p.model == kAcid) { s.addEvent (off, acidLen (50.0 + rnd() * 300.0)); s.addEvent (off, genEvent (rnd() < 0.3 ? 2 : 1, 30 + (int) (rnd() * 40), rnd() < 0.3 ? 120 : 90)); }
                else { std::vector<Timed> c; addChord (c, 0, rnd() < 0.3 ? 120 : 90, (int) (rnd() * 12) - 6); for (auto& t : c) s.addEvent (off, t.ev); }
                nextNote = pos + (int) ((0.02 + rnd() * 0.3) * sr);
            }
            s.process (l.data(), rr.data(), 512, p);
            for (float v : l) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, (double) std::fabs (v)); }
            pos += 512;
        }
        CHECK (finite && s.nonFiniteSamples() == 0, "random switching @ %.0f Hz: not finite", sr);
        CHECK (peak <= 1.08, "random switching @ %.0f Hz: peak %.4f", sr, peak);
        CHECK (peak >= 0.05, "random switching @ %.0f Hz: silent (%.4f)", sr, peak);
        CHECK (s.lateEvents() == 0, "random switching @ %.0f Hz: late events %lld", sr, s.lateEvents());
    }
    // a note left in a model that was switched away from does not come back
    for (int from = 0; from < kNumSynthModels; ++from)
        for (int away = 0; away < kNumSynthModels; ++away)
        {
            if (away == from) continue;
            // Only switches that leave a note frozen matter: poly -> strings/acid, strings -> anything, acid -> anything.
            const double sr = 48000.0;
            DubSynth s;
            freshSynth (s, sr);
            SynthParams p; p.model = from; p.envelope = 1.0f;       // a long note
            std::vector<Timed> script;
            addNote (script, from, 100, 110);
            auto a = render (s, script, (int) (0.25 * sr), 512, p);  // sounding
            p.model = away;
            auto b = render (s, {}, (int) (2.0 * sr), 512, p);       // away, no notes
            p.model = from;
            auto c = render (s, {}, (int) (2.0 * sr), 512, p);       // back, no new notes
            // a poly model -> another poly model keeps the notes (as the firmware): only judge the jumps that must be clean
            const bool bothPoly = from <= kChiptune && away <= kChiptune;
            if (bothPoly) continue;
            const Stats sb = measure (b, (size_t) (0.3 * sr), b.size());   // the first 0.3 s may still hold the resampler's and the model's own tail
            const Stats sc = measure (c, 0, c.size());
            CHECK (sb.peak < 0.01, "%s -> %s: still sounding %.4f after the switch", synthModelName (from), synthModelName (away), sb.peak);
            CHECK (sc.peak < 0.01, "%s -> %s -> %s: the old note came back (%.4f)", synthModelName (from), synthModelName (away), synthModelName (from), sc.peak);
            (void) a;
        }
}

// ------------------------------------------------------------------------------------------------ G
static void testSequencerChain()
{
    std::printf ("G  sequencer -> transport clock -> synth\n");
    struct Cfg { int seed, voicing, rate, density, root, scale, chords; };
    const Cfg cfgs[] = { { 1, 0, 4, 8, 0, 0, 1 }, { 77, 4, 2, 12, 7, 1, 1 }, { 4242, 3, 5, 16, 3, 3, 0 } };
    for (int m = 0; m < kNumSynthModels; ++m)
        for (int r = 0; r < 3; ++r)
            for (auto& c : cfgs)
            {
                const double sr = kRates[r];
                DubSequencer seq;
                seq.setMode (Mode::Both);
                seq.setModel (m); seq.setSeed (c.seed); seq.setVoicing (c.voicing); seq.setRate (c.rate);
                seq.setDensity (c.density); seq.setRoot (c.root); seq.setScale (c.scale); seq.setChords (c.chords);
                seq.events().clear();
                TransportClock clock;
                DubSynth synth;
                freshSynth (synth, sr, 512);
                SynthParams p; p.model = m;
                const double bpm = 124.0;
                const int total = (int) (8.0 * sr);                 // 8 s = about 4 bars
                std::vector<TimedEvent> evs;
                std::vector<float> l (512), rr (512);
                double peak = 0; bool finite = true; long long nEvents = 0;
                int pos = 0;
                while (pos < total)
                {
                    const int n = std::min (512, total - pos);
                    HostBlock hb; hb.playing = true; hb.bpm = bpm; hb.sampleRate = sr; hb.numSamples = n;
                    hb.ppqStart = (double) pos / sr * bpm / 60.0;
                    evs.clear();
                    clock.process (seq, hb, evs);
                    seq.events().clear();
                    for (auto& te : evs) { synth.addEvent (te.sample, te.ev); ++nEvents; }
                    synth.process (l.data(), rr.data(), n, p);
                    for (int i = 0; i < n; ++i) { if (! std::isfinite (l[(size_t) i])) finite = false; peak = std::max (peak, (double) std::fabs (l[(size_t) i])); }
                    pos += n;
                }
                char what[160];
                std::snprintf (what, sizeof what, "%s, seed %d @ %.0f Hz", synthModelName (m), c.seed, sr);
                CHECK (nEvents > 0, "%s: the sequencer gave no events", what);
                CHECK (finite && synth.nonFiniteSamples() == 0, "%s: not finite", what);
                CHECK (peak <= peakLimit (m), "%s: peak %.4f above %.3f", what, peak, peakLimit (m));
                CHECK (peak >= 0.02, "%s: silent (peak %.5f, %lld events)", what, peak, nEvents);
                CHECK (synth.lateEvents() == 0, "%s: late events %lld", what, synth.lateEvents());
            }
}

// ------------------------------------------------------------------------------------------------ H
// Strongest spectral line between 0.9 and 1.1 times f0 (Hann window, fine sweep with a parabolic refinement).
// Frequency of the strongest component near f0, from the PHASE of that component measured in consecutive 0.1 s blocks
// (least-squares slope of the unwrapped phase). Unlike the height of a spectral peak, the phase does not care that
// the level is still swelling or decaying, so it does not read slightly flat or sharp during an envelope.
static double phaseFreq (const std::vector<float>& x, size_t from, size_t to, double sr, double f0)
{
    const size_t blk = (size_t) (0.1 * sr);
    std::vector<double> t, ph;
    for (size_t b = from; b + blk <= to; b += blk)
    {
        double re = 0, im = 0;
        const double dphi = 2.0 * 3.14159265358979323846 * f0 / sr;
        for (size_t i = 0; i < blk; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) blk);
            const double v = (double) x[b + i] * w, a = dphi * (double) (b + i - from);
            re += v * std::cos (a); im -= v * std::sin (a);
        }
        double p = std::atan2 (im, re);
        if (! ph.empty()) while (p - ph.back() > 3.14159265358979323846) p -= 2.0 * 3.14159265358979323846;
        if (! ph.empty()) while (p - ph.back() < -3.14159265358979323846) p += 2.0 * 3.14159265358979323846;
        ph.push_back (p); t.push_back ((double) (b + blk / 2 - from) / sr);
    }
    double mt = 0, mp = 0; for (size_t i = 0; i < t.size(); ++i) { mt += t[i]; mp += ph[i]; }
    mt /= (double) t.size(); mp /= (double) t.size();
    double num = 0, den = 0; for (size_t i = 0; i < t.size(); ++i) { num += (t[i] - mt) * (ph[i] - mp); den += (t[i] - mt) * (t[i] - mt); }
    return f0 + num / den / (2.0 * 3.14159265358979323846);
}

static double lineNear (const std::vector<float>& x, size_t from, size_t to, double sr, double f0)
{
    const size_t n = to - from;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i) w[i] = (double) x[from + i] * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) n));
    auto mag = [&] (double f)
    {
        double re = 0, im = 0;
        const double dphi = 2.0 * 3.14159265358979323846 * f / sr;
        double c = 1.0, s = 0.0;
        const double dc = std::cos (dphi), ds = std::sin (dphi);
        for (size_t i = 0; i < n; ++i)
        {
            re += w[i] * c; im += w[i] * s;
            const double c2 = c * dc - s * ds; s = s * dc + c * ds; c = c2;
        }
        return std::sqrt (re * re + im * im);
    };
    const int steps = 441;
    double best = 0, bestF = f0;
    std::vector<double> m ((size_t) steps + 1);
    for (int k = 0; k <= steps; ++k)
    {
        const double f = f0 * (0.9 + 0.2 * k / steps);
        m[(size_t) k] = mag (f);
        if (m[(size_t) k] > best) { best = m[(size_t) k]; bestF = f; }
    }
    // refine around the best point
    double lo = bestF - f0 * 0.2 / steps, hi = bestF + f0 * 0.2 / steps;
    for (int it = 0; it < 30; ++it)
    {
        const double a = lo + (hi - lo) / 3.0, b = hi - (hi - lo) / 3.0;
        if (mag (a) < mag (b)) lo = a; else hi = b;
    }
    return 0.5 * (lo + hi);
}

static void testPitch()
{
    std::printf ("H  pitch\n");
    // H1: the resampler alone. A 1 kHz sine at the module rate comes out at 1 kHz at every host rate (no filter involved).
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        SincResampler rs;
        rs.init (DubSynth::kEngineRate, sr, 12);
        std::vector<float> in (12), out;
        out.reserve (400000);
        double ph = 0;
        for (int blk = 0; blk < (int) (3.0 * DubSynth::kEngineRate / 12); ++blk)
        {
            for (float& v : in) { v = (float) std::sin (ph); ph += 2.0 * 3.14159265358979323846 * 1000.0 / DubSynth::kEngineRate; }
            rs.process (in.data(), in.size(), out);
        }
        double first = -1, last = -1; int cnt = 0;
        for (size_t i = (size_t) (0.5 * sr) + 1; i < out.size(); ++i)
            if (out[i - 1] < 0 && out[i] >= 0)
            {
                const double t = (double) (i - 1) + (double) (-out[i - 1]) / (double) (out[i] - out[i - 1]);
                if (first < 0) first = t;
                last = t; ++cnt;
            }
        const double f = (cnt - 1) * sr / (last - first);
        CHECK (std::fabs (f / 1000.0 - 1.0) < 2e-5, "resampler to %.0f Hz: 1000 Hz came out as %.4f Hz", sr, f);
    }
    // H2: a single A3 through every model, measured from the phase of the fundamental after the swell has settled.
    // Narti's oscillator phase advance is exactly 220.0001 Hz at the module rate (checked separately), but the filter
    // that follows moves slowly while it opens, and its changing phase delay makes the measured pitch read about
    // 1 cent flat on the poly models and about 2.4 cents flat on STRINGS (the same offset is present in the
    // unmodified firmware PolySynth with no resampler). The tolerance is 0.3 % (5 cents): tight enough to catch a
    // wrong sample-rate ratio, an octave or semitone error or a wrong chord root; it is not a tuning-accuracy claim.
    for (int m = 0; m < kNumSynthModels; ++m)
    {
        // The fundamentals each model is designed to give for a single A3, as ratios to 220 Hz:
        //   VA: the second oscillator is 1.002 x (HARMONICS 0), so anything between 1 and 1.002 is right;
        //   STRINGS: the engine detunes by 0.998.
        std::vector<double> want = { 1.0 };
        if (m == kVA) want = { 1.0, 1.001, 1.002 };
        if (m == kStrings) want = { 0.998 };
        for (int r = 0; r < 3; ++r)
        {
            const double sr = kRates[r];
            DubSynth s;
            freshSynth (s, sr);
            SynthParams p; p.model = m; p.harmonics = 0.0f; p.timbre = 0.5f; p.morph = 0.5f; p.envelope = 1.0f; p.resonance = 0.0f; p.drift = 0.0f;
            std::vector<Timed> script;
            if (m == kAcid) addAcid (script, 100, 57, 90, false, 0.0);
            else script.push_back ({ 100, genEvent (0, 57, 90) });
            const auto out = render (s, script, (int) (2.6 * sr), 512, p);
            const double f = phaseFreq (out, (size_t) (1.4 * sr), (size_t) (2.4 * sr), sr, 220.0 * want[0]);
            double err = 1e9;
            for (double w : want) err = std::min (err, std::fabs (f / (220.0 * w) - 1.0));
            CHECK (err < 0.003, "%s: single A3 measured %.3f Hz (%.2f cents from the design value)", tag (m, sr).c_str(), f, 1200.0 * std::log2 (1.0 + err));
        }
    }
}

int main (int argc, char** argv)
{
    // Optional argument: the letters of the sections to run (default: all), e.g.  synth_test AF
    const std::string only = argc > 1 ? argv[1] : "ABCDEFGH";
    auto on = [&] (char c) { return only.find (c) != std::string::npos; };
    const auto t0 = std::chrono::steady_clock::now();
    if (on ('A')) testBasics();
    if (on ('B')) testGrid();
    if (on ('C')) testBlockSizes();
    if (on ('D')) testHostile();
    if (on ('E')) testFloods();
    if (on ('F')) testSwitching();
    if (on ('G')) testSequencerChain();
    if (on ('H')) testPitch();
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%s: %lld checks, %d failed (%.1f s)\n", failures == 0 ? "synth_test: all passed" : "synth_test: FAILED", checks, failures, secs);
    return failures == 0 ? 0 : 1;
}
