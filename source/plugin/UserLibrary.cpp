#include "UserLibrary.h"

#include <algorithm>

namespace ps::library
{
namespace
{
    std::vector<Entry> list (const juce::File& dir)
    {
        std::vector<Entry> out;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
            out.push_back ({ f.getFileNameWithoutExtension(), f });
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        return out;
    }

    juce::File target (const juce::File& dir, const juce::String& name)
    {
        dir.createDirectory();
        auto safe = juce::File::createLegalFileName (name.trim());
        if (safe.isEmpty())
            safe = "Untitled";
        return dir.getChildFile (safe + ".json");
    }

    juce::File recentFile() { return folder().getChildFile ("recent.json"); }
} // namespace

juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Patternspace");
}
juce::File presetsFolder() { return folder().getChildFile ("Presets"); }
juce::File subsystemsFolder() { return folder().getChildFile ("Subsystems"); }

std::vector<Entry> presets() { return list (presetsFolder()); }
std::vector<Entry> subsystems() { return list (subsystemsFolder()); }

juce::File savePreset (const juce::String& name, const Graph& patch)
{
    const auto file = target (presetsFolder(), name);
    auto* obj = new juce::DynamicObject();
    const juce::var preset (obj);
    obj->setProperty ("name", file.getFileNameWithoutExtension());
    obj->setProperty ("patch", json::toVar (patch));
    return file.replaceWithText (juce::JSON::toString (preset, false)) ? file : juce::File();
}

juce::File saveSubsystem (const juce::String& name, const Node& subsystem)
{
    const auto file = target (subsystemsFolder(), name);
    auto data = presets::subsystemToVar (subsystem);
    if (auto* o = data.getDynamicObject())
        o->setProperty ("name", file.getFileNameWithoutExtension());
    return file.replaceWithText (juce::JSON::toString (data, false)) ? file : juce::File();
}

juce::StringArray recentFiles()
{
    juce::StringArray out;
    juce::var v;
    if (juce::JSON::parse (recentFile().loadFileAsString(), v).wasOk())
        if (auto* a = v.getArray())
            for (auto& s : *a)
                if (juce::File (s.toString()).existsAsFile())
                    out.add (s.toString());
    return out;
}

void addRecentFile (const juce::File& file)
{
    auto recent = recentFiles();
    recent.removeString (file.getFullPathName());
    recent.insert (0, file.getFullPathName());
    while (recent.size() > 10)
        recent.remove (recent.size() - 1);
    juce::Array<juce::var> a;
    for (auto& s : recent)
        a.add (s);
    folder().createDirectory();
    recentFile().replaceWithText (juce::JSON::toString (juce::var (a), false));
}
} // namespace ps::library
