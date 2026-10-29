// Drives the real DubStabAudioProcessor without a host or a window.
// Step 1: it is an instrument with MIDI in, no audio in, stereo out, silent, and its state can be saved and loaded.
// Run: build/plugin_test
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s\n", msg); } } while (0)

int main()
{
    DubStabAudioProcessor p;
    CHECK (p.getName() == "Unofficial Dub Stab Plaits", "plug-in name");
    CHECK (p.acceptsMidi(), "accepts MIDI");
    CHECK (! p.producesMidi(), "does not produce MIDI");
    CHECK (p.getTotalNumInputChannels() == 0, "no audio input");
    CHECK (p.getTotalNumOutputChannels() == 2, "stereo output");

    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    bool allZero = true, finite = true;
    for (int b = 0; b < 20; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 3) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                if (v != 0.0f) allZero = false;
            }
    }
    CHECK (finite, "output finite");
    CHECK (allZero, "scaffold is silent");

    juce::MemoryBlock state;
    p.getStateInformation (state);
    CHECK (state.getSize() > 0, "state not empty");
    p.setStateInformation (state.getData(), (int) state.getSize());

    std::printf (failures == 0 ? "plugin_test: all passed\n" : "plugin_test: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
