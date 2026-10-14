#pragma once

#include <map>
#include <string>
#include <vector>

// The patch data model. It mirrors the prototype's JSON (docs/prototype/patchbay.html), so patches
// move between the two unchanged. Nothing here depends on JUCE.
namespace ps
{
enum class PortType
{
    gate,
    audio
};

// One output of a MIDI out module: a named note fed by its own input.
struct MidiLane
{
    std::string name;
    int note = 36;
};

struct Graph;

struct Node
{
    std::string id;
    std::string kind; // "clock", "euclid", "random", "ca", "grids", "step", "div", "accent", "prob",
                      // "combine", "switch", "sample", "audio", "midi", "sub", "subin", "subout"
    double x = 0, y = 0;
    bool collapsed = false;
    std::string page; // root-level modules sit on a workspace tab; empty inside a subsystem

    std::map<std::string, int> num; // whole-number parameters such as hits, len and ch
    std::string mode;               // Combine: OR, AND, XOR, A-B. Switch: split or merge
    std::string sample;             // Sample player
    std::string name;               // MIDI out and subsystem name
    std::vector<int> cells;         // Step grid
    std::vector<MidiLane> lanes;    // MIDI out

    // Port names and types, set only on subsystems and the Inputs and Outputs modules inside them.
    std::vector<std::string> ins, outs;
    std::vector<PortType> inT, outT;
    bool hasIns = false, hasOuts = false;

    // Tags connect ports without a wire, like Q-SYS signal names: one output owns a tag and any
    // input with the same tag receives it.
    std::map<int, std::string> tagIn, tagOut;

    std::vector<Graph> inner; // a subsystem's own patch: zero or one entries

    int get (const std::string& key, int fallback = 0) const;
    void set (const std::string& key, int value) { num[key] = value; }
};

struct Link
{
    std::string a; // source module id
    int p = 0;     // its output
    std::string b; // destination module id
    int q = 0;     // its input
};

struct Page
{
    std::string id, name;
};

struct Block
{
    std::string id, title, colour, page;
    double x = 0, y = 0, w = 200, h = 120;
};

struct Note
{
    std::string id, text, page;
    double x = 0, y = 0, w = 0;
};

struct Graph
{
    std::vector<Page> pages;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Block> blocks;
    std::vector<Note> notes;
    std::map<std::string, std::string> tagColours; // patch-wide colour per tag name

    int indexOf (const std::string& id) const;
    Node* find (const std::string& id);
    const Node* find (const std::string& id) const;
    std::string freshId (const std::string& prefix) const;
};

// Fills in what older or hand-written patches leave out: a first tab, default parameters, and
// MIDI outs saved before they had several outputs. Applies to subsystems too.
void normalise (Graph& graph);
} // namespace ps
