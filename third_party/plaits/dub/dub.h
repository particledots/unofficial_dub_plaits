// DUB STAB PLAITS for Plaits — generative dub chord sequencer, clocked from TRIG.
// Copyright 2026 Narti Live. MIT license (same terms as the Plaits sources).

#ifndef PLAITS_DUB_DUB_H_
#define PLAITS_DUB_DUB_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace plaits {
namespace dub {

const int kNumSteps = 16;
const int kNumChordModels = 8;   // 0..5 poly models, 6 = Plaits CHORDS, 7 = STRING MACHINE
const int kNumPolyModels = 6;
const int kNumSpeeds = 8;        // 1/2 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
const int kMaxChordNotes = 5;
const int kNumPolyVoices = 6;

// Fixed: C minor, DUB chords (root, 5th, 7th, 3rd and 5th an octave up).
// Everything the panel can set (edited by the UI, read by the audio code).
struct Settings {
  float envelope;       // FREQUENCY knob: attack + decay + filter envelope together
  float resonance;      // RIGHT + FREQUENCY (capped below self-oscillation)
  uint8_t density;      // TIMBRE attenuverter, 1..16
  uint8_t length;       // FM attenuverter, 1..16
  uint8_t speed;        // MORPH attenuverter, 0..7
  uint8_t octave;       // LEFT + FREQUENCY, 1..5
  uint8_t chord_model;  // buttons, 0..7
  int8_t transpose;     // V/OCT, in scale degrees (set by the voice)
  uint8_t cv_tune;      // CV TUNE mode: 0 off, 1 = AUX at 0 V (C3), 2 = AUX at +2 V (C5)
  float cv_scale;       // AUX pitch CV: DAC counts per volt (set in CV TUNE)
};

const float kDefaultCvScale = 6553.6f;   // assumes +/-5 V full scale; CV TUNE trims it
const float kCvZeroNote = 48.0f;         // C3 = 0 V on AUX

void DefaultSettings(Settings* s);

// What the envelope knob and resonance turn into.
struct Shape {
  float attack_s, decay_s, cutoff_hz, env_oct, q;
};
void ComputeShape(const Settings& s, Shape* shape);

struct Pattern {
  int8_t degree[kNumSteps];  // chord mode: scale degree 0..6
  int8_t note[kNumSteps];    // mono mode: scale step 0..13
  uint8_t rank[kNumSteps];   // a step plays when rank < density
  uint8_t accent[kNumSteps];
};

struct Event {
  int num_notes;
  float notes[kMaxChordNotes];   // MIDI-style note numbers, ascending
  float root;                    // chord root (lowest scale degree note)
  bool accent;
  int step;
};

// Builds a C minor DUB chord on a scale degree. Every note is in C minor.
int BuildChord(const Settings& s, int degree, float* notes, float* root);
// Nearest C minor scale degree for a pitch offset in semitones (V/OCT).
int SemitonesToDegrees(float semitones);

class Sequencer {
 public:
  Sequencer() { }
  void Init();
  // Once per audio block. Returns true when a step plays and fills *e.
  bool Process(
      const Settings& s, float trigger_cv, bool trigger_patched, Event* e);

  void New();

  inline const Pattern& pattern() const { return pattern_; }
  inline void set_pattern(const Pattern& p) { pattern_ = p; }
  inline int step() const { return step_; }
  inline bool clock_tick() const { return clock_tick_; }
  inline float lfo(int i) const { return lfo_out_[i]; }

  // random LFO (always on), once per block: HARMONICS, TIMBRE, MORPH, cutoff
  void ProcessLfo();

 private:
  bool FireStep(const Settings& s, Event* e);
  void Generate(Pattern* p);
  void MakeRanks(Pattern* p);

  Pattern pattern_;

  int step_;
  bool trigger_state_;
  bool clock_tick_;
  int clock_count_;
  int32_t blocks_since_edge_;
  int32_t period_;               // measured clock period, in blocks
  int pending_substeps_;
  float substep_phase_;
  float substep_interval_;
  float internal_phase_;
  bool was_patched_;

  float lfo_phase_;
  float lfo_from_[4];
  float lfo_to_[4];
  float lfo_out_[4];

