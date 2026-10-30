// Tests of DirectPlay (MIDI notes that play the sound directly) and PatternExport (pattern -> MIDI notes). JUCE-free.
#include "core/DirectPlay.h"
#include "core/PatternExport.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

using namespace dub;

static int failures = 0;
static long long checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; if (failures <= 40) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } } while (0)

static std::vector<DirectCmd> flushAll (DirectPlay& d, long long from, long long to)
{
    std::vector<DirectCmd> out;
    for (long long s = from; s < to; s += 64) d.flush (s, std::min (to, s + 64), out);
    return out;
}

static std::vector<int> notesOf (const DirectCmd& c) { return std::vector<int> (c.notes.begin(), c.notes.begin() + c.count); }

static void testSingleNote()
{
    DirectPlay d; d.prepare (48000);
    d.setModelKind (false, false);
    d.noteOn (100, 60, 90);
    d.noteOff (20000, 60);
    auto out = flushAll (d, 0, 30000);
    CHECK (out.size() == 1, "single note: %zu commands", out.size());
    if (out.size() == 1)
    {
        CHECK (out[0].kind == DirectCmd::Chord && out[0].count == 1 && out[0].notes[0] == 60 && out[0].root == 60, "single note content");
        CHECK (out[0].vel == 90, "velocity 90 should not accent (%d)", out[0].vel);
        CHECK (out[0].at >= 100 && out[0].at <= 100 + d.windowSamples() + 64, "single note at %lld", out[0].at);
    }
}

static void testChordTogetherAndSpread()
{
    for (long long spread : { 0LL, 1LL, 20LL, 90LL })
    {
        DirectPlay d; d.prepare (48000);
        d.setModelKind (false, false);
        d.noteOn (1000, 64, 110);
        d.noteOn (1000 + spread, 60, 80);
        d.noteOn (1000 + 2 * spread / 2, 67, 80);
        auto out = flushAll (d, 0, 5000);
        CHECK (out.size() == 1, "chord spread %lld: %zu stabs", spread, out.size());
        if (out.size() == 1)
        {
            CHECK (notesOf (out[0]) == std::vector<int> ({ 60, 64, 67 }), "chord spread %lld: wrong notes", spread);
            CHECK (out[0].root == 60, "root %d", out[0].root);
            CHECK (out[0].vel == 127, "accent expected from the 110 velocity note");
        }
    }
    // far apart: two stabs
    DirectPlay d; d.prepare (48000); d.setModelKind (false, false);
    d.noteOn (1000, 60, 90); d.noteOn (1000 + 5000, 67, 90);
    auto out = flushAll (d, 0, 12000);
    CHECK (out.size() == 2, "two separate notes: %zu stabs", out.size());
}

static void testHeldNotes()
{
    // poly: new note while another is held -> new stab with only the new note; held note stays in the root
    DirectPlay d; d.prepare (48000); d.setModelKind (false, false);
    d.noteOn (0, 60, 90);
    d.noteOn (10000, 64, 90);
    auto out = flushAll (d, 0, 20000);
    CHECK (out.size() == 2, "held: %zu", out.size());
    if (out.size() == 2)
    {
        CHECK (notesOf (out[1]) == std::vector<int> ({ 64 }), "poly: second stab has only the new note");
        CHECK (out[1].root == 60, "poly: root stays the lowest held (%d)", out[1].root);
    }
    // release removes from held
    d.noteOff (21000, 60);
    d.noteOn (30000, 72, 90);
    out = flushAll (d, 20000, 40000);
    CHECK (out.size() == 1 && out[0].root == 64, "after releasing 60 the root is 64 (%d)", out.empty() ? -1 : out[0].root);

    // strings: includes everything held
    DirectPlay s; s.prepare (48000); s.setModelKind (false, true);
    s.noteOn (0, 60, 90); s.noteOn (10000, 64, 90);
    out = flushAll (s, 0, 20000);
    CHECK (out.size() == 2, "strings %zu", out.size());
    if (out.size() == 2) CHECK (notesOf (out[1]) == std::vector<int> ({ 60, 64 }), "strings: second chord holds both notes");
}

static void testMaxFive()
{
    DirectPlay d; d.prepare (48000); d.setModelKind (false, false);
    for (int n : { 72, 60, 64, 67, 71, 74, 76 }) d.noteOn (500, n, 90);
    auto out = flushAll (d, 0, 3000);
    CHECK (out.size() == 1 && out[0].count == 5, "max five notes");
    if (out.size() == 1) CHECK (notesOf (out[0]) == std::vector<int> ({ 60, 64, 67, 71, 72 }), "the lowest five are kept");
}

static void testAcid()
{
    DirectPlay d; d.prepare (48000); d.setModelKind (true, false);
    d.noteOn (100, 40, 90);
    d.noteOn (2000, 43, 127);
    d.noteOff (3000, 43);        // back to 40 with a slide
    d.noteOff (4000, 40);        // gate off
    auto out = flushAll (d, 0, 6000);
    CHECK (out.size() == 4, "acid: %zu commands", out.size());
    if (out.size() == 4)
    {
        CHECK (out[0].kind == DirectCmd::AcidNote && out[0].note == 40, "acid 1");
        CHECK (out[1].kind == DirectCmd::AcidSlide && out[1].note == 43 && out[1].vel == 127, "acid 2");
        CHECK (out[2].kind == DirectCmd::AcidSlide && out[2].note == 40, "acid 3");
        CHECK (out[3].kind == DirectCmd::AcidOff, "acid 4");
    }
    // a stuck note is closed by reset
    DirectPlay e; e.prepare (48000); e.setModelKind (true, false);
    e.noteOn (0, 50, 90); flushAll (e, 0, 100);
    e.reset();
    out = flushAll (e, 100, 200);
    CHECK (out.size() == 1 && out[0].kind == DirectCmd::AcidOff, "reset closes the acid gate");
    e.noteOn (300, 50, 90);
    e.allNotesOff (400);
    out = flushAll (e, 200, 600);
    CHECK (out.size() == 2 && out[1].kind == DirectCmd::AcidOff, "allNotesOff closes the acid gate");
}

