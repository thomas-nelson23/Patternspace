#pragma once

#include "../core/Patch.h"
#include "../state/PatchJson.h"
#include "Presets.h"

#include <juce_core/juce_core.h>

#include <vector>

// The user's own presets and subsystems, kept as JSON files in the user's application data
// folder (Patternspace/Presets and Patternspace/Subsystems), plus the list of recently opened
// patch files. Every plug-in instance and the standalone app see the same folders.
namespace ps::library
{
juce::File folder();           // .../Patternspace
juce::File presetsFolder();    // .../Patternspace/Presets
juce::File subsystemsFolder(); // .../Patternspace/Subsystems

struct Entry
{
    juce::String name;
    juce::File file;
};
std::vector<Entry> presets();    // sorted by name
std::vector<Entry> subsystems(); // sorted by name

// Writes a preset or subsystem under the given name (made file-safe) and returns the file, or an
// invalid file if it couldn't be written.
juce::File savePreset (const juce::String& name, const Graph& patch);
juce::File saveSubsystem (const juce::String& name, const Node& subsystem);

// Patch files opened or saved by hand, most recent first, at most ten.
juce::StringArray recentFiles();
void addRecentFile (const juce::File& file);
} // namespace ps::library
