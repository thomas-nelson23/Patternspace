// Tests for the patch engine and patch files. The reference test replays every factory preset and
// checks each module's values against a run of the browser prototype (tests/data/reference-steps.json,
// written by tools/export-prototype.cjs).

#include "../source/core/Engine.h"
#include "../source/core/Modules.h"
#include "../source/core/Patterns.h"
#include "../source/core/Sampler.h"
#include "../source/state/PatchJson.h"
#include "../source/update/UpdateCheck.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <set>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

template <typename A, typename B>
void checkEq (const A& a, const B& b, const juce::String& what)
{
    check (a == b, what + " (got " + juce::String (a) + ", expected " + juce::String (b) + ")");
}

juce::String joined (const std::vector<int>& v)
{
    juce::StringArray s;
    for (int x : v)
        s.add (juce::String (x));
    return s.joinIntoString (",");
}

juce::File repo() { return juce::File (PATTERNSPACE_REPO_DIR); }

std::vector<ps::json::Preset> factoryPresets()
{
    std::vector<ps::json::Preset> out;
    auto files = repo().getChildFile ("presets/factory").findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (auto& f : files)
    {
        ps::json::Preset p;
        check (ps::json::presetFromString (f.loadFileAsString(), p), "parse " + f.getFileName());
        out.push_back (std::move (p));
    }
    return out;
}

void testRandom()
{
    ps::Rng r (1234);
    const double expected[] = { 0.073294978123158216, 0.70341198984533548, 0.90285601909272373 };
    for (double e : expected)
        check (std::abs (r.next() - e) < 1e-15, "mulberry32 matches the prototype");
}

void testPatterns()
{
    checkEq (joined (ps::euclid (5, 8, 0)), juce::String ("1,0,1,0,1,1,0,1"), "E(5,8)");
    checkEq (joined (ps::euclid (3, 8, 2)), juce::String ("1,0,1,0,0,1,0,0"), "E(3,8) rotated by 2");
    checkEq (joined (ps::euclid (4, 16, 0)), juce::String ("1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0"), "E(4,16)");
    checkEq (joined (ps::randomPattern (8, 5, 90)), juce::String ("1,1,1,1,0,0,0,1"), "random loop, seed 90");
    checkEq (joined (ps::randomPattern (8, 0, 90)), juce::String ("0,0,0,0,0,0,0,0"), "zero density stays silent");
    checkEq ((int) ps::cellularRows().size(), 34, "cellular rows");

    const auto empty = ps::drumMapPattern (50, 50, 0, 0, 0, 0, 0);
    int hits = 0;
    for (auto& row : empty)
        for (int v : row)
            hits += v;
    checkEq (hits, 0, "drum map with every fill at zero is silent");
}

void testNotes()
{
    checkEq (juce::String (ps::noteName (36)), juce::String ("C1"), "note 36 is C1");
    checkEq (juce::String (ps::noteName (0)), juce::String ("C-2"), "note 0 is C-2");
    checkEq (ps::noteNumber ("F#1"), 42, "F#1 is 42");
    checkEq (ps::noteNumber ("C-2"), 0, "C-2 is 0");
    checkEq (ps::noteNumber ("nonsense", 7), 7, "unparseable note falls back");
}

void testReferenceRun()
{
    juce::var ref;
    check (juce::JSON::parse (repo().getChildFile ("tests/data/reference-steps.json").loadFileAsString(), ref).wasOk(),
           "parse the reference run");
    const int steps = (int) ref["steps"];
    const auto seed = (std::uint32_t) (int) ref["probabilitySeed"];

    auto presets = factoryPresets();
    check (presets.size() >= 5, "at least the five original factory presets");
    int compared = 0;
    for (auto& preset : presets)
    {
        auto expected = ref["presets"][juce::Identifier (preset.name)];
        auto* modules = expected.getDynamicObject();
        if (! modules)
            continue; // presets made after the prototype have no reference run
        ++compared;

        ps::Engine engine;
        engine.load (preset.patch);
        engine.setProbabilityMode (ps::Engine::ProbabilityMode::stream); // the prototype's one stream
        engine.setRandomSeed (seed);
        ps::StepOutput out;
        int mismatches = 0;
        for (int s = 0; s < steps; ++s)
        {
            out.clear();
            engine.step (s, out);
            for (auto& m : modules->getProperties())
            {
                const auto want = m.value[s].toString();
                const auto got = juce::String (engine.valuesAt (m.name.toString().toStdString()));
                if (got != want && mismatches++ < 5)
                    std::cerr << "  " << preset.name << " step " << s << " " << m.name.toString() << ": got " << got
                              << ", prototype " << want << "\n";
            }
        }
        checkEq (mismatches, 0, preset.name + " plays the same as the prototype");
    }
    checkEq (compared, 5, "the five prototype presets are compared");
}

// Every factory preset is well formed: unique names, every tag an input listens to has a source,
// every wire lands on ports that exist, and it makes sound within four bars.
void testFactoryPresetsAreSound()
{
    std::set<juce::String> names;
    for (auto& preset : factoryPresets())
    {
        check (names.insert (preset.name).second, preset.name + " has a unique name");
        const auto& g = preset.patch;
        std::set<std::string> owned;
        for (auto& n : g.nodes)
            for (auto& [p, t] : n.tagOut)
                check (owned.insert (t).second, preset.name + ": tag " + juce::String (t) + " has one source");
        for (auto& n : g.nodes)
            for (auto& [q, t] : n.tagIn)
                check (owned.count (t) > 0, preset.name + ": " + juce::String (n.id) + " listens to " + juce::String (t) + ", which nothing owns");
        for (auto& l : g.links)
        {
            const auto *a = g.find (l.a), *b = g.find (l.b);
            check (a && b, preset.name + ": wire between " + juce::String (l.a) + " and " + juce::String (l.b) + " names real modules");
            if (a && b)
                check (l.p < (int) ps::outputsOf (*a).size() && l.q < (int) ps::inputsOf (*b).size(),
                       preset.name + ": wire " + juce::String (l.a) + " -> " + juce::String (l.b) + " lands on real ports");
        }
        ps::Engine engine;
        engine.load (g);
        ps::StepOutput out;
        int notes = 0;
        for (int s = 0; s < 64; ++s)
        {
            out.clear();
            engine.step (s, out);
            notes += (int) out.notes.size() + (int) out.hits.size();
        }
        check (notes > 0, preset.name + " plays something in four bars");
    }
}

