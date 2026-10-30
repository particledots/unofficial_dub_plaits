#include "DubSynth.h"
#include "AcidVoice.h"
#include "Resampler.h"

// Some upstream Plaits headers rely on these being pulled in by something else.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dub/dub.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

namespace dub {

const char* synthModelName (int model)
{
    static const char* const names[kNumSynthModels] = {
        "DUB SAW", "VA", "WAVESHAPE", "FM", "PHASE DIST", "CHIPTUNE (stand-in for VHIKK)", "STRINGS", "ACID" };
    return model >= 0 && model < kNumSynthModels ? names[model] : "?";
}

namespace {

constexpr int kBlock = DubSynth::kEngineBlock;
static_assert (kBlock == (int) plaits::kBlockSize, "the engine block is Plaits' block size");

// Events reach the synth this many engine samples later than their time. The synth renders whole blocks, so it is
// always up to one block ahead of the host; this delay keeps an event from arriving after its time has been rendered.
constexpr double kScheduleDelay = kBlock;

// The Plaits / dub.cc objects. The firmware keeps its voice in a global, i.e. in zeroed memory, and some engine members are
// never initialised explicitly; so these are always built in zero-filled memory.
struct PlaitsPart
{
    plaits::dub::PolySynth poly;
    plaits::dub::PostShaper post;
    plaits::StringMachineEngine strings;
    stmlib::BufferAllocator allocator;
    alignas (16) uint8_t arena[16384];
};

struct Cmd
{
    enum Kind { Chord, AcidNote, AcidNoteSlide, AcidGateOff, AcidLen, ChordSet };
    double t = 0;            // engine time (samples) at which it takes effect
    Kind kind = Chord;
    int note = 0, vel = 0;
    double ms = 0;
    int notes[plaits::dub::kMaxChordNotes] {};   // ChordSet: a whole stab at once (direct play)
    int count = 0, root = 60;
};

} // namespace

struct DubSynth::Impl
{
    Impl()
    {
        void* mem = std::calloc (1, sizeof (PlaitsPart));
        part = new (mem) PlaitsPart();
    }
    ~Impl()
    {
        part->~PlaitsPart();
        std::free (part);
    }

    // ---- setup ----
    void prepare (double hostRate, int maxBlock)
    {
        host = hostRate;
        ratio = DubSynth::kEngineRate / hostRate;
        maxHostBlock = std::max (1, maxBlock);
        rs.init (DubSynth::kEngineRate, hostRate, (size_t) kBlock);
        rsAux.init (DubSynth::kEngineRate, hostRate, (size_t) kBlock);
        hostQ.clear();
        hostQ.reserve ((size_t) maxHostBlock + 512);
        hostAuxQ.clear();
        hostAuxQ.reserve ((size_t) maxHostBlock + 512);
        pending.clear();
        pending.reserve (256);
        reset();
    }

    void reset()
    {
        part->allocator.Init (part->arena, sizeof (part->arena));
        part->poly.Init();
        part->post.Init();
        part->strings.Init (&part->allocator);
        part->strings.Reset();
        // The firmware sets these when it registers the engine (voice.cc: RegisterInstance(&string_machine_engine_, false, 0.8, 0.8)).
        part->strings.post_processing_settings.already_enveloped = false;
        part->strings.post_processing_settings.out_gain = 0.8f;
        part->strings.post_processing_settings.aux_gain = 0.8f;
        acid.prepare (DubSynth::kEngineRate);
        rs.reset();
        rsAux.reset();
        hostQ.clear();
        hostAuxQ.clear();
        head = 0;
        pending.clear();
        pendingHead = 0;
        engineAbs = 0;
        hostAbs = 0;
        model = -1;
        lfoPhase = 0.9999f;        // pick the first random targets right away (as the firmware)
        for (int i = 0; i < 4; ++i) lfoFrom[i] = lfoTo[i] = lfoOut[i] = 0.0f;
        chordNote = 60.0f;
        acidLenMs = 0;
        nonFinite = 0;
        late = 0;
    }

