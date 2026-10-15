#pragma once

// Helpers shared by the PatchCanvas source files: the module body layout (the rows each kind of
// module shows), the clipboard, and small drawing and text utilities. Nothing here is part of
// the canvas's interface; PatchCanvas.h is.

#include "../core/Engine.h"
#include "../core/Modules.h"
#include "../core/Patch.h"
#include "PatchCanvas.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ps::canvas
{
constexpr float headerH = 26.0f, ioTop = 30.0f, rowH = 18.0f, padX = 9.0f, stepperH = 16.0f, gapY = 3.0f;
constexpr float minZoom = 0.4f, maxZoom = 2.0f;

inline juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// Shared by every open editor, so modules can be copied from one plugin instance into another.
struct Clipboard
{
    std::vector<ps::Node> nodes;
    std::vector<ps::Link> links; // only the wires between copied modules
};
Clipboard& clipboard();

float portY (const ps::Node& n, int i, int count);
float nodeWidth (const ps::Node& n);
juce::Colour kindColour (const ps::Node& n);

// The rows of a module's body, laid out top to bottom.
enum class ItemType
{
    text,
    bigText,
    stepper,
    cells,
    drumRows,
    modes,
    buses,
    meter,
    xy,
    sample,
    switchPos,
    button,
    ctlBar // a control module's value, 0 to 1
};

struct Item
{
    ItemType type;
    juce::Rectangle<float> r;
    std::string key;
    juce::String text, value;
};

juce::String paramValue (const ps::Node& n, const ps::ParamSpec& spec);
juce::String sampleLabelIn (const ps::Graph& root, const ps::Node& n);
std::vector<Item> bodyItems (const ps::Node& n, float x, float y, float w, const std::map<std::string, int>& patchSettings, const ps::Graph& level, const ps::Graph& root);
std::vector<int> patternOf (const ps::Node& n, const ps::Engine::View* v);

// A menu row in a colour, with a tick when it is the current choice.
void colouredItem (juce::PopupMenu& menu, const juce::String& text, juce::Colour colour, bool ticked, std::function<void()> action);
} // namespace ps::canvas
