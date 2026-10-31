// Tests of the additions to the sequencer that the original dubstab.js does not have: Steps 17-64 (four bars of 16) and the
// order the steps are played in (Direction). JUCE-free.
//
// The original's behaviour (Steps 1-16, forward) is not tested here: tests/seq_crosscheck.cpp does that against the original
// script itself. What is tested here is that the new things are well-behaved, repeatable and do what their names say.
#include "core/DubSequencer.h"
#include "core/PatternExport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

using namespace dub;

static int failures = 0;
static long long checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; if (failures <= 40) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } } while (0)

static const char* kNames[kNumDirections] = { "forward", "reverse", "pendulum", "ping-pong", "random", "random-no-repeat", "drunk", "chance" };

static DubSequencer make (int steps, int direction, int seed = 1)
{
    DubSequencer s;
    s.setSeed (seed);
    s.setSteps (steps);
    s.setDirection (direction);
    return s;
}

static std::vector<int> run (const DubSequencer& s, long long from, long long count)
{
    std::vector<int> v;
    for (long long n = from; n < from + count; ++n) v.push_back (s.stepForIndex (n));
    return v;
}

// circular distance between two steps in a pattern of L
static int circ (int a, int b, int L) { const int d = std::abs (a - b) % L; return std::min (d, L - d); }

static void testSettingsRanges()
{
    DubSequencer s;
    s.setSteps (0);   CHECK (s.settings().length == 1, "steps floor");
    s.setSteps (64);  CHECK (s.settings().length == 64, "steps 64");
    s.setSteps (65);  CHECK (s.settings().length == 64, "steps cap %d", s.settings().length);
    s.setSteps (16.4); CHECK (s.settings().length == 16, "steps rounds");
    s.setDirection (-3); CHECK (s.settings().direction == 0, "direction floor");
    s.setDirection (99); CHECK (s.settings().direction == kNumDirections - 1, "direction cap");
    s.setDirWeight (ChPendulum, 500); CHECK (s.settings().dirWeight[ChPendulum] == 100, "weight cap");
    s.setDirWeight (ChPendulum, -5);  CHECK (s.settings().dirWeight[ChPendulum] == 0, "weight floor");
    s.setDirWeight (99, 50);          // out-of-range mode index: ignored
    s.setRunMin (0); CHECK (s.settings().runMin == 1, "run min floor");
    s.setRunMax (900); CHECK (s.settings().runMax == 64, "run max cap");
    DubSequencer d;
    CHECK (d.settings().direction == DirForward && d.settings().length == 16, "defaults: forward, 16 steps");
}

static void testForwardAndReverse()
{
    for (int L = 1; L <= 64; ++L)
    {
        auto f = make (L, DirForward), r = make (L, DirReverse);
        for (long long n = 0; n < 3L * L + 5; ++n)
        {
            CHECK (f.stepForIndex (n) == (int) (n % L), "forward L=%d n=%lld", L, n);
            CHECK (r.stepForIndex (n) == L - 1 - (int) (n % L), "reverse L=%d n=%lld", L, n);
        }
    }
    auto f = make (13, DirForward);
    CHECK (f.stepForIndex (4000000000LL) == (int) (4000000000LL % 13), "forward far");
}

static void testPendulum()
{
    {
        auto p = make (4, DirPendulum);
        const std::vector<int> want = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0 };
        CHECK (run (p, 0, 13) == want, "pendulum 4 steps");
    }
    {
        auto p = make (2, DirPendulum);
        CHECK (run (p, 0, 6) == (std::vector<int> { 0, 1, 0, 1, 0, 1 }), "pendulum 2 steps");
        auto q = make (1, DirPendulum);
        CHECK (run (q, 0, 4) == (std::vector<int> { 0, 0, 0, 0 }), "pendulum 1 step");
    }
    for (int L : { 3, 8, 16, 17, 33, 64 })
    {
        auto p = make (L, DirPendulum);
        const auto v = run (p, 0, 6L * L);
        for (size_t i = 1; i < v.size(); ++i)
            CHECK (std::abs (v[i] - v[i - 1]) == 1, "pendulum L=%d moves one step at a time (n=%zu: %d -> %d)", L, i, v[i - 1], v[i]);
        CHECK (*std::min_element (v.begin(), v.end()) == 0 && *std::max_element (v.begin(), v.end()) == L - 1, "pendulum L=%d reaches both ends", L);
    }
}

