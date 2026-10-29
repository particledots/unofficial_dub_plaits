#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class DubStabEditor : public juce::AudioProcessorEditor
{
public:
    explicit DubStabEditor (DubStabAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override {}
};
