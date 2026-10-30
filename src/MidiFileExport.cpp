#include "MidiFileExport.h"
#include <algorithm>
#include <cmath>

bool writePatternMidiFile (const dub::ExportSettings& settings, double bpm, const juce::String& trackName, const juce::File& file)
{
    const auto notes = dub::renderPatternNotes (settings);
    const long long total = (long long) std::max (1, settings.bars) * 4 * dub::kTicksPerQuarter;

    juce::MidiMessageSequence track;
    {
        auto m = juce::MidiMessage::textMetaEvent (3, trackName);
        m.setTimeStamp (0);
        track.addEvent (m);
        const double safeBpm = std::isfinite (bpm) && bpm > 1.0 ? std::min (bpm, 999.0) : 120.0;
        auto t = juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / safeBpm));
        t.setTimeStamp (0);
        track.addEvent (t);
        auto ts = juce::MidiMessage::timeSignatureMetaEvent (4, 4);
        ts.setTimeStamp (0);
        track.addEvent (ts);
    }
    for (const auto& n : notes)
    {
        auto on = juce::MidiMessage::noteOn (1, n.note, (juce::uint8) std::min (127, std::max (1, n.velocity)));
        on.setTimeStamp ((double) n.startTick);
        track.addEvent (on);
        auto off = juce::MidiMessage::noteOff (1, n.note, (juce::uint8) 0);
        off.setTimeStamp ((double) n.endTick);
        track.addEvent (off);
    }
    {
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp ((double) total);
        track.addEvent (end);
    }
    track.sort();
    track.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (dub::kTicksPerQuarter);
    mf.addTrack (track);

    if (! file.getParentDirectory().createDirectory().wasOk()) return false;
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk()) return false;
    return mf.writeTo (out);
}