static void testPingPong()
{
    auto p = make (4, DirPingPong);
    CHECK (run (p, 0, 17) == (std::vector<int> { 0, 1, 2, 3, 3, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 0, 0 }), "ping-pong 4 steps (ends twice)");
    auto q = make (1, DirPingPong);
    CHECK (run (q, 0, 3) == (std::vector<int> { 0, 0, 0 }), "ping-pong 1 step");
}

static void testRandomOrders()
{
    for (int L : { 1, 2, 3, 16, 17, 64 })
    {
        auto r = make (L, DirRandom, 5);
        auto a = run (r, 0, 3000);
        auto b = run (r, 0, 3000);
        CHECK (a == b, "random L=%d repeatable", L);
        for (int v : a) CHECK (v >= 0 && v < L, "random L=%d in range (%d)", L, v);
        if (L >= 3) { std::set<int> seen (a.begin(), a.end()); CHECK ((int) seen.size() == L, "random L=%d visits every step (%zu)", L, seen.size()); }
    }
    auto r1 = make (16, DirRandom, 1), r2 = make (16, DirRandom, 2);
    int count[16] = {};
    for (int v : run (r1, 0, 16000)) ++count[v];
    for (int i = 0; i < 16; ++i) CHECK (count[i] > 700 && count[i] < 1300, "random spread: step %d chosen %d times of 16000", i, count[i]);
    CHECK (run (r1, 0, 64) != run (r2, 0, 64), "random: another seed gives another order");

    for (int L : { 2, 5, 16, 33, 64 })
    {
        auto q = make (L, DirRandomNoRepeat, 9);
        std::set<std::vector<int>> rounds;
        for (int c = 0; c < 20; ++c)
        {
            auto v = run (q, (long long) c * L, L);
            auto sorted = v; std::sort (sorted.begin(), sorted.end());
            bool perm = true;
            for (int i = 0; i < L; ++i) perm = perm && sorted[(size_t) i] == i;
            CHECK (perm, "no-repeat L=%d round %d is not every step once", L, c);
            rounds.insert (v);
        }
        if (L >= 5) CHECK (rounds.size() > 15, "no-repeat L=%d: rounds differ (%zu distinct of 20)", L, rounds.size());
        CHECK (run (q, 0, 3 * L) == run (q, 0, 3 * L), "no-repeat repeatable");
    }
}

static void testDrunk()
{
    for (int L : { 1, 2, 3, 16, 64 })
    {
        auto d = make (L, DirDrunk, 4);
        const auto v = run (d, 0, 2000);
        for (size_t i = 0; i < v.size(); ++i)
        {
            CHECK (v[i] >= 0 && v[i] < L, "drunk L=%d in range", L);
            if (i % 256 != 0) CHECK (circ (v[i], v[i - 1], L) <= 1, "drunk L=%d moves at most one step (n=%zu)", L, i);
        }
        CHECK (v == run (d, 0, 2000), "drunk repeatable");
    }
    auto d = make (16, DirDrunk, 4);
    const auto v = run (d, 0, 255);
    int up = 0, down = 0, stay = 0;
    for (size_t i = 1; i < v.size(); ++i) { const int diff = ((v[i] - v[i - 1]) + 16) % 16; if (diff == 1) ++up; else if (diff == 15) ++down; else if (diff == 0) ++stay; }
    CHECK (up > 40 && down > 40 && stay > 10, "drunk: up %d down %d stay %d", up, down, stay);
}

