#pragma once

#include "../core/Engine.h"
#include "../core/Patch.h"
#include "../core/Sampler.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

// The plug-in: runs the patch in time with the host (or its own clock when there is no host
// transport, as in the standalone app), sends MIDI from MIDI out modules and plays Sample players
// into up to 8 stereo outputs.
class PatternspaceProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster
{
public:
    PatternspaceProcessor();
    ~PatternspaceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.7; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the patch, edited on the message thread
    ps::Graph& patch() { return graph; }
    const juce::String& patchName() const { return name; }
    // Call after every edit: recompiles the patch and swaps it in without stopping playback, and
    // records the state before the edit for undo. Consecutive commits with the same non-empty
    // `undoGroup` within a couple of seconds (a run of stepper clicks, a value drag) make one
    // undo step.
    void commitPatch (const juce::String& undoGroup = {});
    // Replaces the whole patch and tells listeners (the editor) to rebuild.
    void loadPatch (const ps::Graph& newPatch, const juce::String& newName, bool undoable = true);

    // ---- undo
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    // True while listeners are told about an undo or redo, so the editor keeps its view instead
    // of refitting as it does for a newly loaded patch.
    bool isRestoringHistory() const { return restoringHistory; }

    // The editor's view (zoom and pan per tab, window size), kept with the plug-in state so the
    // window reopens where it was. Not part of the patch file.
    juce::String getViewState() const;
    void setViewState (const juce::String& state);

    // ---- live state for drawing (message thread)
    // Which subsystem the editor has open, as ids from the root down; empty for the root patch.
    void setViewPath (const std::vector<std::string>& path);
    // Copies what each module of the open graph is doing. Returns false if the engine hasn't caught up with
    // the latest edit yet, or the audio thread holds it right now.
    bool copyView (std::vector<ps::Engine::View>& out) const;
    float busLevel (int bus) const; // audio out 1-8, peak since the last call to decayLevels
    void decayLevels();
    bool isRunning() const { return running.load(); }
    bool isFollowingHost() const { return followingHost.load(); } // the host's transport drives the clock
    double currentTempo() const { return tempo.load(); }
    int meterNumerator() const { return meterNum.load(); }
    int meterDenominator() const { return meterDen.load(); }
    // Puts every generator back to the start of the phrase on the next step, like a reset arriving
    // on every rst input at once.
    void requestReset() { resetRequested = true; }

    juce::AudioProcessorValueTreeState params;

private:
    static BusesProperties makeBuses();
    void sendNoteOffs (juce::MidiBuffer& midi, int numSamples);
    void allNotesOff (juce::MidiBuffer& midi, int offset);

    // message thread
    ps::Graph graph;
    juce::String name;
    int editVersion = 0;
    std::vector<std::string> viewPath;
    struct Snapshot
    {
        ps::Graph graph;
        juce::String name;
    };
    Snapshot committed; // the patch as last committed, compared against to record undo steps
    std::vector<Snapshot> undoStack, redoStack;
    juce::String lastUndoGroup;
    juce::uint32 lastUndoTime = 0;
    bool restoringHistory = false;
    void pushUndo (const juce::String& undoGroup);

    // shared with the audio thread
    mutable juce::SpinLock engineLock;
    std::unique_ptr<ps::Engine> engine;
    int engineVersion = 0;
    juce::CriticalSection stateLock; // the saved patch, which hosts may read from any thread
    juce::String savedPatch, savedName, viewState;

    // audio thread
    ps::Sampler sampler;
    ps::StepOutput stepOut;
    double internalPpq = 0;
    bool wasPlaying = false;
    struct PendingOff
    {
        int channel, note, samplesLeft;
    };
    std::array<PendingOff, 512> pendingOffs {};
    int numPending = 0;
    std::array<std::atomic<float>, ps::Sampler::numBuses> levels {};
    std::atomic<bool> running { false }, followingHost { false }, resetRequested { false };
    std::atomic<double> tempo { 124.0 };
    std::atomic<int> meterNum { 4 }, meterDen { 4 };

    std::atomic<float>* bpmParam = nullptr;
    std::atomic<float>* runParam = nullptr;
    std::atomic<float>* syncParam = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PatternspaceProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternspaceProcessor)
};
