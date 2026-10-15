#include "PluginProcessor.h"

#include "../core/Modules.h"
#include "../state/PatchJson.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <cmath>

juce::AudioProcessor::BusesProperties PatternspaceProcessor::makeBuses()
{
    // Out 1 is the main stereo output. Outs 2-8 are optional: any a host leaves switched off
    // (and all of them in the standalone app) are mixed into Out 1.
    auto buses = BusesProperties().withOutput ("Out 1", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= ps::Sampler::numBuses; ++i)
        buses = buses.withOutput ("Out " + juce::String (i), juce::AudioChannelSet::stereo(), false);
    return buses;
}

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bpm", 1 }, "Tempo",
                                                             juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 124.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("BPM")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "run", 1 }, "Play", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "Host sync", true));
    return layout;
}
} // namespace

PatternspaceProcessor::PatternspaceProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "Patternspace", makeParameters())
{
    bpmParam = params.getRawParameterValue ("bpm");
    runParam = params.getRawParameterValue ("run");
    syncParam = params.getRawParameterValue ("sync");

    const auto& factory = ps::presets::factory();
    if (! factory.empty())
        loadPatch (factory.front().patch, factory.front().name, false);
    else
    {
        ps::Graph g;
        g.nodes.push_back (ps::makeNode ("clock", "clk", 14, 250));
        ps::normalise (g);
        loadPatch (g, "Empty", false);
    }
}

PatternspaceProcessor::~PatternspaceProcessor() = default;

void PatternspaceProcessor::prepareToPlay (double sampleRate, int)
{
    sampler.prepare (sampleRate);
    numPending = 0;
    wasPlaying = false;
    internalPpq = 0;
}

void PatternspaceProcessor::releaseResources() { sampler.stopAll(); }

bool PatternspaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty() && ! layouts.getMainInputChannelSet().isDisabled())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void PatternspaceProcessor::sendNoteOffs (juce::MidiBuffer& midi, int numSamples)
{
    int kept = 0;
    for (int i = 0; i < numPending; ++i)
    {
        auto p = pendingOffs[(size_t) i];
        if (p.samplesLeft < numSamples)
            midi.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), std::max (0, p.samplesLeft));
        else
        {
            p.samplesLeft -= numSamples;
            pendingOffs[(size_t) kept++] = p;
        }
    }
    numPending = kept;
}

void PatternspaceProcessor::allNotesOff (juce::MidiBuffer& midi, int offset)
{
    for (int i = 0; i < numPending; ++i)
        midi.addEvent (juce::MidiMessage::noteOff (pendingOffs[(size_t) i].channel, pendingOffs[(size_t) i].note), offset);
    numPending = 0;
    numPendingOns = 0;
}

void PatternspaceProcessor::scheduleNote (juce::MidiBuffer& midi, int channel, int note, int velocity, int offset, int length, int choke, int n)
{
    if (offset >= n)
    {
        if (numPendingOns < (int) pendingOns.size())
            pendingOns[(size_t) numPendingOns++] = { channel, note, velocity, offset - n, length, choke };
        return;
    }
    offset = std::max (0, offset);
    // A note that is still held ends just before it plays again, and so does the rest of its choke
    // group (an open hat when the closed one hits).
    for (int i = 0; i < numPending;)
    {
        const auto& p = pendingOffs[(size_t) i];
        const bool same = p.channel == channel && p.note == note;
        if (same || (choke != 0 && p.choke == choke))
        {
            midi.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), offset);
            pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPending];
        }
        else
            ++i;
    }
    midi.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) juce::jlimit (1, 127, velocity)), offset);
    length = std::max (1, length);
    if (offset + length < n)
        midi.addEvent (juce::MidiMessage::noteOff (channel, note), offset + length);
    else if (numPending < (int) pendingOffs.size())
        pendingOffs[(size_t) numPending++] = { channel, note, offset + length - n, choke };
    else // the queue is full: a short note beats a stuck one
        midi.addEvent (juce::MidiMessage::noteOff (channel, note), n - 1);
}

void PatternspaceProcessor::flushPendingOns (juce::MidiBuffer& midi, int n)
{
    const auto queued = pendingOns;
    const int count = numPendingOns;
    numPendingOns = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = queued[(size_t) i];
        scheduleNote (midi, p.channel, p.note, p.velocity, p.samplesLeft, p.length, p.choke, n);
    }
}

