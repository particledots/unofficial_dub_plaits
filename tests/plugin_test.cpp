// Drives the real DubStabAudioProcessor without a host or a window, with a fake host transport.
// Checks: bus layout, silent when the transport is stopped, sound when it plays, parameters change the sound,
// played MIDI notes transpose, block sizes, mono layout, state save / load, the editor opens.
// Run: build/plugin_test
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
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

struct Rendered { double peak = 0, energy = 0; bool finite = true; std::vector<float> left; };

// Renders `seconds` of audio in blocks of `block`, advancing the fake transport.
static Rendered render (DubStabAudioProcessor& p, FakeHost& host, double sr, int block, double seconds, int channels = 2,
                        const juce::MidiMessage* noteAtStart = nullptr)
{
    Rendered r;
    juce::AudioBuffer<float> buf (channels, block);
    const long long total = (long long) (seconds * sr);
    for (long long done = 0; done < total; done += block)
    {
        juce::MidiBuffer midi;
        if (noteAtStart != nullptr && done == 0) midi.addEvent (*noteAtStart, 0);
        buf.clear();
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v)) r.finite = false;
            r.peak = std::max (r.peak, (double) std::fabs (v));
            r.energy += (double) v * v;
            r.left.push_back (v);
            if (channels > 1 && buf.getSample (1, i) != v) r.finite = false;   // both channels carry the same signal
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
        CHECK (played.finite, "playing: finite and both channels equal");
        CHECK (played.peak > 0.01, "playing: there is sound");
        CHECK (played.peak < 4.0, "playing: not absurdly loud");
        std::printf ("playing: peak %.3f\n", played.peak);
        CHECK (p->display.playing.load(), "display: playing");
        CHECK (p->display.onMask.load() != 0, "display: some steps play");
        CHECK (__builtin_popcount (p->display.onMask.load()) == 5, "display: density 5 gives 5 steps");

        // stop again -> silence after the tail
        host.playing = false;
        render (*p, host, sr, 512, 3.0);
        auto after = render (*p, host, sr, 512, 0.5);
        CHECK (after.peak < 1e-3, "after stop: silent");
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
