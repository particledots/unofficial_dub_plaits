#include "PluginEditor.h"
#include "core/Version.h"

DubStabEditor::DubStabEditor (DubStabAudioProcessor& p) : AudioProcessorEditor (p) { setSize (520, 220); }

void DubStabEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f));
    g.drawText ("Unofficial Dub Stab Plaits", getLocalBounds().removeFromTop (90), juce::Justification::centred);
    g.setColour (juce::Colour (0xffb0b6bf));
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (juce::String ("Version ") + dub::versionString() + " - scaffold only: no sound yet.\n"
                      "Not affiliated with Narti Live or Mutable Instruments. See the licenses folder for credits.",
                      getLocalBounds().reduced (24).withTrimmedTop (90), juce::Justification::centredTop, 4);
}