    // ---- events ----
    void add (int hostSample, const Event& e)
    {
        Cmd c;
        c.t = (double) (hostAbs + hostSample) * ratio + kScheduleDelay;
        if (e.kind == Event::Gen)
        {
            c.note = e.note; c.vel = e.vel;
            switch (e.type)
            {
                case 0: c.kind = Cmd::Chord; break;
                case 1: c.kind = Cmd::AcidNote; break;
                case 2: c.kind = Cmd::AcidNoteSlide; break;
                default: c.kind = Cmd::AcidGateOff; break;
            }
        }
        else if (e.kind == Event::AcidLen)
        {
            c.kind = Cmd::AcidLen; c.ms = e.ms;
        }
        else return;
        pending.push_back (c);
    }

    void addChordSet (int hostSample, const int* notes, int count, int rootNote, bool accent)
    {
        if (count <= 0) return;
        Cmd c;
        c.t = (double) (hostAbs + hostSample) * ratio + kScheduleDelay;
        c.kind = Cmd::ChordSet;
        c.count = std::min (count, (int) plaits::dub::kMaxChordNotes);
        for (int i = 0; i < c.count; ++i) c.notes[i] = std::min (127, std::max (0, notes[i]));
        c.root = std::min (127, std::max (0, rootNote));
        c.vel = accent ? 127 : 90;
        pending.push_back (c);
    }

    // ---- one block of the engine (12 samples at the module rate) ----
    void changeModel (int m)
    {
        const bool wasPoly = model >= 0 && model <= kChiptune, nowPoly = m <= kChiptune;
        // Leaving a path silences it, so that a note held in it cannot come back when it is selected again.
        if (model >= 0)
        {
            if (wasPoly && ! nowPoly) part->poly.Init();
            if (model == kStrings && m != kStrings) part->post.Init();
            if (model == kAcid && m != kAcid) acid.silence();
        }
        if (m == kStrings) { part->strings.Reset(); part->post.Init(); }
        if (m == kAcid) acid.resetLimiter();
        model = m;
    }

    // NaN-safe: a NaN knob value becomes 0 instead of poisoning the filters for good.
    static inline float clamp01 (float x) { return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f; }

