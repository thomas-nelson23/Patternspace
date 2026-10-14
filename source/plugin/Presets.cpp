#include "Presets.h"

#include <PatternspacePresets.h>

#include <algorithm>

namespace ps::presets
{
namespace
{
    struct Resource
    {
        juce::String file, text;
        juce::var json;
    };

    std::vector<Resource> resources()
    {
        std::vector<Resource> r;
        for (int i = 0; i < PatternspacePresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = PatternspacePresets::getNamedResource (PatternspacePresets::namedResourceList[i], size))
            {
                Resource res { PatternspacePresets::originalFilenames[i], juce::String::fromUTF8 (data, size), {} };
                if (juce::JSON::parse (res.text, res.json).wasOk() && res.json.isObject())
                    r.push_back (std::move (res));
            }
        }
        std::sort (r.begin(), r.end(), [] (const Resource& a, const Resource& b) { return a.file < b.file; });
        return r;
    }
} // namespace

const std::vector<json::Preset>& factory()
{
    static const std::vector<json::Preset> list = [] {
        std::vector<json::Preset> out;
        for (auto& r : resources())
        {
            json::Preset p;
            if (r.json.hasProperty ("patch") && json::presetFromString (r.text, p))
                out.push_back (std::move (p));
        }
        return out;
    }();
    return list;
}

bool subsystemFromVar (const juce::var& v, SubsystemTemplate& out)
{
    if (! v.isObject() || ! v.hasProperty ("g"))
        return false;
    // A template is a subsystem's name, ports and inner patch; read it as a one-module patch.
    auto* node = new juce::DynamicObject();
    const juce::var nodeVar (node);
    node->setProperty ("id", "sub");
    node->setProperty ("k", "sub");
    for (auto* key : { "name", "ins", "inT", "outs", "outT", "g" })
        if (v.hasProperty (key))
            node->setProperty (key, v[key]);
    auto* patch = new juce::DynamicObject();
    const juce::var patchVar (patch);
    patch->setProperty ("nodes", juce::Array<juce::var> { nodeVar });
    Graph g;
    if (! json::fromVar (patchVar, g) || g.nodes.size() != 1 || g.nodes.front().kind != "sub")
        return false;
    out = { v["name"].toString(), g.nodes.front() };
    if (out.name.isEmpty())
        out.name = "Subsystem";
    return true;
}

juce::var subsystemToVar (const Node& subsystem)
{
    Graph g;
    g.nodes.push_back (subsystem);
    const auto patch = json::toVar (g);
    const auto node = patch["nodes"][0];
    auto* file = new juce::DynamicObject();
    for (auto* key : { "name", "ins", "inT", "outs", "outT", "g" })
        if (node.hasProperty (key))
            file->setProperty (key, node[key]);
    return juce::var (file);
}

const std::vector<SubsystemTemplate>& subsystems()
{
    static const std::vector<SubsystemTemplate> list = [] {
        std::vector<SubsystemTemplate> out;
        for (auto& r : resources())
        {
            SubsystemTemplate t;
            if (subsystemFromVar (r.json, t))
                out.push_back (std::move (t));
        }
        return out;
    }();
    return list;
}
} // namespace ps::presets
