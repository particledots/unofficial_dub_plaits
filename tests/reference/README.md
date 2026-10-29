# Reference: the original dubstab.js

`dubstab.js` is the sequencer / harmony script from **Narti Live's** `DubStab-M4L-v1.0.zip` (Max for Live devices,
https://github.com/Narti-Live/dub-base-Plaits, `downloads/`), extracted unchanged from the embedded file list of
`Dub Stab MIDI by Narti Live.amxd` (20,904 bytes at the position the file table gives). MIT licence,
Copyright (c) 2026 Narti Live (the notice is in the file's own header; see also `licenses/NARTI-LIVE-LICENSE.txt`).

It is here for one reason: `src/core/DubSequencer.cpp` is a port of it, and the port is checked against the real thing.

- `dubstab.js.sha256` pins the exact bytes. CI fails if the file is edited.
- `make_scenarios.js` writes `../scenarios.txt` (about 2,000 settings-and-clock runs: every scale, minor-only,
  voicing and root, random full settings, changes while playing, stop, clock jumps, MIDI-in transposition,
  ACID / mono / roots / chords, both device modes). The committed file is its output; it is deterministic.
- `ref_dump.js` loads the original into a Node `vm` with a stand-in for Max's `outlet()` and runs each scenario,
  printing every note-on / note-off, synth event, chord text and 16-step display text, plus the pattern tables.
- `../seq_crosscheck.cpp` runs the C++ port on the same scenarios and prints the same log.
- CI diffs the two logs. They must be identical, byte for byte (acid gate lengths are compared as raw double bits).

Run it yourself (needs Node and a C++17 compiler):

    node tests/reference/ref_dump.js tests/scenarios.txt > expected.txt
    c++ -std=c++17 -O2 -ffp-contract=off -Isrc src/core/DubSequencer.cpp tests/seq_crosscheck.cpp -o seq_crosscheck
    ./seq_crosscheck tests/scenarios.txt > actual.txt
    cmp expected.txt actual.txt && echo identical

## Things that matter for an exact match

- The original seeds its random generator with `s * 2654435761 + 0x9e3779b9` in JavaScript doubles. For large melody seeds
  the product exceeds 2^53 and is rounded; the port does the same arithmetic in doubles on purpose (668 scenarios depend on it).
- `-ffp-contract=off`: without it Apple's compiler may fuse multiply-add on arm64, which JavaScript never does.
- Not ported (Max-only): the number-box outlets, the NEW-button debounce, and the slot numbers of the gen~ event queue.
- One deliberate difference: the original never gets a negative clock; the port ignores negative positions (count-in).
- Tie-break of the rank sort (two steps with exactly equal random score) is ported but cannot be exercised by the scenarios:
  it needs two equal 32-bit random numbers.
