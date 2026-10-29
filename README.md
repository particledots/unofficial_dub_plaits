# Unofficial Dub Stab Plaits

A community AU / VST3 instrument for Logic Pro (and other hosts), based on the **DUB STAB PLAITS** firmware and Max for Live devices by
**Narti Live**: a generative, controlled dub-techno chord-stab machine built on Plaits engines.

Not affiliated with, made or supported by Narti Live, Mutable Instruments or Emilie Gillet. See `licenses/` for the credits
and the MIT licence notices, which must stay with every copy.

**Status: work in progress (step 1 of 7: empty instrument).** It builds and loads as a silent instrument; there is no sound yet.

## Getting the plug-in

1. Open the **Actions** tab, pick the latest passing run, download the **Dub-Stab-Plaits-macOS** artifact.
2. Follow `INSTALL.txt` inside it.

## What has and has not been verified

| Check | Status |
|---|---|
| Builds as AU + VST3 on the GitHub macOS runner, universal arm64 + x86_64, minimum macOS 11, ad-hoc signed | see the latest Actions run |
| Processor test (name, MIDI in, stereo out, silent, state save/load) | see the latest Actions run |
| Loads in Logic on the M4 Mac mini | **not checked** |
| Loads in Logic on the Intel Mac | **not checked** |
