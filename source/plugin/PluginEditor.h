#pragma once

#include "PatchCanvas.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The plug-in window: transport and presets along the top, the module library on the left, the
// workspace tabs and the patch canvas filling the rest.
class PatternspaceEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener
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
    void refreshPresets();
    void refreshZoom();
    void openPatch();
    void savePatch();
    void importSubsystem();
    void exportSubsystem (const ps::Node&);

    PatternspaceProcessor& processor;
    juce::LookAndFeel_V4 look;

    PatchCanvas canvas;
    std::unique_ptr<Library> library;
    std::unique_ptr<TabStrip> tabs;

    juce::ComboBox presetBox;
    juce::TextButton openButton { "Open..." }, saveButton { "Save..." };
    juce::TextButton playButton { "Play" };
    juce::ToggleButton syncButton { "Host sync" };
    juce::Slider tempoSlider;
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, fitButton { "Fit" };
    juce::Label zoomLabel;

    juce::AudioProcessorValueTreeState::ButtonAttachment playAttachment, syncAttachment;
    juce::AudioProcessorValueTreeState::SliderAttachment tempoAttachment;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternspaceEditor)
};
