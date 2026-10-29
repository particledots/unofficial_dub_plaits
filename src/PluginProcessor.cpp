#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Version.h"

DubStabAudioProcessor::DubStabAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void DubStabAudioProcessor::prepareToPlay (double, int) {}

bool DubStabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DubStabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();   // silent for now
}

juce::AudioProcessorEditor* DubStabAudioProcessor::createEditor() { return new DubStabEditor (*this); }

void DubStabAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (1);   // state format version
}

void DubStabAudioProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DubStabAudioProcessor(); }