void PatternspaceProcessor::readSettings()
{
    auto setting = [this] (const char* key, int fallback) {
        const auto it = graph.settings.find (key);
        return it == graph.settings.end() ? fallback : it->second;
    };
    clockRate = juce::jlimit (0, 2, setting ("rate", 0));
    swingAmount = juce::jlimit (50, 75, setting ("swing", 50));
}

void PatternspaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    midi.clear();
    sendNoteOffs (midi, n);
    flushPendingOns (midi, n);

    // Follow the host's transport when it has one and sync is on; otherwise run our own clock.
    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    bool hostKnown = false, playing = false;
    double ppq = 0, bpm = bpmParam->load(), barStartPpq = 0;
    int num = 4, den = 4;
    if (syncParam->load() > 0.5f)
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition())
                if (auto p = pos->getPpqPosition())
                {
                    hostKnown = true;
                    playing = pos->getIsPlaying();
                    ppq = *p;
                    if (auto b = pos->getBpm())
                        bpm = *b;
                    if (auto sig = pos->getTimeSignature())
                    {
                        num = std::max (1, sig->numerator);
                        den = std::max (1, sig->denominator);
                    }
                    if (auto bar = pos->getPpqPositionOfLastBarStart())
                        barStartPpq = *bar;
                }
    meterNum = num;
    meterDen = den;
    // Sixteenths per bar, and the sixteenth bar 1 falls on: 3/4 is 12 steps, 7/8 is 14.
    const int stepsPerBar = std::max (1, (int) std::lround (num * 16.0 / den));
    const auto barStartStep = (std::int64_t) std::llround (barStartPpq * 4.0);
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double samplesPerBeat = sr * 60.0 / bpm;
    if (const int note = auditionNote.exchange (-1); note >= 0)
        scheduleNote (midi, juce::jlimit (1, 16, auditionChannel.load()), juce::jlimit (0, 127, note), 100, 0, (int) (samplesPerBeat / 8.0), 0, n);
    if (! hostKnown)
    {
        playing = runParam->load() > 0.5f;
        if (! playing)
            internalPpq = 0; // start again from bar 1
        ppq = internalPpq;
        if (playing)
            internalPpq += n / samplesPerBeat;
    }

    if (! playing)
    {
        if (wasPlaying)
            allNotesOff (midi, 0);
        wasPlaying = false;
    }
    else
    {
        const double start = ppq * 4.0, end = start + n / samplesPerBeat * 4.0;
        // The Clock's rate: a step per sixteenth, two per sixteenth, or one every other sixteenth.
        const int rate = clockRate.load();
        const int sub = rate == 1 ? 2 : 1, stride = rate == 2 ? 2 : 1;
        const double sixteenth = samplesPerBeat / 4.0, stepLen = sixteenth * stride / sub;
        // MPC-style swing: 50 is straight, 66 about a triplet, 75 the most; every second step
        // comes late by that share of a step.
        const double swing = juce::jlimit (0.0, 0.5, (swingAmount.load() - 50) / 50.0);
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine)
        {
            engine->setMeter (std::max (1, stepsPerBar * sub / stride), barStartStep * sub / stride);
            for (auto k = (std::int64_t) std::ceil (start - 1.0e-7); (double) k < end - 1.0e-7; ++k)
            {
                if (stride == 2 && ((k % 2) + 2) % 2 != 0)
                    continue;
                const double base = (k - start) / 4.0 * samplesPerBeat;
                for (int s = 0; s < sub; ++s)
                {
                    const std::int64_t e = (k - ((k % stride) + stride) % stride) / stride * sub + s;
                    if (resetRequested.exchange (false))
                        engine->resetState(); // and the locate below re-phases from the patch's follow point
                    if (! wasPlaying || engine->lastStep() != e - 1)
                        engine->locate (e); // started, looped or jumped: get the generators back in phase
                    wasPlaying = true;

                    stepOut.clear();
                    engine->step (e, stepOut);
                    const double at = base + s * stepLen + (((e % 2) + 2) % 2 != 0 ? swing * stepLen : 0.0);
                    for (auto& note : stepOut.notes)
                    {
                        const int repeats = std::max (1, note.repeats);
                        for (int r = 0; r < repeats; ++r)
                        {
                            const int offset = (int) std::lround (at + (note.offset + (double) r / repeats) * stepLen);
                            const int length = (int) std::lround ((repeats > 1 ? 0.5 / repeats : note.length) * stepLen);
                            scheduleNote (midi, note.channel, note.note, note.velocity, offset, length, note.choke, n);
                        }
                    }
                    for (auto& hit : stepOut.hits)
                    {
                        const int repeats = std::max (1, hit.repeats);
                        for (int r = 0; r < repeats; ++r)
                            sampler.trigger (hit, std::max (0, (int) std::lround (at + (hit.offset + (double) r / repeats) * stepLen)));
                    }
                }
            }
        }
        wasPlaying = true;
    }
    running = playing;
    followingHost = hostKnown;
    tempo = bpm;

    // Each audio out renders into its own stereo bus when the host has it switched on.
    const bool standalone = wrapperType == wrapperType_Standalone;
    auto main = getBusBuffer (buffer, false, 0);
    float* mainL = main.getNumChannels() > 0 ? main.getWritePointer (0) : nullptr;
    float* mainR = main.getNumChannels() > 1 ? main.getWritePointer (1) : mainL;
    std::array<float*, ps::Sampler::numBuses> left {}, right {};
    for (int b = 0; b < ps::Sampler::numBuses; ++b)
    {
        left[(size_t) b] = mainL;
        right[(size_t) b] = mainR;
        if (b == 0 || standalone || b >= getBusCount (false))
            continue;
        auto bus = getBusBuffer (buffer, false, b);
        if (bus.getNumChannels() >= 2)
        {
            left[(size_t) b] = bus.getWritePointer (0);
            right[(size_t) b] = bus.getWritePointer (1);
        }
    }
    sampler.render (left, right, n);
    for (int b = 0; b < ps::Sampler::numBuses; ++b)
    {
        const float peak = sampler.takePeak (b);
        if (peak > levels[(size_t) b].load())
            levels[(size_t) b] = peak;
    }
}