    void renderBlock (const SynthParams& p)
    {
        const double blockEnd = (double) engineAbs + kBlock;
        const int m = std::min (kNumSynthModels - 1, std::max (0, p.model));
        if (m != model) changeModel (m);

        // ---- the random drift LFO (always on; the firmware's rate, 0.12 Hz, new targets at each cycle) ----
        {
            const float rate = 0.05f * (float) std::exp2 (6.32 * 0.20);
            lfoPhase += rate * (float) (kBlock / DubSynth::kEngineRate);
            if (lfoPhase >= 1.0f)
            {
                lfoPhase -= 1.0f;
                for (int i = 0; i < 4; ++i)
                {
                    lfoFrom[i] = lfoOut[i];
                    lfoTo[i] = stmlib::Random::GetFloat() * 2.0f - 1.0f;
                }
            }
            float x = lfoPhase / 0.5f;
            if (x > 1.0f) x = 1.0f;
            x = x * x * (3.0f - 2.0f * x);
            for (int i = 0; i < 4; ++i) lfoOut[i] = lfoFrom[i] + (lfoTo[i] - lfoFrom[i]) * x;
        }
        const float depth = clamp01 (p.drift) * 0.5f;
        const float h = clamp01 (p.harmonics + lfoOut[0] * depth);
        const float t = clamp01 (p.timbre + lfoOut[1] * depth);
        const float mo = clamp01 (p.morph + lfoOut[2] * depth);

        plaits::dub::Settings s;
        plaits::dub::DefaultSettings (&s);
        const float envelope = clamp01 (p.envelope);
        s.envelope = envelope;
        s.resonance = clamp01 (p.resonance);
        plaits::dub::Shape shape;
        plaits::dub::ComputeShape (s, &shape);
        shape.cutoff_hz *= stmlib::SemitonesToRatio (lfoOut[3] * depth * 24.0f);

        // ---- events that are due ----
        bool fired = false;
        bool rootForced = false;      // direct play names the root itself (the sequencer's root is its lowest note)
        float forcedRoot = 60.0f;
        plaits::dub::Event chord;
        chord.num_notes = 0;
        chord.accent = false;
        chord.step = 0;
        chord.root = 60.0f;
        while (pendingHead < pending.size() && pending[pendingHead].t < blockEnd)
        {
            const Cmd c = pending[pendingHead++];
            if (c.t < (double) engineAbs) ++late;
            switch (c.kind)
            {
                case Cmd::Chord:
                    if (m == kAcid) break;
                    if (chord.num_notes < plaits::dub::kMaxChordNotes)
                        chord.notes[chord.num_notes++] = (float) c.note;
                    if (c.vel >= 100) chord.accent = true;
                    fired = true;
                    break;
                case Cmd::ChordSet:
                    if (m == kAcid) break;
                    chord.num_notes = 0;
                    for (int i = 0; i < c.count && i < plaits::dub::kMaxChordNotes; ++i) chord.notes[chord.num_notes++] = (float) c.notes[i];
                    chord.accent = c.vel >= 100;
                    rootForced = true;
                    forcedRoot = (float) c.root;
                    fired = true;
                    break;
                case Cmd::AcidLen:
                    acidLenMs = c.ms;
                    break;
                case Cmd::AcidNote:
                case Cmd::AcidNoteSlide:
                    if (m == kAcid) acid.note (c.note, c.vel, c.kind == Cmd::AcidNoteSlide, acidLenMs);
                    break;
                case Cmd::AcidGateOff:
                    if (m == kAcid) acid.gateOff();
                    break;
            }
        }
        if (fired && chord.num_notes > 0)
        {
            std::sort (chord.notes, chord.notes + chord.num_notes);
            chord.root = rootForced ? forcedRoot : chord.notes[0];
        }
        else fired = false;

        float out[kBlock], aux[kBlock];
        for (int i = 0; i < kBlock; ++i) aux[i] = 0.0f;
        if (m <= kChiptune)
        {
            if (fired) part->poly.NoteOn (chord, shape);
            part->poly.Render (m, h, t, mo, shape, 1.0f, out, aux, kBlock);
        }
        else if (m == kStrings)
        {
            // Exactly what the firmware's voice.cc does for this engine.
            plaits::StringMachineEngine& e = part->strings;
            if (fired)
            {
                part->post.Trigger (shape, chord.accent);
                float ratios[plaits::dub::kMaxChordNotes];
                chordNote = chord.notes[0];
                for (int i = 0; i < chord.num_notes; ++i)
                    ratios[i] = stmlib::SemitonesToRatio (chord.notes[i] - chord.notes[0]);
                e.mutable_chords()->set_custom_chord (ratios, chord.num_notes);
            }
            plaits::EngineParameters ep;
            ep.trigger = fired ? (plaits::TRIGGER_RISING_EDGE | plaits::TRIGGER_HIGH) : plaits::TRIGGER_LOW;
            ep.accent = fired && chord.accent ? 1.0f : 0.8f;
            ep.harmonics = h;
            ep.timbre = t;
            ep.morph = mo;
            ep.note = chordNote;
            bool alreadyEnveloped = false;
            e.Render (ep, out, aux, kBlock, &alreadyEnveloped);
            for (int i = 0; i < kBlock; ++i) aux[i] = 0.0f;       // the engine's own aux is not the sub-bass sine
            // HARMONICS has no chord to choose here: it opens the filter.
            part->post.Process (shape, 1.0f, stmlib::SemitonesToRatio ((h - 0.5f) * 36.0f), out, aux, kBlock);
            const float g = e.post_processing_settings.out_gain;   // 0.8 on the module (the firmware's registry entry)
            for (int i = 0; i < kBlock; ++i) out[i] *= g;
        }
        else
        {
            for (int i = 0; i < kBlock; ++i) out[i] = (float) acid.process (envelope, h, t, mo);
        }

        const float gain = p.gain;
        for (int i = 0; i < kBlock; ++i)
        {
            float v = out[i] * gain;
            if (! std::isfinite (v)) { v = 0.0f; ++nonFinite; }
            out[i] = v;
        }
        rs.process (out, (size_t) kBlock, hostQ);
        for (int i = 0; i < kBlock; ++i)
        {
            float v = aux[i] * gain;
            aux[i] = std::isfinite (v) ? v : 0.0f;
        }
        rsAux.process (aux, (size_t) kBlock, hostAuxQ);
        engineAbs += kBlock;
    }

