#pragma once

#include "../core/Patch.h"

#include <juce_core/juce_core.h>

// Reads and writes patches as JSON in the prototype's format, so a patch saved in the browser
// prototype loads in the plugin and the other way round.
namespace ps::json
{
juce::var toVar (const Graph& graph);
juce::String toString (const Graph& graph);

// Returns false and leaves `graph` alone if the text isn't a patch. Older formats are upgraded and
// missing parameters filled in.
bool fromVar (const juce::var& value, Graph& graph);
bool fromString (const juce::String& text, Graph& graph);

// A preset file is {"name": ..., "patch": {...}}.
struct Preset
{
    juce::String name;
    Graph patch;
};
bool presetFromString (const juce::String& text, Preset& preset);
} // namespace ps::json
