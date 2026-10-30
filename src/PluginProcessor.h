#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "core/DubSequencer.h"
#include "core/TransportClock.h"
#include "core/DubSynth.h"

// Unofficial Dub Stab Plaits: the DUB STAB sequencer (DubSequencer) follows the host transport (TransportClock) and plays
// the sound models (DubSynth). Played MIDI notes transpose the pattern (C3 = 60 = no change), they do not play directly.
class DubStabAudioProcessor : public juce::AudioProcessor
{
public:
    DubStabAudioProcessor();
    ~DubStabAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // What the editor shows (written by the audio thread, read by the editor's timer).
    struct Display
    {
        std::atomic<unsigned> onMask { 0 };       // bit i: step i plays
        std::atomic<unsigned> accentMask { 0 };   // bit i: step i is accented
        std::atomic<int> length { 16 };
        std::atomic<int> currentStep { -1 };      // -1 when the transport is stopped
        std::atomic<bool> playing { false };
        std::atomic<int> transpose { 0 };         // scale degrees, from the last played note
    } display;

    // Sets a whole-number parameter from the editor (goes through the host, so automation and undo see it).
    void setIntParameter (const juce::String& id, int value);
    int randomSeedValue();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dub::SynthParams readSynthParams() const;
    void applySequencerParams();

    dub::DubSequencer seq;
    dub::TransportClock clock;
    dub::DubSynth synth;
    std::vector<dub::TimedEvent> timed;
    std::vector<float> scratch;     // right channel sink when the host gives a mono output
    double currentRate = 48000.0;
    int maxBlock = 512;
    juce::Random rng;

    std::atomic<float>* pModel; std::atomic<float>* pHarmonics; std::atomic<float>* pTimbre; std::atomic<float>* pMorph;
    std::atomic<float>* pEnvelope; std::atomic<float>* pResonance; std::atomic<float>* pDrift; std::atomic<float>* pVolume;
    std::atomic<float>* pRoot; std::atomic<float>* pScale; std::atomic<float>* pMinor; std::atomic<float>* pVoicing;
    std::atomic<float>* pChords; std::atomic<float>* pDensity; std::atomic<float>* pSteps; std::atomic<float>* pRate;
    std::atomic<float>* pOctave; std::atomic<float>* pSeed; std::atomic<float>* pMelody;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubStabAudioProcessor)
};
