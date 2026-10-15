# Changelog

Notable changes to Patternspace. The section for a version becomes its GitHub release notes
when the tag is pushed (`packaging/release-notes.sh`).

## 0.3.0 (2026-10-15)

Everything from the October 2026 audit, in the order the pull requests landed.

### Engine and host sync
- The clock follows the host's time signature and bar start, so bar and 2-bar outputs land on
  real downbeats in 3/4, 6/8 and 7/8 and after a meter change.
- Probability modules decide from the song position, the module and the patch's seed, so a
  bounce matches what you heard. The patch's seed is in the settings.
- A **Reset** button, and a Clock setting for how generators line up when playback starts or
  jumps: with the phrase, with bar 1 of the song, or free-running.
- Patch files carry a format version and are upgraded as they load.
- Pending note-offs can no longer be dropped; the Clock's header says internal or host.

### Editing
- Undo and redo for every edit (Ctrl/Cmd-Z, Shift-Z), with runs of stepper clicks as one step.
- Drag a value up or down, scroll over it, or double-click to type it. The Random generator's
  seed is editable and its loop can be up to 64 steps.
- Pick a wire up from its input to move or remove it; compatible ports grow while you drag.
- Select all, duplicate, arrow-key nudge, snap to grid, collapse and expand all, and a **?**
  overlay listing every shortcut. Tooltips show.

### Patches and presets
- A patch menu: new patch, factory presets, **My presets**, recent files, save, save as and
  rename, with a dirty guard before anything is loaded over unsaved changes.
- Five more factory patches. Factory patches show as programs in hosts that list them.
- Subsystems save to **My subsystems** or to a file, and load back from either.

### Canvas
- Blocks (titled, coloured, resizable sections that move their modules) and notes.
- A tag menu on every port: pick a tag, colour it, jump to its source, see who listens.
- Group a selection into a subsystem (Ctrl/Cmd-G); crossing wires and tags become ports.
- A minimap, a library search box, drag modules from the library onto the canvas, and a glyph
  per kind of module in the header that lights on collapsed modules.

### Velocity, timing and shapers
- Hits carry a velocity. Accent has its own level and pattern, **Velocity** sets a level with
  humanising, Step grid cells click through off, hit and accent.
- Swing and a 1/16, 1/32 or 1/8 clock rate on the Clock; **Shift**, **Ratchet**, **Fill** and
  **Scene** modules; Switch can take its positions in random order.
- MIDI outs have a note length, a nudge, a General MIDI drum list, a channel and a choke group
  per output, and an **Audition** entry.

### Control and MIDI
- A control signal type with **Macro**, **LFO**, **Random walk** and **Sample & hold** modules
  that steer any numeric parameter, drawn as diamond ports and dotted cables.
- Sixteen host-automatable macros with MIDI learn; **MIDI in** turns notes into gates; **CC out**
  sends controllers; a learnable reset note; MIDI thru.
- **Patternspace MIDI**, an AU MIDI effect for Logic and GarageBand.
- The standalone app's **MIDI** button picks an output device and sends MIDI clock, start and
  stop. A per-DAW routing guide in the README.

### Sound
- Load your own samples by drag and drop or from the sound menu; short ones travel inside the
  patch. Level, pan, start, a low-pass filter, choke groups and poly/mono on every Sample
  player; level and pan on every Audio out; cut-off voices fade instead of clicking; Hermite
  interpolation when pitching.
- An **Out** button with the master meter that opens the output strip: master trim, soft clip
  and a meter per out.

### Build, tests and distribution
- auval and pluginval on the Audio Units and clap-validator on the CLAP in CI, beside pluginval
  on the VST3.
- Headless window tests that load, paint and edit every factory patch.
- Code signing and notarization in the workflow, switched on by repository secrets.
- Release notes come from this file. The canvas source is split into four files.

## 0.2.0 (2026-10-15)

- The Deep Ocean look: one flat deep-blue field, dark glass panels, Sora, JetBrains Mono and
  Michroma.
- Box select, right-drag to pan, copy and paste between modules and between plug-in instances.
- Subsystems can be opened and edited in place, and made empty from the library.
- Drag modules onto a workspace tab to move them there.
- Installers: a macOS .pkg, a Windows Setup.exe and a Linux .deb, built in CI.

## 0.1.0 (2026-10-14)

- First JUCE build: the patch engine, the node canvas, the sampler, VST3, CLAP, AU and LV2
  plug-ins and a standalone app for Linux, Windows and macOS, with an update check in the app.
