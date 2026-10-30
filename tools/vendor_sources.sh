#!/usr/bin/env bash
# Copies the exact upstream source files the sound models need into third_party/ (nothing else).
#
#   tools/vendor_sources.sh <pichenettes/eurorack checkout> <pichenettes/stmlib checkout> <Narti-Live/dub-base-Plaits checkout>
#
# Where each file comes from:
#   * Narti Live's firmware/files/plaits/dub/dub.{h,cc}            -> third_party/plaits/dub/       (MIT, Narti Live)
#   * Narti Live's three small edits of Plaits (a custom chord for ChordBank, mutable_chords() accessors):
#         dsp/chords/chord_bank.{h,cc}, dsp/engine/chord_engine.h, dsp/engine2/string_machine_engine.h
#     (diffed against upstream when vendored: those are the only differences, see third_party/VERSIONS.txt)
#   * everything else: Emilie Gillet's Plaits (pichenettes/eurorack) and stmlib (pichenettes/stmlib), MIT, unmodified.
# The list of files is not hand-picked: it is the include closure of the .cc files named in CORE_SOURCES below, found with
# the compiler (g++ -MM), so a file is vendored if and only if the models need it.
set -euo pipefail
EUR="${1:?path to the pichenettes/eurorack checkout}"
STM="${2:?path to the pichenettes/stmlib checkout (the commit named in third_party/VERSIONS.txt)}"
NAR="${3:?path to the Narti-Live/dub-base-Plaits checkout}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="$HERE/third_party"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# 1. A throw-away tree with upstream Plaits + stmlib, with Narti Live's files laid over it.
mkdir -p "$WORK/plaits" "$WORK/stmlib"
cp -R "$EUR/plaits/." "$WORK/plaits/"
cp -R "$STM/." "$WORK/stmlib/"
rm -rf "$WORK/stmlib/.git"
NP="$NAR/firmware/files/plaits"
for f in dsp/chords/chord_bank.cc dsp/chords/chord_bank.h dsp/engine/chord_engine.h dsp/engine2/string_machine_engine.h; do
  cp "$NP/$f" "$WORK/plaits/$f"
done
mkdir -p "$WORK/plaits/dub"
cp "$NP/dub/dub.cc" "$NP/dub/dub.h" "$WORK/plaits/dub/"

# 2. Include closure of the sources we compile.
CORE_SOURCES="plaits/dub/dub.cc plaits/dsp/engine2/string_machine_engine.cc plaits/dsp/chords/chord_bank.cc plaits/resources.cc stmlib/utils/random.cc stmlib/dsp/units.cc"
FILES="$(cd "$WORK" && for s in $CORE_SOURCES; do
  g++ -std=c++17 -DTEST -I. -include cstdio -include cstdlib -include cmath -include cstdint -MM -MG "$s" | tr ' \\' '\n\n'
  echo "$s"
done | grep -E '^(plaits|stmlib)/|^\./(plaits|stmlib)/' | sed 's#^\./##' | sort -u)"

# 3. Copy.
rm -rf "$T/plaits" "$T/stmlib"
mkdir -p "$T"
for f in $FILES; do
  mkdir -p "$T/$(dirname "$f")"
  cp "$WORK/$f" "$T/$f"
done
cp "$STM/LICENSE" "$T/stmlib/LICENSE"

{
  echo "Vendored by tools/vendor_sources.sh. Only the files the sound models need (include closure of: $CORE_SOURCES)."
  echo
  echo "plaits (Emilie Gillet, MIT):   https://github.com/pichenettes/eurorack @ $(git -C "$EUR" rev-parse HEAD)"
  echo "stmlib (Emilie Gillet, MIT):   https://github.com/pichenettes/stmlib    @ $(git -C "$STM" rev-parse HEAD)"
  echo "dub.h, dub.cc (Narti Live, MIT) and his edits of four Plaits files:"
  echo "                               https://github.com/Narti-Live/dub-base-Plaits @ $(git -C "$NAR" rev-parse HEAD)"
  echo "                               (firmware/files/plaits/...)"
  echo
  echo "Differences between the four edited Plaits files here and upstream (Narti Live's changes, nothing of ours):"
  for f in dsp/chords/chord_bank.cc dsp/chords/chord_bank.h dsp/engine/chord_engine.h dsp/engine2/string_machine_engine.h; do
    echo "  plaits/$f: $(diff "$EUR/plaits/$f" "$NP/$f" | grep -c '^[<>]') changed lines"
  done
  echo
  echo "Files ($(echo "$FILES" | wc -l)):"
  echo "$FILES" | sed 's/^/  /'
} > "$T/VERSIONS.txt"
echo "vendored $(echo "$FILES" | wc -l) files; see third_party/VERSIONS.txt"
