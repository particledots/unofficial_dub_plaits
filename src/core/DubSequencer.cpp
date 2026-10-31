// C++ port of dubstab.js (Narti Live, MIT, Copyright (c) 2026 Narti Live). Function order and names follow the
// JavaScript so the two can be read side by side. Comments marked "JS:" say where it follows the original closely.
#include "DubSequencer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dub {

// ------------------------------------------------------------------ constants (same tables as the JavaScript)

static const int SCALES[4][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },   // Natural minor (Aeolian)
    { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
    { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
    { 0, 2, 3, 5, 7, 8, 11 }    // Harmonic minor
};
static const char* NOTE_NAMES[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
// Root menu C..B -> semitones from C, kept within -5..+6 so the register stays where the module has it (G = -5).
static const int ROOT_OFFSET[12] = { 0, 1, 2, 3, 4, 5, 6, -5, -4, -3, -2, -1 };
// Ticks (480 per quarter) per step:  1/2 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
static const int RATE_TICKS[8] = { 960, 480, 240, 160, 120, 80, 60, 40 };
static const double RANK_BIAS[16] = { 0.5, 0.0, 1.3, 0.35, 0.15, 0.0, 1.2, 0.25,
                                      0.4, 0.0, 1.25, 0.35, 0.1, 0.0, 1.1, 0.45 };
static const double BASE_WEIGHTS[7] = { 4.0, 0.7, 1.5, 2.5, 2.0, 2.0, 1.0 };
// DEEP: open dub techno shapes in root position (as the G minor firmware v1.1).
static const int DEEP_COUNT[8] = { 5, 5, 4, 4, 3, 3, 3, 4 };
static const int DEEP_STEPS[8][5] = {
    { 0, 4, 6, 9, 11 },   // DUB m7      1 5 b7 b3' 5'
    { 0, 4, 6, 8, 9 },    // m9 open     1 5 b7 9 b3'
    { 0, 3, 6, 9, 0 },    // m11 (4ths)  1 4 b7 b3'
    { 0, 4, 6, 10, 0 },   // 7sus4       1 5 b7 4'
    { 0, 4, 8, 0, 0 },    // sus2        1 5 9
    { 0, 4, 7, 0, 0 },    // fifths      1 5 8
    { 0, 4, 9, 0, 0 },    // open minor  1 5 b3'
    { 0, 2, 4, 8, 0 }     // m(add9)     1 b3 5 9
};
static const double DEEP_WEIGHT[8] = { 2.0, 2.0, 2.2, 1.2, 1.0, 1.0, 0.7, 0.5 };
static const int DEEP_NO_THIRD[8] = { 3, 3, 3, 3, 4, 5, 4, 4 };   // on degrees with a major 3rd
static const double DEEP_DEGREE_WEIGHTS[7] = { 5.0, 0.6, 0.6, 2.5, 2.0, 1.2, 1.5 };
static const int DUB_STEPS[5] = { 0, 4, 6, 9, 11 };                // root 5th 7th 3rd' 5th'
static const int TRIAD_STEPS[3] = { 0, 2, 4 };
static const int SEVENTH_STEPS[4] = { 0, 2, 4, 6 };
static const int ACID_OFFSETS[8] = { 0, 0, 7, 0, 4, 0, 2, 7 };

// JS: Math.round
double jsRound (double x)
{
    const double f = std::floor (x);
    return (x - f >= 0.5) ? f + 1.0 : f;
}

static int floorDiv7 (int d) { return d >= 0 ? d / 7 : -((-d + 6) / 7); }
static int mod7 (int d) { return d - 7 * floorDiv7 (d); }
static int floorDiv12 (int d) { return d >= 0 ? d / 12 : -((-d + 11) / 12); }
static int mod12 (int n) { return ((n % 12) + 12) % 12; }

// ------------------------------------------------------------------ random

// JS: (s * 2654435761 + 0x9e3779b9) >>> 0, in doubles exactly like the original (the product is not exact
// once the melody seed is large, and the JavaScript rounds it as a double, so this does too).
void DubSequencer::seedRandom (double s)
{
    const double x = std::trunc (s * 2654435761.0 + 2654435769.0);
    double m = std::fmod (x, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    rngState_ = (uint32_t) m;
    if (! rngState_) rngState_ = 1;
}

// JS: mulberry32, 0..1
double DubSequencer::rand01()
{
    rngState_ += 0x6D2B79F5u;
    uint32_t t = rngState_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}

// ------------------------------------------------------------------ harmony

int DubSequencer::scaleNote (int d) const { return SCALES[S.scale][mod7 (d)] + 12 * floorDiv7 (d); }

// True when the triad on this degree has a minor third (minor or diminished).
bool DubSequencer::isMinorDegree (int d) const { return scaleNote (d + 2) - scaleNote (d) == 3; }

// Minor-only: a degree with a major chord is replaced by the nearest minor one,
// trying the relative minor a third below first (it shares most notes).
int DubSequencer::allowedDegree (int d) const
{
    if (! S.minor || isMinorDegree (d)) return d;
    static const int tries[6] = { -2, 2, -1, 1, -3, 3 };
    for (int i = 0; i < 6; ++i)
        if (isMinorDegree (d + tries[i])) return d + tries[i];
    return d;
}

int DubSequencer::keyOffset() const { return ROOT_OFFSET[S.root]; }

// voicing: 0 = DUB, 1-3 = triad (inversion 0-2), 4-7 = seventh (inversion 0-3), 10-17 = DEEP shape 0..7
Chord DubSequencer::buildChord (int degree, int voicing) const
{
    degree = allowedDegree (degree);
    const int* steps = DUB_STEPS;
    int n = 5, inversion = 0;
    if (voicing >= 10)
    {
        int sh = voicing - 10;
        if (scaleNote (degree + 2) - scaleNote (degree) == 4) sh = DEEP_NO_THIRD[sh];
        steps = DEEP_STEPS[sh];
        n = DEEP_COUNT[sh];
    }
    else if (voicing >= 1 && voicing <= 3) { steps = TRIAD_STEPS; n = 3; inversion = voicing - 1; }
    else if (voicing >= 4 && voicing <= 7) { steps = SEVENTH_STEPS; n = 4; inversion = voicing - 4; }

    const int base = 12 * (S.octave + 1) + keyOffset();
    Chord c;
    c.count = n;
    int raw[6] = {};
    for (int i = 0; i < n; ++i) raw[i] = base + scaleNote (degree + steps[i]);
    c.root = raw[0];
    for (int i = 0; i < inversion; ++i) raw[i] += 12;
    for (int i = 1; i < n; ++i)                                  // JS: raw.sort(numeric); at most 5 notes, so insertion sort
    {
        const int v = raw[i];
        int j = i - 1;
        while (j >= 0 && raw[j] > v) { raw[j + 1] = raw[j]; --j; }
        raw[j + 1] = v;
    }
    const int centreSum = (base + 7 + 12 * floorDiv7 (degree)) * n;
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += raw[i];
    int bestShift = 0, best = 1000000000;
    for (int shift = -12; shift <= 12; shift += 12)
    {
        const int dd = std::abs (sum + shift * n - centreSum);
        if (dd < best) { best = dd; bestShift = shift; }
    }
    for (int i = 0; i < n; ++i) c.notes[(size_t) i] = raw[i] + bestShift;
    c.degree = degree;
    c.voicing = voicing;
    return c;
}

int DubSequencer::semitonesToDegrees (int st) const
{
    const int o = floorDiv12 (st), pc = st - 12 * o;
    const int* sc = SCALES[S.scale];
    int best = 0, bestD = 99;
    for (int i = 0; i < 7; ++i)
    {
        const int d = std::abs (pc - sc[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best + 7 * o;
}

DubSequencer::Acid DubSequencer::acidNote (int degree, int step) const
{
    degree = allowedDegree (degree);
    const Pattern& P = sections_[(size_t) (step / kNumSteps)];
    const int stepIndex = step % kNumSteps, rank = P.rank[(size_t) stepIndex];
    const int h = (rank * 5 + stepIndex * 3 + P.acidShift[(size_t) stepIndex]) & 7;
    const int d = degree + ACID_OFFSETS[h];
    const int base = 12 * (S.octave + 2) + keyOffset();     // one octave above the chords
    return { base + scaleNote (d), h == 3 || h == 6 };
}

// CHORDS off: one note, from the chord root up to two octaves, in the chords register.
int DubSequencer::monoNote (int degree, int st) const
{
    degree = allowedDegree (degree);
    return 12 * (S.octave + 1) + keyOffset() + scaleNote (degree + sections_[(size_t) (st / kNumSteps)].mono[(size_t) (st % kNumSteps)]);
}

std::string DubSequencer::noteName (int n)
{
    return std::string (NOTE_NAMES[mod12 (n)]) + std::to_string ((int) std::floor ((double) n / 12.0 - 1.0));
}

std::string DubSequencer::chordName (const Chord& c) const
{
    if (c.voicing >= 10) return deepName (c);
    const int d = c.degree;
    const int r = scaleNote (d), t = scaleNote (d + 2) - r, f = scaleNote (d + 4) - r, s = scaleNote (d + 6) - r;
    std::string q;
    if (t == 3 && f == 7) q = "m"; else if (t == 4 && f == 7) q = "";
    else if (t == 3 && f == 6) q = "dim"; else if (t == 4 && f == 8) q = "aug"; else q = "?";
    const bool seventh = c.voicing == 0 || c.voicing >= 4;
    if (seventh)
    {
        if (q == "m") q = s == 10 ? "m7" : "mMaj7";
        else if (q == "") q = s == 11 ? "maj7" : "7";
        else if (q == "dim") q = s == 10 ? "m7b5" : "dim7";
        else if (q == "aug") q = s == 11 ? "maj7#5" : "7#5";
    }
    const int pc = mod12 (c.root);
    std::string name = std::string (NOTE_NAMES[pc]) + q;
    const int bass = mod12 (c.notes[0]);
    if (c.voicing != 0 && bass != pc) name += std::string ("/") + NOTE_NAMES[bass];
    return name;
}

// Names a chord from its notes (DEEP shapes: sus, 5, m9, m11, add9).
std::string DubSequencer::deepName (const Chord& c) const
{
    const int pc = mod12 (c.root);
    bool has[12] = {};
    for (int i = 0; i < c.count; ++i) has[mod12 (c.notes[(size_t) i] - c.root)] = true;
    std::string q;
    if (! has[3] && ! has[4])
        q = has[5] ? (has[10] ? "7sus4" : (has[11] ? "maj7sus4" : "sus4"))
          : (has[2] ? (has[10] ? "9sus" : "sus2")
          : (has[6] && has[7] ? (has[11] ? "maj7#11" : "(#11)") : (has[6] ? "(b5)" : "5")));
    else if (has[4]) q = has[11] ? "maj7" : (has[10] ? "7" : "");
    else if (has[6] && ! has[7]) q = has[10] ? "m7b5" : "dim";
    else if (has[10]) q = has[2] ? "m9" : (has[5] ? "m11" : "m7");
    else q = has[2] ? "m(add9)" : "m";
    return std::string (NOTE_NAMES[pc]) + q;
}

// ------------------------------------------------------------------ pattern

int DubSequencer::pickDegree (int avoid)
{
    double w[7], total = 0;
    for (int i = 0; i < 7; ++i)
    {
        w[i] = (S.minor && ! isMinorDegree (i)) ? 0.0 : (S.voicing == kDeepVoicing ? DEEP_DEGREE_WEIGHTS[i] : BASE_WEIGHTS[i]);
        if (i != avoid) total += w[i];
    }
    double r = rand01() * total;
    for (int i = 0; i < 7; ++i)
    {
        if (i == avoid || w[i] == 0) continue;
        r -= w[i];
        if (r <= 0) return i;
    }
    return 0;
}

// Same recipe as the firmware: 1, 2 or 4 chords per pattern, mostly held.
void DubSequencer::generatePattern (Pattern& P, int section)
{
    const double r = rand01();
    const int segments = r < 0.35 ? 1 : (r < 0.75 ? 2 : 4);
    int chord[4] = {};
    chord[0] = rand01() < 0.7 ? 0 : pickDegree (-1);
    for (int k = 1; k < segments; ++k) chord[k] = pickDegree (chord[k - 1]);
    for (int i = 0; i < kNumSteps; ++i)
    {
        int d = chord[i * segments / kNumSteps];
        if (rand01() < 0.06) d = pickDegree (d);
        P.degree[(size_t) i] = d;
    }
    int voicing[4] = {};
    for (int k = 0; k < segments; ++k)
    {
        const double q = rand01(), a = rand01();
        if (S.voicing == 1) voicing[k] = 1 + (int) std::floor (a * 2.999);
        else if (S.voicing == 2) voicing[k] = 4 + (int) std::floor (a * 3.999);
        else if (S.voicing == 3) voicing[k] = q < 0.4 ? 1 + (int) std::floor (a * 2.999) : (q < 0.8 ? 4 + (int) std::floor (a * 3.999) : 0);
        else voicing[k] = 0;
    }
    if (S.voicing == kDeepVoicing)
    {
        // One shape for the whole pattern 65 % of the time, else one per section.
        double total = 0;
        const bool oneShape = rand01() < 0.65;
        for (int i = 0; i < 8; ++i) total += DEEP_WEIGHT[i];
        for (int k = 0; k < segments; ++k)
        {
            if (k > 0 && oneShape) { voicing[k] = voicing[0]; continue; }
            double rr = rand01() * total;
            int v = 0;
            while (v < 7 && (rr -= DEEP_WEIGHT[v]) > 0) ++v;
            voicing[k] = 10 + v;
        }
    }
    for (int i = 0; i < kNumSteps; ++i) P.voicing[(size_t) i] = voicing[i * segments / kNumSteps];

    double score[16];
    int index[16];
    for (int i = 0; i < kNumSteps; ++i) { score[i] = RANK_BIAS[i] + rand01() * 0.9; index[i] = i; }
    // JS: index.sort(function (a, b) { return score[b] - score[a] || a - b; })  -> higher score first, ties by index
    std::sort (index, index + kNumSteps, [&] (int a, int b) { return score[a] != score[b] ? score[a] > score[b] : a < b; });
    for (int i = 0; i < kNumSteps; ++i) P.rank[(size_t) index[i]] = i;
    for (int i = 0; i < kNumSteps; ++i) P.accent[(size_t) i] = rand01() < (RANK_BIAS[i] > 1.0 ? 0.35 : 0.15) ? 1 : 0;

    // Mono line (CHORDS off): the firmware's random walk over two octaves of the scale, starting from each
    // step's chord root. Melody 0 = the line that comes with the pattern; other melody seeds give new lines
    // (and new ACID lines) over the same chords and rhythm.
    if (S.melody) seedRandom ((double) S.seed * 7919.0 + (double) S.melody * 104729.0 + 17.0 + (double) section * 1299709.0);
    for (int i = 0; i < kNumSteps; ++i) P.acidShift[(size_t) i] = S.melody ? (int) std::floor (rand01() * 8) : 0;
    int n = rand01() < 0.5 ? 0 : 7;
    for (int i = 0; i < kNumSteps; ++i)
    {
        const double w = rand01();
        if (w < 0.25) {
        } else if (w < 0.7) {
            n += rand01() < 0.5 ? -1 : 1;
        } else if (w < 0.9) {
            n += rand01() < 0.5 ? -2 : 2;
        } else {
            const int a = rand01() < 0.5 ? 0 : 7;
            const int b = rand01() < 0.3 ? 4 : 0;
            n = a + b;
        }
        if (n < 0) n = 1;
        if (n > 13) n = 12;
        P.mono[(size_t) i] = n;
    }
}

// Bar 1 is the original's pattern (same random numbers, so the cross-check against dubstab.js still applies to it). Bars 2-4,
// used by Steps 17-64, are further patterns of the same recipe with their own random numbers from the same seed.
void DubSequencer::generate()
{
    seedRandom (S.seed);
    generatePattern (sections_[0], 0);
    for (int k = 1; k < kSections; ++k)
    {
        seedRandom ((double) S.seed * 131.0 + (double) k * 99991.0 + 500009.0);
        generatePattern (sections_[(size_t) k], k);
    }
    showPattern (-1);
}

// ------------------------------------------------------------------ output

int DubSequencer::rateTicks (int rate) { return RATE_TICKS[std::max (0, std::min (7, rate))]; }

std::string DubSequencer::patternText (const Settings& s, const Pattern& p, int current)
{
    std::string out;
    const int len = std::max (1, std::min (16, s.length));
    for (int i = 0; i < 16; ++i)
    {
        const bool on = p.rank[(size_t) i] < s.density;
        char ch = i >= len ? ' ' : (on ? (p.accent[(size_t) i] ? 'X' : 'x') : '.');
        if (i == current) ch = on ? '#' : '_';
        out += ch;
        if (i == 3 || i == 7 || i == 11) out += ' ';
    }
    return out;
}

void DubSequencer::push (Event e)
{
    e.tick = now_;
    events_.push_back (std::move (e));
}

void DubSequencer::showPattern (int current)
{
    if (! emitText) return;
    // The text shows one bar of 16: the bar the current step is in (bar 1 when nothing is running).
    const int section = current >= 0 ? std::min (kSections - 1, current / kNumSteps) : 0;
    Settings shown = S;
    if (S.length > kNumSteps) shown.length = std::max (1, std::min (kNumSteps, S.length - section * kNumSteps));
    Event e; e.kind = Event::PatternText;
    e.text = patternText (shown, sections_[(size_t) section], current >= 0 ? current % kNumSteps : -1);
    push (std::move (e));
}

// MIDI out (Mode::Midi): notes held until an off tick.
void DubSequencer::midiNoteOn (int note, int vel)
{
    if (note < 0 || note > 127) return;
    for (int i = (int) pendingOffs_.size() - 1; i >= 0; --i)
        if (pendingOffs_[(size_t) i].note == note)
        {
            Event off; off.kind = Event::MidiOff; off.note = note; push (off);
            pendingOffs_.erase (pendingOffs_.begin() + i);
        }
    Event on; on.kind = Event::MidiOn; on.note = note; on.vel = vel; push (on);
}

void DubSequencer::scheduleOff (int note, double tick) { pendingOffs_.push_back ({ tick, note }); }

void DubSequencer::processOffs (double tick)
{
    for (int i = (int) pendingOffs_.size() - 1; i >= 0; --i)
        if (pendingOffs_[(size_t) i].tick <= tick)
        {
            Event off; off.kind = Event::MidiOff; off.note = pendingOffs_[(size_t) i].note; push (off);
            pendingOffs_.erase (pendingOffs_.begin() + i);
        }
}

void DubSequencer::allOff()
{
    for (const auto& p : pendingOffs_) { Event off; off.kind = Event::MidiOff; off.note = p.note; push (off); }
    pendingOffs_.clear();
}

void DubSequencer::genEvent (int type, int note, int vel)
{
    Event e; e.kind = Event::Gen; e.type = type;
    e.note = std::max (0, std::min (127, note));
    e.vel = std::max (1, std::min (127, vel));
    push (e);
}

// ------------------------------------------------------------------ order of the steps

namespace {
// A small integer hash (lowbias32-style) used for every "random" order, so they depend only on the seed and the position.
uint32_t mix32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
uint32_t hash4 (uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = mix32 (a + 0x9E3779B9U);
    h = mix32 (h ^ (b + 0x85EBCA6BU));
    h = mix32 (h ^ (c + 0xC2B2AE35U));
    return mix32 (h ^ (d + 0x27D4EB2FU));
}
constexpr long long kEpoch = 256;       // Drunk / Chance restart from a fresh position every 256 steps, so any step can be found quickly
} // namespace

int DubSequencer::stepForIndex (long long n) const
{
    const int L = std::max (1, std::min (kMaxSteps, S.length));
    if (n < 0) n = 0;
    const uint32_t seed = (uint32_t) S.seed;
    const uint32_t nLo = (uint32_t) (n & 0xffffffffLL), nHi = (uint32_t) ((unsigned long long) n >> 32);
    switch (S.direction)
    {
        case DirReverse:
            return L - 1 - (int) (n % L);
        case DirPendulum:           // up, then back down, the two end steps played once each time round
        {
            if (L < 2) return 0;
            const long long period = 2LL * L - 2;
            const int m = (int) (n % period);
            return m < L ? m : (int) (period - m);
        }
        case DirPingPong:           // up, then back down, the two end steps played twice
        {
            const int m = (int) (n % (2LL * L));
            return m < L ? m : 2 * L - 1 - m;
        }
        case DirRandom:
            return (int) (hash4 (seed, nLo, nHi, 1) % (uint32_t) L);
        case DirRandomNoRepeat:     // every step once, in a new order each time round
        {
            int perm[kMaxSteps];
            for (int i = 0; i < L; ++i) perm[i] = i;
            const uint32_t cycleLo = (uint32_t) ((n / L) & 0xffffffffLL);
            for (int i = L - 1; i > 0; --i)
            {
                const int j = (int) (hash4 (seed, cycleLo, (uint32_t) i, 2) % (uint32_t) (i + 1));
                std::swap (perm[i], perm[j]);
            }
            return perm[n % L];
        }
        case DirDrunk:              // each step moves one to the left, one to the right, or stays
        {
            const uint32_t epoch = (uint32_t) ((n / kEpoch) & 0xffffffffLL);
            const int m = (int) (n % kEpoch);
            int c = (int) (hash4 (seed, epoch, 0, 3) % (uint32_t) L);
            for (int i = 1; i <= m; ++i)
            {
                const uint32_t r = hash4 (seed, epoch, (uint32_t) i, 4) % 10;
                c += r < 4 ? -1 : (r < 8 ? 1 : 0);
                c = ((c % L) + L) % L;
            }
            return c;
        }
        case DirChance:             // runs of random length, each in an order picked by the weights
        {
            const uint32_t epoch = (uint32_t) ((n / kEpoch) & 0xffffffffLL);
            const int m = (int) (n % kEpoch);
            auto draw = [&] (uint32_t k) { return hash4 (seed, epoch, k, 5); };
            const int lo = std::max (1, std::min (S.runMin, S.runMax)), hi = std::max (S.runMin, S.runMax);
            uint32_t k = 0;
            int c = (int) (draw (k++) % (uint32_t) L), pdir = 1, mode = -1, remaining = 0;
            for (int i = 0; i <= m; ++i)
            {
                if (remaining <= 0)
                {
                    // pick the next order; the one just finished is left out unless it is the only one with a weight
                    int total = 0, others = 0;
                    for (int q = 0; q < kNumChanceModes; ++q) { total += S.dirWeight[q]; if (q != mode) others += S.dirWeight[q]; }
                    const int usable = others > 0 ? others : total;
                    int pick = ChForward;
                    if (usable > 0)
                    {
                        int r = (int) (draw (k++) % (uint32_t) usable);
                        for (int q = 0; q < kNumChanceModes; ++q)
                        {
                            const int w = (others > 0 && q == mode) ? 0 : S.dirWeight[q];
                            if (r < w) { pick = q; break; }
                            r -= w;
                        }
                    }
                    mode = pick;
                    remaining = lo + (hi > lo ? (int) (draw (k++) % (uint32_t) (hi - lo + 1)) : 0);
                }
                if (i > 0)
                {
                    switch (mode)
                    {
                        case ChReverse:  c = (c + L - 1) % L; break;
                        case ChPendulum:
                            if (L > 1)
                            {
                                c += pdir;
                                if (c >= L) { c = L - 2; pdir = -1; }
                                else if (c < 0) { c = 1; pdir = 1; }
                            }
                            break;
                        case ChPingPong:
                            if (c + pdir >= L || c + pdir < 0) pdir = -pdir; else c += pdir;
                            break;
                        case ChRandom:   c = (int) (draw (k++) % (uint32_t) L); break;
                        case ChDrunk:
                        {
                            const uint32_t r = draw (k++) % 10;
                            c += r < 4 ? -1 : (r < 8 ? 1 : 0);
                            c = ((c % L) + L) % L;
                            break;
                        }
                        default:         c = (c + 1) % L; break;
                    }
                }
                --remaining;
            }
            return c;
        }
        case DirForward:
        default:
            return (int) (n % L);
    }
}

bool DubSequencer::stepPlays (int step) const
{
    step = std::max (0, std::min (kMaxSteps - 1, step));
    return sections_[(size_t) (step / kNumSteps)].rank[(size_t) (step % kNumSteps)] < S.density;
}

bool DubSequencer::stepAccented (int step) const
{
    step = std::max (0, std::min (kMaxSteps - 1, step));
    return sections_[(size_t) (step / kNumSteps)].accent[(size_t) (step % kNumSteps)] != 0;
}

// ------------------------------------------------------------------ clock

void DubSequencer::ticks (double t)
{
    const double g = jsRound (t * (480.0 / 480.0) / (double) kGrid) * kGrid;
    if (g == lastGrid_) return;
    lastGrid_ = g;
    now_ = t;
    processOffs (g);
    if (g < 0) return;                       // before the start of the song: nothing (the original has no such case)
    const int rt = RATE_TICKS[S.rate];
    const long long gi = (long long) g;
    if (gi % rt != 0) return;
    const long long n = gi / rt;
    fireStep (stepForIndex (n), g, rt);
}

void DubSequencer::fireStep (int st, double g, int rt)
{
    const Pattern& P = sections_[(size_t) (st / kNumSteps)];
    const int si = st % kNumSteps;                       // step within its bar of 16
    const bool plays = P.rank[(size_t) si] < S.density;
    showPattern (st);
    if (! plays) { lastTickFired_ = false; return; }
    const int accent = P.accent[(size_t) si];
    const int degree = P.degree[(size_t) si] + S.transpose;
    const Chord c = buildChord (degree, P.voicing[(size_t) si]);
    const int vel = accent ? 127 : 90;
    const bool useAcid = (mode_ == Mode::Both) ? (S.model == 7) : (S.output == 1);
    auto text = [&] (const std::string& s) { if (emitText) { Event e; e.kind = Event::ChordText; e.text = s; push (std::move (e)); } };
    if (useAcid)
    {
        const Acid a = acidNote (degree, st);
        const bool slide = a.slide && lastTickFired_;
        const double gateTicks = rt * (slide ? 1.05 : 0.55);
        if (mode_ == Mode::Midi)
        {
            midiNoteOn (a.note, vel);
            scheduleOff (a.note, g + std::max ((double) kGrid, jsRound (gateTicks / kGrid) * kGrid));
        }
        else
        {
            Event al; al.kind = Event::AcidLen; al.ms = (rt / 480.0 * 60000.0 / tempoBpm_) * (slide ? 1.05 : 0.55); push (al);
            genEvent (slide ? 2 : 1, a.note, vel);
        }
        if (emitText) text (noteName (a.note) + (slide ? " slide" : "") + "  (" + chordName (c) + ")");
    }
    else if (! S.chords && ! (mode_ == Mode::Midi && S.output == 2))
    {
        const int mn = monoNote (degree, st);
        if (mode_ == Mode::Midi)
        {
            midiNoteOn (mn, vel);
            scheduleOff (mn, g + std::max ((double) kGrid, jsRound (rt * S.gate / kGrid) * kGrid));
        }
        else
            genEvent (0, mn, vel);
        if (emitText) text (noteName (mn) + "  (" + chordName (c) + ")");
    }
    else if (mode_ == Mode::Midi)
    {
        const double gt = g + std::max ((double) kGrid, jsRound (rt * S.gate / kGrid) * kGrid);
        if (S.output == 2)           // roots: one note, one octave below the chord
        {
            midiNoteOn (c.root - 12, vel);
            scheduleOff (c.root - 12, gt);
        }
        else
            for (int i = 0; i < c.count; ++i) { midiNoteOn (c.notes[(size_t) i], vel); scheduleOff (c.notes[(size_t) i], gt); }
        if (emitText) text (chordName (c));
    }
    else
    {
        for (int j = 0; j < c.count; ++j) genEvent (0, c.notes[(size_t) j], vel);
        if (emitText) text (chordName (c));
    }
    lastTickFired_ = true;
}

void DubSequencer::state (int playing)
{
    if (! playing)
    {
        allOff();
        if (mode_ == Mode::Both) genEvent (3, 0, 1);
        lastGrid_ = -1;
        lastTickFired_ = false;
        showPattern (-1);
    }
}

void DubSequencer::noteIn (int midiNote) { S.transpose = semitonesToDegrees (midiNote - 60); }

// ------------------------------------------------------------------ parameters

void DubSequencer::setInt (int& field, double v, int lo, int hi, bool regen)
{
    v = jsRound (v);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if ((double) field == v) return;
    field = (int) v;
    if (regen) generate(); else showPattern (-1);
}

void DubSequencer::setRoot (double v) { setInt (S.root, v, 0, 11, false); }
void DubSequencer::setScale (double v) { setInt (S.scale, v, 0, 3, true); }
void DubSequencer::setMinor (double v) { setInt (S.minor, v, 0, 1, true); }
void DubSequencer::setVoicing (double v) { setInt (S.voicing, v, 0, 4, true); }
void DubSequencer::setOutput (double v) { setInt (S.output, v, 0, 2, false); allOff(); }
void DubSequencer::setDensity (double v) { setInt (S.density, v, 0, 16, false); }
void DubSequencer::setSteps (double v) { setInt (S.length, v, 1, kMaxSteps, false); }
void DubSequencer::setDirection (double v) { setInt (S.direction, v, 0, kNumDirections - 1, false); }
void DubSequencer::setDirWeight (int mode, double v)
{
    if (mode >= 0 && mode < kNumChanceModes) setInt (S.dirWeight[mode], v, 0, 100, false);
}
void DubSequencer::setRunMin (double v) { setInt (S.runMin, v, 1, kMaxSteps, false); }
void DubSequencer::setRunMax (double v) { setInt (S.runMax, v, 1, kMaxSteps, false); }
void DubSequencer::setRate (double v) { setInt (S.rate, v, 0, 7, false); }
void DubSequencer::setOctave (double v) { setInt (S.octave, v, 1, 5, false); }
void DubSequencer::setGate (double v) { S.gate = std::max (0.05, std::min (2.0, v)); }
void DubSequencer::setSeed (double v) { setInt (S.seed, v, 0, 99999, true); }
void DubSequencer::setMelody (double v) { setInt (S.melody, v, 0, 99999, true); }

void DubSequencer::setModel (double v)
{
    v = jsRound (v);
    if (v == (double) S.model) return;
    if (S.model == 7) genEvent (3, 0, 1);
    S.model = (int) v;
}

void DubSequencer::setChords (double v)
{
    const int c = v != 0 ? 1 : 0;
    if (c == S.chords) return;
    S.chords = c;
    allOff();
}

DubSequencer::DubSequencer()
{
    events_.reserve (1024);
    pendingOffs_.reserve (64);
    generate();
}

} // namespace dub
