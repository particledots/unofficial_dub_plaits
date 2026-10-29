#include "TransportClock.h"
#include <algorithm>
#include <cmath>

namespace dub {

void TransportClock::drain (DubSequencer& seq, int sample, std::vector<TimedEvent>& out)
{
    for (Event& e : seq.events())
    {
        TimedEvent te;
        te.sample = sample;
        te.ev = std::move (e);
        out.push_back (std::move (te));
    }
    seq.events().clear();
}

void TransportClock::process (DubSequencer& seq, const HostBlock& b, std::vector<TimedEvent>& out)
{
    if (! b.playing || ! b.positionValid)
    {
        if (running_)
        {
            running_ = false;
            seq.state (0);                 // release held notes, forget the previous step
            drain (seq, 0, out);
        }
        return;
    }
    if (b.numSamples <= 0 || b.bpm <= 0 || b.sampleRate <= 0) return;

    seq.setTempo (b.bpm);
    const double kTicksPerQuarter = 480.0;
    const double t0 = b.ppqStart * kTicksPerQuarter;
    const double tps = b.bpm * kTicksPerQuarter / (60.0 * b.sampleRate);      // ticks per sample
    const double t1 = t0 + b.numSamples * tps;

    // Continuous playback: this block starts where the last one ended (a little slack for the host's rounding).
    const double tolerance = std::max (0.5, 3.0 * tps);
    const bool jumped = ! running_ || std::fabs (t0 - prevEnd_) > tolerance;
    if (jumped)
    {
        if (running_)                      // relocated while playing: release what is held
        {
            seq.state (0);
            drain (seq, 0, out);
        }
        const double first = std::ceil (t0 / (double) DubSequencer::kGrid - 1e-6);
        nextK_ = first > 0 ? (long long) first : 0;
    }
    running_ = true;

    const double slack = 1e-5;             // a grid point within `slack` ticks of the block end belongs to the next block
    for (;;)
    {
        const double gt = (double) DubSequencer::kGrid * (double) nextK_;
        if (gt >= t1 - slack) break;
        int sample = (int) std::floor ((gt - t0) / tps + 0.5);
        sample = std::max (0, std::min (b.numSamples - 1, sample));
        seq.ticks (gt);
        drain (seq, sample, out);
        ++nextK_;
    }
    prevEnd_ = t1;
}

} // namespace dub