void testMidiOutput()
{
    auto presets = factoryPresets();
    auto it = std::find_if (presets.begin(), presets.end(), [] (auto& p) { return p.name == "Four on the Floor"; });
    check (it != presets.end(), "Four on the Floor exists");
    if (it == presets.end())
        return;

    ps::Engine engine;
    engine.load (it->patch);
    ps::StepOutput out;
    int kicks = 0, hats = 0, claps = 0;
    for (int s = 0; s < 16; ++s)
    {
        out.clear();
        engine.step (s, out);
        for (auto& n : out.notes)
        {
            check (n.channel == 10, "drum notes on channel 10");
            kicks += n.note == 36;
            hats += n.note == 42;
            claps += n.note == 39;
        }
    }
    checkEq (kicks, 4, "four kicks in a bar");
    checkEq (hats, 5, "hats from the step grid");
    checkEq (claps, 2, "claps on 2 and 4");

    // The same preset also drives three sample players into audio outs 1-3.
    out.clear();
    engine.locate (0);
    engine.step (0, out);
    check (! out.hits.empty() && out.hits.front().bus == 1, "the kick sample plays on audio out 1");
}

void testLocate()
{
    auto presets = factoryPresets();
    auto it = std::find_if (presets.begin(), presets.end(), [] (auto& p) { return p.name == "Four on the Floor"; });
    if (it == presets.end())
        return;
    ps::Engine continuous, jumped;
    continuous.load (it->patch);
    jumped.load (it->patch);
    ps::StepOutput a, b;
    for (int s = 0; s < 40; ++s)
    {
        a.clear();
        continuous.step (s, a);
    }
    jumped.locate (40);
    for (int s = 40; s < 72; ++s)
    {
        a.clear();
        b.clear();
        continuous.step (s, a);
        jumped.step (s, b);
        check (a.notes.size() == b.notes.size(), "locating mid-bar keeps the generators in phase at step " + juce::String (s));
    }
}

// Probability decides from the song position: two engines agree, and so does one after a jump.
void testDeterministicProbability()
{
    ps::Graph g;
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    auto p = ps::makeNode ("prob", "p", 0, 0);
    p.set ("chance", 5);
    g.nodes = { clk, p };
    g.links = { { "clk", 0, "p", 0 } };
    ps::normalise (g);

    auto run = [&] (int from, int to, bool jump) {
        ps::Engine e;
        e.load (g);
        if (jump)
            e.locate (from);
        juce::String s;
        ps::StepOutput o;
        for (int i = from; i < to; ++i)
        {
            o.clear();
            e.step (i, o);
            s += juce::String (e.valuesAt ("p"));
        }
        return s;
    };
    const auto whole = run (0, 64, false);
    checkEq (run (0, 64, false), whole, "two engines make the same choices");
    checkEq (run (32, 64, true), whole.substring (32), "a jump to bar 3 makes the choices bar 3 always makes");
    check (whole.containsChar ('1') && whole.containsChar ('0'), "a 50% chance plays some steps and skips others");

    g.settings["seed"] = 99;
    check (run (0, 64, false) != whole, "a different patch seed makes different choices");
}

// The Clock's bar outputs follow the host's meter; the follow setting decides what a jump does.
void testMeterAndFollow()
{
    ps::Graph g;
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    auto e = ps::makeNode ("euclid", "e", 0, 0);
    e.set ("len", 7);
    e.set ("hits", 1);
    g.nodes = { clk, e };
    g.links = { { "clk", 0, "e", 0 }, { "clk", 1, "e", 1 } };
    ps::normalise (g);

    ps::Engine engine;
    engine.load (g);
    engine.setMeter (12, 0); // 3/4
    ps::StepOutput o;
    juce::String bars;
    for (int i = 0; i < 24; ++i)
    {
        o.clear();
        engine.step (i, o);
        bars += juce::String (ps::levelOf (engine.view()[0].out[1]));
    }
    checkEq (bars, juce::String ("100000000000100000000000"), "in 3/4 the bar output fires every 12 sixteenths");
    engine.setMeter (16, 8); // bar 1 starts on step 8
    o.clear();
    engine.step (24, o);
    checkEq (ps::levelOf (engine.view()[0].out[1]), 1, "a bar start that isn't step 0 is respected");

    // Phrase: an 11-step pattern re-phases at a jump. Free: it keeps counting.
    auto phase = [&] (int follow) {
        g.settings["follow"] = follow;
        g.links = { { "clk", 0, "e", 0 } };
        g.find ("e")->set ("len", 11);
        ps::Engine x;
        x.load (g);
        for (int i = 0; i < 10; ++i)
        {
            o.clear();
            x.step (i, o);
        }
        x.locate (40);
        return x.view()[1].pos;
    };
    checkEq (phase (0), 7, "phrase mode replays from step 32: 8 steps in, position 7");
    checkEq (phase (1), 39 % 11, "song mode replays from bar 1: position 39 mod 11");
    checkEq (phase (2), 9, "free mode keeps the position it had");
    g.settings.erase ("follow");
}

