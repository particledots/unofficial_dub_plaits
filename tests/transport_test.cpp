// Tests of TransportClock (the part between the host's transport and the sequencer), JUCE-free:
//   A  block size does not matter: the events are exactly those of driving the sequencer tick by tick, and every
//      event lands on the right sample (+-1), for 3 sample rates x 3 tempos x 7 block sizes x 4 settings x 2 starts
//   B  stop: held notes are released at once, nothing plays while stopped, a restart follows the song position
//   C  loop / locate while playing: held notes are released, the pattern follows the new position
//   D  tempo changes between blocks: same notes, sample positions follow the tempo, acid gate length follows the tempo
//   E  count-in (negative song position): nothing until position 0
//   F  stopped transport and a host that gives no position: silent
//   G  random fuzz: random blocks, tempos, stops and jumps; no note is ever left hanging
// (The sequencer itself is checked against the original JavaScript by seq_crosscheck.)
// Run: build/transport_test
#include "core/TransportClock.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace dub;

static int failures = 0;
static int checks = 0;
static void fail (const std::string& msg) { ++failures; if (failures <= 40) std::printf ("FAIL: %s\n", msg.c_str()); }
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { char b_[512]; std::snprintf (b_, sizeof b_, __VA_ARGS__); fail (b_); } } while (0)

// ---------------------------------------------------------------- helpers

struct Cfg
{
    const char* name;
    Mode mode;
    std::function<void (DubSequencer&)> apply;
};

static DubSequencer makeSeq (const Cfg& c)
{
    DubSequencer s;
    s.setMode (c.mode);
    c.apply (s);
    s.events().clear();
    return s;
}

static const std::vector<Cfg>& configs()
{
    static const std::vector<Cfg> v = {
        { "midi default", Mode::Midi, [] (DubSequencer& s) { s.setSeed (1); s.setGate (1.5); } },
        { "midi mono 1/16T", Mode::Midi, [] (DubSequencer& s) { s.setSeed (77); s.setChords (0); s.setRate (5); s.setDensity (12); s.setVoicing (4); s.setRoot (7); } },
        { "midi acid 1/32", Mode::Midi, [] (DubSequencer& s) { s.setSeed (4242); s.setOutput (1); s.setRate (6); s.setDensity (16); s.setMelody (9); s.setScale (3); } },
        { "both acid", Mode::Both, [] (DubSequencer& s) { s.setSeed (3); s.setModel (7); s.setMelody (2); s.setRate (4); s.setDensity (14); } },
        { "both chords", Mode::Both, [] (DubSequencer& s) { s.setSeed (900); s.setRate (2); s.setDensity (9); s.setVoicing (3); } },
    };
    return v;
}

// Drives the sequencer tick by tick, the way the Max clock does. Grid points in [fromGrid, toTick).
static std::vector<Event> directRun (DubSequencer& seq, double fromGrid, double toTick, const std::function<double (double)>& bpmAt = nullptr)
{
    std::vector<Event> out;
    for (double g = fromGrid; g < toTick - 1e-5; g += DubSequencer::kGrid)
    {
        if (bpmAt) seq.setTempo (bpmAt (g));
        seq.ticks (g);
        for (auto& e : seq.events()) out.push_back (e);
        seq.events().clear();
    }
    return out;
}

static double firstGrid (double startTick)
{
    const double k = std::ceil (startTick / DubSequencer::kGrid - 1e-6);
    return (k > 0 ? k : 0) * DubSequencer::kGrid;
}

struct Abs { long long sample; Event ev; };

struct Sim
{
    TransportClock clock;
    DubSequencer seq;
    std::vector<TimedEvent> blockEvents;
    std::vector<Abs> all;
    long long samplePos = 0;

    explicit Sim (const Cfg& c) : seq (makeSeq (c)) { blockEvents.reserve (4096); }

    void block (const HostBlock& b)
    {
        blockEvents.clear();
        clock.process (seq, b, blockEvents);
        for (auto& te : blockEvents)
        {
            CHECK (te.sample >= 0 && te.sample < std::max (1, b.numSamples), "event sample %d outside block of %d", te.sample, b.numSamples);
            all.push_back ({ samplePos + te.sample, te.ev });
        }
        samplePos += b.numSamples;
    }
};

