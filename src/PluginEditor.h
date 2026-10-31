#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <memory>

class DubStabPanel;

// The window: a fixed-size panel (900 x 780 units) scaled to whatever size the user drags the window to.
class DubStabEditor : public juce::AudioProcessorEditor
{
public:
    explicit DubStabEditor (DubStabAudioProcessor&);
    ~DubStabEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 900, kBaseHeight = 780;

    // What dragging from the DRAG MIDI box would hand to the host (the file is written now). For the tests.
    juce::File makeDragFile();
    // The About / credits panel (the ABOUT button opens it). For the tests.
    void showAbout (bool show);
    bool aboutShown() const;
    juce::String aboutTextShown() const;
    // Moves the panel's controls for a test; returns the number of child components that are visible.
    int countVisibleControls() const;

private:
    DubStabAudioProcessor& proc;
    std::unique_ptr<DubStabPanel> panel;
    juce::TooltipWindow tooltipWindow { this, 700 };
};
