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
  the synthesis engines the Dub Stab models are built on (used from step 3 on).
  The Plaits firmware copyright line is part of Narti Live's licence file and is kept as it is.

Both licence notices are in `licenses/NARTI-LIVE-LICENSE.txt` (verbatim copy of the upstream file).
The fonts mentioned at the end of that file belong to Narti Live's website and app; this plug-in does not use them.

## Not taken from anywhere

- **VHIKK** (model 6 on the module) is not in any published source. It will be written new from the published
  description of how it sounds, and will not be identical.