    PlaitsPart* part = nullptr;
    AcidVoice acid;
    SincResampler rs, rsAux;
    std::vector<float> hostQ, hostAuxQ;
    size_t head = 0;
    std::vector<Cmd> pending;
    size_t pendingHead = 0;

    double host = 48000.0, ratio = DubSynth::kEngineRate / 48000.0;
    int maxHostBlock = 512;
    long long engineAbs = 0, hostAbs = 0;
    int model = -1;
    float lfoPhase = 0.9999f, lfoFrom[4] {}, lfoTo[4] {}, lfoOut[4] {};
    float chordNote = 60.0f;
    double acidLenMs = 0;
    long long nonFinite = 0, late = 0;
};

DubSynth::DubSynth() : impl_ (new Impl()) {}
DubSynth::~DubSynth() = default;

void DubSynth::prepare (double hostSampleRate, int maxBlock) { impl_->prepare (hostSampleRate, maxBlock); }
void DubSynth::reset() { impl_->reset(); }
void DubSynth::addEvent (int hostSample, const Event& e) { impl_->add (hostSample, e); }
void DubSynth::addChordNotes (int hostSample, const int* notes, int count, int rootNote, bool accent)
{
    impl_->addChordSet (hostSample, notes, count, rootNote, accent);
}

void DubSynth::process (float* outL, float* outR, int n, const SynthParams& p) { process (outL, outR, nullptr, n, p); }

void DubSynth::process (float* outL, float* outR, float* outAux, int n, const SynthParams& p)
{
    Impl& s = *impl_;
    while (s.hostQ.size() - s.head < (size_t) n) s.renderBlock (p);
    std::copy (s.hostQ.begin() + (std::ptrdiff_t) s.head, s.hostQ.begin() + (std::ptrdiff_t) s.head + n, outL);
    if (outR != outL) std::copy (outL, outL + n, outR);
    if (outAux != nullptr)
        std::copy (s.hostAuxQ.begin() + (std::ptrdiff_t) s.head, s.hostAuxQ.begin() + (std::ptrdiff_t) s.head + n, outAux);
    s.head += (size_t) n;
    // drop what has been handed out
    s.hostQ.erase (s.hostQ.begin(), s.hostQ.begin() + (std::ptrdiff_t) s.head);
    s.hostAuxQ.erase (s.hostAuxQ.begin(), s.hostAuxQ.begin() + (std::ptrdiff_t) s.head);
    s.head = 0;
    if (s.pendingHead > 0)
    {
        s.pending.erase (s.pending.begin(), s.pending.begin() + (std::ptrdiff_t) s.pendingHead);
        s.pendingHead = 0;
    }
    s.hostAbs += n;
}

int DubSynth::latencyInHostSamples() const
{
    const double engineSamples = kScheduleDelay + 17.0;   // scheduling delay + resampler (16 taps + one)
    return (int) std::ceil (engineSamples / impl_->ratio);
}

int DubSynth::activeVoices() const { return impl_->part->poly.active_voices(); }
long long DubSynth::nonFiniteSamples() const { return impl_->nonFinite; }
long long DubSynth::lateEvents() const { return impl_->late; }

} // namespace dub
