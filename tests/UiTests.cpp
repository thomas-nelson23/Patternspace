// Smoke tests for the window. Each factory patch is loaded into the processor, shown in the
// editor, painted on every tab and inside every subsystem, and put through the canvas's edits
// (select all, copy and paste, duplicate, group, collapse, every module kind added, undo). Then
// the processor runs a few blocks of audio and MIDI. Nothing is compared pixel for pixel: the
// test is that none of it crashes, trips an assertion, or leaves the patch different from what
// undo says it should be.

#include "../source/core/Modules.h"
#include "../source/plugin/PluginEditor.h"
#include "../source/plugin/PluginProcessor.h"
#include "../source/plugin/Presets.h"
#include "../source/state/PatchJson.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
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

juce::Image snapshot (juce::Component& c)
{
    return c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
}

bool mostlyPainted (const juce::Image& image)
{
    // The editor is never one flat colour: text, panels, cables and the dot grid give a sample
    // grid dozens of colours. A blank or half-drawn window gives a handful.
    if (! image.isValid())
        return false;
    std::set<juce::uint32> colours;
    for (int y = 4; y < image.getHeight(); y += image.getHeight() / 40)
        for (int x = 4; x < image.getWidth(); x += image.getWidth() / 40)
            colours.insert (image.getPixelAt (x, y).getARGB());
    return colours.size() >= 30;
}

void exerciseEditor (const ps::json::Preset& preset)
{
    PatternspaceProcessor processor;
    processor.loadPatch (preset.patch, preset.name, false);
    const auto original = ps::json::toString (processor.patch());

    std::unique_ptr<juce::AudioProcessorEditor> editorBase (processor.createEditor());
    auto* editor = dynamic_cast<PatternspaceEditor*> (editorBase.get());
    check (editor != nullptr, preset.name + ": the processor makes its editor");
    if (editor == nullptr)
        return;
    editor->setSize (1280, 800);
    auto& canvas = editor->patchCanvas();

    check (mostlyPainted (snapshot (*editor)), preset.name + ": the window paints");
    for (auto& page : processor.patch().pages)
    {
        canvas.setPage (page.id);
        canvas.fitView();
        check (mostlyPainted (snapshot (*editor)), preset.name + ": tab " + page.name + " paints");
    }
    canvas.setPage (processor.patch().pages.front().id);

    // Inside every subsystem, at every level.
    std::function<void (const ps::Graph&, int)> visit = [&] (const ps::Graph& g, int depth) {
        for (auto& n : g.nodes)
            if (n.kind == "sub")
            {
                canvas.openSubsystem (n.id);
                check (canvas.insideSubsystem(), preset.name + ": opened " + n.name);
                canvas.fitView();
                check (mostlyPainted (snapshot (*editor)), preset.name + ": inside " + n.name + " paints");
                if (! n.inner.empty())
                    visit (n.inner.front(), depth + 1);
                canvas.closeSubsystem ((size_t) depth);
            }
    };
    visit (processor.patch(), 0);
    check (! canvas.insideSubsystem(), preset.name + ": back at the root");

    // Every edit, then undone, leaves the patch as it was.
    const auto nodesBefore = processor.patch().nodes.size();
    canvas.selectAll();
    canvas.copySelection();
    canvas.pasteClipboard();
    check (processor.patch().nodes.size() > nodesBefore, preset.name + ": paste adds modules");
    snapshot (*editor);
    processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after paste restores the patch");

    canvas.selectAll();
    canvas.duplicateSelection();
    check (processor.patch().nodes.size() > nodesBefore, preset.name + ": duplicate adds modules");
    processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after duplicate restores the patch");

    canvas.selectAll();
    canvas.nudgeSelection (10, 10);
    processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after a nudge restores the patch");

    canvas.collapseAll (true);
    snapshot (*editor);
    canvas.collapseAll (false);
    snapshot (*editor);
    while (processor.canUndo())
        processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after collapsing restores the patch");

    canvas.selectAll();
    canvas.groupSelection();
    snapshot (*editor);
    processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after grouping restores the patch");

    canvas.addBlock ({ 0, 0 });
    canvas.addNote ({ 50, 50 });
    snapshot (*editor);
    processor.undo();
    processor.undo();
    check (ps::json::toString (processor.patch()) == original, preset.name + ": undo after a block and a note restores the patch");

    // The processor runs: a few blocks of audio with the internal clock, every out on.
    processor.setPlayHead (nullptr);
    processor.prepareToPlay (44100, 256);
    juce::AudioBuffer<float> audio (processor.getTotalNumOutputChannels(), 256);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int block = 0; block < 64; ++block)
    {
        midi.clear();
        processor.processBlock (audio, midi);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                if (! std::isfinite (audio.getSample (ch, i)))
                    finite = false;
    }
    check (finite, preset.name + ": 64 blocks of audio are finite");
    processor.releaseResources();
    editorBase.reset();
}

