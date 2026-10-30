// DubSynth: the eight sound models of DUB STAB PLAITS, driven by the events of DubSequencer.
//
//   0 DUB SAW
//   1 VA           |  Narti Live's PolySynth from the firmware (third_party/plaits/dub/dub.cc, used as published):
//   2 WAVESHAPE    |  six voices, one filter and envelope for the whole chord
//   3 FM           |
//   4 PHASE DIST  /
//   5 CHIPTUNE      the firmware's published model 6 slot (CHIPTUNE). Stand-in for VHIKK, which is not in any published
//                   source; it will be replaced when VHIKK is written (plan step 6).
//   6 STRINGS       the real Plaits string machine engine (Emilie Gillet) playing the sequencer's chord, behind
//                   Narti Live's envelope / filter (PostShaper)
//   7 ACID          mono acid bass, ported from the gen~ code of the Max for Live devices (AcidVoice)
//
// Like the firmware, everything runs at the module's sample rate (47872.34 Hz) in blocks of 12 samples and is converted to
// the host rate by a windowed-sinc resampler. Output is mono (copied to both channels).
//
// Threading: prepare()/reset() may allocate; addEvent() and process() do not (within the sizes given to prepare()).
#pragma once
#include "DubSequencer.h"
#include <memory>
#include <vector>

namespace dub {

enum SynthModel { kDubSaw = 0, kVA, kWaveshape, kFM, kPhaseDist, kChiptune, kStrings, kAcid, kNumSynthModels };
const char* synthModelName (int model);

struct SynthParams
{
    int model = kDubSaw;      // 0..7, see above
    float harmonics = 0.5f;   // 0..1  (meaning per model, see the manual of the module)
    float timbre = 0.5f;
    float morph = 0.5f;
    float envelope = 0.5f;    // the FREQUENCY knob: short pluck -> dub stab -> long swell (ACID: filter envelope amount)
    float resonance = 0.35f;  // 0..1, capped so that the filter never self-oscillates
    float drift = 0.15f;      // 0..1, the slow random LFO on harmonics / timbre / morph / cutoff (0.15 = as on the module)
    float gain = 1.0f;        // linear output gain
};

class DubSynth
{
public:
    static constexpr double kEngineRate = 47872.34;   // plaits::kCorrectedSampleRate
    static constexpr int kEngineBlock = 12;           // plaits::kBlockSize

    DubSynth();
    ~DubSynth();
    DubSynth (const DubSynth&) = delete;
    DubSynth& operator= (const DubSynth&) = delete;

    // maxBlock: the largest number of host samples one process() call will be given.
    void prepare (double hostSampleRate, int maxBlock);
    void reset();                                  // silence, as after power-up (keeps the settings of prepare())

    // Sequencer events (Mode::Both: Gen and AcidLen are used, the rest is ignored).
    // hostSample = position inside the buffer the next process() call renders; call in time order, before process().
    void addEvent (int hostSample, const Event& e);

    // Renders numSamples into outL and outR (the same mono signal). Events added before are consumed.
    void process (float* outL, float* outR, int numSamples, const SynthParams& p);

    // Delay between an event and the first sound it makes, in host samples (report it to the host).
    int latencyInHostSamples() const;

    // For tests and diagnostics.
    int activeVoices() const;            // poly models: voices still sounding
    long long nonFiniteSamples() const;  // number of NaN / infinite samples that were replaced by 0 (must stay 0)
    long long lateEvents() const;        // events that could not be applied at their time (must stay 0)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dub
