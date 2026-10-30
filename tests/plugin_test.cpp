// Drives the real DubStabAudioProcessor without a host or a window, with a fake host transport.
// Checks: bus layout, silent when the transport is stopped, sound when it plays, parameters change the sound,
// played MIDI notes transpose, block sizes, mono layout, state save / load, the editor opens.
// Run: build/plugin_test
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <vector>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s\n", msg); } } while (0)

struct FakeHost : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying (playing);
        i.setBpm (bpm);
        i.setPpqPosition (ppq);
        return i;
    }
};

static bool reseed = true;
struct Rendered { double peak = 0, energy = 0; bool finite = true; std::vector<float> left, right; };

// Renders `seconds` of audio in blocks of `block`, advancing the fake transport.
static Rendered render (DubStabAudioProcessor& p, FakeHost& host, double sr, int block, double seconds, int channels = 2,
                        const juce::MidiMessage* noteAtStart = nullptr, const juce::MidiMessage* noteOffAt = nullptr, double offSeconds = 0.0)
{
    Rendered r;
    if (reseed) stmlib::Random::Seed (1);       // the drift uses one global random generator for all instances
    juce::AudioBuffer<float> buf (channels, block);
    const long long total = (long long) (seconds * sr);
    for (long long done = 0; done < total; done += block)
    {
        juce::MidiBuffer midi;
        if (noteAtStart != nullptr && done == 0) midi.addEvent (*noteAtStart, 0);
        if (noteOffAt != nullptr && done <= (long long) (offSeconds * sr) && (long long) (offSeconds * sr) < done + block) midi.addEvent (*noteOffAt, 0);
        buf.clear();
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v)) r.finite = false;
            r.peak = std::max (r.peak, (double) std::fabs (v));
            r.energy += (double) v * v;
            r.left.push_back (v);
            if (channels > 1)
            {
                const float w = buf.getSample (1, i);
                if (! std::isfinite (w)) r.finite = false;
                r.right.push_back (w);
            }
        }
        if (host.playing) host.ppq += block / sr * host.bpm / 60.0;
    }
    return r;
}

static void setParam (DubStabAudioProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

static double diff (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) d += std::fabs ((double) a[i] - b[i]);
    return d;
}

