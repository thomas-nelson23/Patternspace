#pragma once

#include <juce_core/juce_core.h>

// What the standalone app's update check reads from GitHub, kept free of UI and networking so the
// tests can cover it.
namespace ps::update
{
// The latest published release, as far as this platform cares.
struct Release
{
    juce::String version;   // "0.2.0", taken from the tag with any leading "v" removed
    juce::String pageUrl;   // the release's page on GitHub
    juce::String notes;     // the release notes, as written
    juce::String assetName; // this platform's download, empty when the release has none
    juce::String assetUrl;
    juce::int64 assetSize = 0;
    bool isInstaller = false; // the .pkg or Setup.exe rather than the zip
};

// Compares dotted version numbers ("v0.10.1" > "0.9"). Missing parts count as 0 and anything after
// a "-" or "+" is ignored. Returns <0, 0 or >0 like strcmp.
int compareVersions (const juce::String& a, const juce::String& b);

// "macOS", "Windows" or "Linux": the name the release workflow puts in each platform's zip.
juce::String platformName();

// The file extension of the platform's installer (".pkg", ".exe"), or empty where there is none.
// Linux has a .deb, but the zip works on every distribution, so the app offers that.
juce::String installerExtension (const juce::String& platform);

// Reads a release from GitHub's "latest release" API response and picks this platform's download:
// the installer whose name has "-<platform>" in it if there is one, otherwise the zip. Returns
// false for anything that isn't a published release.
bool parseRelease (const juce::var& json, const juce::String& platform, Release& out);
} // namespace ps::update
