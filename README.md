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
- The clock follows the host's tempo, transport and time signature while **Host sync** is on, so
  the Clock's bar and 2-bar outputs land on the host's downbeats in 3/4 or 7/8 too. In the
  standalone app, or with sync off, **Play** and the tempo control run the internal clock.
- When playback starts or jumps, the Clock module's **Phrase / Song / Free** setting decides how
  the generators line up: with the last two-bar phrase (the default), with bar 1 of the song (so a
  bounce always matches playback), or not at all (free-running patterns keep going). **Reset** in
  the top bar re-phases every generator by hand.
- Probability modules decide from the song position, the module and the patch's seed, so the same
  bar always makes the same choices and renders match what you heard.

## Installing

Download the installer for your system from the
[latest release](https://github.com/thomas-nelson23/Patternspace/releases/latest):

- **macOS**: `Patternspace-<version>-macOS.pkg` installs the app in Applications and the AU, VST3
  and CLAP plug-ins in `/Library/Audio/Plug-Ins`. The installer isn't signed yet, so the first
  time macOS says it can't check it: open **System Settings > Privacy & Security**, scroll down
  and click **Open Anyway** (or Control-click the .pkg and choose **Open**). The plug-ins it
  installs aren't quarantined, so there's no `xattr` step.
- **Windows**: `Patternspace-<version>-Windows-Setup.exe` installs the app in Program Files and
  the VST3 and CLAP plug-ins in `C:\Program Files\Common Files\VST3` and `\CLAP`. It isn't
  signed yet, so SmartScreen may warn: click **More info**, then **Run anyway**. Uninstall it from
  **Settings > Apps**.
- **Linux (Debian, Ubuntu 24.04 and newer)**: `sudo apt install ./patternspace_<version>_amd64.deb`
  puts `patternspace` on your path with a menu entry, and the VST3, CLAP and LV2 plug-ins in
  `/usr/lib/vst3`, `/usr/lib/clap` and `/usr/lib/lv2`. On other distributions, use the zip.

Each installer lets you leave out the parts you don't want. The zips have the same files to copy
by hand. Rescan plug-ins in your DAW after installing.

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
  libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libcurl4-openssl-dev
```

For a universal macOS build, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

Every push builds all three platforms in GitHub Actions, runs the tests and pluginval, builds the
installers, and uploads the plug-ins and installers as build artifacts. Plug-ins copied out of a
downloaded zip are quarantined on macOS until you run `xattr -dr com.apple.quarantine` on them;
the .pkg avoids that. The installers are built by `packaging/macos/build-pkg.sh`,
`packaging/windows/Patternspace.iss` (Inno Setup 6) and `packaging/linux/build-deb.sh`.

## Updates and releases

The standalone app has an **Updates** button. It checks the latest
[GitHub release](https://github.com/thomas-nelson23/Patternspace/releases) once a day at launch
(untick **Check at launch** in its menu to stop that) and whenever you pick **Check for
updates...**. When a newer version is out the button turns green and offers to download it to your
Downloads folder. On macOS and Windows that's the installer: **Install now** quits Patternspace
and opens it, and it updates the plug-ins too. On Linux it's the zip; replace the app with the one
in its `Standalone` folder. The plug-ins don't check for updates themselves.

To publish a release, bump `VERSION` in `project(Patternspace ...)` in `CMakeLists.txt`, merge it,
then push a matching tag:

```sh
git tag v0.2.0 && git push origin v0.2.0
```

The Build workflow builds all three platforms and publishes the release with
`Patternspace-<version>-macOS.pkg`, `Patternspace-<version>-Windows-Setup.exe`,
`patternspace_<version>_amd64.deb`, and a zip per platform. It stops if the tag and the
version don't match, since the app compares its own version with the tag.

## Using it

- **?** (or F1) in the tab row lists every shortcut. Ctrl/Cmd-Z and Shift-Z undo and redo any edit.
- Drag a value up or down to change it (Shift for finer steps), scroll over it, or double-click
  it to type a number. The arrows beside it still step by one.
- Drag empty space to box-select modules (shift adds to the selection), right-drag or middle-drag to pan, Ctrl/Cmd-scroll or pinch to zoom, **Fit** to see the whole tab.
- Click a module in the library to add it to the current tab, or drag it onto the workspace to
  put it right there. The search box at the top of the library filters it; Enter adds the first
  match.
- Drag from an output to an input to wire it; the ports it can land on grow while you drag. Drag a
  wired input to move the wire to another input, or drop it on empty space to remove it. Click a
  wire and press Delete to remove it too.
- Ctrl/Cmd-C, X and V copy, cut and paste the selected modules with the wires between them. A paste lands at the mouse; pasted outputs start untagged because each tag has one source.
- Click a port to tag it. The menu lists the tags already in the patch to listen to, lets you
  type a new one, pick its colour, jump to the output that owns it or see which inputs use it.
  An input with the same tag as an output receives it without a wire, across tabs too; hover a
  tag to see its links drawn dashed. Wires win over tags.
- Drag a module by its header. Shift-click to select several. Click the arrow, or double-click
  the header, to collapse it.
- Right-click empty space to add a module right there, paste, select all, collapse or expand
  everything, or turn on **Snap** (a 10 px grid). Ctrl/Cmd-A selects all, Ctrl/Cmd-D duplicates,
  the arrow keys nudge the selection (Shift for 10 px).
- Right-click a module to copy, cut, duplicate, collapse it, delete it or move it to another tab, or drag selected modules onto a tab to move them there. Wires to modules that
  stay behind become tags, so the patch keeps playing.
- Select some modules and press Ctrl/Cmd-G (or right-click, **Group into a subsystem**) to turn
  them into one subsystem. Wires and tags that cross its edge become its ports, so the patch
  keeps playing the same.
- Double-click empty space to write a note; right-click it for **Add a block here**, a titled
  section that moves its modules with it. Ctrl/Cmd-B draws a block around the selection. Drag a
  block's corner to resize it, double-click its title to rename it, and right-click it for
  colours. Delete removes a selected block or note.
- The minimap in the bottom-right corner shows the whole tab; click or drag on it to move around.
  **M** hides and shows it.
- Click **Open** on a subsystem (or double-click it) to see and edit the patch inside. The path
  at the top left, Esc, or a tab goes back out. Edits change that copy of the subsystem only.
- **New subsystem** in the library adds an empty one and opens it. Add its inputs and outputs with
  the + buttons on the Inputs and Outputs modules, right-click a port there to rename it, make it
  audio or remove it, then add modules and wire them up. Right-click a subsystem to rename it.
- The patch name at the top left opens the patch menu: **New patch**, the factory presets, **My
  presets** (patches you saved with **Save as preset...**, kept in your Patternspace folder under
  Application Support or AppData), **Open recent**, **Save**, **Save as...** and **Rename**. A dot
  before the name means unsaved changes, and loading over them asks first. **Open...** and
  **Save...** read and write patch files anywhere.
- Right-click a subsystem to save it to **My subsystems**, which every patch lists in the library,
  or to a file for sharing; **Add from file...** reads one back. The factory patches also show up
  as programs in hosts that list them.

## Layout

- `source/core`: the patch engine in plain C++ with no JUCE dependency. It matches the browser
  prototype step for step on every factory patch.
- `source/state`: patch files, in the prototype's JSON format, so patches move between the two.
  Files carry a format version (`"v": 2`); older files are upgraded as they load.
- `source/plugin`: the JUCE processor, the patch canvas and the window.
- `presets`: factory patches and subsystems, embedded in the plug-in.
- `tests`: engine tests, including a 128-step comparison against the prototype for every
  factory patch (`tests/data/reference-steps.json`).
- `docs/prototype/patchbay.html`: the browser prototype the plug-in is built from.
- `packaging`: the macOS .pkg, Windows Setup.exe and Linux .deb builders.
- `tools/export-prototype.cjs`: regenerates the presets and reference data from the prototype
  with Playwright: `NODE_PATH=$(npm root -g) node tools/export-prototype.cjs`.

## Licences

Patternspace is built with [JUCE](https://juce.com), which is available under the AGPLv3 or a
commercial JUCE licence, and the MIT-licensed
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions). Distributing a
closed-source build needs a JUCE licence. The interface uses [Sora](https://github.com/sora-xor/sora-font),
[JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) and
[Michroma](https://github.com/googlefonts/Michroma-font), embedded from `resources/fonts` under the
SIL Open Font License (`resources/fonts/licenses`).

The look follows the Home Dashboard's Deep Ocean theme: one flat deep-blue field, dark glass
panels with hairline edges, cyan for what is live or chosen, red for small signals, and tracked
capitals for panel titles. Its colours and metrics live in `source/plugin/Theme.h`.