static std::unique_ptr<DubStabAudioProcessor> make (FakeHost& host, double sr, int block, int outs = 2)
{
    stmlib::Random::Seed (1);                   // the drift uses one global random generator for all instances
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto* raw = static_cast<DubStabAudioProcessor*> (base.release());
    std::unique_ptr<DubStabAudioProcessor> p (raw);
    p->setPlayConfigDetails (0, outs, sr, block);
    p->setPlayHead (&host);
    p->prepareToPlay (sr, block);
    return p;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;                         // JUCE needs its message manager, like inside a host
    const double sr = 48000.0;

    {   // --- basics and silence while stopped ---
        FakeHost host;
        auto p = make (host, sr, 512);
        CHECK (p->getName() == "Unofficial Dub Stab Plaits", "plug-in name");
        CHECK (p->acceptsMidi(), "accepts MIDI");
        CHECK (! p->producesMidi(), "does not produce MIDI");
        CHECK (p->getTotalNumInputChannels() == 0, "no audio input");
        CHECK (p->getTotalNumOutputChannels() == 2, "stereo output");
        CHECK (p->getLatencySamples() > 0 && p->getLatencySamples() < 1000, "latency reported");

        auto stopped = render (*p, host, sr, 512, 1.0);
        CHECK (stopped.finite, "stopped: finite");
        CHECK (stopped.peak == 0.0, "stopped transport is silent");
        CHECK (p->display.currentStep.load() == -1, "stopped: no current step");

        // --- plays ---
        host.playing = true;
        auto played = render (*p, host, sr, 512, 8.0);
        CHECK (played.finite, "playing: finite");
        CHECK (played.peak > 0.01, "playing: there is sound");
        CHECK (played.peak < 4.0, "playing: not absurdly loud");
        std::printf ("playing: peak %.3f\n", played.peak);
        CHECK (p->display.playing.load(), "display: playing");
        CHECK (p->display.onMask.load() != 0, "display: some steps play");
        CHECK (__builtin_popcount (p->display.onMask.load()) == 5, "display: density 5 gives 5 steps");

        // stop again -> silence after the tail
        host.playing = false;
        render (*p, host, sr, 512, 30.0);                   // the echo and reverb tails fade out
        auto after = render (*p, host, sr, 512, 0.5);
        CHECK (after.peak < 1e-3, "after stop: silent once the tails are gone");
    }

    {   // --- every model makes sound; parameters change the sound ---
        for (int m = 0; m < dub::kNumSynthModels; ++m)
        {
            FakeHost host; host.playing = true;
            auto p = make (host, sr, 256);
            setParam (*p, "model", (float) m);
            auto r = render (*p, host, sr, 256, 6.0);
            char msg[96]; std::snprintf (msg, sizeof msg, "model %d (%s): finite", m, dub::synthModelName (m));
            CHECK (r.finite, msg);
            std::snprintf (msg, sizeof msg, "model %d (%s): has sound", m, dub::synthModelName (m));
            CHECK (r.peak > 0.005, msg);
        }
        FakeHost h1, h2; h1.playing = h2.playing = true;
        auto a = make (h1, sr, 256), b = make (h2, sr, 256);
        setParam (*b, "timbre", 0.9f);
        auto ra = render (*a, h1, sr, 256, 3.0), rb = render (*b, h2, sr, 256, 3.0);
        CHECK (diff (ra.left, rb.left) > 1.0, "timbre changes the sound");
        FakeHost h3, h4; h3.playing = h4.playing = true;
        auto c = make (h3, sr, 256), d = make (h4, sr, 256);
        setParam (*d, "seed", 777.0f);
        auto rc = render (*c, h3, sr, 256, 3.0), rd = render (*d, h4, sr, 256, 3.0);
        CHECK (diff (rc.left, rd.left) > 1.0, "seed changes the pattern");
        CHECK (c->display.onMask.load() != d->display.onMask.load() || c->display.accentMask.load() != d->display.accentMask.load(),
               "seed changes the displayed pattern");
    }

    {   // --- a played note transposes the pattern ---
        FakeHost h1, h2; h1.playing = h2.playing = true;
        auto a = make (h1, sr, 256), b = make (h2, sr, 256);
        auto note = juce::MidiMessage::noteOn (1, 67, 0.8f);
        auto ra = render (*a, h1, sr, 256, 3.0), rb = render (*b, h2, sr, 256, 3.0, 2, &note);
        CHECK (diff (ra.left, rb.left) > 1.0, "a played note transposes the pattern");
        CHECK (b->display.transpose.load() != 0, "display shows the transpose");
        auto home = juce::MidiMessage::noteOn (1, 60, 0.8f);
        render (*b, h2, sr, 256, 0.1, 2, &home);
        CHECK (b->display.transpose.load() == 0, "C3 (60) = no transpose");
    }

    {   // --- block size invariance of the pattern (the display step follows the transport) and odd sizes ---
        for (int block : { 1, 7, 64, 480, 1024, 4096 })
        {
            FakeHost host; host.playing = true;
            auto p = make (host, sr, block);
            auto r = render (*p, host, sr, block, 2.0);
            char msg[96]; std::snprintf (msg, sizeof msg, "block %d: finite and sound", block);
            CHECK (r.finite && r.peak > 0.0, msg);
        }
        FakeHost host; host.playing = true;       // host gives a bigger block than it announced
        auto p = make (host, sr, 128);
        juce::AudioBuffer<float> big (2, 3000);
        juce::MidiBuffer m;
        p->processBlock (big, m);
        bool fin = true; for (int i = 0; i < 3000; ++i) if (! std::isfinite (big.getSample (0, i))) fin = false;
        CHECK (fin, "block bigger than announced: finite");
    }

    {   // --- mono layout and other sample rates ---
        FakeHost host; host.playing = true;
        auto p = make (host, sr, 256, 1);
        auto r = render (*p, host, sr, 256, 3.0, 1);
        CHECK (r.finite && r.peak > 0.005, "mono output works");
        for (double rate : { 44100.0, 96000.0 })
        {
            FakeHost h; h.playing = true;
            auto q = make (h, rate, 256);
            auto rr = render (*q, h, rate, 256, 3.0);
            char msg[64]; std::snprintf (msg, sizeof msg, "%.0f Hz: finite and sound", rate);
            CHECK (rr.finite && rr.peak > 0.005, msg);
        }
    }

    {   // --- state ---
        FakeHost host;
        auto p = make (host, sr, 512);
        setParam (*p, "model", 6); setParam (*p, "seed", 4242); setParam (*p, "melody", 99); setParam (*p, "timbre", 0.8f);
        setParam (*p, "root", 7); setParam (*p, "density", 9); setParam (*p, "chords", 0);
        juce::MemoryBlock state;
        p->getStateInformation (state);
        CHECK (state.getSize() > 0, "state not empty");

        auto q = make (host, sr, 512);
        q->setStateInformation (state.getData(), (int) state.getSize());
        CHECK ((int) *q->apvts.getRawParameterValue ("model") == 6, "state: model");
        CHECK ((int) *q->apvts.getRawParameterValue ("seed") == 4242, "state: seed");
        CHECK ((int) *q->apvts.getRawParameterValue ("melody") == 99, "state: melody");
        CHECK ((int) *q->apvts.getRawParameterValue ("root") == 7, "state: root");
        CHECK ((int) *q->apvts.getRawParameterValue ("density") == 9, "state: density");
        CHECK ((int) *q->apvts.getRawParameterValue ("chords") == 0, "state: chords off");
        CHECK (std::fabs (*q->apvts.getRawParameterValue ("timbre") - 0.8f) < 1e-4f, "state: timbre");

        q->setStateInformation ("garbage", 7);                                     // bad data must not crash or change anything
        CHECK ((int) *q->apvts.getRawParameterValue ("seed") == 4242, "bad state data is ignored");
        q->setStateInformation (nullptr, 0);
    }


    {   // --- the effects: stereo, tails, dry preset ---
        FakeHost host; host.playing = true;
        auto p = make (host, sr, 512);
        auto r = render (*p, host, sr, 512, 6.0);
        CHECK (diff (r.left, r.right) > 1.0, "default echo/reverb give a stereo signal");
        host.playing = false;
        auto tail = render (*p, host, sr, 512, 1.0);
        CHECK (tail.peak > 1e-4, "echo/reverb ring on after the transport stops");

        FakeHost h2; h2.playing = true;
        auto q = make (h2, sr, 512);
        setParam (*q, "echo_mix", 0); setParam (*q, "reverb_mix", 0);
        auto rq = render (*q, h2, sr, 512, 3.0);
        CHECK (rq.right.size() == rq.left.size() && diff (rq.left, rq.right) < 1e-3, "no effects, width 100 %: both channels equal");
        h2.playing = false;
        render (*q, h2, sr, 512, 10.0);                         // the last stabs ring out
        auto z = render (*q, h2, sr, 512, 0.5);
        CHECK (z.peak < 1e-6, "no effects: silent once the last stabs have rung out");

        FakeHost h3; h3.playing = true;
        auto w = make (h3, sr, 512);
        setParam (*w, "echo_mix", 0); setParam (*w, "reverb_mix", 0); setParam (*w, "width", 0);
        auto rw = render (*w, h3, sr, 512, 3.0);
        CHECK (diff (rw.left, rw.right) < 1e-3, "width 0: mono");
        FakeHost h4; h4.playing = true;
        auto w2 = make (h4, sr, 512);
        setParam (*w2, "echo_mix", 0); setParam (*w2, "reverb_mix", 0); setParam (*w2, "width", 2);
        auto rw2 = render (*w2, h4, sr, 512, 3.0);
        CHECK (diff (rw2.left, rw2.right) > 1.0, "width 200 %: wide");
    }

    {   // --- two identical plug-ins make identical sound ---
        for (int m = 0; m < dub::kNumSynthModels; ++m)
        {
            FakeHost h1, h2; h1.playing = h2.playing = true;
            auto a = make (h1, sr, 256), b = make (h2, sr, 256);
            setParam (*a, "model", (float) m); setParam (*b, "model", (float) m);
            auto ra = render (*a, h1, sr, 256, 3.0), rb = render (*b, h2, sr, 256, 3.0);
            size_t bad = 0; while (bad < ra.left.size() && ra.left[bad] == rb.left[bad]) ++bad;
            char msg[96]; std::snprintf (msg, sizeof msg, "model %d: identical plug-ins differ (first at sample %zu of %zu)", m, bad, ra.left.size());
            CHECK (bad == ra.left.size(), msg);
        }
    }

    {   // --- the sub-bass sine ---
        for (int m : { 0, 1, 2, 3, 4, 5 })
        {
            FakeHost h1, h2; h1.playing = h2.playing = true;
            auto a = make (h1, sr, 256), b = make (h2, sr, 256);
            setParam (*a, "model", (float) m); setParam (*b, "model", (float) m);
            setParam (*a, "echo_mix", 0); setParam (*a, "reverb_mix", 0);
            setParam (*b, "echo_mix", 0); setParam (*b, "reverb_mix", 0); setParam (*b, "sub", 1.0f);
            auto ra = render (*a, h1, sr, 256, 4.0), rb = render (*b, h2, sr, 256, 4.0);
            char msg[96]; std::snprintf (msg, sizeof msg, "model %d: sub adds sound (diff %.1f)", m, diff (ra.left, rb.left));
            CHECK (rb.finite && diff (ra.left, rb.left) > 1.0 && rb.peak < 4.0, msg);
        }
        for (int m : { 6, 7 })                                   // STRINGS and ACID have no sub: the knob changes nothing
        {
            FakeHost h1, h2; h1.playing = h2.playing = true;
            auto a = make (h1, sr, 256), b = make (h2, sr, 256);
            setParam (*a, "model", (float) m); setParam (*b, "model", (float) m); setParam (*b, "sub", 1.0f);
            auto ra = render (*a, h1, sr, 256, 3.0), rb = render (*b, h2, sr, 256, 3.0);
            char msg[64]; std::snprintf (msg, sizeof msg, "model %d: sub knob changes nothing", m);
            CHECK (diff (ra.left, rb.left) < 1e-3, msg);
        }
    }

    {   // --- free-run: plays at its own tempo while the host is stopped ---
        FakeHost host;                                          // stopped
        auto p = make (host, sr, 256);
        auto off = render (*p, host, sr, 256, 2.0);
        CHECK (off.peak == 0.0, "free-run off: silent while stopped");
        setParam (*p, "free_run", 1); setParam (*p, "free_bpm", 140);
        auto on = render (*p, host, sr, 256, 6.0);
        CHECK (on.peak > 0.01 && on.finite, "free-run on: plays while the host is stopped");
        CHECK (p->display.playing.load() && p->display.freeRunning.load(), "display: free-running");
        CHECK (std::fabs (p->display.bpm.load() - 140.0f) < 0.1f, "display: free-run tempo");
        host.playing = true; host.bpm = 100;
        render (*p, host, sr, 256, 0.5);
        CHECK (! p->display.freeRunning.load() && p->display.playing.load(), "the host takes over when it plays");
        CHECK (std::fabs (p->display.bpm.load() - 100.0f) < 0.1f, "display: host tempo");

        // the same pattern, free-run at 120 vs host at 120 from the start: same notes
        FakeHost hf; auto pf = make (hf, sr, 256);
        setParam (*pf, "free_run", 1); setParam (*pf, "free_bpm", 120);
        FakeHost hh; hh.playing = true; hh.bpm = 120; auto ph = make (hh, sr, 256);
        auto rf = render (*pf, hf, sr, 256, 4.0), rh = render (*ph, hh, sr, 256, 4.0);
        size_t firstBad = 0; while (firstBad < rf.left.size() && firstBad < rh.left.size() && std::fabs (rf.left[firstBad] - rh.left[firstBad]) < 1e-4f) ++firstBad;
        std::printf ("free-run vs host: diff %.4f, first different sample %zu of %zu\n", diff (rf.left, rh.left), firstBad, rh.left.size());
        CHECK (diff (rf.left, rh.left) < 1e-2 * std::max (1.0, rh.energy), "free-run at 120 BPM equals the host at 120 BPM from bar 1");
    }

    {   // --- MIDI notes play the sound directly ---
        FakeHost host; host.playing = true;                     // the pattern would play if direct mode did not turn it off
        auto p = make (host, sr, 256);
        setParam (*p, "midi_mode", 1);
        auto none = render (*p, host, sr, 256, 2.0);
        CHECK (none.peak == 0.0, "direct mode: nothing plays without notes (the pattern is off)");
        CHECK (p->display.direct.load(), "display: direct");
        auto n1 = juce::MidiMessage::noteOn (1, 60, 0.8f);
        auto single = render (*p, host, sr, 256, 1.5, 2, &n1);
        CHECK (single.peak > 0.01 && single.finite, "direct: a single note makes sound");

        // a chord of three notes at the same time = one stab, not three
        auto buildChord = [&] (bool together)
        {
            FakeHost h; h.playing = true;
            auto q = make (h, sr, 256);
            setParam (*q, "midi_mode", 1); setParam (*q, "echo_mix", 0); setParam (*q, "reverb_mix", 0);
            juce::AudioBuffer<float> buf (2, 256);
            Rendered r;
            for (int blockNo = 0; blockNo < 200; ++blockNo)
            {
                juce::MidiBuffer midi;
                if (blockNo == 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), together ? 0 : 3);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), together ? 0 : 6);
                }
                buf.clear();
                q->processBlock (buf, midi);
                for (int i = 0; i < 256; ++i) { r.left.push_back (buf.getSample (0, i)); r.peak = std::max (r.peak, (double) std::fabs (buf.getSample (0, i))); }
            }
            return r;
        };
        auto c1 = buildChord (true), c2 = buildChord (false);
        CHECK (c1.peak > 0.01 && c2.peak > 0.01, "direct: a chord makes sound");
        CHECK (c2.peak < c1.peak * 1.6, "direct: notes a few samples apart are one stab, not louder separate ones");

        // different notes give different sound; velocity >= 100 accents
        FakeHost ha, hb; ha.playing = hb.playing = true;
        auto a = make (ha, sr, 256), b = make (hb, sr, 256);
        setParam (*a, "midi_mode", 1); setParam (*b, "midi_mode", 1);
        auto lo = juce::MidiMessage::noteOn (1, 48, 0.8f), hi = juce::MidiMessage::noteOn (1, 72, 0.8f);
        auto ra = render (*a, ha, sr, 256, 1.0, 2, &lo), rb = render (*b, hb, sr, 256, 1.0, 2, &hi);
        CHECK (diff (ra.left, rb.left) > 1.0, "direct: the played note sets the pitch");

        // ACID: gate follows the key
        FakeHost hc; hc.playing = true;
        auto c = make (hc, sr, 256);
        setParam (*c, "midi_mode", 1); setParam (*c, "model", 7); setParam (*c, "echo_mix", 0); setParam (*c, "reverb_mix", 0);
        auto on = juce::MidiMessage::noteOn (1, 45, 0.9f), off = juce::MidiMessage::noteOff (1, 45);
        auto held = render (*c, hc, sr, 256, 1.0, 2, &on, &off, 0.5);
        CHECK (held.peak > 0.01 && held.finite, "direct ACID: sound while the key is down");

        // all notes off / switching back to the pattern leaves nothing hanging
        setParam (*p, "midi_mode", 0);
        auto back = render (*p, host, sr, 256, 6.0);
        CHECK (back.peak > 0.01, "switching back to Transpose: the pattern plays again");
        CHECK (! p->display.direct.load(), "display: not direct");
    }

    {   // --- presets ---
        const auto& list = presets::factory();
        CHECK (list.size() >= 8, "there are factory presets");
        for (size_t i = 0; i < list.size(); ++i)
        {
            FakeHost host; host.playing = true;
            auto p = make (host, sr, 256);
            p->applyFactoryPreset ((int) i);
            auto r = render (*p, host, sr, 256, 5.0);
            char msg[128]; std::snprintf (msg, sizeof msg, "factory preset '%s': finite, has sound, not too loud (peak %.2f)", list[i].name, r.peak);
            CHECK (r.finite && r.peak > 0.005 && r.peak < 4.0, msg);
            CHECK (p->presetName() == list[i].name, "the preset name is remembered");
        }
        // presets leave the setup alone
        FakeHost host; auto p = make (host, sr, 256);
        setParam (*p, "midi_mode", 1); setParam (*p, "free_run", 1); setParam (*p, "free_bpm", 99);
        p->applyFactoryPreset (1);
        CHECK ((int) *p->apvts.getRawParameterValue ("midi_mode") == 1 && (int) *p->apvts.getRawParameterValue ("free_run") == 1
               && (int) *p->apvts.getRawParameterValue ("free_bpm") == 99, "a preset does not change MIDI mode / free-run");

        // save and load a user preset
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dub_preset_test");
        dir.deleteRecursively(); dir.createDirectory();
        p->setPresetFolderForTests (dir);
        setParam (*p, "model", 3); setParam (*p, "timbre", 0.77f); setParam (*p, "seed", 31337); setParam (*p, "echo_time", 5);
        CHECK (p->saveUserPreset ("My / test: sound"), "user preset saved");
        auto files = presets::userFiles (dir);
        CHECK (files.size() == 1, "one user preset file");
        setParam (*p, "model", 0); setParam (*p, "timbre", 0.1f); setParam (*p, "seed", 5);
        if (files.size() == 1) CHECK (p->loadUserPreset (files[0]), "user preset loads");
        CHECK ((int) *p->apvts.getRawParameterValue ("model") == 3 && (int) *p->apvts.getRawParameterValue ("seed") == 31337
               && (int) *p->apvts.getRawParameterValue ("echo_time") == 5
               && std::fabs (*p->apvts.getRawParameterValue ("timbre") - 0.77f) < 1e-3f, "user preset restores the sound");
        CHECK (! p->saveUserPreset ("///"), "a name with nothing usable is refused");
        if (files.size() == 1)
        {
            juce::File bad = dir.getChildFile ("broken.dubstabpreset");
            bad.replaceWithText ("this is not xml");
            CHECK (! p->loadUserPreset (bad), "a broken preset file is refused");
            CHECK ((int) *p->apvts.getRawParameterValue ("model") == 3, "... and changes nothing");
        }
        dir.deleteRecursively();
    }

    {   // --- the pattern as a MIDI file ---
        FakeHost host; auto p = make (host, sr, 256);
        setParam (*p, "seed", 12); setParam (*p, "export_bars", 3);     // 8 bars
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dub_export_test.mid");
        CHECK (p->writePatternMidi (file), "MIDI file written");
        juce::FileInputStream in (file);
        juce::MidiFile mf;
        CHECK (in.openedOk() && mf.readFrom (in), "MIDI file can be read back");
        CHECK (mf.getTimeFormat() == 480, "480 ticks per quarter");
        CHECK (mf.getNumTracks() >= 1, "has a track");
        if (mf.getNumTracks() >= 1)
        {
            auto* t = mf.getTrack (0);
            int ons = 0, offs = 0; double last = 0;
            for (auto* e : *t)
            {
                if (e->message.isNoteOn()) ++ons;
                if (e->message.isNoteOff()) ++offs;
                last = std::max (last, e->message.getTimeStamp());
            }
            CHECK (ons > 0 && ons == offs, "every note on has a note off");
            CHECK (last <= 8 * 1920 + 1, "nothing after the end");
            CHECK (std::fabs (last - 8 * 1920) < 2, "the clip is exactly 8 bars long");
        }
        CHECK (p->patternMidiName().contains ("12"), "the file name has the seed");
        file.deleteFile();
    }

    {   // --- state: the new parameters ---
        FakeHost host; auto p = make (host, sr, 512);
        setParam (*p, "sub", 0.6f); setParam (*p, "echo_mix", 0.55f); setParam (*p, "echo_time", 4); setParam (*p, "reverb_decay", 7.5f);
        setParam (*p, "width", 1.4f); setParam (*p, "midi_mode", 1); setParam (*p, "free_run", 1); setParam (*p, "free_bpm", 133);
        p->setPresetName ("Remembered");
        juce::MemoryBlock state; p->getStateInformation (state);
        auto q = make (host, sr, 512);
        q->setStateInformation (state.getData(), (int) state.getSize());
        auto get = [&] (const char* id) { return (double) *q->apvts.getRawParameterValue (id); };
        CHECK (std::fabs (get ("sub") - 0.6) < 1e-3 && std::fabs (get ("echo_mix") - 0.55) < 1e-3 && (int) get ("echo_time") == 4, "state: sub / echo");
        CHECK (std::fabs (get ("reverb_decay") - 7.5) < 1e-2 && std::fabs (get ("width") - 1.4) < 1e-3, "state: reverb / width");
        CHECK ((int) get ("midi_mode") == 1 && (int) get ("free_run") == 1 && std::fabs (get ("free_bpm") - 133) < 1e-2, "state: MIDI mode / free-run");
        CHECK (q->presetName() == "Remembered", "state: preset name");
        CHECK (q->getNumPrograms() == 1, "one host program");
    }

    {   // --- the editor: every size, the drag file ---
        FakeHost host; host.playing = true;
        auto p = make (host, sr, 512);
        render (*p, host, sr, 512, 0.5);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* e = static_cast<DubStabEditor*> (ed.get());
        for (double scale : { 0.6, 1.0, 1.5, 2.0 })
        {
            const int w = (int) (DubStabEditor::kBaseWidth * scale);
            ed->setSize (w, (int) (DubStabEditor::kBaseHeight * scale));
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds());
            char msg[64]; std::snprintf (msg, sizeof msg, "editor draws at %.0f %%", scale * 100);
            CHECK (img.isValid() && img.getWidth() >= 1, msg);
        }
        CHECK (e->countVisibleControls() > 20, "the controls are visible");
        auto f = e->makeDragFile();
        CHECK (f.existsAsFile() && f.getSize() > 30 && f.hasFileExtension ("mid"), "the drag handle makes a MIDI file");
        f.deleteFile();
        ed->setSize (DubStabEditor::kBaseWidth, DubStabEditor::kBaseHeight);
        juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds());
        if (img.isValid())
        {
            juce::File out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dub_editor_full.png");
            juce::FileOutputStream fos (out);
            fos.setPosition (0); fos.truncate();
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::printf ("editor snapshot: %s\n", out.getFullPathName().toRawUTF8());
        }
        ed.reset();
    }

    {   // --- the editor opens and closes, buttons work ---
        FakeHost host; host.playing = true;
        auto p = make (host, sr, 512);
        render (*p, host, sr, 512, 0.5);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        CHECK (ed != nullptr, "editor created");
        if (ed)
        {
            CHECK (ed->getWidth() > 300 && ed->getHeight() > 200, "editor has a size");
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds());
            CHECK (img.isValid(), "editor draws");
            if (img.isValid()) img.getPixelAt (5, 5);
            if (img.isValid())
            {
                juce::File out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dub_editor.png");
                juce::FileOutputStream fos (out);
                fos.setPosition (0); fos.truncate();
                juce::PNGImageFormat().writeImageToStream (img, fos);
                std::printf ("editor snapshot: %s\n", out.getFullPathName().toRawUTF8());
            }
        }
        const int before = (int) *p->apvts.getRawParameterValue ("seed");
        p->setIntParameter ("seed", p->randomSeedValue());
        CHECK ((int) *p->apvts.getRawParameterValue ("seed") != before || before == 0, "NEW PATTERN changes the seed");
        ed.reset();
    }

    std::printf (failures == 0 ? "plugin_test: all passed\n" : "plugin_test: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
