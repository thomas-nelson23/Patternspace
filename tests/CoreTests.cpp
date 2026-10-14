// Tests for the patch engine and patch files. The reference test replays every factory preset and
// checks each module's values against a run of the browser prototype (tests/data/reference-steps.json,
// written by tools/export-prototype.cjs).

#include "../source/core/Engine.h"
#include "../source/core/Modules.h"
#include "../source/core/Patterns.h"
#include "../source/state/PatchJson.h"
#include "../source/update/UpdateCheck.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <iostream>

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
    check (presets.size() == 5, "five factory presets");
    for (auto& preset : presets)
    {
        auto expected = ref["presets"][juce::Identifier (preset.name)];
        auto* modules = expected.getDynamicObject();
        check (modules != nullptr, "reference has " + preset.name);
        if (! modules)
            continue;

        ps::Engine engine;
        engine.load (preset.patch);
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

// The editor draws an open subsystem from the engine's view of the graph inside it.
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
        mismatches += juce::String (v) != juce::String (engine.valuesAt ("sn/pr")) ? 1 : 0;
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
    testMidiOutput();
    testLocate();
    testJsonRoundTrip();
    testEditing();
    testTagsAndFeedback();
    testSubsystemView();
    testNewSubsystem();
    testUpdateCheck();

    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
