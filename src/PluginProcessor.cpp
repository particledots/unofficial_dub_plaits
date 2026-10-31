#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiFileExport.h"
#include "Presets.h"
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
    auto fltAttr = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, const AudioParameterFloatAttributes& a)
    { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, a)); };
    auto num = [&] (const char* id, const char* name, int lo, int hi, int def)
    { p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto toggle = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    const auto percent = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; });

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
    fltAttr ("sub", "Sub", NormalisableRange<float> (0.f, 1.f), 0.0f, percent);            // the module's AUX sub-bass sine, mixed in

    StringArray echoTimes;
    for (int i = 0; i < dub::kNumEchoTimes; ++i) echoTimes.add (dub::echoTimeName (i));
    dub::FxParams fxDefaults;
    fltAttr ("echo_mix", "Echo Mix", NormalisableRange<float> (0.f, 1.f), fxDefaults.echoMix, percent);
    choice ("echo_time", "Echo Time", echoTimes, fxDefaults.echoTime);
    fltAttr ("echo_fb", "Echo Feedback", NormalisableRange<float> (0.f, 0.95f), fxDefaults.echoFeedback, percent);
    fltAttr ("echo_tone", "Echo Tone", NormalisableRange<float> (0.f, 1.f), fxDefaults.echoTone, percent);
    fltAttr ("reverb_mix", "Reverb Mix", NormalisableRange<float> (0.f, 1.f), fxDefaults.reverbMix, percent);
    fltAttr ("reverb_decay", "Reverb Decay", NormalisableRange<float> (0.3f, 12.0f, 0.01f, 0.45f), fxDefaults.reverbDecay,
             AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })
                                            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); }));
    fltAttr ("width", "Width", NormalisableRange<float> (0.f, 2.f), fxDefaults.width, percent);

    choice ("root", "Root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    choice ("scale", "Scale", { "Natural minor", "Dorian", "Phrygian", "Harmonic minor" }, 0);
    toggle ("minor_only", "Minor chords only", false);
    choice ("voicing", "Voicing", { "Dub", "Triads", "Sevenths", "Mixed", "Deep" }, 0);
    toggle ("chords", "Chords", true);
    num ("density", "Density", 0, 16, 5);
    num ("steps", "Steps", 1, 64, 16);
    choice ("direction", "Direction", { "Forward", "Reverse", "Pendulum", "Ping-pong", "Random", "Random (no repeats)", "Drunk walk", "Chance mix" }, 0);
    {
        dub::Settings d;      // the defaults of the sequencer
        num ("dir_w_fwd", "Chance: Forward", 0, 100, d.dirWeight[dub::ChForward]);
        num ("dir_w_rev", "Chance: Reverse", 0, 100, d.dirWeight[dub::ChReverse]);
        num ("dir_w_pend", "Chance: Pendulum", 0, 100, d.dirWeight[dub::ChPendulum]);
        num ("dir_w_ping", "Chance: Ping-pong", 0, 100, d.dirWeight[dub::ChPingPong]);
        num ("dir_w_rand", "Chance: Random", 0, 100, d.dirWeight[dub::ChRandom]);
        num ("dir_w_drunk", "Chance: Drunk walk", 0, 100, d.dirWeight[dub::ChDrunk]);
        num ("run_min", "Chance: Shortest run", 1, 64, d.runMin);
        num ("run_max", "Chance: Longest run", 1, 64, d.runMax);
    }
    choice ("rate", "Rate", { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" }, 4);
    num ("octave", "Octave", 1, 5, 3);
    num ("seed", "Seed", 0, 99999, 1);
    num ("melody", "Melody", 0, 99999, 0);

    // setup (not part of presets)
    choice ("midi_mode", "MIDI Notes", { "Transpose the pattern", "Play the notes directly" }, 0);
    toggle ("free_run", "Free-run when stopped", false);
    fltAttr ("free_bpm", "Free-run Tempo", NormalisableRange<float> (30.f, 300.f, 0.1f), 120.0f,
             AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " BPM"; })
                                            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); }));
    choice ("export_bars", "MIDI Export Bars", { "1", "2", "4", "8", "16" }, 2);
    choice ("export_len", "MIDI Export Note Length", { "Short (25%)", "Half (50%)", "Full (100%)" }, 1);
    return { p.begin(), p.end() };
}

