#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>

// A plain, working editor: model menu, knobs, sequencer settings, a 16-step display and NEW / NEW MELODY buttons.
class DubStabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DubStabEditor (DubStabAudioProcessor&);
    ~DubStabEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };
    struct Number   // a slider with an editable number box (density, steps, octave, seed, melody)
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const char* id, const char* name);
    void addChoice (Choice&, const char* id, const char* name, const juce::StringArray& items);
    void addNumber (Number&, const char* id, const char* name);
    void updateKnobLabels();
    void timerCallback() override;

    DubStabAudioProcessor& proc;

    Choice model, root, scale, voicing, rate;
    Knob harmonics, timbre, morph, envelope, resonance, drift, volume;
    Number density, steps, octave, seed, melody;
    juce::ToggleButton chords { "CHORDS" }, minorOnly { "MINOR CHORDS ONLY" };
    std::unique_ptr<ButtonAtt> chordsAtt, minorAtt;
    juce::TextButton newPattern { "NEW PATTERN" }, newMelody { "NEW MELODY" }, ownMelody { "ORIGINAL MELODY" };

    juce::Rectangle<int> stepArea, headerArea;
    int shownModel = -1, shownStep = -2;
    unsigned shownOn = 0xffffffff, shownAccent = 0xffffffff;
    int shownLength = -1;
    bool shownPlaying = false;
    int shownTranspose = 0;
    juce::String transportText;
};