void testJsonRoundTrip()
{
    for (auto& p : factoryPresets())
    {
        const auto once = ps::json::toString (p.patch);
        ps::Graph again;
        check (ps::json::fromString (once, again), "re-read " + p.name);
        checkEq (ps::json::toString (again), once, p.name + " survives a save and load");
    }

    ps::Graph legacy;
    check (ps::json::fromString (R"({"nodes":[{"id":"m","k":"midi","x":0,"y":0,"p":{"note":"F#1","name":"Hat","ch":10}}],"links":[]})", legacy),
           "read a patch from before MIDI outs had several outputs");
    check (legacy.nodes.size() == 1 && legacy.nodes[0].lanes.size() == 1 && legacy.nodes[0].lanes[0].note == 42
               && legacy.nodes[0].lanes[0].name == "Hat",
           "an old one-note MIDI out becomes a one-output MIDI out");
    check (legacy.pages.size() == 1 && legacy.nodes[0].page == "p1", "a patch without tabs gets one");
    check (legacy.settings.count ("follow") && legacy.settings["follow"] == 0 && legacy.settings["seed"] == 1234,
           "a version 1 file gets the settings version 2 added");
    check (ps::json::toString (legacy).contains ("\"v\": 2"), "saved files carry the format version");

    ps::Graph junk;
    check (! ps::json::fromString ("not json", junk), "garbage is rejected");
    check (! ps::json::fromString ("{\"nodes\":3}", junk), "a non-patch is rejected");
}

void testEditing()
{
    ps::Graph g;
    g.nodes.push_back (ps::makeNode ("euclid", "e", 0, 0));
    g.nodes.push_back (ps::makeNode ("switch", "s", 200, 0));
    g.nodes.push_back (ps::makeNode ("midi", "m", 400, 0));
    g.links.push_back ({ "e", 0, "s", 2 });
    g.links.push_back ({ "s", 3, "m", 3 });
    g.nodes[1].tagOut[3] = "FOUR";

    auto& sw = *g.find ("s");
    checkEq ((int) ps::outputsOf (sw).size(), 4, "a new Switch has four outputs");
    check (ps::stepParam (g, sw, "ways", -1), "ways steps down");
    checkEq ((int) ps::outputsOf (sw).size(), 3, "three ways, three outputs");
    checkEq ((int) g.links.size(), 1, "the wire from output 4 is dropped");
    check (sw.tagOut.empty(), "the tag on output 4 is dropped");

    auto& e = *g.find ("e");
    check (ps::stepParam (g, e, "len", -10), "steps go down");
    checkEq (e.get ("len"), 6, "16 - 10 steps");
    checkEq (e.get ("hits"), 4, "hits still fit");
    check (ps::stepParam (g, e, "len", -3), "steps go down again");
    checkEq (e.get ("hits"), 3, "hits never exceed steps");

    auto& m = *g.find ("m");
    checkEq ((int) m.lanes.size(), 4, "a new MIDI out has four outputs");
    check (ps::stepParam (g, m, "nout", 2), "add two outputs");
    checkEq ((int) m.lanes.size(), 6, "six outputs");
    checkEq (m.lanes[4].note, 39, "the fifth output takes the next GM drum (clap)");
    checkEq (juce::String (m.lanes[4].name), juce::String ("Clap"), "and its name");
    check (ps::stepParam (g, m, "nout", -15), "remove outputs");
    checkEq ((int) m.lanes.size(), 1, "at least one output stays");
}

void testTagsAndFeedback()
{
    // A tag carries a clock across tabs; a wire loop doesn't hang the engine.
    ps::Graph g;
    g.pages = { { "p1", "A" }, { "p2", "B" } };
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    clk.tagOut[0] = "CLK";
    clk.page = "p1";
    auto e = ps::makeNode ("euclid", "e", 0, 0);
    e.set ("hits", 16);
    e.tagIn[0] = "CLK";
    e.page = "p2";
    auto a = ps::makeNode ("accent", "a", 0, 0);
    auto b = ps::makeNode ("accent", "b", 0, 0);
    g.nodes = { clk, e, a, b };
    g.links = { { "a", 0, "b", 0 }, { "b", 0, "a", 0 } };
    ps::Engine engine;
    engine.load (g);
    ps::StepOutput out;
    engine.step (0, out);
    checkEq (juce::String (engine.valuesAt ("e")), juce::String ("1"), "the tag reaches the other tab");
    checkEq (juce::String (engine.valuesAt ("a")), juce::String ("0"), "a feedback loop reads silence");
}
// A subsystem made from scratch: ports added, wired, removed and renamed from either side.
void testNewSubsystem()
{
    ps::Graph g;
    g.nodes.push_back (ps::makeNode ("clock", "clk", 0, 0));
    g.nodes.push_back (ps::makeSubsystem ("sub", "Mine", 200, 0));
    auto* sub = g.find ("sub");
    checkEq ((int) sub->inner.front().nodes.size(), 2, "a new subsystem holds just its Inputs and Outputs");
    checkEq ((int) ps::inputsOf (*sub).size() + (int) ps::outputsOf (*sub).size(), 0, "and has no ports yet");

    ps::addSubsystemPort (*sub, false, "spare", ps::PortType::gate);
    ps::addSubsystemPort (*sub, false, "clk", ps::PortType::gate);
    ps::addSubsystemPort (*sub, true, "hits", ps::PortType::gate);
    auto& inner = sub->inner.front();
    const auto* in = ps::subsystemInputs (*sub);
    const auto* out = ps::subsystemOutputs (*sub);
    checkEq ((int) ps::outputsOf (*in).size(), 2, "the Inputs module has an output per subsystem input");
    checkEq ((int) ps::inputsOf (*out).size(), 1, "the Outputs module has an input per subsystem output");

    const auto inId = in->id, outId = out->id;
    auto e = ps::makeNode ("euclid", "e", 200, 0);
    e.set ("hits", 16);
    inner.nodes.push_back (e);
    inner.links.push_back ({ inId, 1, "e", 0 });
    inner.links.push_back ({ "e", 0, outId, 0 });
    g.links.push_back ({ "clk", 0, "sub", 1 });

    auto play = [&] {
        ps::Engine engine;
        engine.load (g);
        ps::StepOutput o;
        engine.step (0, o);
        return juce::String (engine.valuesAt ("sub"));
    };
    checkEq (play(), juce::String ("1"), "the clock plays through the new subsystem");

    ps::removeSubsystemPort (g, *sub, false, 0);
    checkEq (juce::String (ps::inputsOf (*sub).front()), juce::String ("clk"), "removing an input shifts the rest down");
    checkEq (g.links.back().q, 0, "the outside wire follows");
    checkEq (play(), juce::String ("1"), "and the subsystem still plays");

    ps::renameSubsystemPort (*sub, true, 0, "gate");
    checkEq (juce::String (ps::inputsOf (*ps::subsystemOutputs (*sub)).front()), juce::String ("gate"), "renaming shows inside too");

    ps::normalise (g); // what loading adds: a first tab
    ps::Graph copy;
    check (ps::json::fromString (ps::json::toString (g), copy), "a patch with a new subsystem saves and loads");
    checkEq (juce::String (ps::json::toString (copy)), juce::String (ps::json::toString (g)), "unchanged");

    ps::setSubsystemPortType (g, *sub, true, 0, ps::PortType::audio);
    checkEq ((int) sub->inner.front().links.size(), 1, "a gate wire into a port that became audio is dropped");
}


// Grouping a selection into a subsystem keeps the patch playing the same: wires and tags that
// cross the new edge become ports.
void testGroupIntoSubsystem()
{
    ps::Graph g;
    g.pages = { { "p1", "A" } };
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    clk.tagOut[0] = "CLK";
    auto e = ps::makeNode ("euclid", "e", 200, 0); // hears the clock by tag
    e.set ("hits", 5);
    e.set ("len", 8);
    e.tagIn[0] = "CLK";
    auto p = ps::makeNode ("prob", "p", 400, 0); // wired from e; its output is tagged for two listeners
    p.set ("chance", 10);
    p.tagOut[0] = "HITS";
    auto a = ps::makeNode ("accent", "a", 600, 0);
    a.tagIn[0] = "HITS";
    auto b = ps::makeNode ("accent", "b", 600, 100);
    b.tagIn[0] = "HITS";
    auto d = ps::makeNode ("div", "d", 600, 200); // wired straight from p
    g.nodes = { clk, e, p, a, b, d };
    g.links = { { "e", 0, "p", 0 }, { "p", 0, "d", 0 } };
    for (auto& n : g.nodes)
        n.page = "p1";

    auto play = [] (const ps::Graph& graph) {
        ps::Engine engine;
        engine.load (graph);
        juce::String trace;
        for (int s = 0; s < 32; ++s)
        {
            ps::StepOutput o;
            engine.step (s, o);
            trace += juce::String (engine.valuesAt ("a")) + juce::String (engine.valuesAt ("b")) + juce::String (engine.valuesAt ("d")) + ",";
        }
        return trace;
    };
    const auto before = play (g);
    check (before.contains ("1"), "the patch plays before grouping");

    const auto r = ps::groupIntoSubsystem (g, { "e", "p" }, "Engine", 200, 0, 350);
    check (! r.id.empty(), "the group has an id");
    checkEq (r.inputs, 1, "the clock tag became one input");
    checkEq (r.outputs, 1, "the hit output serves the wire and both tag listeners");
    checkEq (r.tags, 2, "two tags crossed the edge: CLK in, HITS out");
    checkEq ((int) g.nodes.size(), 5, "clock, subsystem, two accents and the divider remain outside");
    const auto* sub = g.find (r.id);
    check (sub != nullptr && sub->kind == "sub", "the subsystem is in the patch");
    checkEq (juce::String (sub->name), juce::String ("Engine"), "named as asked");
    checkEq (juce::String (sub->tagOut.at (0)), juce::String ("HITS"), "the subsystem's output took over the tag");
    checkEq ((int) sub->inner.front().nodes.size(), 4, "Inputs, Outputs, Euclidean and Probability inside");
    check (sub->inner.front().find ("e")->tagIn.empty(), "the inner Euclidean no longer tags, it is wired from Inputs");
    bool wireIn = false, wireOut = false;
    for (auto& l : g.links)
    {
        wireIn = wireIn || (l.a == "clk" && l.b == r.id);
        wireOut = wireOut || (l.a == r.id && l.b == "d");
    }
    check (wireIn, "the clock is wired into the subsystem");
    check (wireOut, "the subsystem is wired on to the divider");
    checkEq (play (g), before, "the patch plays exactly as before");

    ps::normalise (g);
    ps::Graph copy;
    check (ps::json::fromString (ps::json::toString (g), copy), "a grouped patch saves and loads");
    checkEq (play (copy), before, "and still plays the same after a round trip");

    checkEq ((int) ps::groupIntoSubsystem (g, { "nope" }, "Empty", 0, 0, 0).id.size(), 0, "grouping nothing does nothing");
}


// The signal carries velocity and ratchets; the new shapers bend it in known ways.
void testVelocityAndShapers()
{
    checkEq (ps::levelOf (ps::kHit), 1, "a plain hit is level 1");
    checkEq (ps::levelOf (ps::kAccent), 2, "an accent is level 2");
    checkEq (ps::ratchetsOf (ps::withRatchets (ps::kHit, 3)), 3, "ratchets ride above the velocity");
    checkEq (ps::velocityOf (ps::withRatchets (ps::kHit, 3)), ps::kHit, "and leave it alone");
    checkEq (ps::withRatchets (0, 3), 0, "a rest can't ratchet");

    // clock -> euclid (16 of 16) -> shaper -> midi, one lane
    auto build = [] (const std::string& kind, std::function<void (ps::Node&)> set = {}) {
        ps::Graph g;
        g.pages = { { "p", "A" } };
        auto clk = ps::makeNode ("clock", "clk", 0, 0);
        auto e = ps::makeNode ("euclid", "e", 0, 0);
        e.set ("hits", 16);
        auto sh = ps::makeNode (kind, "sh", 0, 0);
        if (set)
            set (sh);
        auto m = ps::makeNode ("midi", "m", 0, 0);
        ps::resizeLanes (m, 1);
        g.nodes = { clk, e, sh, m };
        g.links = { { "clk", 0, "e", 0 }, { "e", 0, "sh", 0 }, { "sh", 0, "m", 0 } };
        return g;
    };
    auto run = [] (const ps::Graph& g, int steps, std::function<void (int, const ps::StepOutput&)> each) {
        ps::Engine engine;
        engine.load (g);
        engine.setMeter (16, 0);
        ps::StepOutput out;
        for (int s = 0; s < steps; ++s)
        {
            out.clear();
            engine.step (s, out);
            each (s, out);
        }
    };

    // Accent: every N steps at one level, the rest at another.
    run (build ("accent", [] (ps::Node& n) { n.set ("every", 4); n.set ("lvl", 120); n.set ("soft", 70); }), 8, [] (int s, const ps::StepOutput& o) {
        checkEq ((int) o.notes.size(), 1, "one note per step through Accent");
        if (! o.notes.empty())
            checkEq (o.notes.front().velocity, s % 4 == 0 ? 120 : 70, "Accent sets the velocity on step " + juce::String (s));
    });
    // Velocity: a fixed level, deterministic humanising.
    run (build ("vel", [] (ps::Node& n) { n.set ("vel", 55); }), 4, [] (int, const ps::StepOutput& o) {
        if (! o.notes.empty())
            checkEq (o.notes.front().velocity, 55, "Velocity sets the level");
    });
    {
        juce::String a, b;
        for (auto* t : { &a, &b })
            run (build ("vel", [] (ps::Node& n) { n.set ("vel", 80); n.set ("rnd", 5); }), 8, [t] (int, const ps::StepOutput& o) {
                if (! o.notes.empty())
                    *t += juce::String (o.notes.front().velocity) + ",";
            });
        checkEq (a, b, "humanised velocities repeat for the same song position");
        check (a.contains (",") && a != "80,80,80,80,80,80,80,80,", "and vary around the level");
    }
    // Shift: the pattern arrives N steps late. Euclid 4 of 16 gives hits on 0, 4, 8, 12.
    {
        auto g = build ("shift", [] (ps::Node& n) { n.set ("steps", 3); });
        g.find ("e")->set ("hits", 4);
        juce::String hits;
        run (g, 16, [&hits] (int s, const ps::StepOutput& o) { if (! o.notes.empty()) hits += juce::String (s) + ","; });
        checkEq (hits, juce::String ("3,7,11,15,"), "Shift delays by three steps");
    }
    // Ratchet: every hit repeats; a MIDI out passes the count on.
    run (build ("ratchet", [] (ps::Node& n) { n.set ("n", 3); n.set ("chance", 10); }), 2, [] (int, const ps::StepOutput& o) {
        if (! o.notes.empty())
            checkEq (o.notes.front().repeats, 3, "Ratchet asks for three notes in the step");
    });
    // Fill: silence in, hits appear only on the last bar of every two.
    {
        auto g = build ("fill", [] (ps::Node& n) { n.set ("bars", 2); n.set ("dens", 10); });
        g.find ("e")->set ("hits", 0);
        int bar1 = 0, bar2 = 0;
        run (g, 32, [&] (int s, const ps::StepOutput& o) { (s < 16 ? bar1 : bar2) += (int) o.notes.size(); });
        checkEq (bar1, 0, "Fill stays quiet on an ordinary bar");
        check (bar2 > 4, "and fills the last bar of the cycle");
    }
    // Scene: picks one of its inputs.
    {
        ps::Graph g;
        g.pages = { { "p", "A" } };
        auto clk = ps::makeNode ("clock", "clk", 0, 0);
        auto a = ps::makeNode ("euclid", "a", 0, 0);
        a.set ("hits", 16);
        auto b = ps::makeNode ("euclid", "b", 0, 0);
        b.set ("hits", 0);
        auto sc = ps::makeNode ("scene", "sc", 0, 0);
        sc.set ("ways", 2);
        sc.set ("scene", 2);
        g.nodes = { clk, a, b, sc };
        g.links = { { "clk", 0, "a", 0 }, { "clk", 0, "b", 0 }, { "a", 0, "sc", 0 }, { "b", 0, "sc", 1 } };
        checkEq ((int) ps::inputsOf (sc).size(), 2, "a Scene has an input per way");
        ps::Engine engine;
        engine.load (g);
        ps::StepOutput out;
        engine.step (0, out);
        checkEq (juce::String (engine.valuesAt ("sc")), juce::String ("0"), "scene 2 hears the silent input");
        g.find ("sc")->set ("scene", 1);
        engine.load (g);
        engine.step (0, out);
        checkEq (juce::String (engine.valuesAt ("sc")), juce::String ("1"), "scene 1 hears the busy one");
    }
    // Switch in random order: deterministic, never the same position twice in a row.
    {
        auto g = build ("switch", [] (ps::Node& n) { n.set ("ways", 4); n.set ("rnd", 1); });
        g.links = { { "clk", 0, "e", 0 }, { "clk", 0, "sh", 0 }, { "e", 0, "sh", 2 } };
        juce::String a, b;
        for (auto* t : { &a, &b })
        {
            ps::Engine engine;
            engine.load (g);
            ps::StepOutput out;
            int previous = -1;
            for (int s = 0; s < 32; ++s)
            {
                engine.step (s, out);
                const int sel = engine.view()[2].sel;
                check (sel != previous, "a random Switch moves every step");
                previous = sel;
                *t += juce::String (sel);
            }
        }
        checkEq (a, b, "its order is the same every run");
    }
    // Step grid levels and length.
    {
        auto st = ps::makeNode ("step", "st", 0, 0);
        ps::Graph g;
        checkEq ((int) st.cells.size(), 16, "a Step grid starts with 16 cells");
        ps::stepParam (g, st, "len", 8);
        checkEq ((int) st.cells.size(), 24, "its length grows the grid");
        st.cells[0] = 2;
        st.cells[1] = 1;
        g.nodes = { ps::makeNode ("clock", "clk", 0, 0), st };
        g.links = { { "clk", 0, "st", 0 } };
        ps::Engine engine;
        engine.load (g);
        ps::StepOutput out;
        engine.step (0, out);
        checkEq (juce::String (engine.valuesAt ("st")), juce::String ("2"), "a level-2 cell is an accent");
        engine.step (1, out);
        checkEq (juce::String (engine.valuesAt ("st")), juce::String ("1"), "a level-1 cell a hit");
    }
    // MIDI out: note length, nudge, per-lane channel and choke groups reach the output.
    {
        auto g = build ("accent");
        auto* m = g.find ("m");
        m->set ("len", 3);
        m->set ("ofs", -10);
        m->lanes.front().channel = 5;
        m->lanes.front().choke = 2;
        run (g, 1, [] (int, const ps::StepOutput& o) {
            check (! o.notes.empty(), "a note plays");
            if (o.notes.empty())
                return;
            const auto& n = o.notes.front();
            checkEq (n.channel, 5, "the lane's own channel wins");
            check (std::abs (n.length - 2.5) < 1e-9, "length is in steps, ending half a step early");
            check (std::abs (n.offset + 0.1) < 1e-9, "nudge is a share of a step");
            check (n.choke != 0, "the choke group is set");
        });
        ps::Graph copy;
        check (ps::json::fromString (ps::json::toString (g), copy), "lanes with channel and choke save");
        checkEq (copy.find ("m")->lanes.front().channel, 5, "and load the channel");
        checkEq (copy.find ("m")->lanes.front().choke, 2, "and the choke group");
    }
}


// Control modules put out a value each step and steer a parameter of another module.
void testControlSignals()
{
    ps::Graph g;
    g.pages = { { "p", "A" } };
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    auto e = ps::makeNode ("euclid", "e", 0, 0);
    e.set ("hits", 0);
    auto mac = ps::makeNode ("macro", "mac", 0, 0); // Macro 1 sweeps the Euclidean's hits 0..16
    mac.set ("macro", 1);
    mac.target = "e:hits";
    mac.set ("lo", 0);
    mac.set ("hi", 16);
    auto lfo = ps::makeNode ("lfo", "lfo", 0, 0);
    lfo.set ("period", 4);
    lfo.mode = "saw";
    auto cc = ps::makeNode ("cc", "cc", 0, 0);
    cc.set ("cc", 74);
    cc.set ("ch", 3);
    g.nodes = { clk, e, mac, lfo, cc };
    g.links = { { "clk", 0, "e", 0 }, { "lfo", 0, "cc", 0 } };
    check (ps::outputType (lfo, 0) == ps::PortType::control, "an LFO's output is a control");
    check (ps::inputType (cc, 0) == ps::PortType::control, "a CC out takes a control");
    check (! ps::modulatableParams (e).empty(), "a Euclidean has parameters a control can steer");

    ps::Engine engine;
    engine.load (g);
    ps::StepOutput out;
    int hits = 0;
    for (int s = 0; s < 16; ++s)
    {
        out.clear();
        engine.step (s, out);
        hits += engine.valuesAt ("e") == "1";
    }
    checkEq (hits, 0, "with Macro 1 at zero the Euclidean has no hits");
    engine.setMacro (0, 1.0f);
    hits = 0;
    for (int s = 16; s < 32; ++s)
    {
        out.clear();
        engine.step (s, out);
        hits += engine.valuesAt ("e") == "1";
    }
    checkEq (hits, 16, "at full it has sixteen");
    engine.setMacro (0, 0.25f);
    hits = 0;
    for (int s = 32; s < 48; ++s)
    {
        out.clear();
        engine.step (s, out);
        hits += engine.valuesAt ("e") == "1";
    }
    checkEq (hits, 4, "a quarter of the way gives four");
    checkEq (engine.controlAt ("mac"), 250, "the Macro's value is 250 of 1000");

    // The saw LFO climbs over its period and the CC out sends only changes.
    juce::String ccs;
    for (int s = 48; s < 56; ++s)
    {
        out.clear();
        engine.step (s, out);
        for (auto& c : out.controls)
            ccs += juce::String (c.value) + (c.channel == 3 && c.cc == 74 ? "" : "?") + ",";
    }
    checkEq (ccs, juce::String ("0,32,64,95,0,32,64,95,"), "a saw over four steps reaches the CC out on channel 3");

    // Sample & hold with nothing on its input draws a seeded value on each trigger; Random walk
    // wanders but stays in range; both repeat for the same song position.
    auto sh = ps::makeNode ("sh", "sh", 0, 0);
    auto walk = ps::makeNode ("walk", "walk", 0, 0);
    walk.set ("step", 50);
    g.nodes = { clk, sh, walk };
    g.links = { { "clk", 1, "sh", 0 } };
    juce::String a, b;
    for (auto* t : { &a, &b })
    {
        ps::Engine en;
        en.load (g);
        en.setMeter (4, 0);
        for (int s = 0; s < 12; ++s)
        {
            out.clear();
            en.step (s, out);
            const int w = en.controlAt ("walk");
            check (w >= 0 && w <= 1000, "the walk stays within 0 to 1000");
            *t += juce::String (en.controlAt ("sh")) + ":" + juce::String (w) + ",";
        }
    }
    checkEq (a, b, "control signals repeat for the same song position");
    check (a.substring (0, a.indexOf (":")) != "0", "sample and hold drew a value on the first bar");

    // MIDI in: a note that arrived fires the matching output on the next step.
    auto in = ps::makeNode ("midiin", "in", 0, 0);
    g.nodes = { clk, in };
    g.links = {};
    ps::Engine en;
    en.load (g);
    en.noteIn (1, 38, 100); // snare, lane 2
    out.clear();
    en.step (0, out);
    checkEq (juce::String (en.valuesAt ("in")), juce::String ("0,1,0,0"), "the snare pad fired its output");
    out.clear();
    en.step (1, out);
    checkEq (juce::String (en.valuesAt ("in")), juce::String ("0,0,0,0"), "and only for that step");

    ps::Graph copy;
    check (ps::json::fromString (ps::json::toString (g), copy), "control modules save and load");
    g.nodes = { clk, e, mac };
    check (ps::json::fromString (ps::json::toString (g), copy) && copy.find ("mac")->target == "e:hits", "with their target");
}

// The editor draws an open subsystem from the engine's view of the graph inside it.
void testSamplesAndMixing()
{
    // A patch sound: a one-second ramp at 48 kHz, played by one Sample player into Out 2.
    ps::Graph g;
    g.pages = { { "p", "A" } };
    ps::Sample s;
    s.id = "s1";
    s.name = "ramp";
    s.rate = 48000;
    s.data.resize (48000);
    for (size_t i = 0; i < s.data.size(); ++i)
        s.data[i] = 0.5f;
    g.samples.push_back (s);
    auto clk = ps::makeNode ("clock", "clk", 0, 0);
    auto e = ps::makeNode ("euclid", "e", 0, 0);
    e.set ("hits", 16);
    auto sp = ps::makeNode ("sample", "sp", 0, 0);
    sp.sample = "s1";
    sp.set ("lvl", 50);
    sp.set ("pan", 50);
    sp.set ("choke", 2);
    sp.set ("mono", 1);
    sp.set ("start", 10);
    sp.set ("cut", 60);
    auto out = ps::makeNode ("audio", "out", 0, 0);
    out.set ("bus", 2);
    g.nodes = { clk, e, sp, out };
    g.links = { { "clk", 0, "e", 0 }, { "e", 0, "sp", 0 }, { "sp", 0, "out", 0 } };

    // The file format carries the sound (it is under the embed limit) and the player's settings.
    ps::Graph again;
    check (ps::json::fromString (ps::json::toString (g), again), "a patch with a sample re-reads");
    check (again.samples.size() == 1 && again.samples[0].id == "s1" && again.samples[0].name == "ramp" && again.samples[0].rate == 48000
               && again.samples[0].data.size() == 48000 && std::abs (again.samples[0].data[100] - 0.5f) < 0.001f,
           "the sound is embedded as 16-bit PCM and comes back");
    check (again.nodes[2].get ("choke") == 2 && again.nodes[2].get ("mono") == 1 && again.nodes[2].get ("cut") == 60, "the player's settings round-trip");
    ps::Sample longOne = s;
    longOne.id = "s2";
    longOne.data.resize (48000 * 5);
    longOne.path = "/tmp/nowhere.wav";
    g.samples.push_back (longOne);
    ps::Graph again2;
    check (ps::json::fromString (ps::json::toString (g), again2) && again2.samples.size() == 2 && again2.samples[1].data.empty()
               && again2.samples[1].path == "/tmp/nowhere.wav",
           "a sound over the embed limit keeps its path only");
    g.samples.pop_back();

    // The engine names the sound after the built-in ones, with the player's level, pan and so on.
    ps::Engine engine;
    engine.load (g);
    engine.setMeter (16, 0);
    ps::StepOutput o;
    o.clear();
    engine.step (0, o);
    checkEq ((int) o.hits.size(), 1, "one hit into one out");
    if (! o.hits.empty())
    {
        const auto& h = o.hits[0];
        checkEq (h.sample, (int) ps::sampleNames().size(), "the patch's first sound follows the built-in ones");
        checkEq (h.bus, 2, "into Out 2");
        check (h.level == 50 && h.pan == 50 && h.start == 10 && h.cutoff == 60 && h.choke == 2 && h.mono, "the hit carries the player's settings");
    }

    // The sampler: level and pan land on the right channel and bus, the start offset skips the
    // beginning, and a second mono hit fades the first out instead of stacking.
    ps::Sampler sampler;
    sampler.prepare (48000);
    sampler.setUserSamples (ps::Sampler::bankFor (g.samples));
    checkEq (sampler.numSounds(), (int) ps::sampleNames().size() + 1, "the bank holds the built-in sounds and the patch's");
    std::vector<float> l (512, 0.0f), r (512, 0.0f), other (512, 0.0f);
    std::array<float*, ps::Sampler::numBuses> left {}, right {};
    for (auto& p : left) p = other.data();
    for (auto& p : right) p = other.data();
    left[1] = l.data();
    right[1] = r.data();
    auto hit = o.hits.empty() ? ps::StepOutput::Hit {} : o.hits[0];
    hit.cutoff = 100; // open, so the level is easy to read
    sampler.trigger (hit, 0);
    sampler.render (left, right, 512);
    const float expected = 0.5f * 0.4f * 0.5f; // the sound, a plain hit's gain, Level 50%
    check (std::abs (r[1] - expected) < 0.02f, "panned hard right, the right channel carries the level: " + juce::String (r[1]));
    check (std::abs (l[1]) < 0.001f, "and the left channel nothing");
    check (std::abs (other[1]) < 0.001f, "and no other out hears it");
    checkEq (sampler.activeVoices(), 1, "one voice plays");
    sampler.trigger (hit, 0); // mono: the first voice goes into a fading tail
    checkEq (sampler.activeVoices(), 1, "a mono retrigger replaces the voice rather than stacking");
    std::fill (r.begin(), r.end(), 0.0f);
    sampler.render (left, right, 512);
    check (r[0] > expected * 1.5f && r[400] < expected * 1.2f, "the old voice fades over a few milliseconds instead of cutting: " + juce::String (r[0]) + " then " + juce::String (r[400]));

    // The filter closes the sound down: at a low cutoff a step input's first samples are quieter.
    ps::Sampler dull;
    dull.prepare (48000);
    dull.setUserSamples (ps::Sampler::bankFor (g.samples));
    hit.cutoff = 10;
    hit.mono = false;
    std::fill (r.begin(), r.end(), 0.0f);
    dull.trigger (hit, 0);
    dull.render (left, right, 512);
    check (r[0] < expected * 0.5f && r[500] > r[0], "a low cutoff rounds the attack off: " + juce::String (r[0]) + " then " + juce::String (r[500]));

    // Choke: a hit in group 2 cuts a different player's voice in group 2.
    ps::Sampler choke;
    choke.prepare (48000);
    choke.setUserSamples (ps::Sampler::bankFor (g.samples));
    hit.cutoff = 100;
    hit.owner = 1;
    choke.trigger (hit, 0);
    hit.owner = 2;
    choke.trigger (hit, 0);
    checkEq (choke.activeVoices(), 1, "a choke group holds one voice at a time");
    hit.choke = 0;
    choke.trigger (hit, 0);
    checkEq (choke.activeVoices(), 2, "outside the group, voices stack");

    // A missing sound (bad index) is ignored, not a crash; the built-in sounds still play.
    hit.sample = 99;
    choke.trigger (hit, 0);
    checkEq (choke.activeVoices(), 2, "an unknown sound is ignored");
    hit.sample = 0;
    hit.bus = 1;
    choke.trigger (hit, 0);
    checkEq (choke.activeVoices(), 3, "a built-in sound still plays");
}

void testSubsystemView()
{
    auto presets = factoryPresets();
    auto it = std::find_if (presets.begin(), presets.end(), [] (auto& p) { return p.patch.find ("sn") != nullptr; });
    check (it != presets.end(), "a factory preset has the subsystem sn");
    if (it == presets.end())
        return;
    const auto& patch = it->patch;
    const auto& inner = patch.find ("sn")->inner.front();

    ps::Engine engine;
    engine.load (patch);
    engine.setViewPath ({ "sn" });
    checkEq ((int) engine.view().size(), (int) inner.nodes.size(), "the view lists the modules inside sn");
    const int pr = inner.indexOf ("pr");
    ps::StepOutput out;
    int mismatches = 0, hits = 0;
    for (int s = 0; s < 64; ++s)
    {
        out.clear();
        engine.step (s, out);
        const int v = engine.view()[(size_t) pr].out[0];
        hits += v > 0 ? 1 : 0;
        mismatches += juce::String (ps::levelOf (v)) != juce::String (engine.valuesAt ("sn/pr")) ? 1 : 0;
    }
    checkEq (mismatches, 0, "the view inside sn follows its modules");
    check (hits > 0, "the snare inside sn plays");

    engine.setViewPath ({ "sn", "missing" });
    checkEq ((int) engine.view().size(), (int) patch.nodes.size(), "a broken path shows the root patch");
    engine.setViewPath ({});
    checkEq ((int) engine.view().size(), (int) patch.nodes.size(), "an empty path shows the root patch");
}

void testUpdateCheck()
{
    using ps::update::compareVersions;
    check (compareVersions ("0.2.0", "0.1.0") > 0, "a newer minor version wins");
    check (compareVersions ("v0.10.0", "0.9.3") > 0, "versions compare as numbers, not text");
    check (compareVersions ("v1.0", "1.0.0") == 0, "a leading v and missing parts don't matter");
    check (compareVersions ("1.0.0-beta.2", "1.0.0") == 0, "suffixes are ignored");
    check (compareVersions ("0.1.0", "0.1.1") < 0, "an older version loses");

    auto release = juce::JSON::parse (R"({
        "tag_name": "v0.2.0", "html_url": "https://github.com/o/r/releases/tag/v0.2.0", "body": "Notes ",
        "draft": false, "prerelease": false,
        "assets": [
            { "name": "Patternspace-0.2.0-Linux.zip", "browser_download_url": "https://x/linux.zip", "size": 10 },
            { "name": "Patternspace-0.2.0-macOS.zip", "browser_download_url": "https://x/mac.zip", "size": 20 },
            { "name": "Patternspace-0.2.0-Windows.zip", "browser_download_url": "https://x/win.zip", "size": 30 }
        ] })");
    ps::update::Release r;
    check (ps::update::parseRelease (release, "macOS", r), "a release parses");
    checkEq (r.version, juce::String ("0.2.0"), "the version comes from the tag");
    checkEq (r.assetUrl, juce::String ("https://x/mac.zip"), "the macOS zip is picked");
    checkEq (r.notes, juce::String ("Notes"), "the notes are kept");
    check (ps::update::parseRelease (release, "Windows", r) && r.assetSize == 30, "the Windows zip is picked");
    check (ps::update::parseRelease (release, "Haiku", r) && r.assetUrl.isEmpty(), "no zip for an unknown platform");
    auto withInstallers = juce::JSON::parse (R"({
        "tag_name": "v0.3.0", "draft": false, "prerelease": false,
        "assets": [
            { "name": "Patternspace-0.3.0-Linux.zip", "browser_download_url": "https://x/linux.zip", "size": 10 },
            { "name": "patternspace_0.3.0_amd64.deb", "browser_download_url": "https://x/linux.deb", "size": 11 },
            { "name": "Patternspace-0.3.0-macOS.zip", "browser_download_url": "https://x/mac.zip", "size": 20 },
            { "name": "Patternspace-0.3.0-macOS.pkg", "browser_download_url": "https://x/mac.pkg", "size": 21 },
            { "name": "Patternspace-0.3.0-Windows.zip", "browser_download_url": "https://x/win.zip", "size": 30 },
            { "name": "Patternspace-0.3.0-Windows-Setup.exe", "browser_download_url": "https://x/win.exe", "size": 31 }
        ] })");
    check (ps::update::parseRelease (withInstallers, "macOS", r) && r.assetUrl == "https://x/mac.pkg" && r.isInstaller,
           "the macOS installer wins over the zip");
    check (ps::update::parseRelease (withInstallers, "Windows", r) && r.assetUrl == "https://x/win.exe" && r.isInstaller,
           "the Windows installer wins over the zip");
    check (ps::update::parseRelease (withInstallers, "Linux", r) && r.assetUrl == "https://x/linux.zip" && ! r.isInstaller,
           "Linux gets the zip");
    check (ps::update::parseRelease (release, "macOS", r) && ! r.isInstaller, "a release without installers still offers the zip");
    check (! ps::update::parseRelease (juce::JSON::parse (R"({"tag_name":"v1.0.0","prerelease":true})"), "Linux", r),
           "a pre-release is ignored");
    check (! ps::update::parseRelease (juce::JSON::parse (R"({"message":"Not Found"})"), "Linux", r), "a 404 body is rejected");
}
} // namespace

int main()
{
    testRandom();
    testPatterns();
    testNotes();
    testReferenceRun();
    testFactoryPresetsAreSound();
    testMidiOutput();
    testLocate();
    testDeterministicProbability();
    testMeterAndFollow();
    testJsonRoundTrip();
    testEditing();
    testTagsAndFeedback();
    testSubsystemView();
    testNewSubsystem();
    testGroupIntoSubsystem();
    testVelocityAndShapers();
    testControlSignals();
    testSamplesAndMixing();
    testUpdateCheck();

    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
