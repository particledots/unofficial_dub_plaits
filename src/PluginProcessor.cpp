#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Version.h"
#include <algorithm>
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout DubStabAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def)
    { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def)); };
    auto num = [&] (const char* id, const char* name, int lo, int hi, int def)
    { p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto toggle = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    StringArray models;
    for (int i = 0; i < dub::kNumSynthModels; ++i) models.add (dub::synthModelName (i));
    choice ("model", "Model", models, 0);
    flt ("harmonics", "Harmonics", 0.f, 1.f, 0.5f);
    flt ("timbre", "Timbre", 0.f, 1.f, 0.5f);
    flt ("morph", "Morph", 0.f, 1.f, 0.5f);
    flt ("envelope", "Envelope", 0.f, 1.f, 0.5f);
    flt ("resonance", "Resonance", 0.f, 1.f, 0.35f);
    flt ("drift", "Drift", 0.f, 1.f, 0.15f);
    flt ("volume", "Volume", 0.f, 1.5f, 1.0f);

    choice ("root", "Root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    choice ("scale", "Scale", { "Natural minor", "Dorian", "Phrygian", "Harmonic minor" }, 0);
    toggle ("minor_only", "Minor chords only", false);
    choice ("voicing", "Voicing", { "Dub", "Triads", "Sevenths", "Mixed", "Deep" }, 0);
    toggle ("chords", "Chords", true);
    num ("density", "Density", 0, 16, 5);
    num ("steps", "Steps", 1, 16, 16);
    choice ("rate", "Rate", { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" }, 4);
    num ("octave", "Octave", 1, 5, 3);
    num ("seed", "Seed", 0, 99999, 1);
    num ("melody", "Melody", 0, 99999, 0);
    return { p.begin(), p.end() };
}

DubStabAudioProcessor::DubStabAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pModel = get ("model"); pHarmonics = get ("harmonics"); pTimbre = get ("timbre"); pMorph = get ("morph");
    pEnvelope = get ("envelope"); pResonance = get ("resonance"); pDrift = get ("drift"); pVolume = get ("volume");
    pRoot = get ("root"); pScale = get ("scale"); pMinor = get ("minor_only"); pVoicing = get ("voicing");
    pChords = get ("chords"); pDensity = get ("density"); pSteps = get ("steps"); pRate = get ("rate");
    pOctave = get ("octave"); pSeed = get ("seed"); pMelody = get ("melody");
    seq.setMode (dub::Mode::Both);
    timed.reserve (256);
}

void DubStabAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlock = std::max (samplesPerBlock, 16);
    scratch.assign ((size_t) maxBlock, 0.0f);
    synth.prepare (sampleRate, maxBlock);
    synth.reset();
    clock.reset();
    seq.state (0);
    seq.events().clear();
    setLatencySamples (synth.latencyInHostSamples());
}

bool DubStabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

dub::SynthParams DubStabAudioProcessor::readSynthParams() const
{
    dub::SynthParams s;
    s.model = std::clamp ((int) std::lround (pModel->load()), 0, dub::kNumSynthModels - 1);
    s.harmonics = pHarmonics->load(); s.timbre = pTimbre->load(); s.morph = pMorph->load();
    s.envelope = pEnvelope->load(); s.resonance = pResonance->load(); s.drift = pDrift->load();
    s.gain = pVolume->load();
    return s;
}

void DubStabAudioProcessor::applySequencerParams()
{
    // The setters do nothing when the value has not changed, so this is cheap to call every block.
    seq.setModel (std::clamp ((int) std::lround (pModel->load()), 0, dub::kNumSynthModels - 1));
    seq.setRoot (pRoot->load()); seq.setScale (pScale->load()); seq.setMinor (pMinor->load());
    seq.setChords (pChords->load()); seq.setDensity (pDensity->load()); seq.setSteps (pSteps->load());
    seq.setRate (pRate->load()); seq.setOctave (pOctave->load());
    seq.setVoicing (pVoicing->load());
    seq.setSeed (pSeed->load()); seq.setMelody (pMelody->load());
}

void DubStabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    buffer.clear();
    if (n == 0 || channels == 0) { midi.clear(); return; }

    // Host transport
    dub::HostBlock hb;
    hb.sampleRate = currentRate;
    hb.numSamples = n;
    hb.positionValid = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            hb.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) hb.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { hb.ppqStart = *ppq; hb.positionValid = true; }
        }
    seq.setTempo (hb.bpm);

    applySequencerParams();

    // Played notes transpose the pattern (note-on with velocity 0 is a note-off and is ignored)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getVelocity() > 0) seq.noteIn (m.getNoteNumber());
    }
    midi.clear();

    // Sequencer -> synth
    timed.clear();
    clock.process (seq, hb, timed);
    for (auto& te : timed) synth.addEvent (std::clamp (te.sample, 0, n - 1), te.ev);
    seq.events().clear();

    // Synth in pieces no larger than the size given to prepare(), in case a host sends bigger blocks
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto sp = readSynthParams();
    int done = 0;
    while (done < n)
    {
        const int chunk = std::min (maxBlock, n - done);
        synth.process (left + done, right != nullptr ? right + done : scratch.data(), chunk, sp);
        done += chunk;
    }

    // For the editor
    const auto& P = seq.pattern();
    const auto& S = seq.settings();
    unsigned on = 0, acc = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (P.rank[(size_t) i] < S.density) on |= 1u << i;
        if (P.accent[(size_t) i]) acc |= 1u << i;
    }
    display.onMask.store (on); display.accentMask.store (acc);
    display.length.store (std::clamp (S.length, 1, 16));
    display.transpose.store (S.transpose);
    const bool running = hb.playing && hb.positionValid;
    display.playing.store (running);
    if (running && hb.ppqStart >= 0)
    {
        const long long step = (long long) std::floor (hb.ppqStart * 480.0 / dub::DubSequencer::rateTicks (S.rate));
        display.currentStep.store ((int) (step % std::clamp (S.length, 1, 16)));
    }
    else display.currentStep.store (-1);
}

juce::AudioProcessorEditor* DubStabAudioProcessor::createEditor() { return new DubStabEditor (*this); }

void DubStabAudioProcessor::setIntParameter (const juce::String& id, int value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
        prm->endChangeGesture();
    }
}

int DubStabAudioProcessor::randomSeedValue() { return 1 + rng.nextInt (99999); }

void DubStabAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 2, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void DubStabAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DubStabAudioProcessor(); }
