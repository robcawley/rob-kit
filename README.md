# ROB KIT

A human-feeling drum instrument for Logic Pro (Audio Unit), built from Rob Cawley's own drum recordings.

> Made by **Rob Cawley**, a musician in Dublin, for his own records, and shared free for anyone to try.
> If you make something with it, I'd love to hear it. Post it anywhere, and if you like, link back to my music:
> **[robcawley.com](https://robcawley.com)** · **[Bandcamp](https://robcawley.bandcamp.com)** · the new album **[Room Tone](https://robcawley.com/roomtone/)**.

![ROB KIT](<Design/ROB KIT - interface.png>)

## What it does

- Plays Logic's standard drum note map, so Session Player, Drummer regions or your own MIDI can drive it directly.
- **Feel:** velocity shapes level, brightness, stick attack and head pitch; every hit varies a little; hats alternate; snare wires buzz in sympathy with the kick and toms; optional timing drift.
- **THE ROOM:** room, size, glue, drive, width, output, plus PUNCH (transient shaper) and SMASH (parallel compression).
- Per-group level, tune, decay and tone; click the drums on screen to play them.

## Download

Get the ready-built plug-in from the **[latest release](https://github.com/robcawley/rob-kit/releases/latest)** (macOS 12 or later, Apple Silicon and Intel).

1. Unzip it and move `ROB KIT.component` into `~/Library/Audio/Plug-Ins/Components` (in Finder: Go → Go to Folder…, then paste that path).
2. The plug-in isn't signed with an Apple developer certificate, so macOS blocks it the first time. Open Terminal and run:

       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"ROB KIT.component"

3. Restart Logic. It appears under AU Instruments → Rob Cawley → ROB KIT.

The download includes Rob's 18 drum sounds: kicks, snares, hats, toms, crashes and rides. The extra articulations (side stick, rimshots, hat pedal, ride bell and so on) and the hand percussion are made from Logic Pro's own sound library, which can't be shared. To add them, extract them from your own copy of Logic with the steps under *Build from source* below (stop after `prepare.py`), then copy them where the plug-in looks:

    mkdir -p ~/Music/"ROB KIT"/Samples
    cp -R Samples/Fill ~/Music/"ROB KIT"/Samples/

Restart Logic and those notes will play too.

## Build from source

To build it yourself, you need a Mac with the Xcode Command Line Tools and CMake. You also need Logic Pro with its sound library installed, plus `ffmpeg` and Python 3 with `numpy` (for example `brew install ffmpeg` and `pip3 install numpy`).

Rob's 18 drum sounds are included in `Samples/Rob`. The extra articulations (side stick, rimshots, hat pedal, ride bell and so on) and the hand percussion are made from Logic Pro's own sound library on your Mac. Apple's sounds can't be shared, so you extract them from your own copy of Logic:

    git clone https://github.com/robcawley/rob-kit.git
    cd rob-kit
    bash tools/extract_socal.sh
    python3 tools/extract_perc.py
    mkdir -p /tmp/robkit-src && cp Samples/Rob/*.wav /tmp/robkit-src/
    python3 tools/prepare.py /tmp/robkit-src

Then build:

    git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git JUCE
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$PWD/JUCE"
    cmake --build build -j 8

The build puts the samples inside the plug-in and installs it to `~/Library/Audio/Plug-Ins/Components`. In Logic: AU Instruments → Rob Cawley → ROB KIT.

## Sister projects

- [BRUISER](https://github.com/robcawley/bruiser): a drummer that plays ROB KIT.
- [NEON RIOT](https://github.com/robcawley/neon-riot): a heavy bass synthesizer.
- [THE GREAT COMPOSER](https://github.com/robcawley/the-great-composer): chords in, orchestral MIDI out.

## Licence

Code: AGPL-3.0 (see `LICENSE`), the same open-source licence as the JUCE framework.

Rob's drum samples (`Samples/Rob`) and the interface artwork (`Design/`, `Resources/Interface/`) are © Rob Cawley. Use them freely inside ROB KIT, including in music you release, but please don't redistribute or sell them as a sample pack or artwork pack.

Fonts: Rajdhani and Share Tech Mono, SIL Open Font Licence.
