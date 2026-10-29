# Unofficial Dub Stab Plaits

A community AU / VST3 instrument for Logic Pro (and other hosts), based on the **DUB STAB PLAITS** firmware and Max for Live devices by
**Narti Live**: a generative, controlled dub-techno chord-stab machine built on Plaits engines.

Not affiliated with, made or supported by Narti Live, Mutable Instruments or Emilie Gillet. See `licenses/` for the credits
and the MIT licence notices, which must stay with every copy.

**Status: work in progress (steps 1-2 of 7: empty instrument + sequencer).** It builds and loads as a silent instrument.
The generative sequencer (a C++ port of Narti Live's `dubstab.js`) and its transport clock are in and tested, but they are not
connected to the plug-in yet, and there is no sound until step 3.

## Getting the plug-in

1. Open the **Actions** tab, pick the latest passing run, download the **Dub-Stab-Plaits-macOS** artifact.
2. Follow `INSTALL.txt` inside it.

## What has and has not been verified

Rows say where each result comes from. "Locally" means I ran it on Linux while writing it; "CI" means the GitHub macOS run (see the Actions tab).

| Check | Status |
|---|---|
| Builds as AU + VST3 on the GitHub macOS runner, universal arm64 + x86_64, minimum macOS 11, ad-hoc signed | see the latest Actions run |
| Processor test (name, MIDI in, stereo out, silent, state save/load) | see the latest Actions run |
| Reference script `tests/reference/dubstab.js` is byte-identical to the one in Narti Live's zip | pinned by SHA-256, checked in CI |
| Sequencer port vs the original JavaScript: about 2,000 scenarios, about 430,000 log lines (notes, synth events, chord names, pattern tables, acid gate lengths) identical | identical locally with g++ and clang++; CI repeats it on macOS with Node |
| The cross-check can fail: 7 deliberate port mistakes (rank bias, probability, gate length, off-order, scale table, seeding arithmetic...) | all caught locally; one tie-break case cannot be reached by any scenario (see `tests/reference/README.md`) |
| Transport clock: events identical for 7 block sizes, 3 sample rates, 3 tempos, on-grid and off-grid starts; sample positions within 1 sample; stop releases held notes; restart and loop jump follow the song position; tempo changes; count-in; random fuzz with no hanging notes (about 300,000 checks) | passes locally; CI repeats it |
| The transport tests can fail: 10 deliberate mistakes in the clock | 9 caught; 1 harmless (the sequencer already ignores negative positions) |
| Sequencer connected to the plug-in, sound models, parameters, editor | **not built yet** (steps 3-4) |
| Loads in Logic on the M4 Mac mini | **not checked** |
| Loads in Logic on the Intel Mac | **not checked** |
| Transport sync in a real Logic session, MIDI feel, sound next to the module or the Max devices | **not checked** |