static void setOnly (DubSequencer& c, int mode)
{
    for (int m = 0; m < kNumChanceModes; ++m) c.setDirWeight (m, m == mode ? 100 : 0);
}

static void testChance()
{
    {
        auto c = make (16, DirChance, 3);
        setOnly (c, ChForward);
        const auto v = run (c, 0, 600);
        for (size_t i = 1; i < v.size(); ++i)
            if (i % 256 != 0) CHECK (v[i] == (v[i - 1] + 1) % 16, "chance forward-only: n=%zu %d -> %d", i, v[i - 1], v[i]);
    }
    {
        auto c = make (16, DirChance, 3);
        setOnly (c, ChReverse);
        const auto v = run (c, 0, 600);
        for (size_t i = 1; i < v.size(); ++i)
            if (i % 256 != 0) CHECK (v[i] == (v[i - 1] + 15) % 16, "chance reverse-only: n=%zu %d -> %d", i, v[i - 1], v[i]);
    }
    {
        auto c = make (16, DirChance, 3);
        for (int m = 0; m < kNumChanceModes; ++m) c.setDirWeight (m, 0);
        const auto v = run (c, 0, 300);
        for (size_t i = 1; i < v.size(); ++i)
            if (i % 256 != 0) CHECK (v[i] == (v[i - 1] + 1) % 16, "chance with no weights plays forward");
    }
    // forward and reverse, runs of exactly 8: they alternate every 8 steps
    {
        auto c = make (32, DirChance, 7);
        for (int m = 0; m < kNumChanceModes; ++m) c.setDirWeight (m, (m == ChForward || m == ChReverse) ? 100 : 0);
        c.setRunMin (8); c.setRunMax (8);
        const auto v = run (c, 0, 256);
        std::vector<int> dirs;          // +1 / -1 per move
        for (size_t i = 1; i < v.size(); ++i) dirs.push_back (v[i] == (v[i - 1] + 1) % 32 ? 1 : (v[i] == (v[i - 1] + 31) % 32 ? -1 : 0));
        for (size_t i = 0; i < dirs.size(); ++i) CHECK (dirs[i] != 0, "chance alternating: unexpected jump at %zu", i);
        // the first run has 7 moves (its first step is the start), each later run 8
        for (size_t i = 1; i < 7; ++i) CHECK (dirs[i] == dirs[0], "run 1 keeps one direction");
        for (size_t r = 0; r < 12; ++r)
        {
            const size_t a = 7 + r * 8;
            for (size_t i = a; i < a + 8 && i < dirs.size(); ++i) CHECK (dirs[i] == dirs[a], "run %zu keeps one direction (move %zu)", r + 2, i);
            if (a + 8 < dirs.size()) CHECK (dirs[a + 8] == -dirs[a], "run %zu then run %zu go opposite ways", r + 2, r + 3);
        }
        CHECK (dirs[7] == -dirs[0], "run 1 then run 2 go opposite ways");
    }
    for (int L : { 1, 2, 3, 16, 64 })
    {
        auto c = make (L, DirChance, 11);
        for (int m = 0; m < kNumChanceModes; ++m) c.setDirWeight (m, 50);
        c.setRunMin (1); c.setRunMax (6);
        const auto v = run (c, 0, 5000);
        for (int x : v) CHECK (x >= 0 && x < L, "chance L=%d in range", L);
        CHECK (v == run (c, 0, 5000), "chance repeatable");
        if (L == 16)
        {
            int fwd = 0, rev = 0, other = 0;
            for (size_t i = 1; i < v.size(); ++i)
            {
                if (i % 256 == 0) continue;
                const int d = (v[i] - v[i - 1] + 16) % 16;
                if (d == 1) ++fwd; else if (d == 15) ++rev; else ++other;
            }
            CHECK (fwd > 500 && rev > 500 && other > 200, "chance mixes orders: fwd %d rev %d other %d", fwd, rev, other);
        }
    }
    auto a = make (16, DirChance, 1), b = make (16, DirChance, 2);
    CHECK (run (a, 0, 200) != run (b, 0, 200), "chance: another seed gives another walk");
    a.setRunMin (20); a.setRunMax (3);
    for (int x : run (a, 0, 600)) CHECK (x >= 0 && x < 16, "chance with run min above max stays in range");
}

