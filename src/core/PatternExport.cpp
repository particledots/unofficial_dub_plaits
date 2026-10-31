#include "PatternExport.h"
#include "DubSequencer.h"
#include <algorithm>
#include <map>

namespace dub {

std::vector<ExportNote> renderPatternNotes (const ExportSettings& s)
{
    DubSequencer seq;
    seq.setMode (Mode::Midi);
    seq.setModel (s.model);
    seq.setOutput (s.model == 7 ? 1 : 0);
    seq.setRoot (s.root); seq.setScale (s.scale); seq.setMinor (s.minor); seq.setVoicing (s.voicing);
    seq.setDensity (s.density); seq.setSteps (s.steps); seq.setRate (s.rate); seq.setOctave (s.octave);
    seq.setSeed (s.seed); seq.setMelody (s.melody);
    seq.setDirection (s.direction);
    for (int m = 0; m < kNumChanceModes; ++m) seq.setDirWeight (m, s.dirWeight[m]);
    seq.setRunMin (s.runMin); seq.setRunMax (s.runMax);
    seq.setChords (s.chords);
    seq.setGate (s.gate);
    if (s.transposeNote >= 0) seq.noteIn (s.transposeNote);
    seq.events().clear();

    const long long total = (long long) std::max (1, s.bars) * 4 * kTicksPerQuarter;
    std::vector<ExportNote> done;
    std::multimap<int, ExportNote> open;       // by note number

    auto take = [&] (std::vector<Event>& evs)
    {
        for (auto& e : evs)
        {
            if (e.kind == Event::MidiOn)
            {
                ExportNote n; n.startTick = (long long) e.tick; n.note = e.note; n.velocity = e.vel;
                open.insert ({ e.note, n });
            }
            else if (e.kind == Event::MidiOff)
            {
                auto it = open.find (e.note);
                if (it != open.end())
                {
                    ExportNote n = it->second; open.erase (it);
                    n.endTick = std::min (total, std::max (n.startTick + 1, (long long) e.tick));
                    done.push_back (n);
                }
            }
        }
        evs.clear();
    };

    for (long long t = 0; t < total; t += DubSequencer::kGrid)
    {
        seq.ticks ((double) t);
        take (seq.events());
    }
    seq.state (0);                              // releases what is still held
    take (seq.events());
    for (auto& kv : open)                       // anything left (should not happen)
    {
        ExportNote n = kv.second; n.endTick = total;
        if (n.endTick > n.startTick) done.push_back (n);
    }
    std::sort (done.begin(), done.end(), [] (const ExportNote& a, const ExportNote& b)
               { return a.startTick != b.startTick ? a.startTick < b.startTick : a.note < b.note; });
    return done;
}

} // namespace dub
