// AcidVoice: the ACID model (mono bass) of DUB STAB PLAITS, ported line by line from the gen~ code inside Narti Live's
// Max for Live devices (DubStab-M4L-v1.0.zip, "Dub Stab Synth" / "Dub Stab Plaits"; MIT, Copyright (c) 2026 Narti Live).
// The firmware's own ACID source is not published; the gen~ code is the only readable version, so this is what is ported.
//
// What it is: a polyBLEP saw -> 4-pole resonant ladder low-pass (zero-delay feedback, soft-clipped) -> small post filters
//   -> amplitude envelope -> peak limiter with a soft knee.
//   FREQUENCY = filter envelope amount, HARMONICS = cutoff, TIMBRE = accent, MORPH = resonance.
//
// Everything is double precision on purpose (the gen~ original is float64), so that tests/acid_crosscheck can compare this
// port against the original gen~ text executed in Node with a very tight tolerance.
// JUCE-free and allocation-free.
#pragma once

namespace dub {

class AcidVoice
{
public:
    // sampleRate: the rate this voice runs at (the synth runs it at the module rate, 47872.34 Hz).
    void prepare (double sampleRate);

    // Events, as in the gen~ code's event queue (type 1 acid note, 2 acid note with slide, 3 gate off).
    //   lengthMs > 0: the gate closes by itself after that many milliseconds (the sequencer sends the length first).
    void note (int midiNote, int velocity, bool slide, double lengthMs);
    void gateOff();

    // One sample.  envelope = FREQUENCY, harmonics = HARMONICS (cutoff), timbre = TIMBRE (accent), morph = MORPH (resonance),
    // all 0..1.  Returns the mono output.
    double process (double envelope, double harmonics, double timbre, double morph);

    // Forget everything (used when the model is switched away, so a stale note cannot come back).
    void silence();
    // Zero the output limiter, as the gen~ code does when the model changes ("lvpeak = 0").
    void resetLimiter() { lvpeak_ = 0; }

private:
    double sr_ = 47872.34, r48_ = 48000.0 / 47872.34;

    // gen~ History variables (same names, a_ prefix)
    double phase_ = 0, freq_ = 0.0025, target_ = 0.0025, glide_ = 1;
    double a0_ = 0, a1_ = 0, a2_ = 0, a3_ = 0, hp_ = 0, wlp_ = 0;
    double fenv_ = 0, fdec_ = 0.9999, env_ = 0, sweep_ = 0;
    int accent_ = 0, gate_ = 0;
    double left_ = 0;
    double lvpeak_ = 0;
};

} // namespace dub