static void testCommandTimingClamp()
{
    DirectPlay d; d.prepare (44100); d.setModelKind (false, false);
    d.noteOn (500, 60, 90);
    std::vector<DirectCmd> out;
    d.flush (400, 464, out);
    CHECK (out.empty(), "chord not due yet");
    d.flush (464, 528, out);
    d.flush (528, 592, out);
    CHECK (out.size() == 1, "chord comes out once (%zu)", out.size());
    if (! out.empty()) CHECK (out[0].at >= 464 && out[0].at < 592, "inside the block that closed it (%lld)", out[0].at);
}

static void testHostileInput()
{
    DirectPlay d; d.prepare (48000); d.setModelKind (false, false);
    for (int i = 0; i < 5000; ++i) { d.noteOn (i, (i * 7) % 200 - 20, i % 130); if (i % 3 == 0) d.noteOff (i, (i * 5) % 128); }
    auto out = flushAll (d, 0, 6000);
    for (auto& c : out) CHECK (c.count >= 0 && c.count <= 5 && (c.count == 0 || (c.notes[0] >= 0 && c.notes[(size_t) c.count - 1] <= 127)), "bad chord");
    d.noteOff (6000, 33);       // a note-off for a note that is not held
    d.allNotesOff (6000);
    CHECK (d.heldCount() == 0, "allNotesOff leaves nothing held");
}

static ExportSettings base()
{
    ExportSettings s;
    s.root = 0; s.scale = 0; s.minor = 0; s.voicing = 0; s.density = 8; s.steps = 16; s.rate = 4; s.octave = 3; s.seed = 7; s.bars = 4;
    return s;
}

static void testExport()
{
    auto s = base();
    auto a = renderPatternNotes (s);
    auto b = renderPatternNotes (s);
    CHECK (! a.empty(), "export: no notes");
    CHECK (a.size() == b.size(), "export is not repeatable");
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) same = a[i].startTick == b[i].startTick && a[i].note == b[i].note && a[i].endTick == b[i].endTick;
    CHECK (same, "export differs between two runs");
    for (auto& n : a)
    {
        CHECK (n.startTick >= 0 && n.endTick > n.startTick && n.endTick <= s.bars * 1920, "note times %lld..%lld", n.startTick, n.endTick);
        CHECK (n.note >= 0 && n.note <= 127 && n.velocity > 0 && n.velocity < 128, "note %d vel %d", n.note, n.velocity);
    }
    CHECK (std::is_sorted (a.begin(), a.end(), [] (const ExportNote& x, const ExportNote& y) { return x.startTick < y.startTick || (x.startTick == y.startTick && x.note < y.note); }), "export not sorted");
    // more bars = more notes, never fewer
    auto s8 = s; s8.bars = 8;
    CHECK (renderPatternNotes (s8).size() >= a.size(), "8 bars has fewer notes than 4");
    // a different seed gives a different pattern
    auto s2 = s; s2.seed = 8;
    auto c = renderPatternNotes (s2);
    bool diff = c.size() != a.size();
    for (size_t i = 0; ! diff && i < a.size(); ++i) diff = a[i].startTick != c[i].startTick || a[i].note != c[i].note;
    CHECK (diff, "seeds 7 and 8 give the same notes");
    // acid exports single notes
    auto sa = s; sa.model = 7;
    auto ac = renderPatternNotes (sa);
    CHECK (! ac.empty(), "acid export empty");
    // transpose moves everything
    auto st = s; st.transposeNote = 62;
    auto t = renderPatternNotes (st);
    CHECK (! t.empty() && t.size() == a.size(), "transposed export has a different note count");
    // density 0 or tiny gate must not crash
    auto s0 = s; s0.density = 0; s0.gate = 0.0; renderPatternNotes (s0);
    auto sz = s; sz.bars = 0; for (auto& n : renderPatternNotes (sz)) CHECK (n.endTick <= 1920, "0 bars is treated as 1 bar");
    for (int steps : { 1, 2, 7, 16, 32 })
        for (int rate : { 0, 1, 2, 3, 4, 5 })
        {
            auto sx = s; sx.steps = steps; sx.rate = rate;
            for (auto& n : renderPatternNotes (sx)) CHECK (n.endTick <= sx.bars * 1920 && n.endTick > n.startTick, "steps %d rate %d note out of range", steps, rate);
        }
}

int main()
{
    testSingleNote();
    testChordTogetherAndSpread();
    testHeldNotes();
    testMaxFive();
    testAcid();
    testCommandTimingClamp();
    testHostileInput();
    testExport();
    if (failures == 0) std::printf ("direct_test: all %lld checks passed\n", checks);
    else std::printf ("direct_test: %d of %lld checks FAILED\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
