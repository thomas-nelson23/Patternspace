#pragma once

#include "../update/UpdateCheck.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The standalone app's updater. It asks GitHub for the latest release, compares it with this
// build's version and, if it's newer, offers to download this platform's zip to the Downloads
// folder and show it there. It never replaces the running app itself.
class Updater : private juce::URL::DownloadTaskListener
{
public:
    // Dialogs are centred on `parent`.
    explicit Updater (juce::Component& parent);
    ~Updater() override;

    // Shows the result either way.
    void checkNow();
    // Checks quietly, at most once a day, if the user hasn't turned it off. Only says something
    // when there is a version the user hasn't skipped.
    void checkIfDue();

    bool checksAtLaunch() const;
    void setChecksAtLaunch (bool);

    bool updateAvailable() const { return available; }
    std::function<void()> onStateChanged; // called when updateAvailable() changes

    static juce::String currentVersion();

private:
    enum class Result { found, upToDate, noRelease, failed };

    void startCheck (bool userInitiated);
    void checkFinished (Result, const ps::update::Release&, bool userInitiated);
    void offer (const ps::update::Release&);
    void download (const ps::update::Release&);
    void finished (juce::URL::DownloadTask*, bool success) override;
    void progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total) override;
    void downloadFinished (bool success);
    void showDownloadFailed();
    void cancelDownload();

    juce::Component::SafePointer<juce::Component> parent;
    std::unique_ptr<juce::PropertiesFile> settings;

    bool checking = false, available = false;

    ps::update::Release pending;
    juce::File target;
    std::unique_ptr<juce::URL::DownloadTask> task;
    std::unique_ptr<juce::AlertWindow> progressWindow;
    double progressValue = -1.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Updater)
    juce::WeakReference<Updater> self; // copied into background callbacks
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Updater)
};
