# Unofficial Dub Stab Plaits

A community AU / VST3 instrument for Logic Pro (and other hosts), based on the **DUB STAB PLAITS** firmware and Max for Live devices by
**Narti Live**: a generative, controlled dub-techno chord-stab machine built on Plaits engines.

Not affiliated with, made or supported by Narti Live, Mutable Instruments or Emilie Gillet. See `licenses/` for the credits
and the MIT licence notices, which must stay with every copy.

**Status: work in progress (steps 1-2 of 7: empty instrument + sequencer).** It builds and loads as a silent instrument.
The generative sequencer (a C++ port of Narti Live's `dubstab.js`) and its transport clock are in and tested, but they are not
connected to the plug-in yet, and there is no sound until step 3.

## Getting the plug-in

1. Open the **Actions** tab, pick the latest passing run, and download an artifact from the bottom of the run page:
   **Dub-Stab-Plaits-macOS** (built on GitHub's Apple-silicon runner) or **Dub-Stab-Plaits-macOS-built-on-intel-runner**
   (built on its Intel runner). Both are the same universal plug-in (arm64 + x86_64). GitHub's Apple-silicon runners are
   often busy, so the first one may be missing from a run; the second one is built every time.
2. Follow `INSTALL.txt` inside it.

## What has and has not been verified

"CI" means the GitHub macOS run (Actions tab); the numbers below are what its log reports as annotations. "Locally" means I ran it on Linux while writing it.

| Check | Status |
|---|---|
| AU + VST3 build, universal, minimum macOS 11, ad-hoc signed | **confirmed in CI on the Intel runner**: both AU and VST3 contain x86_64 and arm64, both slices say minimum macOS 11.0, the ad-hoc signature verifies. Not yet seen on the Apple-silicon runner (it had no free runner when tried) |
| Processor test (name, MIDI in, stereo out, silent, state save/load) | **passes in CI** (Intel runner) |
| Reference script `tests/reference/dubstab.js` is byte-identical to the one in Narti Live's zip | pinned by SHA-256, **passes in CI** |
| Sequencer port vs the original JavaScript: 2,080 scenarios, 429,450 log lines (notes, synth events, chord names, pattern tables, acid gate lengths) identical | **identical in CI** (Intel runner, Node 20) and locally with g++ and clang++. Not yet run on Apple silicon |
| The cross-check can fail: 7 deliberate port mistakes (rank bias, probability, gate length, off-order, scale table, seeding arithmetic, sort tie-break) | 6 caught locally; the tie-break case cannot be reached by any scenario (needs two equal 32-bit random numbers) |
| Transport clock: events identical for 7 block sizes, 3 sample rates, 3 tempos, on-grid and off-grid starts; sample positions within 1 sample; stop releases held notes; restart and loop jump follow the song position; tempo changes; count-in; random fuzz with no hanging notes | **297,542 checks pass in CI** (Intel runner) |
| The transport tests can fail: 10 deliberate mistakes in the clock | 9 caught locally; 1 harmless (the sequencer already ignores negative positions) |
| Sequencer connected to the plug-in, sound models, parameters, editor | **not built yet** (steps 3-4) |
| Loads in Logic on the M4 Mac mini | **not checked** |
| Loads in Logic on the Intel Mac | **not checked** |
| Transport sync in a real Logic session, MIDI feel, sound next to the module or the Max devices | **not checked** |