static void testAllDirectionsBasics()
{
    for (int d = 0; d < kNumDirections; ++d)
        for (int L = 1; L <= 64; ++L)
        {
            auto s = make (L, d, 100 + L);
            for (long long n : { 0LL, 1LL, 2LL, 63LL, 255LL, 256LL, 257LL, 1000LL, 99999LL, 123456789LL, 4000000000LL, 9000000000000LL, -5LL })
            {
                const int x = s.stepForIndex (n);
                CHECK (x >= 0 && x < L, "%s L=%d n=%lld gave %d", kNames[d], L, n, x);
            }
        }
    // asking in a different order (a jump in the song) gives the same answers as playing up to it
    for (int d = 0; d < kNumDirections; ++d)
    {
        auto s = make (21, d, 77);
        const auto seq = run (s, 0, 1200);
        for (long long n : { 1199LL, 3LL, 600LL, 255LL, 256LL, 0LL, 777LL })
            CHECK (s.stepForIndex (n) == seq[(size_t) n], "%s: jumping to %lld gives a different step", kNames[d], n);
    }
}

static void testBars()
{
    DubSequencer a, b;
    a.setSeed (42); b.setSeed (42);
    a.setSteps (64);
    CHECK (a.pattern().degree == b.pattern().degree && a.pattern().rank == b.pattern().rank && a.pattern().accent == b.pattern().accent
           && a.pattern().voicing == b.pattern().voicing && a.pattern().mono == b.pattern().mono, "bar 1 does not depend on Steps");
    int different = 0;
    for (int k = 1; k < DubSequencer::kSections; ++k)
        if (a.patternSection (k).degree != a.patternSection (0).degree || a.patternSection (k).rank != a.patternSection (0).rank) ++different;
    CHECK (different == 3, "bars 2-4 differ from bar 1 (%d of 3)", different);
    for (int k = 0; k < DubSequencer::kSections; ++k)
    {
        auto r = a.patternSection (k).rank; std::sort (r.begin(), r.end());
        bool ok = true; for (int i = 0; i < 16; ++i) ok = ok && r[(size_t) i] == i;
        CHECK (ok, "bar %d ranks are a permutation", k + 1);
    }
    DubSequencer c; c.setSeed (42);
    for (int k = 0; k < 4; ++k) CHECK (a.patternSection (k).degree == c.patternSection (k).degree, "bar %d repeatable", k + 1);
    c.setSeed (43);
    CHECK (c.patternSection (2).degree != a.patternSection (2).degree || c.patternSection (2).rank != a.patternSection (2).rank, "seed changes bar 3");
    DubSequencer m1, m2; m1.setSeed (42); m2.setSeed (42); m1.setMelody (5); m2.setMelody (6);
    CHECK (m1.patternSection (1).mono != m2.patternSection (1).mono, "melody changes bar 2's line");
    CHECK (m1.patternSection (1).mono != m1.patternSection (2).mono || m1.patternSection (1).acidShift != m1.patternSection (2).acidShift, "bars do not share a melody");
    a.setDensity (7);
    int plays = 0;
    for (int st = 0; st < 64; ++st)
    {
        const auto& p = a.patternSection (st / 16);
        CHECK (a.stepPlays (st) == (p.rank[(size_t) (st % 16)] < 7), "stepPlays %d", st);
        CHECK (a.stepAccented (st) == (p.accent[(size_t) (st % 16)] != 0), "stepAccented %d", st);
        plays += a.stepPlays (st);
    }
    CHECK (plays == 28, "density 7 plays 7 steps in each bar (%d)", plays);
}

