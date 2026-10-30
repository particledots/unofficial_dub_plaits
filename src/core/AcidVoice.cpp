#include "AcidVoice.h"
#include <algorithm>
#include <cmath>

namespace dub {

namespace {
constexpr double kPi = 3.141592653589793;

// gen~ dsblep / dsclip, same arithmetic
inline double blep (double t, double dt)
{
    double r = 0;
    if (t < dt) { const double u = t / dt; r = u + u - u * u - 1; }
    else if (t > 1 - dt) { const double u = (t - 1) / dt; r = u * u + u + u + 1; }
    return r;
}

inline double softClip (double x)
{
    if (x > 3) return 1;
    if (x < -3) return -1;
    return x * (27 + x * x) / (27 + 9 * x * x);
}
} // namespace

void AcidVoice::prepare (double sampleRate)
{
    sr_ = sampleRate;
    r48_ = 48000.0 / sampleRate;
    silence();
    lvpeak_ = 0;
}

void AcidVoice::silence()
{
    phase_ = 0; freq_ = 0.0025; target_ = 0.0025; glide_ = 1;
    a0_ = a1_ = a2_ = a3_ = 0; hp_ = wlp_ = 0;
    fenv_ = 0; fdec_ = 0.9999; env_ = 0; sweep_ = 0;
    accent_ = 0; gate_ = 0; left_ = 0;
}

void AcidVoice::gateOff()
{
    gate_ = 0;
    left_ = 0;
}

void AcidVoice::note (int nt, int vel, bool slide, double lengthMs)
{
    target_ = 440 * std::exp2 ((nt - 69) / 12.0) / sr_;
    const int accOn = vel >= 100 ? 1 : 0;
    if (slide && gate_ != 0)
    {
        glide_ = 1 - std::pow (1 - 0.0012, r48_);
    }
    else
    {
        glide_ = 1;
        fenv_ = 1;
        fdec_ = accOn ? 1 - 6.9 / (0.2 * sr_) : 1 - 6.9 / (0.55 * sr_);
        if (accOn) sweep_ = sweep_ + 0.45 * (1 - sweep_);
    }
    accent_ = accOn;
    gate_ = 1;
    left_ = lengthMs > 0 ? std::max (1.0, std::floor (lengthMs * 0.001 * sr_)) : -1;
}

double AcidVoice::process (double ev, double kh, double kt, double km)
{
    // ---- ACID: FREQUENCY = env mod, HARMONICS = cutoff, MORPH = resonance, TIMBRE = accent
    ev = std::min (1.0, std::max (0.0, ev));
    const double aacc = accent_ ? kt : 0;
    const double baseHz = 90 * std::exp2 (kh * 84 / 12);
    const double envOct = 0.5 + ev * 4.5 + aacc * 1.6;
    const double sweepOct = sweep_ * kt * 1.2;
    double ak = 3.55 * km * (2 - km) * 0.5 + 0.25 * km;
    ak = std::min (ak, 3.6);
    const double inGain = 1 + 0.55 * ak;
    const double ampOn = accent_ ? 1 + 0.7 * kt : 0.72;
    freq_ = freq_ + (target_ - freq_) * glide_;
    phase_ = phase_ + freq_;
    if (phase_ >= 1) phase_ = phase_ - 1;
    const double saw = 2 * phase_ - 1 - blep (phase_, freq_);
    if (left_ > 0)
    {
        left_ = left_ - 1;
        if (left_ <= 0) gate_ = 0;
    }
    const double atgt = gate_ ? ampOn : 0;
    env_ = env_ + (atgt - env_) * (gate_ ? 1 - std::pow (0.98, r48_) : 1 - std::pow (0.9965, r48_));
    fenv_ = fenv_ * fdec_;
    const double fc = std::min (baseHz * std::exp2 (envOct * fenv_ + sweepOct), 10560.0);
    const double g = std::tan (kPi * fc / sr_);
    const double G = g / (1 + g);
    const double G2 = G * G;
    const double G4 = G2 * G2;
    const double S = (1 - G) * (G2 * G * a0_ + G2 * a1_ + G * a2_ + a3_);
    const double x = saw * 0.5 * inGain;
    const double yest = (G4 * x + S) / (1 + ak * G4);
    const double u = softClip ((x - ak * yest) * 1.4) * 0.72;
    double v = (u - a0_) * G;
    double y = v + a0_;
    a0_ = y + v;
    v = (y - a1_) * G;
    y = v + a1_;
    a1_ = y + v;
    v = (y - a2_) * G;
    y = v + a2_;
    a2_ = y + v;
    v = (y - a3_) * G;
    y = v + a3_;
    a3_ = y + v;
    wlp_ = wlp_ + (1 - std::pow (0.45, r48_)) * (y - wlp_);
    hp_ = hp_ + (1 - std::pow (1 - 0.0046, r48_)) * (wlp_ - hp_);
    sweep_ = sweep_ * std::pow (0.9985, r48_ / 12);
    const double lvin = (wlp_ - hp_) * env_ * 1.5;
    double lvx = lvin * 0.5;
    double lva = std::fabs (lvx);
    if (lva > lvpeak_) lvpeak_ = lvpeak_ + (lva - lvpeak_) * (1 - std::pow (0.75, r48_));
    else lvpeak_ = lvpeak_ * std::pow (0.99996, r48_);
    if (lvpeak_ > 0.5) lvx = lvx * 0.5 / lvpeak_;
    lva = std::fabs (lvx);
    if (lva > 0.5)
    {
        const double k = 0.5 + 0.5 * softClip ((lva - 0.5) * 2);
        lvx = lvx < 0 ? -k : k;
    }
    return lvx * 0.6;
}

} // namespace dub