static bool sameEvent (const Event& a, const Event& b)
{
    return a.kind == b.kind && a.tick == b.tick && a.note == b.note && a.vel == b.vel && a.type == b.type && a.ms == b.ms;
}

static std::string describe (const Event& e)
{
    char b[160];
    std::snprintf (b, sizeof b, "kind %d tick %.0f note %d vel %d type %d ms %.4f", (int) e.kind, e.tick, e.note, e.vel, e.type, e.ms);
    return b;
}

// compares the sim's events (from index `from`, up to `count` events) with a reference list
static void compareLists (const char* what, const std::vector<Abs>& got, size_t from, const std::vector<Event>& ref)
{
    CHECK (got.size() >= from + ref.size(), "%s: %zu events, expected at least %zu", what, got.size() - std::min (got.size(), from), ref.size());
    const size_t n = std::min (ref.size(), got.size() >= from ? got.size() - from : (size_t) 0);
    for (size_t i = 0; i < n; ++i)
        if (! sameEvent (got[from + i].ev, ref[i]))
        {
            CHECK (false, "%s: event %zu differs: got [%s] expected [%s]", what, i, describe (got[from + i].ev).c_str(), describe (ref[i]).c_str());
            return;
        }
}

// every MIDI note-on has its note-off, and a note is never on twice (ignoring notes outside 0..127, which the plug-in drops)
struct NoteBalance
{
    std::map<int, int> on;
    bool ok = true;
    void feed (const Event& e)
    {
        if (e.kind == Event::MidiOn) { if (++on[e.note] > 1) ok = false; }
        else if (e.kind == Event::MidiOff && e.note >= 0 && e.note <= 127) { if (--on[e.note] < 0) ok = false; }
    }
    bool allReleased() const { for (auto& kv : on) if (kv.second != 0) return false; return true; }
};

// ---------------------------------------------------------------- A: steady transport, any block size

static void testSteady (const Cfg& cfg, double sr, double bpm, int blockSize, double startPpq, double durTicks)
{
    char label[200];
    std::snprintf (label, sizeof label, "steady [%s] sr %.0f bpm %.1f block %d start %.4f", cfg.name, sr, bpm, blockSize, startPpq);

    Sim sim (cfg);
    const double tps = bpm * 480.0 / (60.0 * sr);
    const double startTick = startPpq * 480.0;
    const long long total = (long long) std::ceil (durTicks / tps);
    HostBlock b; b.playing = true; b.bpm = bpm; b.sampleRate = sr;
    for (long long done = 0; done < total; done += blockSize)
    {
        b.numSamples = (int) std::min<long long> (blockSize, total - done);
        b.ppqStart = startPpq + (double) done * bpm / (60.0 * sr);
        sim.block (b);
    }
    const size_t playEvents = sim.all.size();
    const double endTick = startTick + (double) total * tps;

    DubSequencer ref = makeSeq (cfg);
    ref.setTempo (bpm);
    const auto expected = directRun (ref, firstGrid (startTick), endTick);
    CHECK (playEvents == expected.size(), "%s: %zu events, expected %zu", label, playEvents, expected.size());
    compareLists (label, sim.all, 0, expected);

    for (size_t i = 0; i < std::min (playEvents, expected.size()); ++i)          // sample positions
    {
        const double want = (sim.all[i].ev.tick - startTick) / tps;
        CHECK (std::fabs ((double) sim.all[i].sample - want) <= 1.0, "%s: event %zu at sample %lld, expected %.2f", label, i, sim.all[i].sample, want);
        if (i > 0) CHECK (sim.all[i].sample >= sim.all[i - 1].sample, "%s: events out of order", label);
    }

    // transport stops: everything still sounding is released at once, then silence
    HostBlock stop; stop.playing = false; stop.numSamples = 512; stop.bpm = bpm; stop.sampleRate = sr;
    const size_t before = sim.all.size();
    sim.block (stop); sim.block (stop);
    for (size_t i = before; i < sim.all.size(); ++i)
        CHECK (sim.all[i].ev.kind == Event::MidiOff || sim.all[i].ev.kind == Event::Gen, "%s: unexpected event after stop", label);
    NoteBalance nb;
    for (auto& a : sim.all) nb.feed (a.ev);
    CHECK (nb.ok, "%s: a note was switched on twice or off without being on", label);
    CHECK (nb.allReleased(), "%s: a note is still held after stop", label);
}