// The sequencer, run over time: the note-ons at each step.
static std::map<int, std::vector<int>> notesByStep (DubSequencer& s, int rate, int totalSteps)
{
    std::map<int, std::vector<int>> out;
    const int rt = DubSequencer::rateTicks (rate);
    s.events().clear();
    for (long long t = 0; t < (long long) totalSteps * rt; t += 20)
    {
        s.ticks ((double) t);
        if (t % rt == 0) out[(int) (t / rt)];
        for (auto& e : s.events()) if (e.kind == Event::MidiOn) out[(int) (t / rt)].push_back (e.note);
        s.events().clear();
    }
    return out;
}

static DubSequencer midiSeq (int steps, int density, int direction)
{
    DubSequencer s; s.setMode (Mode::Midi); s.setSeed (8); s.setDensity (density); s.setSteps (steps); s.setRate (4); s.setDirection (direction);
    return s;
}

static void testPlayingBars()
{
    auto s = midiSeq (64, 16, DirForward);
    auto got = notesByStep (s, 4, 128);
    for (int j = 0; j < 128; ++j)
    {
        const int st = j % 64;
        const auto& p = s.patternSection (st / 16);
        const auto chord = s.buildChord (p.degree[(size_t) (st % 16)], p.voicing[(size_t) (st % 16)]);
        std::vector<int> want (chord.notes.begin(), chord.notes.begin() + chord.count);
        std::vector<int> have = got[j];
        std::sort (want.begin(), want.end()); std::sort (have.begin(), have.end());
        CHECK (want == have, "64 steps: step %d plays the chord of bar %d step %d", j, st / 16 + 1, st % 16 + 1);
    }
    auto t = midiSeq (16, 16, DirForward);
    auto g16 = notesByStep (t, 4, 32);
    for (int j = 0; j < 32; ++j) CHECK (g16[j] == g16[j % 16], "16 steps repeat every 16");
    for (int j = 0; j < 16; ++j) CHECK (g16[j] == got[j], "bar 1 of 64 steps = the 16-step pattern (step %d)", j);

    for (int L : { 5, 16, 40 })
    {
        auto f = midiSeq (L, 16, DirForward), r = midiSeq (L, 16, DirReverse);
        auto gf = notesByStep (f, 4, 2 * L), gr = notesByStep (r, 4, 2 * L);
        for (int j = 0; j < 2 * L; ++j) CHECK (gr[j] == gf[L - 1 - (j % L)], "reverse L=%d step %d", L, j);
    }
    {
        auto p = midiSeq (16, 6, DirPendulum), f = midiSeq (16, 6, DirForward);
        auto gf = notesByStep (f, 4, 16), gp = notesByStep (p, 4, 60);
        for (int j = 0; j < 60; ++j)
        {
            const int want = p.stepForIndex (j);
            CHECK (gp[j] == gf[want], "pendulum: step %d plays pattern step %d", j, want);
        }
    }
}

static std::string textAt (DubSequencer& s, int step)
{
    s.events().clear();
    s.ticks ((double) step * DubSequencer::rateTicks (s.settings().rate));
    std::string t;
    for (auto& e : s.events()) if (e.kind == Event::PatternText) t = e.text;
    s.events().clear();
    return t;
}

static void testDisplayText()
{
    DubSequencer s; s.setMode (Mode::Midi); s.setSeed (3); s.setSteps (40); s.setRate (4); s.emitText = true;
    auto marker = [] (const std::string& x) { for (size_t i = 0; i < x.size(); ++i) if (x[i] == '#' || x[i] == '_') return (int) i; return -1; };
    const auto at20 = textAt (s, 20), at36 = textAt (s, 36);
    CHECK (marker (at20) == 5, "text at step 20: marker at %d (%s)", marker (at20), at20.c_str());
    CHECK (marker (at36) == 5, "text at step 36: marker at %d (%s)", marker (at36), at36.c_str());
    CHECK (at36.size() == 19 && at36.substr (10, 9) == "         ", "text at step 36 shows only the 8 cells in use (%s)", at36.c_str());
}

