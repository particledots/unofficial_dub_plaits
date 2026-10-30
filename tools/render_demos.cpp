// Renders one WAV file per sound model: the real chain (DubSequencer -> TransportClock -> DubSynth) playing 8 bars at
// 124 BPM from the start of the song, dry (no echo, no reverb), default knobs. A listening aid for CI and for anyone
// without Logic at hand; it is not part of the plug-in.
//
//   render_demos <output folder> [sample rate = 48000] [seed = 1]
//
// Output: dubstab-1-DUB-SAW.wav ... dubstab-8-ACID.wav (16-bit mono) and demos.txt (what each file is).
#include "core/DubSynth.h"
#include "core/TransportClock.h"
#include "stmlib/utils/random.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace dub;

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::fprintf (stderr, "cannot write %s\n", path.c_str()); std::exit (1); }
    const uint32_t dataBytes = (uint32_t) (x.size() * 2);
    auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc ((int) ((v >> (8 * i)) & 255), f); };
    auto u16 = [&] (uint16_t v) { std::fputc (v & 255, f); std::fputc (v >> 8, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (1); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 2); u16 (2); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (float v : x)
    {
        const long q = std::lround (std::max (-1.0f, std::min (1.0f, v)) * 32767.0f);
        u16 ((uint16_t) (int16_t) q);
    }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: render_demos <output folder> [sample rate] [seed]\n"); return 2; }
    const std::string dir = argv[1];
    const int sr = argc > 2 ? std::atoi (argv[2]) : 48000;
    const int seed = argc > 3 ? std::atoi (argv[3]) : 1;
    const double bpm = 124.0;
    const double seconds = 8 * 4 * 60.0 / bpm + 2.0;       // 8 bars + 2 s of tail

    std::string index = "Dub Stab Plaits: one file per sound model. Dry (no echo or reverb), default knobs, 124 BPM, 8 bars,\n"
                        "C natural minor, DUB voicing, seed " + std::to_string (seed) + ". Played by the real sequencer.\n\n";
    for (int m = 0; m < kNumSynthModels; ++m)
    {
        DubSequencer seq;
        seq.setMode (Mode::Both);
        seq.setModel (m);
        seq.setSeed (seed);
        seq.setRate (4);                   // 1/16
        seq.setDensity (m == kAcid ? 12 : 8);
        seq.events().clear();
        TransportClock clock;
        DubSynth synth;
        stmlib::Random::Seed (1);
        synth.prepare (sr, 512);
        SynthParams p; p.model = m;

        const int total = (int) (seconds * sr);
        std::vector<float> out ((size_t) total), l (512), r (512);
        std::vector<TimedEvent> evs;
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = std::min (512, total - pos);
            HostBlock hb; hb.playing = pos < (int) (8 * 4 * 60.0 / bpm * sr); hb.bpm = bpm; hb.sampleRate = sr; hb.numSamples = n;
            hb.ppqStart = (double) pos / sr * bpm / 60.0;
            evs.clear();
            clock.process (seq, hb, evs);
            seq.events().clear();
            for (auto& te : evs) synth.addEvent (te.sample, te.ev);
            synth.process (l.data(), r.data(), n, p);
            std::copy (l.begin(), l.begin() + n, out.begin() + pos);
        }
        std::string name = synthModelName (m);
        for (char& c : name) if (c == ' ' || c == '(' || c == ')') c = '-';
        while (name.find ("--") != std::string::npos) name.erase (name.find ("--"), 1);
        if (! name.empty() && name.back() == '-') name.pop_back();
        const std::string file = "dubstab-" + std::to_string (m + 1) + "-" + name + ".wav";
        writeWav (dir + "/" + file, out, sr);
        float pk = 0; for (float v : out) pk = std::max (pk, std::fabs (v));
        char line[300];
        std::snprintf (line, sizeof line, "%s   model %d: %s   (peak %.2f)\n", file.c_str(), m + 1, synthModelName (m), pk);
        index += line;
        std::printf ("%s", line);
    }
    index += "\nModel 6 is CHIPTUNE for now: a stand-in for VHIKK, which is not in any published source.\n";
    FILE* f = std::fopen ((dir + "/demos.txt").c_str(), "wb");
    if (f) { std::fwrite (index.data(), 1, index.size(), f); std::fclose (f); }
    return 0;
}
