# Unofficial Dub Stab Plaits

A community AU / VST3 instrument for Logic Pro (and other hosts), based on the **DUB STAB PLAITS** firmware and Max for Live devices by
**Narti Live**: a generative, controlled dub-techno chord-stab machine built on Plaits engines.

Not affiliated with, made or supported by Narti Live, Mutable Instruments or Emilie Gillet. See `licenses/` for the credits
and the MIT licence notices, which must stay with every copy.

**Status: step 4 of 7 written (the plug-in makes sound).** The sequencer (a C++ port of Narti Live's `dubstab.js`), its
transport clock and the eight sound models are connected to the plug-in, with a window, parameters and saved state. Extras on
top of the module: free-run mode, built-in echo / reverb / stereo width, a sub-bass sine (the module's AUX), a choice of what
incoming MIDI notes do (transpose the pattern, or play the sound directly, single notes or chords), the current pattern as a
MIDI file you can drag into a Logic track, presets, and a resizable window. Whether it sounds right is for Bryn to judge by ear.

### What the extra controls do
- **FREE-RUN WHEN STOPPED**: when the host is stopped the pattern keeps playing at FREE-RUN TEMPO (30-300 BPM). When the host plays, the host's tempo and position take over.
- **ECHO** (default: 30 % mix, 1/8 dotted, 45 % feedback, tone 50 %, ping-pong) and **REVERB** (default: 20 % mix, 2.5 s decay) are built in, after the sound. Set both mixes to 0 for a dry sound. **WIDTH** 100 % keeps the sound as it is; below 100 % narrows towards mono; above 100 % spreads the dry sound too.
- **SUB**: the module's AUX sub-bass sine (an octave under the chord root), mixed into the main output. Not available on STRINGS and ACID (the module has no sub there).
- **MIDI NOTES DO THIS**: *Transpose the pattern* (middle C = no change, as in the Max device) or *Play the notes directly* (the pattern is off; notes played within 2 ms of each other make one stab, so a MIDI effect that sends chords can drive it; up to 5 notes per stab).
- **DRAG MIDI TO A TRACK** writes the current pattern as a MIDI file (4/4, from bar 1, 1-16 bars) and hands it to the host; **SAVE MIDI...** saves it as a file.
- **PRESETS**: factory presets and your own, kept in `~/Library/Application Support/particledots/Unofficial Dub Stab Plaits/Presets`. A preset never changes the MIDI-notes or free-run settings.

## Hearing the sound models before the plug-in makes sound

Each CI run also uploads **Dub-Stab-Plaits-macOS-built-on-intel-runner-model-demos**: eight WAV files, one per model, played by
the real sequencer (dry, 124 BPM, 8 bars, C minor, default knobs). Model 6 is a stand-in (CHIPTUNE), see below.

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
| Sound models (`DubSynth`): all 8 models x 3 sample rates (44.1 / 48 / 96 kHz): output finite, bounded, not silent; identical output for any host buffer size; model switching; hostile parameter values (NaN, out of range); event floods; the whole sequencer -> clock -> synth chain; pitch (A3 within 0.3 %, resampler within 0.002 %) | **9,268 checks pass locally** (g++, plus an AddressSanitizer / UBSan run); CI runs the same tests (see the run's annotations) |
| The sound-model tests can fail: deliberate mistakes in the synth | caught locally (checked by hand-made mutations) |
| Pitch of the poly models and STRINGS | measured about 1 cent flat (poly) and about 2.4 cents flat (STRINGS) after the filter opens. Narti Live's unmodified firmware code shows the same offset with no resampler involved; the oscillator itself runs at exactly 220.0001 Hz. Not a tuning claim, only a bounds check |
| ACID model vs the gen~ code of the Max devices | ported by hand in double precision; **not yet cross-checked against the original gen~ code** |
| **How the models sound** (next to the module, the Max devices, or anything else) | **not checked: needs Bryn to listen** |
| Model 6 | **CHIPTUNE stand-in**: the firmware's published slot 6. VHIKK is not in any published source |
| Plug-in processor (fake host, no window): stopped = silent, plays, every model, sub, effects tails, free-run equals host at the same tempo, direct play, factory + user presets, MIDI export file, saved state, editor at 60-200 %, drag file | **passes locally** (Linux, g++); CI runs the same test |
| Echo / reverb / width (`fx_test`): echo time at 3 sample rates and 3 tempos, ping-pong, stability at worst-case settings, reverb decay time within 5 % of the setting, level, width, buffer-size independence, NaN input | **299 checks pass locally**; CI runs it |
| Direct play and MIDI export (`direct_test`): single note, chords with notes a few samples apart, held notes, five-note limit, ACID gate, repeatable export | **5,386 checks pass locally**; CI runs it |
| Editor layout | looked at in a snapshot rendered on Linux; **not yet seen on a Mac** |
| Drag a MIDI region into Logic, free-run feel, MIDI chords from Dotriaconta-tone, preset sounds | **not checked in Logic** |
| Loads in Logic on the M4 Mac mini | **confirmed by Bryn (29 Oct 2026)** for the earlier silent build; not yet checked for this version |
| Loads in Logic on the Intel Mac | **confirmed by Bryn (29 Oct 2026)** for the earlier silent build; not yet checked for this version |
| Transport sync in a real Logic session, sound next to the module or the Max devices | **not checked** |
