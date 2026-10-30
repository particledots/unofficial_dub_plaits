#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "core/DubSequencer.h"
#include "core/TransportClock.h"
#include "core/DubSynth.h"
#include "core/DirectPlay.h"
#include "core/Effects.h"
#include "core/PatternExport.h"

// Unofficial Dub Stab Plaits: the DUB STAB sequencer (DubSequencer) follows the host transport (TransportClock), or a
// free-running clock, and plays the sound models (DubSynth); the built-in echo / reverb / width (DubFx) come after.
// Played MIDI notes either transpose the pattern (C3 = 60 = no change) or play the sound directly (DirectPlay).
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
    double getTailLengthSeconds() const override { return dub::DubFx::maxTailSeconds(); }

    // One host program only: the presets are in the editor's PRESETS menu. (A host that lists programs may call
    // setCurrentProgram when it loads a project, which would overwrite the saved state.)
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
        std::atomic<int> currentStep { -1 };      // -1 when nothing is running
        std::atomic<bool> playing { false };      // the sequencer runs (host transport or free-running)
        std::atomic<bool> freeRunning { false };  // ... on its own clock because the host is stopped
        std::atomic<bool> direct { false };       // MIDI notes play the sound directly (the pattern is off)
        std::atomic<int> transpose { 0 };         // scale degrees, from the last played note
        std::atomic<float> bpm { 120.0f };        // the tempo in use (host, or the free-running tempo)
    } display;

    // Sets a whole-number parameter from the editor (goes through the host, so automation and undo see it).
    void setIntParameter (const juce::String& id, int value);
    int randomSeedValue();

    // --- presets ---
    void applyFactoryPreset (int index);
    bool saveUserPreset (const juce::String& name);                 // into presets::userFolder()
    bool loadUserPreset (const juce::File& file);
    juce::String presetName() const;                                 // the last preset chosen (saved with the project)
    void setPresetName (const juce::String&);
    void setPresetFolderForTests (const juce::File& f) { presetFolder = f; }
    juce::File userPresetFolder() const;

    // --- pattern as a MIDI file (drag into the host / save) ---
    dub::ExportSettings exportSettings() const;
    bool writePatternMidi (const juce::File& file) const;
    juce::String patternMidiName() const;                            // e.g. "Dub Stab seed 1" (no extension)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dub::SynthParams readSynthParams() const;
    dub::FxParams readFxParams (double bpm) const;
    void applySequencerParams();

    dub::DubSequencer seq;
    dub::TransportClock clock;
    dub::DubSynth synth;
    dub::DirectPlay directPlay;
    dub::DubFx fx;
    std::vector<dub::TimedEvent> timed;
    std::vector<dub::DirectCmd> directOut;
    std::vector<float> dryBuf, auxBuf, fxL, fxR;
    double currentRate = 48000.0;
    int maxBlock = 512;
    long long hostAbs = 0;              // host samples since prepareToPlay (time base of DirectPlay)
    double freePpq = 0.0;               // position of the free-running clock, in quarter notes
    double lastHostBpm = 120.0;
    bool wasDirect = false;
    std::atomic<int> transposeNote { -1 };
    juce::Random rng;
    juce::File presetFolder;

    std::atomic<float>* pModel; std::atomic<float>* pHarmonics; std::atomic<float>* pTimbre; std::atomic<float>* pMorph;
    std::atomic<float>* pEnvelope; std::atomic<float>* pResonance; std::atomic<float>* pDrift; std::atomic<float>* pVolume;
    std::atomic<float>* pSub;
    std::atomic<float>* pEchoMix; std::atomic<float>* pEchoTime; std::atomic<float>* pEchoFb; std::atomic<float>* pEchoTone;
    std::atomic<float>* pReverbMix; std::atomic<float>* pReverbDecay; std::atomic<float>* pWidth;
    std::atomic<float>* pRoot; std::atomic<float>* pScale; std::atomic<float>* pMinor; std::atomic<float>* pVoicing;
    std::atomic<float>* pChords; std::atomic<float>* pDensity; std::atomic<float>* pSteps; std::atomic<float>* pRate;
    std::atomic<float>* pOctave; std::atomic<float>* pSeed; std::atomic<float>* pMelody;
    std::atomic<float>* pMidiMode; std::atomic<float>* pFreeRun; std::atomic<float>* pFreeBpm;
    std::atomic<float>* pExportBars; std::atomic<float>* pExportLen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubStabAudioProcessor)
};
