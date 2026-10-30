// DUB STAB PLAITS for Plaits — generative dub chord sequencer, clocked from TRIG.
// Copyright 2026 Narti Live. MIT license (same terms as the Plaits sources).

#include "plaits/dub/dub.h"

#include <algorithm>

#include "stmlib/utils/random.h"
#include "plaits/dsp/engine/engine.h"

namespace plaits {
namespace dub {

using namespace stmlib;

// ------------------------------------------------------------------ helpers

// 2^x, about 1e-4 relative error, for block-rate control math.
static inline float Pow2(float x) {
  if (x < -40.0f) x = -40.0f;
  if (x > 40.0f) x = 40.0f;
  float fl = static_cast<float>(static_cast<int32_t>(x + 64.0f)) - 64.0f;
  float f = x - fl;
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
  int32_t e = static_cast<int32_t>(fl);
  union { float f; int32_t i; } u;
  u.i = (e + 127) << 23;
  return p * u.f;
}

static inline float Rand() { return Random::GetFloat(); }
static inline float RandBipolar() { return Random::GetFloat() * 2.0f - 1.0f; }

// One knob for the whole envelope: fully left = short dark pluck, noon = dub
// stab, fully right = long swell with a big filter sweep.
const float Leveler::kCeiling = 0.5f;

void ComputeShape(const Settings& s, Shape* shape) {
  float v = s.envelope;
  CONSTRAIN(v, 0.0f, 1.0f);
  shape->decay_s = 0.06f * Pow2(v * 6.6f);                  // 60 ms .. 6 s
  float swell = v > 0.55f ? (v - 0.55f) * (1.0f / 0.45f) : 0.0f;
  shape->attack_s = 0.0015f + swell * swell * 1.2f;         // 1.5 ms .. 1.2 s
  shape->env_oct = 1.5f + v * 3.5f;                         // filter envelope 1.5 .. 5 octaves
  shape->cutoff_hz = 300.0f;
  float r = s.resonance;
  CONSTRAIN(r, 0.0f, 1.0f);
  shape->q = 0.6f + r * r * 2.9f;                           // max Q 3.5: never self-oscillates
}

// sin(2 pi p) for p in [0, 1)
static inline float Sine(float p) {
  float y = 6.2831853f * p - 3.1415927f;
  float a = 1.2732395f * y - 0.40528473f * y * fabsf(y);
  a = 0.225f * (a * fabsf(a) - a) + a;
  return -a;
}
static inline float Frac(float x) {
  return x - static_cast<float>(static_cast<int32_t>(x + 1024.0f) - 1024);
}
static inline float Blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}
static inline float Saw(float t, float dt) { return 2.0f * t - 1.0f - Blep(t, dt); }
static inline float Pulse(float t, float dt, float pw) {
  float t2 = t + pw;
  if (t2 >= 1.0f) t2 -= 1.0f;
  return (Saw(t, dt) - Saw(t2, dt)) * 0.5f;
}
static inline void Advance(float* phase, float increment) {
  float t = *phase + increment;
  if (t >= 1.0f) t -= 1.0f;
  *phase = t;
}

// ------------------------------------------------------------------ harmony

static const int8_t kMinor[7] = { 0, 2, 3, 5, 7, 8, 10 };      // C minor
static const int8_t kDubSteps[5] = { 0, 4, 6, 9, 11 };          // root 5th 7th 3rd' 5th'
static const int kNumDubNotes = 5;

static inline int FloorDiv7(int d) { return d >= 0 ? d / 7 : -((6 - d) / 7); }

int BuildChord(const Settings& s, int degree, float* notes, float* root) {
  const int n = kNumDubNotes;
  const int base = 12 * (s.octave + 1);   // C
  int raw[kNumDubNotes];
  for (int i = 0; i < n; ++i) {
    int d = degree + kDubSteps[i];
    int o = FloorDiv7(d);
    raw[i] = base + kMinor[d - 7 * o] + 12 * o;
  }
  *root = static_cast<float>(raw[0]);
  // The DUB voicing spans more than an octave, so it only moves by whole
  // octaves (towards the middle of the register): no doubled notes, and
  // every note stays in C minor.
  const int centre_sum = (base + 7 + 12 * FloorDiv7(degree)) * n;  // V/OCT octaves still move it
  int sum = 0;
  for (int i = 0; i < n; ++i) sum += raw[i];
  int best_shift = 0, best = 1 << 30;
  for (int shift = -12; shift <= 12; shift += 12) {
    int d = sum + shift * n - centre_sum;
    d = d < 0 ? -d : d;
    if (d < best) { best = d; best_shift = shift; }
  }
  for (int i = 0; i < n; ++i) notes[i] = static_cast<float>(raw[i] + best_shift);
  return n;
}

int SemitonesToDegrees(float semitones) {
  int st = static_cast<int>(semitones + (semitones >= 0.0f ? 0.5f : -0.5f));
  int o = st >= 0 ? st / 12 : -((11 - st) / 12);
  int pc = st - 12 * o;
  int best = 0, best_d = 99;
  for (int i = 0; i < 7; ++i) {
    int d = pc - kMinor[i]; d = d < 0 ? -d : d;
    if (d < best_d) { best_d = d; best = i; }
  }
  return best + 7 * o;
}

void DefaultSettings(Settings* s) {
  s->envelope = 0.5f;
  s->resonance = 0.35f;
  s->density = 5;
  s->length = 16;
  s->speed = 4;        // 1/16
  s->octave = 3;
  s->chord_model = 0;  // DUB SAW
  s->transpose = 0;
  s->cv_tune = 0;
  s->cv_scale = kDefaultCvScale;
}

// ------------------------------------------------------------------ sequencer

static const float kRankBias[kNumSteps] = {
  0.5f, 0.0f, 1.3f, 0.35f, 0.15f, 0.0f, 1.2f, 0.25f,
  0.4f, 0.0f, 1.25f, 0.35f, 0.1f, 0.0f, 1.1f, 0.45f
};
// Steps per clock pulse = kMultiply / kDivide (the clock is taken as 16ths).
//                                       1/2 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
static const int kDivide[kNumSpeeds] =   { 8,  4,  2,  4,   1,   2,    1,   1 };
static const int kMultiply[kNumSpeeds] = { 1,  1,  1,  3,   1,   3,    2,   3 };

void Sequencer::Init() {
  step_ = -1;
  trigger_state_ = false;
  clock_tick_ = false;
  clock_count_ = 0;
  blocks_since_edge_ = 0;
  period_ = 500;
  pending_substeps_ = 0;
  substep_phase_ = 0.0f;
  substep_interval_ = 0.0f;
  internal_phase_ = 1e9f;
  was_patched_ = false;
  lfo_phase_ = 0.9999f;  // pick the first LFO targets right away (v1.2)
  for (int i = 0; i < 4; ++i) lfo_from_[i] = lfo_to_[i] = lfo_out_[i] = 0.0f;
  Generate(&pattern_);
}

void Sequencer::MakeRanks(Pattern* p) {
  float score[kNumSteps];
  int index[kNumSteps];
  for (int i = 0; i < kNumSteps; ++i) {
    score[i] = kRankBias[i] + Rand() * 0.9f;
    index[i] = i;
  }
  for (int i = 0; i < kNumSteps; ++i)
    for (int j = i + 1; j < kNumSteps; ++j)
      if (score[index[j]] > score[index[i]]) { int t = index[i]; index[i] = index[j]; index[j] = t; }
  for (int r = 0; r < kNumSteps; ++r) p->rank[index[r]] = r;
}

static int PickDegree(int avoid) {
  static const float w[7] = { 4.0f, 0.7f, 1.5f, 2.5f, 2.0f, 2.0f, 1.0f };
  float total = 0.0f;
  for (int i = 0; i < 7; ++i) if (i != avoid) total += w[i];
  float r = Rand() * total;
  for (int i = 0; i < 7; ++i) {
    if (i == avoid) continue;
    r -= w[i];
    if (r <= 0.0f) return i;
  }
  return 0;
}

void Sequencer::Generate(Pattern* p) {
  // Chords: 1, 2 or 4 chords per pattern, mostly held (dub style).
  float r = Rand();
  int segments = r < 0.35f ? 1 : (r < 0.75f ? 2 : 4);
  int chord[4];
  chord[0] = Rand() < 0.7f ? 0 : PickDegree(-1);
  for (int k = 1; k < segments; ++k) chord[k] = PickDegree(chord[k - 1]);
  for (int i = 0; i < kNumSteps; ++i) {
    int d = chord[i * segments / kNumSteps];
    if (Rand() < 0.06f) d = PickDegree(d);
    p->degree[i] = d;
  }
  // Mono: a random walk over two octaves of the scale.
  int n = Rand() < 0.5f ? 0 : 7;
  for (int i = 0; i < kNumSteps; ++i) {
    float q = Rand();
    if (q < 0.25f) {
    } else if (q < 0.7f) {
      n += Rand() < 0.5f ? -1 : 1;
    } else if (q < 0.9f) {
      n += Rand() < 0.5f ? -2 : 2;
    } else {
      n = (Rand() < 0.5f ? 0 : 7) + (Rand() < 0.3f ? 4 : 0);
    }
    if (n < 0) n = 1;
    if (n > 13) n = 12;
    p->note[i] = n;
  }
  MakeRanks(p);
  for (int i = 0; i < kNumSteps; ++i) {
    p->accent[i] = Rand() < (kRankBias[i] > 1.0f ? 0.35f : 0.15f);
  }
}

void Sequencer::New() {
  Generate(&pattern_);
}

bool Sequencer::FireStep(const Settings& s, Event* e) {
  clock_tick_ = true;
  int length = s.length < 1 ? 1 : (s.length > kNumSteps ? kNumSteps : s.length);
  step_ = (step_ + 1) % length;
  if (pattern_.rank[step_] >= s.density) return false;
  e->step = step_;
  e->accent = pattern_.accent[step_];
  e->num_notes = BuildChord(s, pattern_.degree[step_] + s.transpose, e->notes, &e->root);
  return true;
}

bool Sequencer::Process(
    const Settings& s, float trigger_cv, bool trigger_patched, Event* e) {
  clock_tick_ = false;
  bool fired = false;
  const int ratio = s.speed < kNumSpeeds ? s.speed : 4;
  if (trigger_patched) {
    if (!was_patched_) {
      pending_substeps_ = 0;
      clock_count_ = 0;
      trigger_state_ = false;
      blocks_since_edge_ = 0;
    }
    bool edge = false;
    if (!trigger_state_) {
      if (trigger_cv > 0.3f) { trigger_state_ = true; edge = true; }
    } else if (trigger_cv < 0.1f) {
      trigger_state_ = false;
    }
    if (blocks_since_edge_ < 0x7fffffff) ++blocks_since_edge_;
    if (edge) {
      // Measure the clock period (between 1 ms and 4 s).
      if (blocks_since_edge_ >= 4 && blocks_since_edge_ <= 16000) {
        period_ = blocks_since_edge_;
      }
      blocks_since_edge_ = 0;
      if (clock_count_ % kDivide[ratio] == 0) {
        // A group of kDivide pulses holds kMultiply evenly spaced steps.
        fired = FireStep(s, e);
        pending_substeps_ = kMultiply[ratio] - 1;
        substep_interval_ = static_cast<float>(period_ * kDivide[ratio]) / kMultiply[ratio];
        substep_phase_ = 0.0f;
      } else if (pending_substeps_ > 0) {
        substep_phase_ += 1.0f;
        if (substep_phase_ >= substep_interval_) {
          substep_phase_ -= substep_interval_;
          --pending_substeps_;
          fired = FireStep(s, e);
        }
      }
      clock_count_ = (clock_count_ + 1) % 840;
    } else if (pending_substeps_ > 0) {
      substep_phase_ += 1.0f;
      if (substep_phase_ >= substep_interval_) {
        substep_phase_ -= substep_interval_;
        --pending_substeps_;
        fired = FireStep(s, e);
      }
    }
  } else {
    // No clock patched: internal 16ths at 120 BPM.
    float interval = 500.0f * kDivide[ratio] / kMultiply[ratio];
    internal_phase_ += 1.0f;
    if (internal_phase_ >= interval) {
      internal_phase_ = internal_phase_ > 1e8f ? 0.0f : internal_phase_ - interval;
      if (internal_phase_ > interval) internal_phase_ = 0.0f;
      fired = FireStep(s, e);
    }
  }
  was_patched_ = trigger_patched;
  return fired;
}

void Sequencer::ProcessLfo() {
  // Always on: random targets with a smooth slide, speed 20 (0.12 Hz).
  const float rate = 0.05f * Pow2(6.32f * 0.20f);
  lfo_phase_ += rate * (float(kBlockSize) / 48000.0f);
  if (lfo_phase_ >= 1.0f) {
    lfo_phase_ -= 1.0f;
    for (int i = 0; i < 4; ++i) {
      lfo_from_[i] = lfo_out_[i];
      lfo_to_[i] = RandBipolar();
    }
  }
  float x = lfo_phase_ / 0.5f;
  if (x > 1.0f) x = 1.0f;
  x = x * x * (3.0f - 2.0f * x);
  for (int i = 0; i < 4; ++i) {
    lfo_out_[i] = lfo_from_[i] + (lfo_to_[i] - lfo_from_[i]) * x;
  }
}

// ------------------------------------------------------------------ poly synth

void PolySynth::Init() {
  leveler_.Init();
  for (int i = 0; i < kNumPolyVoices; ++i) {
    Voice& v = voice_[i];
    v.env.Init();
    v.phase[0] = Rand(); v.phase[1] = Rand();
    v.state = 0.0f;
    v.delay = 0;
    v.note = 60.0f;
    v.increment = 0.0f;
    v.velocity = 0.0f;
    age_[i] = 0;
  }
  age_counter_ = 0;
  filter_.Init();
  lp_ = 0.0f;
  filter_env_.Init();
  chord_note_ = 60.0f;
  accent_ = 0.0f;
  chuff_ = 0.0f;
  sub_phase_ = 0.0f;
  sub_increment_ = 0.0f;
  sub_env_.Init();
}

int PolySynth::active_voices() const {
  int n = 0;
  for (int i = 0; i < kNumPolyVoices; ++i) n += voice_[i].env.active() || voice_[i].delay > 0;
  return n;
}

void PolySynth::NoteOn(const Event& e, const Shape& shape) {
  const float transpose = 0.0f;
  const float attack = shape.attack_s, decay = shape.decay_s;
  const float ai = Envelope::AttackIncrement(attack), dc = Envelope::DecayCoefficient(decay);
  for (int n = 0; n < e.num_notes; ++n) {
    // free voice first, then the quietest decaying one
    int best = 0;
    float best_score = 1e9f;
    for (int i = 0; i < kNumPolyVoices; ++i) {
      const Voice& v = voice_[i];
      float score;
      if (!v.env.active() && v.delay <= 0) score = -1e6f;
      else score = v.env.value() * v.velocity - 1e-6f * static_cast<float>(age_counter_ - age_[i]);
      if (score < best_score) { best_score = score; best = i; }
    }
    Voice& v = voice_[best];
    bool stolen = v.env.active();
    if (!stolen) v.env.Init();
    v.note = e.notes[n] + transpose;
    v.increment = NoteToFrequency(v.note);
    v.velocity = e.accent ? 1.0f : 0.72f;
    // strum: 3 ms per note plus a little humanisation (in 0.25 ms blocks);
    // a stolen voice restarts at once from its current level (no click)
    v.delay = stolen ? 0 : (n * 3 + static_cast<int>(Random::GetWord() >> 30)) * 4;
    v.env.TriggerWith(ai, dc);
    age_[best] = ++age_counter_;
  }
  chord_note_ = e.root + transpose;
  accent_ = e.accent ? 1.0f : 0.0f;
  chuff_ = 1.0f;
  filter_env_.TriggerWith(ai, dc);
  sub_increment_ = NoteToFrequency(e.root + transpose - 12.0f);
  sub_env_.Trigger(attack, decay * 1.3f);
}

struct VoiceConsts {
  float d0, d1, pw, harmonics, timbre, morph, fold, idx, fb, pd, res, levels;
  float saw_gain, pulse_gain;
  int duty;
};

// One oscillator sample. All state is passed by reference to locals so the
// compiler keeps it in registers (no aliasing with the output buffer).
template<int MODEL>
static inline float Osc(float& p0, float& p1, float& st, const float dt, const VoiceConsts& k) {
  float x;
  if (MODEL == 0) {         // DUB SAW: H detune, T saw > pulse (noise chuff is per chord)
    float t2 = p0 + k.pw; if (t2 >= 1.0f) t2 -= 1.0f;
    float u2 = p1 + k.pw; if (u2 >= 1.0f) u2 -= 1.0f;
    x = (Saw(p0, k.d0) + Saw(p1, k.d1)) * k.saw_gain - (Saw(t2, k.d0) + Saw(u2, k.d1)) * k.pulse_gain;
    Advance(&p0, k.d0);
    Advance(&p1, k.d1);
  } else if (MODEL == 1) {  // VA: H interval of osc 2, T pulse width, M mix
    float t2 = p0 + k.pw; if (t2 >= 1.0f) t2 -= 1.0f;
    float a = (Saw(p0, dt) - Saw(t2, dt)) * 0.7f;
    x = a * k.saw_gain + Saw(p1, k.d1) * k.pulse_gain;
    Advance(&p0, dt);
    Advance(&p1, k.d1);
  } else if (MODEL == 2) {  // WAVESHAPE: H asymmetry, T fold, M triangle > saw
    float tri = 4.0f * fabsf(p0 - 0.5f) - 1.0f;
    float base = tri + (Saw(p0, dt) - tri) * k.morph;
    x = Sine(Frac(base * k.fold + k.harmonics)) * 0.8f;
    Advance(&p0, dt);
  } else if (MODEL == 3) {  // FM: H ratio, T index, M feedback
    float m = Sine(Frac(p1 + k.fb * st));
    st = m;
    x = Sine(Frac(p0 + k.idx * m)) * 0.8f;
    Advance(&p0, dt);
    Advance(&p1, k.d1);
  } else if (MODEL == 4) {  // PHASE DIST: H resonance ratio, T distortion, M blend
    float t = p0;
    float p = t < k.pd ? t * k.d0 : 0.5f + (t - k.pd) * k.d1;
    float a = Sine(Frac(p + 0.25f));
    float b = Sine(Frac(t * k.res + 0.25f)) * (1.0f - t);
    x = (a + (b - a) * k.morph) * 0.8f;
    Advance(&p0, dt);
  } else {                  // CHIPTUNE: H duty, T crush, M triangle
    float t2 = p0 + k.pw; if (t2 >= 1.0f) t2 -= 1.0f;
    float sq = (Saw(p0, dt) - Saw(t2, dt)) * 0.7f;
    float tri = static_cast<float>(static_cast<int>((1.0f - 2.0f * fabsf(2.0f * p0 - 1.0f)) * 7.5f + 8.0f)) * (1.0f / 7.5f) - 1.0f;
    float o = sq + (tri - sq) * k.morph;
    if (k.duty) o = static_cast<float>(static_cast<int>(o * k.levels + 64.5f) - 64) * k.d1;
    x = o * 0.6f;
    Advance(&p0, dt);
  }
  return x;
}

// True when a band-limited saw at phase p (increment d) has no discontinuity
// and needs no polyBLEP correction for the next n samples.
static inline bool Smooth(float p, float d, size_t n) {
  return p >= d && p + static_cast<float>(n + 1) * d < 1.0f - d;
}

template<int MODEL>
static void RenderVoiceT(float* phase, float* state, float dt, const VoiceConsts& kref,
                         Envelope* env, float gain, float* out, size_t size) {
  const VoiceConsts k = kref;
  float p0 = phase[0], p1 = phase[1], st = *state;
  float ev, ai, dc;
  int es;
  env->Load(&ev, &es, &ai, &dc);
  if (MODEL == 0 || MODEL == 1) {
    // Fast path: when no saw edge falls inside this block, every oscillator is
    // a straight line, so the mix is a linear ramp.
    float p0b = p0 + k.pw; if (p0b >= 1.0f) p0b -= 1.0f;
    float p1b = p1 + k.pw; if (p1b >= 1.0f) p1b -= 1.0f;
    const float d0 = MODEL == 0 ? k.d0 : dt;
    bool smooth = Smooth(p0, d0, size) && Smooth(p1, k.d1, size) &&
        Smooth(p0b, d0, size) && (MODEL == 1 || Smooth(p1b, k.d1, size));
    if (smooth) {
      float x, slope;
      if (MODEL == 0) {
        x = (2.0f * (p0 + p1) - 2.0f) * k.saw_gain - (2.0f * (p0b + p1b) - 2.0f) * k.pulse_gain;
        slope = 2.0f * (d0 + k.d1) * (k.saw_gain - k.pulse_gain);
      } else {
        float pulse = 2.0f * (p0 - p0b) * 0.7f;
        x = pulse * k.saw_gain + (2.0f * p1 - 1.0f) * k.pulse_gain;
        slope = 2.0f * k.d1 * k.pulse_gain;
      }
      x *= gain; slope *= gain;
      for (size_t n = 0; n < size; ++n) {
        if (es == 1) { ev += ai; if (ev >= 1.0f) { ev = 1.0f; es = 2; } }
        else { ev *= dc; }
        out[n] += x * ev;
        x += slope;
      }
      if (es == 2 && ev < 0.0002f) { ev = 0.0f; es = 0; }
      env->Store(ev, es);
      phase[0] = p0 + d0 * static_cast<float>(size);
      phase[1] = p1 + k.d1 * static_cast<float>(size);
      return;
    }
  }
  for (size_t n = 0; n < size; ++n) {
    float x = Osc<MODEL>(p0, p1, st, dt, k);
    if (es == 1) { ev += ai; if (ev >= 1.0f) { ev = 1.0f; es = 2; } }
    else { ev *= dc; }
    out[n] += x * ev * gain;
  }
  if (es == 2 && ev < 0.0002f) { ev = 0.0f; es = 0; }
  env->Store(ev, es);
  phase[0] = p0; phase[1] = p1; *state = st;
}

void PolySynth::Render(
    int model,
    float harmonics, float timbre, float morph,
    const Shape& shape, float level,
    float* out, float* aux, size_t size) {
  std::fill(&out[0], &out[size], 0.0f);
  static const float fm_ratios[12] = {
    0.5f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f };
  static const float duty[4] = { 0.125f, 0.25f, 0.5f, 0.75f };

  VoiceConsts k;
  k.harmonics = harmonics; k.timbre = timbre; k.morph = morph;
  k.fold = 0.25f * (1.0f + 5.0f * timbre);
  k.idx = timbre * timbre * 4.0f;
  k.fb = morph * 0.45f;
  k.pd = 0.5f - timbre * 0.47f;
  k.res = 1.0f + harmonics * 7.0f;
  k.levels = 2.0f + (1.0f - timbre) * (1.0f - timbre) * 30.0f;
  k.duty = timbre > 0.03f;
  k.saw_gain = 1.0f; k.pulse_gain = 0.0f;
  k.pw = 0.5f;
  switch (model) {
    case 0:
      // saw + pulse mix: saw(t)*(1-T) + (saw(t) - saw(t+pw)) * 0.8 * T
      k.pw = 0.5f + 0.35f * timbre;
      k.saw_gain = 0.5f * ((1.0f - timbre) + 0.8f * timbre);
      k.pulse_gain = 0.5f * 0.8f * timbre;
      break;
    case 1:
      k.pw = 0.5f + 0.42f * timbre;
      k.saw_gain = 1.0f - morph * 0.7f;
      k.pulse_gain = 0.3f + morph * 0.7f;
      break;
    case 2:
      k.harmonics = harmonics * 0.25f + 0.25f;
      break;
    case 5:
      k.pw = duty[static_cast<int>(harmonics * 3.99f)];
      break;
  }

  // Shared chord filter.
  const float keytrack = Pow2(0.025f * (chord_note_ - 60.0f));
  float fc = shape.cutoff_hz * keytrack *
      Pow2((shape.env_oct + accent_) * filter_env_.value());
  float f = fc * (1.0f / 48000.0f);
  if (f > 0.4f) f = 0.4f;
  filter_.set_f_q<FREQUENCY_FAST>(f, shape.q);
  float g1 = f * 6.2831853f; if (g1 > 0.9f) g1 = 0.9f;
  for (size_t n = 0; n < size; ++n) filter_env_.Next();

  const float det = 1.0f + harmonics * 0.0175f, inv_det = 1.0f / det;
  const float va_ratio = Pow2(harmonics * 19.0f * (1.0f / 12.0f)) * 1.002f;
  const float fm_ratio = fm_ratios[static_cast<int>(harmonics * 11.99f)];

  for (int i = 0; i < kNumPolyVoices; ++i) {
    Voice& v = voice_[i];
    if (v.delay > 0) { --v.delay; continue; }
    if (!v.env.active()) continue;
    float dt = v.increment;
    float gain = 0.5f * v.velocity;
    switch (model) {
      case 0:
        k.d0 = dt * inv_det; k.d1 = dt * det;
        RenderVoiceT<0>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
      case 1:
        k.d1 = dt * va_ratio;
        RenderVoiceT<1>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
      case 2:
        RenderVoiceT<2>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
      case 3:
        k.d1 = dt * fm_ratio;
        RenderVoiceT<3>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
      case 4:
        k.d0 = 0.5f / k.pd; k.d1 = 0.5f / (1.0f - k.pd);
        RenderVoiceT<4>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
      default:
        k.d1 = 1.0f / k.levels;
        RenderVoiceT<5>(v.phase, &v.state, dt, k, &v.env, gain, out, size);
        break;
    }
  }

  // Noise "chuff" at each chord onset (DUB SAW: MORPH), then the filter.
  const float chuff_amount = model == 0 ? morph * 0.35f : 0.0f;
  for (size_t n = 0; n < size; ++n) {
    float x = out[n];
    if (chuff_amount > 0.0f) {
      x += RandBipolar() * (chuff_ + 0.03f) * chuff_amount * filter_env_.value();
      chuff_ *= 0.986f;
    }
    float y = filter_.Process<FILTER_MODE_LOW_PASS>(x);
    lp_ += g1 * (y - lp_);
    out[n] = leveler_.Process(lp_) * level;
    float e = sub_env_.Next();
    aux[n] = Sine(sub_phase_) * e * 0.55f * level;
    Advance(&sub_phase_, sub_increment_);
  }
}

// ------------------------------------------------------------------ post shaper

void PostShaper::Process(const Shape& shape, float level, float cutoff_ratio,
                         float* out, float* aux, size_t size) {
  float fc = shape.cutoff_hz * cutoff_ratio * Pow2((shape.env_oct + accent_) * env_.value());
  float f = fc * (1.0f / 48000.0f);
  if (f > 0.4f) f = 0.4f;
  f_[0].set_f_q<FREQUENCY_FAST>(f, shape.q);
  float g1 = f * 6.2831853f; if (g1 > 0.9f) g1 = 0.9f;
  float ev, ai, dc;
  int es;
  env_.Load(&ev, &es, &ai, &dc);
  float lp = lp_[0];
  for (size_t n = 0; n < size; ++n) {
    if (es == 1) { ev += ai; if (ev >= 1.0f) { ev = 1.0f; es = 2; } }
    else if (es == 2) { ev *= dc; }
    float e = ev * level;
    float a = f_[0].Process<FILTER_MODE_LOW_PASS>(out[n]);
    lp += g1 * (a - lp);
    out[n] = leveler_.Process(lp * e);   // the resonant filter can push loud engines over
  }
  if (es == 2 && ev < 0.0002f) { ev = 0.0f; es = 0; }
  env_.Store(ev, es);
  lp_[0] = lp;
}

}  // namespace dub
}  // namespace plaits
