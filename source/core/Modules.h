#pragma once

#include "Patch.h"

#include <cstdint>
#include <set>
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

// The velocities a plain hit and an accent carry (see Engine.h).
constexpr int kHitVelocity = 96, kAccentVelocity = 127;

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

// Control: Macro, LFO, Random walk and Sample & hold put out a value from 0 to 1 each step and
// can steer one parameter of another module on the same level (node.target is "id:key", with
// num["lo"] and num["hi"] the range it sweeps). modulatableParams() lists what can be steered.
bool isControlSource (const Node& node);
std::vector<ParamSpec> modulatableParams (const Node& node);
bool parseTarget (const std::string& target, std::string& id, std::string& key);

// Steps a whole-number parameter, clamped to its range (some ranges depend on other parameters,
// such as Euclidean hits never exceeding steps). Returns false when nothing changed.
bool stepParam (Graph& graph, Node& node, const std::string& key, int delta);

// MIDI out outputs. Added outputs take the next unused General MIDI drum.
void resizeLanes (Node& node, int count);
std::string noteName (int note); // 36 -> "C1"
int noteNumber (const std::string& name, int fallback = 36);
const char* gmDrumName (int note); // nullptr when the note isn't a common GM drum
const std::vector<std::pair<int, const char*>>& gmDrumList(); // the common GM drums, note and name

// Drops wires and tags on ports a module no longer has.
void prunePorts (Graph& graph, const Node& node);

// Subsystem ports. A subsystem's inputs are the outputs of the Inputs module inside it, and its
// outputs are the inputs of the Outputs module, so these change both sides together, along with
// the wires and tags on them. `parent` is the graph the subsystem sits in.
Node makeSubsystem (const std::string& id, const std::string& name, double x, double y); // no ports yet
Node* subsystemInputs (Node& subsystem);  // the Inputs module inside, made if missing
Node* subsystemOutputs (Node& subsystem); // the Outputs module inside, made if missing
void addSubsystemPort (Node& subsystem, bool output, const std::string& name, PortType type);
void removeSubsystemPort (Graph& parent, Node& subsystem, bool output, int port);
void renameSubsystemPort (Node& subsystem, bool output, int port, const std::string& name);
// Changing a port's type drops the wires on it, since gate and audio ports don't connect.
void setSubsystemPortType (Graph& parent, Node& subsystem, bool output, int port, PortType type);

// Moves the modules with these ids into a new subsystem at (x, y). Wires that cross the new edge
// become ports, one per outside port, and so do tags that cross it, since a tag only reaches
// across one level: the subsystem's output takes over a tag that outside inputs listen to.
// Inside, the modules keep their arrangement, with the Inputs module to their left and the
// Outputs module to their right. Modules it can't move (missing, Inputs, Outputs) are skipped.
struct Grouped
{
    std::string id; // the new subsystem, empty when nothing could be grouped
    int inputs = 0, outputs = 0, tags = 0;
};
Grouped groupIntoSubsystem (Graph& graph, const std::set<std::string>& ids, const std::string& name, double x, double y, double width);

// The modes a Combine or Switch can be in, in display order.
const std::vector<std::string>& combineModes();
extern const char* const kCombineAMinusB; // "A−B" as the prototype spells it

const std::vector<std::string>& sampleNames();
} // namespace ps
