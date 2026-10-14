# Patternspace

A generative, node-based drum sequencer and sample player. Patch generators (Euclidean, Random,
Cellular, Step grid, Drum map) through shapers (Accent, Probability, Combine, Switch, Clock
divider) into MIDI outs and sample players, wire them with cables or wireless tags, and run it in
your DAW or on its own.

## Formats

| | VST3 | CLAP | AU | LV2 | Standalone |
|---|---|---|---|---|---|
| macOS (Apple silicon and Intel) | yes | yes | yes | | yes |
| Windows | yes | yes | | | yes |
| Linux | yes | yes | | yes | yes |

The plug-in is an instrument with MIDI output:

- **MIDI out** modules send notes to the host's MIDI out, so Patternspace can drive any drum
  plug-in or hardware. Each MIDI out has 1 to 16 named outputs, each with its own note.
- **Sample player** modules play built-in drum sounds into **Audio out** modules. There are 8
  stereo outputs. Out 1 is always on; enable Outs 2 to 8 in your DAW to give each sound its own
  mixer channel. Any output left off is mixed into Out 1.
- The clock follows the host's tempo and transport while **Host sync** is on. In the standalone
  app, or with sync off, **Play** and the tempo control run the internal clock.

## Building

You need CMake 3.22 or newer and a C++17 compiler (Xcode 14+, Visual Studio 2022, or GCC 11+ /
Clang 14+). JUCE 8 and the CLAP extensions are downloaded when you configure.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plug-ins land in `build/Patternspace_artefacts/Release/<format>/`. Add
`-DPATTERNSPACE_COPY_PLUGINS=ON` to have the build copy them into your system plug-in folders.

On Linux, install the JUCE dependencies first. On Ubuntu 24.04:

```sh
sudo apt install ninja-build libasound2-dev libjack-jackd2-dev ladspa-sdk libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

For a universal macOS build, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

Every push builds all three platforms in GitHub Actions, runs the tests and pluginval, and
uploads the plug-ins as build artifacts. Those builds aren't signed, so macOS blocks them until you
run `xattr -dr com.apple.quarantine` on the plug-in.

## Using it

- Drag empty space to pan, Ctrl/Cmd-scroll or pinch to zoom, **Fit** to see the whole tab.
- Click a module in the library to add it to the current tab.
- Drag from an output to an input to wire it. Click a wire and press Delete to remove it.
- Click a port to give it a tag. An input with the same tag as an output receives it without a
  wire, across tabs too. Wires win over tags.
- Drag a module by its header. Shift-click to select several. Click the arrow, or double-click
  the header, to collapse it.
- Right-click a module to collapse it, delete it or move it to another tab. Wires to modules that
  stay behind become tags, so the patch keeps playing.
- **Open...** and **Save...** read and write patch files. Subsystems can be saved from their
  right-click menu and added to any patch with **Add from file...** in the library.

## Layout

- `source/core`: the patch engine in plain C++ with no JUCE dependency. It matches the browser
  prototype step for step on every factory patch.
- `source/state`: patch files, in the prototype's JSON format, so patches move between the two.
- `source/plugin`: the JUCE processor, the patch canvas and the window.
- `presets`: factory patches and subsystems, embedded in the plug-in.
- `tests`: engine tests, including a 128-step comparison against the prototype for every
  factory patch (`tests/data/reference-steps.json`).
- `docs/prototype/patchbay.html`: the browser prototype the plug-in is built from.
- `tools/export-prototype.cjs`: regenerates the presets and reference data from the prototype
  with Playwright: `NODE_PATH=$(npm root -g) node tools/export-prototype.cjs`.

## Not in the plug-in yet

The prototype can still do a few things the plug-in can't: drawing new blocks and text labels
(existing ones show and move), making a subsystem from a selection, opening a subsystem to edit
what's inside, choosing tag colours, and undo.

## Licences

Patternspace is built with [JUCE](https://juce.com), which is available under the AGPLv3 or a
commercial JUCE licence, and the MIT-licensed
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions). Distributing a
closed-source build needs a JUCE licence.