  DISALLOW_COPY_AND_ASSIGN(Sequencer);
};

// AD envelope, exponential decay, retriggers from its current level.
class Envelope {
 public:
  void Init() { value_ = 0.0f; stage_ = 0; }
  // Precomputed coefficients (one division each, shared by a whole chord).
  static inline float AttackIncrement(float seconds) { return 1.0f / (seconds * 48000.0f); }
  static inline float DecayCoefficient(float seconds) {
    float c = 1.0f - 6.9f / (seconds * 48000.0f);
    return c < 0.0f ? 0.0f : c;
  }
  inline void TriggerWith(float attack_increment, float decay_coefficient) {
    attack_inc_ = attack_increment;
    decay_coef_ = decay_coefficient;
    stage_ = 1;
  }
  void Trigger(float attack_seconds, float decay_seconds) {
    attack_inc_ = 1.0f / (attack_seconds * 48000.0f);
    decay_coef_ = 1.0f - 6.9f / (decay_seconds * 48000.0f);
    if (decay_coef_ < 0.0f) decay_coef_ = 0.0f;
    stage_ = 1;
  }
  inline float Next() {
    if (stage_ == 1) {
      value_ += attack_inc_;
      if (value_ >= 1.0f) { value_ = 1.0f; stage_ = 2; }
    } else if (stage_ == 2) {
      value_ *= decay_coef_;
      if (value_ < 0.0002f) { value_ = 0.0f; stage_ = 0; }
    }
    return value_;
  }
  inline float value() const { return value_; }
  inline bool active() const { return stage_ != 0; }
  // Raw access so inner loops can keep the envelope in registers.
  inline void Load(float* v, int* st, float* ai, float* dc) const {
    *v = value_; *st = stage_; *ai = attack_inc_; *dc = decay_coef_;
  }
  inline void Store(float v, int st) { value_ = v; stage_ = st; }
 private:
  float value_, attack_inc_, decay_coef_;
  int stage_;
};


// Output leveler (v1.4): 6 dB of headroom, then a peak limiter (fast attack,
// ~0.7 s release) keeps stacked chords and resonance out of the soft clipper,
// which used to distort them.
class Leveler {
 public:
  void Init() { peak_ = 0.0f; }
  inline float Process(float x) {
    x *= 0.5f;
    const float a = x < 0.0f ? -x : x;
    if (a > peak_) peak_ += (a - peak_) * 0.25f;
    else peak_ *= 0.99996f;
    if (peak_ > kCeiling) x *= kCeiling / peak_;
    return Knee(x);
  }
  // Clean (linear) up to half scale, then a smooth knee up to full scale.
  static inline float Knee(float x) {
    const float a = x < 0.0f ? -x : x;
    if (a <= 0.5f) return x;
    const float y = 0.5f + 0.5f * stmlib::SoftClip((a - 0.5f) * 2.0f);
    return x < 0.0f ? -y : y;
  }
 private:
  static const float kCeiling;
  float peak_;
};

// Six light polyphonic chord models with per-voice envelope + filter,
// and a sub-bass sine on AUX that follows the chord root.
class PolySynth {
 public:
  PolySynth() { }
  void Init();
  void NoteOn(const Event& e, const Shape& shape);
  void Render(
      int model,
      float harmonics, float timbre, float morph,
      const Shape& shape, float level,
      float* out, float* aux, size_t size);
  int active_voices() const;
 private:
  struct Voice {
    float note, increment, phase[2], state, velocity;
    Envelope env;
    int delay;
  };
  Voice voice_[kNumPolyVoices];
  uint32_t age_[kNumPolyVoices];
  uint32_t age_counter_;
  // One filter for the whole chord, with its own envelope (paraphonic).
  stmlib::Svf filter_;
  float lp_;
  Envelope filter_env_;
  float chord_note_;
  float accent_;
  float chuff_;
  float sub_phase_, sub_increment_;
  Envelope sub_env_;
  Leveler leveler_;
  DISALLOW_COPY_AND_ASSIGN(PolySynth);
};

// Envelope + low-pass applied after a Plaits engine (replaces the LPG).
class PostShaper {
 public:
  void Init() { env_.Init(); f_[0].Init(); f_[1].Init(); lp_[0] = lp_[1] = 0.0f; leveler_.Init(); }
  void Trigger(const Shape& shape, bool accent) {
    env_.Trigger(shape.attack_s, shape.decay_s);
    accent_ = accent ? 1.0f : 0.0f;
  }
  void Process(const Shape& shape, float level, float cutoff_ratio,
               float* out, float* aux, size_t size);
  inline float envelope() const { return env_.value(); }
 private:
  Envelope env_;
  stmlib::Svf f_[2];
  float lp_[2];
  float accent_;
  Leveler leveler_;
};

}  // namespace dub
}  // namespace plaits

#endif  // PLAITS_DUB_DUB_H_