static void testSteadyAll()
{
    const double rates[] = { 44100, 48000, 96000 };
    const double tempos[] = { 70, 120, 174.3 };
    const int blocks[] = { 1, 17, 64, 480, 512, 1000, 4096 };
    const double starts[] = { 0.0, 3.3 };               // 3.3 quarter notes = 1584 ticks (on the grid); the second run also starts mid-song
    for (const auto& cfg : configs())
        for (double sr : rates)
            for (double bpm : tempos)
                for (int bs : blocks)
                    for (double start : starts)
                    {
                        if (bs == 1 && (sr != 44100 || bpm != 174.3)) continue;           // one-sample blocks: one corner is enough
                        testSteady (cfg, sr, bpm, bs, start, bs == 1 ? 1920 : 7680);
                    }
    // a start that is not on the grid: the first grid point after it is the first one fired
    testSteady (configs()[0], 48000, 120, 333, 1.0123, 3840);
    testSteady (configs()[3], 44100, 133.7, 1024, 7.77777, 3840);
}

// ---------------------------------------------------------------- B: stop and restart

static void testStop()
{
    for (const auto& cfg : configs())
    {
        Sim sim (cfg);
        const double sr = 48000, bpm = 120, tps = bpm * 480.0 / (60.0 * sr);
        HostBlock b; b.playing = true; b.bpm = bpm; b.sampleRate = sr; b.numSamples = 480;
        const int firstBlocks = (int) std::ceil (1100.0 / (480 * tps));      // ~1100 ticks of playing: stops while notes are held
        for (int i = 0; i < firstBlocks; ++i) { b.ppqStart = i * 480.0 * tps / 480.0; sim.block (b); }
        const size_t playing = sim.all.size();
        const double stopTick = firstBlocks * 480.0 * tps;

        HostBlock s = b; s.playing = false;
        sim.block (s);
        const size_t afterFirstStop = sim.all.size();
        sim.block (s); sim.block (s);
        CHECK (sim.all.size() == afterFirstStop, "[%s] events while stopped", cfg.name);
        for (size_t i = playing; i < afterFirstStop; ++i)
            CHECK ((sim.all[i].ev.kind == Event::MidiOff || sim.all[i].ev.kind == Event::Gen), "[%s] stop should only release notes", cfg.name);

        DubSequencer ref1 = makeSeq (cfg); ref1.setTempo (bpm);
        compareLists ("stop: first run", sim.all, 0, directRun (ref1, 0, stopTick));
        NoteBalance nb; for (auto& a : sim.all) nb.feed (a.ev);
        CHECK (nb.allReleased() && nb.ok, "[%s] hanging note after stop", cfg.name);

        // restart elsewhere in the song: the pattern is a function of the position
        const double restartPpq = 6.0;
        const size_t mark = sim.all.size();
        for (int i = 0; i < 12; ++i) { b.ppqStart = restartPpq + i * 480.0 * tps / 480.0; sim.block (b); }
        DubSequencer ref2 = makeSeq (cfg); ref2.setTempo (bpm);
        compareLists ("stop: restart", sim.all, mark, directRun (ref2, restartPpq * 480.0, restartPpq * 480.0 + 12 * 480.0 * tps));
    }
}

// ---------------------------------------------------------------- C: loop jump while playing

