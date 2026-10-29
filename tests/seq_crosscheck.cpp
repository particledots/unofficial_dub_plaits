// Runs the C++ sequencer on the scenarios in tests/scenarios.txt and prints the same event log as
// tests/reference/ref_dump.js prints for the original JavaScript. CI diffs the two.
//
// usage: seq_crosscheck <scenarios.txt> > actual.txt
#include "core/DubSequencer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace dub;

static std::string hex64 (double x)
{
    uint64_t b; std::memcpy (&b, &x, 8);
    char buf[32]; std::snprintf (buf, sizeof buf, "%016llx", (unsigned long long) b);
    return buf;
}

static void dumpPattern (const Pattern& P, std::string& out)
{
    auto arr = [&] (const char* name, const std::array<int, 16>& a)
    {
        out += name;
        for (int i = 0; i < 16; ++i) { if (i) out += ','; out += std::to_string (a[(size_t) i]); }
    };
    arr ("P d=", P.degree); arr (" v=", P.voicing); arr (" r=", P.rank);
    arr (" a=", P.accent); arr (" m=", P.mono); arr (" s=", P.acidShift);
    out += '\n';
}

static void flush (DubSequencer& seq, long long label, std::string& out)
{
    for (const Event& e : seq.events())
    {
        out += std::to_string (label) + ' ';
        switch (e.kind)
        {
            case Event::MidiOn:      out += "ON " + std::to_string (e.note) + ' ' + std::to_string (e.vel); break;
            case Event::MidiOff:     out += "OFF " + std::to_string (e.note); break;
            case Event::Gen:         out += "GEN " + std::to_string (e.type) + ' ' + std::to_string (e.note) + ' ' + std::to_string (e.vel); break;
            case Event::AcidLen:     out += "ALEN " + hex64 (e.ms); break;
            case Event::ChordText:   out += "CHORD " + e.text; break;
            case Event::PatternText: out += "PAT " + e.text; break;
        }
        out += '\n';
    }
    seq.events().clear();
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: seq_crosscheck <scenarios.txt>\n"); return 2; }
    std::ifstream in (argv[1]);
    if (! in) { std::fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }

    std::string line, out;
    int index = 0;
    while (std::getline (in, line))
    {
        std::istringstream ls (line);
        std::vector<std::string> tokens;
        for (std::string tk; ls >> tk;) tokens.push_back (tk);
        if (tokens.empty() || tokens[0][0] == '#') continue;

        std::string trimmed;
        for (size_t i = 0; i < tokens.size(); ++i) trimmed += (i ? " " : "") + tokens[i];

        DubSequencer seq;
        seq.emitText = true;
        for (const auto& tk : tokens)
            if (tk.rfind ("mode=", 0) == 0) seq.setMode (tk.substr (5) == "both" ? Mode::Both : Mode::Midi);
        seq.events().clear();                          // what the construction printed is not part of the log

        out += "## " + std::to_string (index) + ' ' + trimmed + '\n';
        dumpPattern (seq.pattern(), out);
        long long t = 0;
        for (const auto& tk : tokens)
        {
            if (tk == "stop") { seq.state (0); flush (seq, t, out); continue; }
            const size_t eq = tk.find ('=');
            if (eq == std::string::npos) { std::fprintf (stderr, "bad token %s\n", tk.c_str()); return 2; }
            const std::string key = tk.substr (0, eq);
            const double val = std::strtod (tk.c_str() + eq + 1, nullptr);
            bool regen = false;
            if (key == "mode") continue;
            else if (key == "run") { for (double k = 0; k < val; k += 20) { seq.ticks ((double) t); flush (seq, t, out); t += 20; } continue; }
            else if (key == "jump") { t = (long long) val; continue; }
            else if (key == "note") seq.noteIn ((int) val);
            else if (key == "tempo") seq.setTempo (val);
            else if (key == "root") seq.setRoot (val);
            else if (key == "scale") { seq.setScale (val); regen = true; }
            else if (key == "minor") { seq.setMinor (val); regen = true; }
            else if (key == "voicing") { seq.setVoicing (val); regen = true; }
            else if (key == "output") seq.setOutput (val);
            else if (key == "density") seq.setDensity (val);
            else if (key == "steps") seq.setSteps (val);
            else if (key == "rate") seq.setRate (val);
            else if (key == "octave") seq.setOctave (val);
            else if (key == "gate") seq.setGate (val);
            else if (key == "seed") { seq.setSeed (val); regen = true; }
            else if (key == "melody") { seq.setMelody (val); regen = true; }
            else if (key == "model") seq.setModel (val);
            else if (key == "chords") seq.setChords (val);
            else { std::fprintf (stderr, "unknown key %s\n", key.c_str()); return 2; }
            flush (seq, t, out);
            if (regen) dumpPattern (seq.pattern(), out);
        }
        dumpPattern (seq.pattern(), out);
        ++index;
    }
    std::fwrite (out.data(), 1, out.size(), stdout);
    return 0;
}
