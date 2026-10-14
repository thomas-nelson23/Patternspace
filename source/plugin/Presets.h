#pragma once

#include "../core/Patch.h"
#include "../state/PatchJson.h"

#include <juce_core/juce_core.h>

#include <vector>

// The factory presets and subsystems in presets/, embedded in the plug-in.
namespace ps::presets
{
const std::vector<json::Preset>& factory();

struct SubsystemTemplate
{
    juce::String name;
    Node node; // a "sub" module ready to add; give it an id and position first
};
const std::vector<SubsystemTemplate>& subsystems();

// Subsystem files hold {"name", "ins", "inT", "outs", "outT", "g"}, as presets/subsystems/ does,
// so a part of one patch can be saved and added to another.
bool subsystemFromVar (const juce::var& file, SubsystemTemplate& out);
juce::var subsystemToVar (const Node& subsystem);
} // namespace ps::presets