static void testExport()
{
    ExportSettings e;
    e.density = 16; e.steps = 64; e.rate = 4; e.seed = 12; e.bars = 8; e.chords = 1;
    const auto fwd = renderPatternNotes (e);
    CHECK (! fwd.empty(), "64-step export has notes");
    for (auto& n : fwd) CHECK (n.endTick <= 8 * 1920 && n.endTick > n.startTick, "export note in range");
    // 64 steps at 1/16 = 7680 ticks = 4 bars: bars 5-8 repeat bars 1-4
    int mismatch = 0, count = 0;
    for (auto& a : fwd)
        if (a.startTick < 4 * 1920)
        {
            ++count;
            bool found = false;
            for (auto& b : fwd) if (b.startTick == a.startTick + 4 * 1920 && b.note == a.note) { found = true; break; }
            if (! found) ++mismatch;
        }
    CHECK (count > 0 && mismatch == 0, "export repeats after 64 steps (%d of %d missing)", mismatch, count);

    auto rev = e; rev.direction = DirReverse;
    const auto r = renderPatternNotes (rev);
    CHECK (! r.empty() && r.size() == fwd.size(), "reverse export has the same number of notes (%zu vs %zu)", r.size(), fwd.size());
    std::vector<int> firstR, lastF;
    for (auto& n : r) if (n.startTick == 0) firstR.push_back (n.note);
    for (auto& n : fwd) if (n.startTick == 63 * 120) lastF.push_back (n.note);
    std::sort (firstR.begin(), firstR.end()); std::sort (lastF.begin(), lastF.end());
    CHECK (! firstR.empty() && firstR == lastF, "reverse export starts on the last step of the forward pattern");

    auto ch = e; ch.direction = DirChance; ch.runMin = 3; ch.runMax = 9;
    ch.dirWeight[ChForward] = 80; ch.dirWeight[ChReverse] = 40; ch.dirWeight[ChRandom] = 25; ch.dirWeight[ChPendulum] = 0; ch.dirWeight[ChPingPong] = 0; ch.dirWeight[ChDrunk] = 0;
    const auto c1 = renderPatternNotes (ch), c2 = renderPatternNotes (ch);
    CHECK (c1 == c2 && ! c1.empty(), "chance export is repeatable");

    // the exported notes are what the sequencer plays: compare every step of the first two passes
    DubSequencer s = midiSeq (64, 16, DirChance);
    s.setSeed (12); s.setRunMin (3); s.setRunMax (9);
    for (int m = 0; m < kNumChanceModes; ++m) s.setDirWeight (m, ch.dirWeight[m]);
    for (int j = 0; j < 128; ++j)
    {
        const int st = s.stepForIndex (j);
        const auto& p = s.patternSection (st / 16);
        const auto chord = s.buildChord (p.degree[(size_t) (st % 16)], p.voicing[(size_t) (st % 16)]);
        std::vector<int> want (chord.notes.begin(), chord.notes.begin() + chord.count), have;
        for (auto& n : c1) if (n.startTick == (long long) j * 120) have.push_back (n.note);
        std::sort (want.begin(), want.end()); std::sort (have.begin(), have.end());
        CHECK (want == have, "chance export: step %d", j);
    }
}

int main()
{
    testSettingsRanges();
    testForwardAndReverse();
    testPendulum();
    testPingPong();
    testRandomOrders();
    testDrunk();
    testChance();
    testAllDirectionsBasics();
    testBars();
    testPlayingBars();
    testDisplayText();
    testExport();
    std::printf ("%s: %lld checks, %d failed\n", failures == 0 ? "direction_test passed" : "direction_test FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}