static void testJump()
{
    for (const auto& cfg : configs())
        for (double jumpTo : { 0.0, 1.0, 9.5, 2.25 })
        {
            Sim sim (cfg);
            const double sr = 44100, bpm = 133, tps = bpm * 480.0 / (60.0 * sr);
            HostBlock b; b.playing = true; b.bpm = bpm; b.sampleRate = sr; b.numSamples = 441;
            const int n1 = 400, n2 = 300;
            for (int i = 0; i < n1; ++i) { b.ppqStart = 4.0 + i * 441 * tps / 480.0; sim.block (b); }
            const double endOfFirst = 4.0 * 480.0 + n1 * 441 * tps;
            const size_t mark = sim.all.size();
            for (int i = 0; i < n2; ++i) { b.ppqStart = jumpTo + i * 441 * tps / 480.0; sim.block (b); }

            DubSequencer ref = makeSeq (cfg); ref.setTempo (bpm);
            auto e1 = directRun (ref, firstGrid (4.0 * 480.0), endOfFirst);
            ref.state (0);
            std::vector<Event> relocation;
            for (auto& e : ref.events()) relocation.push_back (e);
            ref.events().clear();
            auto e2 = directRun (ref, firstGrid (jumpTo * 480.0), jumpTo * 480.0 + n2 * 441 * tps);
            std::vector<Event> expected = e1;
            expected.insert (expected.end(), relocation.begin(), relocation.end());
            expected.insert (expected.end(), e2.begin(), e2.end());
            char label[120]; std::snprintf (label, sizeof label, "jump [%s] to %.2f", cfg.name, jumpTo);
            compareLists (label, sim.all, 0, expected);
            CHECK (sim.all.size() == expected.size(), "%s: %zu events, expected %zu", label, sim.all.size(), expected.size());
            // the release happens in the first sample of the block with the jump, before anything else of that block
            for (size_t i = e1.size(); i < e1.size() + relocation.size() && i < sim.all.size(); ++i)
                CHECK (sim.all[i].sample == (long long) n1 * 441, "%s: release not at the start of the jump block", label);
            (void) mark;
            HostBlock stop = b; stop.playing = false;
            sim.block (stop);
            NoteBalance nb; for (auto& a : sim.all) nb.feed (a.ev);
            CHECK (nb.ok && nb.allReleased(), "%s: hanging note", label);
        }
}

// ---------------------------------------------------------------- D: tempo changes

static void testTempo()
{
    for (const auto& cfg : configs())
    {
        Sim sim (cfg);
        const double sr = 48000;
        const double tempos[] = { 120, 90, 140, 75, 200, 60 };
        struct Seg { double t0, t1, bpm; long long s0; };
        std::vector<Seg> segs;
        double ppq = 0; long long samples = 0;
        for (int k = 0; k < 24; ++k)
        {
            const double bpm = tempos[k % 6];
            const double tps = bpm * 480.0 / (60.0 * sr);
            HostBlock b; b.playing = true; b.bpm = bpm; b.sampleRate = sr; b.numSamples = 700 + 13 * k; b.ppqStart = ppq;
            segs.push_back ({ ppq * 480.0, ppq * 480.0 + b.numSamples * tps, bpm, samples });
            sim.block (b);
            ppq += b.numSamples * bpm / (60.0 * sr);
            samples += b.numSamples;
        }
        auto bpmAt = [&] (double g) { for (auto& s : segs) if (g >= s.t0 - 1e-6 && g < s.t1 - 1e-5) return s.bpm; return segs.back().bpm; };
        DubSequencer ref = makeSeq (cfg);
        const auto expected = directRun (ref, 0, segs.back().t1, bpmAt);
        CHECK (sim.all.size() == expected.size(), "tempo [%s]: %zu events, expected %zu", cfg.name, sim.all.size(), expected.size());
        compareLists ("tempo", sim.all, 0, expected);
        for (auto& a : sim.all)
        {
            for (auto& s : segs)
                if (a.ev.tick >= s.t0 - 1e-6 && a.ev.tick < s.t1 - 1e-5)
                {
                    const double want = (double) s.s0 + (a.ev.tick - s.t0) / (s.bpm * 480.0 / (60.0 * sr));
                    CHECK (std::fabs ((double) a.sample - want) <= 1.0, "tempo [%s]: event at sample %lld, expected %.2f", cfg.name, a.sample, want);
                    break;
                }
        }
    }
}

