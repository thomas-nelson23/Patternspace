#pragma once

#include "Patch.h"

#include <cstdint>
#include <string>
#include <vector>

// What each kind of module is: its ports, default parameters and editable ranges.
namespace ps
{
struct KindInfo
{
    const char* id;
    const char* title;
    std::uint32_t colour; // 0xRRGGBB
    std::vector<std::string> ins, outs;
    std::vector<PortType> inT, outT; // types per port; missing entries are gates
    int width = 150;
    bool generator = false; // shows its pattern and loop position
};

const KindInfo* kindInfo (const std::string& kind);

struct LibrarySection
{
    const char* title;
    std::vector<std::string> kinds;
};
const std::vector<LibrarySection>& librarySections();

// Ports as the patch currently has them. Switches and MIDI outs change their port count with their
// parameters, and subsystems carry their own port lists.
std::vector<std::string> inputsOf (const Node& node);
std::vector<std::string> outputsOf (const Node& node);
PortType inputType (const Node& node, int input);
PortType outputType (const Node& node, int output);

std::string titleOf (const Node& node);
std::string subtitleOf (const Node& node);

// A new module of the given kind with default parameters.
Node makeNode (const std::string& kind, const std::string& id, double x, double y);
void applyDefaults (Node& node);

struct ParamSpec
{
    const char* key;
    const char* label;
    int minimum, maximum;
    int displayScale = 1;          // shown as value * scale
    const char* suffix = "";       // for example "%"
};
std::vector<ParamSpec> paramsOf (const Node& node);

// Steps a whole-number parameter, clamped to its range (some ranges depend on other parameters,
// such as Euclidean hits never exceeding steps). Returns false when nothing changed.
bool stepParam (Graph& graph, Node& node, const std::string& key, int delta);

// MIDI out outputs. Added outputs take the next unused General MIDI drum.
void resizeLanes (Node& node, int count);
std::string noteName (int note); // 36 -> "C1"
int noteNumber (const std::string& name, int fallback = 36);
const char* gmDrumName (int note); // nullptr when the note isn't a common GM drum

// Drops wires and tags on ports a module no longer has.
void prunePorts (Graph& graph, const Node& node);

// The modes a Combine or Switch can be in, in display order.
const std::vector<std::string>& combineModes();
extern const char* const kCombineAMinusB; // "A−B" as the prototype spells it

const std::vector<std::string>& sampleNames();
} // namespace ps