// Parameters set from the audio thread (as a host's automation does) survive a save and load into a
// fresh instance, with no message loop running in between, as clap-validator checks.
void stateRoundTrip()
{
    PatternspaceProcessor a;
    a.prepareToPlay (44100, 64);
    juce::AudioBuffer<float> audio (a.getTotalNumOutputChannels(), 64);
    juce::MidiBuffer midi;
    std::map<juce::String, float> wanted;
    int i = 0;
    for (auto* p : a.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float v = (float) ((i++ * 37) % 100) / 100.0f;
            p->setValueNotifyingHost (v);
            // The state holds the value on the parameter's own scale, so a switch or a stepped range snaps.
            wanted[withId->paramID] = withId->convertTo0to1 (withId->convertFrom0to1 (v));
        }
    a.processBlock (audio, midi);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    PatternspaceProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    int wrong = 0;
    for (auto* p : b.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (std::abs (p->getValue() - wanted[withId->paramID]) > 0.002f)
            {
                if (wrong++ < 3)
                    std::cerr << "  " << withId->paramID << ": " << p->getValue() << " after load, set " << wanted[withId->paramID] << "\n";
            }
    check (wrong == 0, "every parameter survives a state save and load");
    juce::MemoryBlock again;
    b.getStateInformation (again);
    check (again == state, "saving the reloaded state gives the same bytes");
}

void exerciseEveryModule()
{
    PatternspaceProcessor processor;
    ps::Graph g;
    g.nodes.push_back (ps::makeNode ("clock", "clk", 14, 250));
    ps::normalise (g);
    processor.loadPatch (g, "Every module", false);
    std::unique_ptr<juce::AudioProcessorEditor> editorBase (processor.createEditor());
    auto* editor = dynamic_cast<PatternspaceEditor*> (editorBase.get());
    if (editor == nullptr)
        return;
    editor->setSize (1280, 800);
    auto& canvas = editor->patchCanvas();
    int kinds = 0;
    for (auto& section : ps::librarySections())
        for (auto& kind : section.kinds)
        {
            canvas.addModule (kind);
            ++kinds;
        }
    check (processor.patch().nodes.size() == (size_t) kinds + 1, "every kind of module can be added");
    canvas.fitView();
    check (mostlyPainted (snapshot (*editor)), "a patch with every module paints");
    canvas.collapseAll (true);
    check (mostlyPainted (snapshot (*editor)), "and paints collapsed");
    canvas.selectAll();
    canvas.groupSelection();
    check (mostlyPainted (snapshot (*editor)), "and paints grouped into a subsystem");
    processor.setPlayHead (nullptr);
    processor.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> audio (processor.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 16; ++block)
    {
        midi.clear();
        processor.processBlock (audio, midi);
    }
    processor.releaseResources();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (auto& preset : ps::presets::factory())
        exerciseEditor (preset);
    exerciseEveryModule();
    stateRoundTrip();
    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