void PatternspaceProcessor::pushUndo (const juce::String& undoGroup)
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool merge = undoGroup.isNotEmpty() && undoGroup == lastUndoGroup && now - lastUndoTime < 2000 && ! undoStack.empty();
    lastUndoGroup = undoGroup;
    lastUndoTime = now;
    if (merge)
        return;
    undoStack.push_back (committed);
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PatternspaceProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back ({ graph, name });
    auto back = std::move (undoStack.back());
    undoStack.pop_back();
    graph = std::move (back.graph);
    name = back.name;
    lastUndoGroup.clear();
    committed = { graph, name };
    restoringHistory = true;
    commitPatch();
    sendSynchronousChangeMessage();
    restoringHistory = false;
}

void PatternspaceProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back ({ graph, name });
    auto next = std::move (redoStack.back());
    redoStack.pop_back();
    graph = std::move (next.graph);
    name = next.name;
    lastUndoGroup.clear();
    committed = { graph, name };
    restoringHistory = true;
    commitPatch();
    sendSynchronousChangeMessage();
    restoringHistory = false;
}

void PatternspaceProcessor::commitPatch (const juce::String& undoGroup)
{
    auto text = ps::json::toString (graph);
    {
        // Record the state before this edit, unless nothing changed (a drag that went nowhere).
        const juce::ScopedLock sl (stateLock);
        if (! restoringHistory && (text != savedPatch || name != savedName))
        {
            pushUndo (undoGroup);
            committed = { graph, name };
        }
    }
    readSettings();
    auto next = std::make_unique<ps::Engine>();
    next->load (graph);
    next->setViewPath (viewPath);
    const int version = ++editVersion;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine)
            next->adoptState (*engine);
        std::swap (engine, next);
        engineVersion = version;
    }
    const juce::ScopedLock sl (stateLock);
    savedPatch = std::move (text);
    savedName = name;
}

int PatternspaceProcessor::getNumPrograms() { return std::max (1, (int) ps::presets::factory().size()); }

int PatternspaceProcessor::getCurrentProgram()
{
    const auto& factory = ps::presets::factory();
    const juce::ScopedLock sl (stateLock);
    for (int i = 0; i < (int) factory.size(); ++i)
        if (factory[(size_t) i].name == savedName)
            return i;
    return 0;
}

