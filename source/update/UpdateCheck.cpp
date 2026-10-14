#include "UpdateCheck.h"

namespace ps::update
{
namespace
{
juce::Array<int> versionParts (juce::String v)
{
    v = v.trim();
    if (v.startsWithIgnoreCase ("v"))
        v = v.substring (1);
    v = v.upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);
    juce::Array<int> parts;
    for (auto& p : juce::StringArray::fromTokens (v, ".", ""))
        parts.add (p.getIntValue());
    return parts;
}
} // namespace

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto pa = versionParts (a), pb = versionParts (b);
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i], y = pb[i]; // Array returns 0 past the end
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

juce::String platformName()
{
#if JUCE_MAC
    return "macOS";
#elif JUCE_WINDOWS
    return "Windows";
#else
    return "Linux";
#endif
}

bool parseRelease (const juce::var& json, const juce::String& platform, Release& out)
{
    if (! json.isObject() || (bool) json["draft"] || (bool) json["prerelease"])
        return false;
    auto tag = json["tag_name"].toString().trim();
    if (tag.startsWithIgnoreCase ("v"))
        tag = tag.substring (1);
    if (tag.isEmpty() || ! juce::CharacterFunctions::isDigit (tag[0]))
        return false;

    out = {};
    out.version = tag;
    out.pageUrl = json["html_url"].toString();
    out.notes = json["body"].toString().trim();

    if (auto* assets = json["assets"].getArray())
        for (auto& asset : *assets)
        {
            const auto name = asset["name"].toString();
            if (name.endsWithIgnoreCase (".zip") && name.containsIgnoreCase ("-" + platform))
            {
                out.assetName = name;
                out.assetUrl = asset["browser_download_url"].toString();
                out.assetSize = (juce::int64) asset["size"];
                break;
            }
        }
    return true;
}
} // namespace ps::update
