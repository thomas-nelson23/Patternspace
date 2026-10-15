#pragma once

#include "PatchCanvas.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Updater.h"
#include "UserLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The plug-in window: transport and presets along the top, the module library on the left, the
// workspace tabs and the patch canvas filling the rest.
class PatternspaceEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit PatternspaceEditor (PatternspaceProcessor&);
    ~PatternspaceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Library;
    class TabStrip;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshPresets();
    void refreshZoom();
    void showPatchMenu();
    void newPatch();
    void openPatch();
    void openPatchFile (const juce::File&);
    void loadPreset (const ps::json::Preset&);
    void savePatch();   // to the file it came from, else Save as
    void savePatchAs(); // a file dialog
    void savePreset();  // into the user preset folder, by name
    void renamePatch();
    // Runs `go` now, or after asking what to do with unsaved changes.
    void confirmDiscard (const juce::String& action, std::function<void()> go);
    void askForText (const juce::String& title, const juce::String& prompt, const juce::String& initial, std::function<void (const juce::String&)> done);
    void importSubsystem();
    void exportSubsystem (const ps::Node&);       // into the user's library
    void exportSubsystemToFile (const ps::Node&); // anywhere, for sharing
    std::function<void()> pendingAfterSave;
    void showUpdatesMenu();
    void refreshUpdateButton();
    void refreshTransport(); // dims the internal clock's controls while the host drives it
    void saveViewState();

    PatternspaceProcessor& processor;
    ps::theme::Look look;

    PatchCanvas canvas;
    std::unique_ptr<Library> library;
    std::unique_ptr<TabStrip> tabs;

    juce::TextButton patchButton; // the patch name; opens the patch menu
    juce::TextButton openButton { "Open..." }, saveButton { "Save..." };
    juce::TextButton playButton { "Play" }, resetButton { "Reset" };
    juce::ToggleButton syncButton { "Host sync" };
    juce::TooltipWindow tooltips { nullptr, 500 }; // without one, setTooltip text never shows
    juce::Slider tempoSlider;
    juce::TextButton zoomOut { juce::String::fromUTF8 ("\xE2\x88\x92") }, zoomIn { "+" }, fitButton { "Fit" };
    juce::TextButton collapseButton { "Collapse" }, snapButton { "Snap" }, helpButton { "?" };
    juce::Label zoomLabel;

    juce::AudioProcessorValueTreeState::ButtonAttachment playAttachment, syncAttachment;
    juce::AudioProcessorValueTreeState::SliderAttachment tempoAttachment;

    std::unique_ptr<juce::FileChooser> chooser;

    // Standalone only: plug-ins are updated by reinstalling them, not from inside a host.
    std::unique_ptr<Updater> updater;
    juce::TextButton updatesButton { "Updates" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternspaceEditor)
};