void PatternspaceProcessor::setCurrentProgram (int index)
{
    const auto& factory = ps::presets::factory();
    if (index < 0 || index >= (int) factory.size())
        return;
    auto load = [safe = juce::WeakReference<PatternspaceProcessor> (this), index] {
        if (auto* self = safe.get())
            self->loadPatch (ps::presets::factory()[(size_t) index].patch, ps::presets::factory()[(size_t) index].name);
    };
    if (juce::MessageManager::existsAndIsCurrentThread())
        load();
    else
        juce::MessageManager::callAsync (load);
}

const juce::String PatternspaceProcessor::getProgramName (int index)
{
    const auto& factory = ps::presets::factory();
    return index >= 0 && index < (int) factory.size() ? factory[(size_t) index].name : juce::String();
}

void PatternspaceProcessor::setPatchName (const juce::String& newName)
{
    if (newName == name)
        return;
    name = newName;
    commitPatch();
    sendChangeMessage();
}

bool PatternspaceProcessor::isDirty() const
{
    const juce::ScopedLock sl (stateLock);
    return savedPatch != cleanPatch;
}

void PatternspaceProcessor::markClean()
{
    const juce::ScopedLock sl (stateLock);
    cleanPatch = savedPatch;
}

void PatternspaceProcessor::loadPatch (const ps::Graph& newPatch, const juce::String& newName, bool undoable)
{
    graph = newPatch;
    name = newName;
    file = juce::File();
    viewPath.clear(); // the editor goes back to the root of a new patch
    if (! undoable)
    {
        undoStack.clear();
        redoStack.clear();
        committed = { graph, name };
        restoringHistory = true;
        commitPatch();
        restoringHistory = false;
    }
    else
        commitPatch();
    markClean();
    sendChangeMessage();
}

void PatternspaceProcessor::setViewPath (const std::vector<std::string>& path)
{
    if (path == viewPath)
        return;
    viewPath = path;
    const juce::SpinLock::ScopedLockType lock (engineLock);
    if (engine)
        engine->setViewPath (viewPath);
}

bool PatternspaceProcessor::copyView (std::vector<ps::Engine::View>& out) const
{
    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || ! engine || engineVersion != editVersion)
        return false;
    out = engine->view();
    return true;
}

juce::String PatternspaceProcessor::getViewState() const
{
    const juce::ScopedLock sl (stateLock);
    return viewState;
}

void PatternspaceProcessor::setViewState (const juce::String& state)
{
    const juce::ScopedLock sl (stateLock);
    viewState = state;
}

float PatternspaceProcessor::busLevel (int bus) const
{
    return bus >= 1 && bus <= ps::Sampler::numBuses ? levels[(size_t) bus - 1].load() : 0.0f;
}

void PatternspaceProcessor::decayLevels()
{
    for (auto& l : levels)
        l = l.load() * 0.7f;
}

void PatternspaceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = params.copyState().createXml();
    if (! xml)
        return;
    {
        const juce::ScopedLock sl (stateLock);
        xml->setAttribute ("patch", savedPatch);
        xml->setAttribute ("patchName", savedName);
    }
    {
        const juce::ScopedLock sl (stateLock);
        xml->setAttribute ("view", viewState);
    }
    copyXmlToBinary (*xml, destData);
}

void PatternspaceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (! xml || ! xml->hasTagName (params.state.getType()))
        return;
    const auto patchText = xml->getStringAttribute ("patch");
    const auto patchName = xml->getStringAttribute ("patchName");
    setViewState (xml->getStringAttribute ("view"));
    xml->removeAttribute ("patch");
    xml->removeAttribute ("patchName");
    xml->removeAttribute ("view");
    params.replaceState (juce::ValueTree::fromXml (*xml));

    ps::Graph g;
    if (patchText.isEmpty() || ! ps::json::fromString (patchText, g))
        return;
    if (juce::MessageManager::existsAndIsCurrentThread())
        loadPatch (g, patchName, false);
    else
        juce::MessageManager::callAsync ([safe = juce::WeakReference<PatternspaceProcessor> (this), g, patchName] {
            if (auto* self = safe.get())
                self->loadPatch (g, patchName, false);
        });
}

juce::AudioProcessorEditor* PatternspaceProcessor::createEditor() { return new PatternspaceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PatternspaceProcessor(); }