DubStabAudioProcessor::DubStabAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pModel = get ("model"); pHarmonics = get ("harmonics"); pTimbre = get ("timbre"); pMorph = get ("morph");
    pEnvelope = get ("envelope"); pResonance = get ("resonance"); pDrift = get ("drift"); pVolume = get ("volume");
    pSub = get ("sub");
    pEchoMix = get ("echo_mix"); pEchoTime = get ("echo_time"); pEchoFb = get ("echo_fb"); pEchoTone = get ("echo_tone");
    pReverbMix = get ("reverb_mix"); pReverbDecay = get ("reverb_decay"); pWidth = get ("width");
    pRoot = get ("root"); pScale = get ("scale"); pMinor = get ("minor_only"); pVoicing = get ("voicing");
    pChords = get ("chords"); pDensity = get ("density"); pSteps = get ("steps"); pRate = get ("rate");
    pOctave = get ("octave"); pSeed = get ("seed"); pMelody = get ("melody");
    pDirection = get ("direction");
    pDirW[dub::ChForward] = get ("dir_w_fwd"); pDirW[dub::ChReverse] = get ("dir_w_rev"); pDirW[dub::ChPendulum] = get ("dir_w_pend");
    pDirW[dub::ChPingPong] = get ("dir_w_ping"); pDirW[dub::ChRandom] = get ("dir_w_rand"); pDirW[dub::ChDrunk] = get ("dir_w_drunk");
    pRunMin = get ("run_min"); pRunMax = get ("run_max");
    pMidiMode = get ("midi_mode"); pFreeRun = get ("free_run"); pFreeBpm = get ("free_bpm");
    pExportBars = get ("export_bars"); pExportLen = get ("export_len");
    seq.setMode (dub::Mode::Both);
    timed.reserve (256);
    directOut.reserve (512);
}

void DubStabAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlock = std::max (samplesPerBlock, 16);
    dryBuf.assign ((size_t) maxBlock, 0.0f);
    auxBuf.assign ((size_t) maxBlock, 0.0f);
    fxL.assign ((size_t) maxBlock, 0.0f);
    fxR.assign ((size_t) maxBlock, 0.0f);
    synth.prepare (sampleRate, maxBlock);
    synth.reset();
    fx.prepare (sampleRate, maxBlock);
    directPlay.prepare (sampleRate);
    clock.reset();
    seq.state (0);
    seq.events().clear();
    hostAbs = 0;
    freePpq = 0.0;
    wasDirect = false;
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

dub::FxParams DubStabAudioProcessor::readFxParams (double bpm) const
{
    dub::FxParams f;
    f.echoMix = pEchoMix->load();
    f.echoTime = std::clamp ((int) std::lround (pEchoTime->load()), 0, dub::kNumEchoTimes - 1);
    f.echoFeedback = pEchoFb->load();
    f.echoTone = pEchoTone->load();
    f.reverbMix = pReverbMix->load();
    f.reverbDecay = pReverbDecay->load();
    f.width = pWidth->load();
    f.bpm = bpm;
    return f;
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
    seq.setDirection (pDirection->load());
    for (int m = 0; m < dub::kNumChanceModes; ++m) seq.setDirWeight (m, pDirW[m]->load());
    seq.setRunMin (pRunMin->load()); seq.setRunMax (pRunMax->load());
}

void DubStabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    buffer.clear();
    if (n == 0 || channels == 0) { midi.clear(); return; }

    const bool direct = std::lround (pMidiMode->load()) == 1;
    const int modelIndex = std::clamp ((int) std::lround (pModel->load()), 0, dub::kNumSynthModels - 1);

    // ---- transport: the host's, or the free-running clock when the host is stopped ----
    dub::HostBlock hb;
    hb.sampleRate = currentRate;
    hb.numSamples = n;
    hb.positionValid = false;
    bool hostPlaying = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) if (*bpm > 0) lastHostBpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { hb.ppqStart = *ppq; hb.positionValid = true; }
        }
    hb.playing = hostPlaying;
    hb.bpm = lastHostBpm;
    bool freeActive = false;
    if (direct)
        hb.playing = false;                                  // the pattern is off while notes play the sound directly
    else if (! (hostPlaying && hb.positionValid) && pFreeRun->load() >= 0.5f)
    {
        hb.playing = true;
        hb.positionValid = true;
        hb.ppqStart = freePpq;
        hb.bpm = std::clamp ((double) pFreeBpm->load(), 30.0, 300.0);
        freePpq += (double) n / currentRate * hb.bpm / 60.0;
        freeActive = true;
    }
    if (! freeActive) freePpq = 0.0;                         // the next free run starts from the top of the pattern
    seq.setTempo (hb.bpm);

    applySequencerParams();

    // ---- MIDI notes: transpose the pattern, or play the sound ----
    const long long blockStart = hostAbs;
    if (direct != wasDirect) { directPlay.reset(); wasDirect = direct; }
    directPlay.setModelKind (modelIndex == dub::kAcid, modelIndex == dub::kStrings);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const long long at = blockStart + (long long) std::clamp (meta.samplePosition, 0, n - 1);
        if (direct)
        {
            if (m.isNoteOn()) directPlay.noteOn (at, m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff()) directPlay.noteOff (at, m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) directPlay.allNotesOff (at);
        }
        else if (m.isNoteOn())          // (a note-on with velocity 0 is a note-off and is ignored)
        {
            seq.noteIn (m.getNoteNumber());
            transposeNote.store (m.getNoteNumber());
        }
    }
    midi.clear();

    // ---- sequencer -> synth ----
    timed.clear();
    clock.process (seq, hb, timed);
    for (auto& te : timed) synth.addEvent (std::clamp (te.sample, 0, n - 1), te.ev);
    seq.events().clear();

    // ---- direct play -> synth ----
    directOut.clear();
    directPlay.flush (blockStart, blockStart + n, directOut);
    for (const auto& c : directOut)
    {
        const int at = (int) std::clamp (c.at - blockStart, 0LL, (long long) n - 1);
        if (c.kind == dub::DirectCmd::Chord)
            synth.addChordNotes (at, c.notes.data(), c.count, c.root, c.vel >= 100);
        else if (c.kind == dub::DirectCmd::AcidOff)
        {
            dub::Event g; g.kind = dub::Event::Gen; g.type = 3; g.note = 0; g.vel = 1;
            synth.addEvent (at, g);
        }
        else
        {
            dub::Event len; len.kind = dub::Event::AcidLen; len.ms = 0;       // no automatic gate length: the key holds it
            synth.addEvent (at, len);
            dub::Event g; g.kind = dub::Event::Gen; g.type = c.kind == dub::DirectCmd::AcidSlide ? 2 : 1;
            g.note = c.note; g.vel = c.vel;
            synth.addEvent (at, g);
        }
    }

    // ---- synth + sub-bass -> effects, in pieces no larger than the size given to prepareToPlay ----
    const auto sp = readSynthParams();
    const auto fp = readFxParams (hb.bpm);
    const float sub = std::clamp (pSub->load(), 0.0f, 1.0f);
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    int done = 0;
    while (done < n)
    {
        const int chunk = std::min (maxBlock, n - done);
        synth.process (dryBuf.data(), dryBuf.data(), auxBuf.data(), chunk, sp);
        if (sub > 0.0f)
            for (int i = 0; i < chunk; ++i) dryBuf[(size_t) i] += sub * auxBuf[(size_t) i];
        fx.process (dryBuf.data(), fxL.data(), fxR.data(), chunk, fp);
        for (int i = 0; i < chunk; ++i)
        {
            if (right != nullptr) { left[done + i] = fxL[(size_t) i]; right[done + i] = fxR[(size_t) i]; }
            else left[done + i] = 0.5f * (fxL[(size_t) i] + fxR[(size_t) i]);
        }
        done += chunk;
    }
    hostAbs += n;

    // ---- for the editor ----
    const auto& S = seq.settings();
    uint64_t on = 0, acc = 0;
    for (int i = 0; i < dub::DubSequencer::kMaxSteps; ++i)
    {
        if (seq.stepPlays (i)) on |= (uint64_t) 1 << i;
        if (seq.stepAccented (i)) acc |= (uint64_t) 1 << i;
    }
    display.onMask.store (on); display.accentMask.store (acc);
    display.length.store (std::clamp (S.length, 1, dub::DubSequencer::kMaxSteps));
    display.direction.store (S.direction);
    display.transpose.store (S.transpose);
    display.bpm.store ((float) hb.bpm);
    display.direct.store (direct);
    display.freeRunning.store (freeActive);
    const bool running = hb.playing && hb.positionValid;
    display.playing.store (running);
    if (running && hb.ppqStart >= 0)
    {
        const long long step = (long long) std::floor (hb.ppqStart * 480.0 / dub::DubSequencer::rateTicks (S.rate));
        display.currentStep.store (seq.stepForIndex (step));
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

// ---------------------------------------------------------------- presets

juce::File DubStabAudioProcessor::userPresetFolder() const
{
    return presetFolder != juce::File() ? presetFolder : presets::userFolder();
}

void DubStabAudioProcessor::applyFactoryPreset (int index)
{
    const auto& list = presets::factory();
    if (index < 0 || index >= (int) list.size()) return;
    presets::apply (apvts, list[(size_t) index].values);
    setPresetName (list[(size_t) index].name);
}

bool DubStabAudioProcessor::saveUserPreset (const juce::String& name)
{
    juce::File written;
    if (! presets::saveUser (apvts, name, userPresetFolder(), &written)) return false;
    setPresetName (written.getFileNameWithoutExtension());
    return true;
}

bool DubStabAudioProcessor::loadUserPreset (const juce::File& file)
{
    juce::String name;
    if (! presets::loadUser (apvts, file, &name)) return false;
    setPresetName (name);
    return true;
}

juce::String DubStabAudioProcessor::presetName() const { return apvts.state.getProperty ("presetName", juce::String()).toString(); }
void DubStabAudioProcessor::setPresetName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

// ---------------------------------------------------------------- pattern as a MIDI file

dub::ExportSettings DubStabAudioProcessor::exportSettings() const
{
    dub::ExportSettings s;
    s.root = (int) std::lround (pRoot->load());
    s.scale = (int) std::lround (pScale->load());
    s.minor = pMinor->load() >= 0.5f ? 1 : 0;
    s.voicing = (int) std::lround (pVoicing->load());
    s.density = (int) std::lround (pDensity->load());
    s.steps = (int) std::lround (pSteps->load());
    s.direction = (int) std::lround (pDirection->load());
    for (int m = 0; m < dub::kNumChanceModes; ++m) s.dirWeight[m] = (int) std::lround (pDirW[m]->load());
    s.runMin = (int) std::lround (pRunMin->load()); s.runMax = (int) std::lround (pRunMax->load());
    s.rate = (int) std::lround (pRate->load());
    s.octave = (int) std::lround (pOctave->load());
    s.seed = (int) std::lround (pSeed->load());
    s.melody = (int) std::lround (pMelody->load());
    s.model = std::clamp ((int) std::lround (pModel->load()), 0, dub::kNumSynthModels - 1);
    s.chords = pChords->load() >= 0.5f ? 1 : 0;
    s.transposeNote = transposeNote.load();
    static const double gates[3] = { 0.25, 0.5, 1.0 };
    static const int bars[5] = { 1, 2, 4, 8, 16 };
    s.gate = gates[std::clamp ((int) std::lround (pExportLen->load()), 0, 2)];
    s.bars = bars[std::clamp ((int) std::lround (pExportBars->load()), 0, 4)];
    return s;
}

juce::String DubStabAudioProcessor::patternMidiName() const
{
    const auto s = exportSettings();
    juce::String n = "Dub Stab seed " + juce::String (s.seed);
    if (s.melody != 0) n << " melody " << s.melody;
    return n;
}

bool DubStabAudioProcessor::writePatternMidi (const juce::File& file) const
{
    return writePatternMidiFile (exportSettings(), (double) display.bpm.load(), patternMidiName(), file);
}

// ---------------------------------------------------------------- state

void DubStabAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 3, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void DubStabAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DubStabAudioProcessor(); }
