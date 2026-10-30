# Credits

**Unofficial Dub Stab Plaits** is a community port. It is not made, endorsed or supported by Narti Live,
Mutable Instruments or Emilie Gillet, and none of them are affiliated with it.

## Where it comes from

- **DUB STAB PLAITS** by **Narti Live** (MIT, Copyright (c) 2026 Narti Live):
  the generative dub-stab sequencer, chord logic and sound-model recipes.
  - Firmware repository: https://github.com/Narti-Live/dub-base-Plaits (`firmware/files/plaits/dub/dub.cc`)
  - Max for Live devices (`DubStab-M4L-v1.0.zip` in the same repository): the newer sequencer with root note,
    scales, voicings, DEEP and ACID, written in JavaScript (`dubstab.js`). `src/core/DubSequencer.cpp` is a
    line-by-line C++ port of it, and an unmodified copy of that script is kept in `tests/reference/` so the
    port can be checked against it (see `tests/reference/README.md`).
  - Project page: https://narti-live.github.io/dub-base-Plaits
- **Plaits** by **Emilie Gillet** / Mutable Instruments (MIT, Copyright (c) 2016-2021 Emilie Gillet):
  the synthesis engines the Dub Stab models are built on. The STRINGS model is the Plaits string machine engine, and the
  oscillator, filter and DSP helper code (stmlib) is vendored in `third_party/`; `third_party/VERSIONS.txt` lists the exact
  upstream commits and files.
- **Narti Live's `dub.cc`** (the firmware's polyphonic dub synth: DUB SAW, VA, WAVESHAPE, FM, PHASE DIST and the model-6 slot
  CHIPTUNE) is used as published, vendored in `third_party/plaits/dub/`. The ACID bass voice (`src/core/AcidVoice.cpp`) is a
  port of the gen~ code in the Max for Live devices.
  The Plaits firmware copyright line is part of Narti Live's licence file and is kept as it is.

Both licence notices are in `licenses/NARTI-LIVE-LICENSE.txt` (verbatim copy of the upstream file).
The fonts mentioned at the end of that file belong to Narti Live's website and app; this plug-in does not use them.

## Not taken from anywhere

- **VHIKK** (model 6 on the module) is not in any published source. For now model 6 is the firmware's published CHIPTUNE
  slot as a stand-in. VHIKK will be written new from the published description of how it sounds, and will not be identical.
