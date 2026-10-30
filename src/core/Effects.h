// DubFx: the effects built into Unofficial Dub Stab Plaits (not part of the hardware module or of Narti Live's code;
// written for this plug-in).
//
//   dry mono signal --> ECHO (stereo ping-pong, tempo-synced, filtered + soft-clipped feedback)
//                   --> REVERB (8-line feedback delay network, fed by the dry signal and the echoes)
//                   --> WIDTH (mid/side; above 100 % the dry sound also gets a decorrelated stereo spread)
//
// The echo and the reverb are sends: the dry signal always passes at full level, "mix" is how much of the return is added.
// JUCE-free, allocation only in prepare(). Everything is per-sample deterministic, so the output does not depend on the
// host's buffer size.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

namespace dub {

constexpr int kNumEchoTimes = 7;
const char* echoTimeName (int index);          // "1/16", "1/8T", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2"
double echoTimeBeats (int index);              // length in quarter notes
constexpr int kDefaultEchoTime = 3;            // 1/8 dotted

struct FxParams
{
    float echoMix = 0.30f;        // 0..1   echo return level
    int   echoTime = kDefaultEchoTime;
    float echoFeedback = 0.45f;   // 0..0.95
    float echoTone = 0.5f;        // 0..1   darkness of the repeats: 0 = dark (800 Hz), 1 = bright (9 kHz)
    float reverbMix = 0.20f;      // 0..1   reverb return level
    float reverbDecay = 2.5f;     // seconds to fall by 60 dB (0.3 .. 12)
    float width = 1.0f;           // 0 = mono, 1 = as made, 2 = extra wide
    double bpm = 120.0;           // tempo the echo time follows
};

class DubFx
{
public:
    DubFx();
    ~DubFx();
    DubFx (const DubFx&) = delete;
    DubFx& operator= (const DubFx&) = delete;

    // maxBlock: the most samples one process() call will be given (larger calls are handled in pieces).
    void prepare (double sampleRate, int maxBlock);
    void reset();                                       // clear the echo and reverb tails
    // dry: mono input. outL / outR: stereo output (must not overlap the input).
    void process (const float* dry, float* outL, float* outR, int n, const FxParams& p);

    // The tail the effects can have after the input stops, in seconds (worst case, for the host).
    static double maxTailSeconds();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dub