// ---------------------------------------------------------------- E, F: count-in, stopped, no position

static void testCountInAndSilence()
{
    for (const auto& cfg : configs())
    {
        const double sr = 48000, bpm = 120, tps = bpm * 480.0 / (60.0 * sr);
        Sim sim (cfg);
        HostBlock b; b.playing = true; b.bpm = bpm; b.sampleRate = sr; b.numSamples = 960;
        const double startPpq = -2.0;                                   // two quarter notes of count-in
        for (int i = 0; i < 100; ++i) { b.ppqStart = startPpq + i * 960 * tps / 480.0; sim.block (b); }
        const double endTick = startPpq * 480.0 + 100 * 960 * tps;
        DubSequencer ref = makeSeq (cfg); ref.setTempo (bpm);
        const auto expected = directRun (ref, 0, endTick);
        CHECK (sim.all.size() == expected.size() && ! expected.empty(), "count-in [%s]: %zu events, expected %zu", cfg.name, sim.all.size(), expected.size());
        compareLists ("count-in", sim.all, 0, expected);
        for (auto& a : sim.all) CHECK (a.ev.tick >= 0, "event before the song start");

        Sim quiet (cfg), nopos (cfg);
        HostBlock q = b; q.playing = false;
        HostBlock np = b; np.positionValid = false;
        for (int i = 0; i < 200; ++i) { q.ppqStart = i * 2.0; quiet.block (q); nopos.block (np); }
        CHECK (quiet.all.empty(), "[%s] events with the transport stopped", cfg.name);
        CHECK (nopos.all.empty(), "[%s] events with no host position", cfg.name);
    }
}

// ---------------------------------------------------------------- G: fuzz

static void testFuzz()
{
    uint32_t rs = 0xC0FFEE;
    auto rnd = [&] { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.0; };
    for (int run = 0; run < 60; ++run)
    {
        const Cfg& cfg = configs()[(size_t) run % configs().size()];
        Sim sim (cfg);
        NoteBalance nb;
        const double sr = rnd() < 0.5 ? 44100 : 48000;
        double ppq = std::floor (rnd() * 40.0 * 4) / 4, bpm = 60 + rnd() * 160;
        bool playing = true;
        size_t seen = 0;
        for (int i = 0; i < 1500; ++i)
        {
            const double r = rnd();
            if (r < 0.01) playing = ! playing;
            else if (r < 0.03) ppq = std::floor (rnd() * 40.0 * 8) / 8;               // locate
            else if (r < 0.08) bpm = 40 + rnd() * 260;                                 // tempo
            if (r > 0.995) { sim.seq.setDensity (std::floor (rnd() * 17)); sim.seq.setRate (std::floor (rnd() * 8)); sim.seq.setChords (rnd() < 0.5 ? 1 : 0); }
            HostBlock b; b.playing = playing; b.bpm = bpm; b.sampleRate = sr; b.ppqStart = ppq;
            b.numSamples = 1 + (int) (rnd() * rnd() * 6000);
            sim.block (b);
            if (playing) ppq += b.numSamples * bpm / (60.0 * sr);
            for (; seen < sim.all.size(); ++seen)
            {
                nb.feed (sim.all[seen].ev);
                if (! playing) CHECK (sim.all[seen].ev.kind == Event::MidiOff || sim.all[seen].ev.kind == Event::Gen, "fuzz: sound started while stopped");
            }
        }
        HostBlock stop; stop.playing = false; stop.numSamples = 64;
        sim.block (stop);
        for (; seen < sim.all.size(); ++seen) nb.feed (sim.all[seen].ev);
        CHECK (nb.ok, "fuzz run %d: a note was switched on twice or off without being on", run);
        CHECK (nb.allReleased(), "fuzz run %d: a note was left hanging", run);
    }
}

int main()
{
    testSteadyAll();
    testStop();
    testJump();
    testTempo();
    testCountInAndSilence();
    testFuzz();
    std::printf ("transport_test: %d checks, %s\n", checks, failures == 0 ? "all passed" : "FAILED");
    if (failures) std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
