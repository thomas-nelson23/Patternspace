#include "PluginProcessor.h"

#include "../core/Modules.h"
#include "../state/PatchJson.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <cmath>
#include <functional>

juce::AudioProcessor::BusesProperties PatternspaceProcessor::makeBuses()
{
    // Out 1 is the main stereo output. Outs 2-8 are optional: any a host leaves switched off
    // (and all of them in the standalone app) are mixed into Out 1. The MIDI effect has none.
    if (PATTERNSPACE_MIDI_EFFECT)
        return BusesProperties();
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
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "MIDI thru", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "master", 1 }, "Master",
                                                             juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "clip", 1 }, "Soft clip", true));
    for (int i = 1; i <= ps::Engine::numMacros; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "m" + juce::String (i), 1 }, "Macro " + juce::String (i),
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
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
    thruParam = params.getRawParameterValue ("thru");
    masterParam = params.getRawParameterValue ("master");
    clipParam = params.getRawParameterValue ("clip");
    formats.registerBasicFormats();
    for (auto& g : busGainL) g = 1.0f;
    for (auto& g : busGainR) g = 1.0f;
    for (int i = 0; i < ps::Engine::numMacros; ++i)
    {
        macroParams[(size_t) i] = params.getRawParameterValue ("m" + juce::String (i + 1));
        macroCcs[(size_t) i] = -1;
    }
    thruBuffer.ensureSize (4096);
    deviceBuffer.ensureSize (8192);

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

PatternspaceProcessor::~PatternspaceProcessor()
{
    setMidiOutput ({}); // stops the device thread
}

void PatternspaceProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::SpinLock::ScopedLockType lock (samplerLock);
    sampler.prepare (sampleRate);
    sampler.setUserSamples (std::vector<ps::Sampler::Sound> (userBank));
    numPending = 0;
    wasPlaying = false;
    internalPpq = 0;
}

void PatternspaceProcessor::releaseResources() { sampler.stopAll(); }

bool PatternspaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (PATTERNSPACE_MIDI_EFFECT)
        return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
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

juce::String PatternspaceProcessor::midiOutputId() const
{
    const juce::SpinLock::ScopedLockType lock (midiOutLock);
    return midiOutIdentifier;
}

void PatternspaceProcessor::setMidiOutput (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiOutput> opened;
    if (identifier.isNotEmpty())
    {
        opened = juce::MidiOutput::openDevice (identifier);
        if (opened != nullptr)
            opened->startBackgroundThread();
    }
    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType lock (midiOutLock);
        old = std::move (midiOut);
        midiOut = std::move (opened);
        midiOutIdentifier = midiOut != nullptr ? identifier : juce::String();
        devicePpq = -1;
    }
    if (old != nullptr)
        old->stopBackgroundThread();
}

void PatternspaceProcessor::readSettings()
{
    auto setting = [this] (const char* key, int fallback) {
        const auto it = graph.settings.find (key);
        return it == graph.settings.end() ? fallback : it->second;
    };
    clockRate = juce::jlimit (0, 2, setting ("rate", 0));
    swingAmount = juce::jlimit (50, 75, setting ("swing", 50));

    // Each audio out's level and pan, wherever it sits; the first module on a bus decides.
    std::array<bool, ps::Sampler::numBuses> seen {};
    std::function<void (const ps::Graph&)> walk = [&] (const ps::Graph& g) {
        for (auto& n : g.nodes)
        {
            if (n.kind == "audio")
            {
                const int b = juce::jlimit (1, ps::Sampler::numBuses, n.get ("bus", 1)) - 1;
                if (seen[(size_t) b])
                    continue;
                seen[(size_t) b] = true;
                const float level = juce::jlimit (0, 200, n.get ("lvl", 100)) / 100.0f;
                const float pan = juce::jlimit (-50, 50, n.get ("pan")) / 100.0f + 0.5f;
                busGainL[(size_t) b] = level * std::cos (pan * juce::MathConstants<float>::halfPi);
                busGainR[(size_t) b] = level * std::sin (pan * juce::MathConstants<float>::halfPi);
            }
            for (auto& inner : n.inner)
                walk (inner);
        }
    };
    walk (graph);
    for (size_t b = 0; b < seen.size(); ++b)
        if (! seen[b])
            busGainL[b] = busGainR[b] = 1.0f;
}

void PatternspaceProcessor::updateSamples()
{
    std::string signature;
    for (auto& s : graph.samples)
        signature += s.id + ":" + std::to_string (s.data.size()) + ":" + std::to_string ((int) s.rate) + ";";
    if (signature == samplesSignature)
        return;
    samplesSignature = signature;
    auto bank = ps::Sampler::bankFor (graph.samples);
    const juce::SpinLock::ScopedLockType lock (samplerLock);
    userBank = bank;
    sampler.setUserSamples (std::move (bank));
}

bool PatternspaceProcessor::loadSampleFile (const juce::File& f, ps::Sample& sample)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return false;
    const auto frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, frames);
    if (! reader->read (&buffer, 0, frames, 0, true, true))
        return false;
    sample.name = f.getFileNameWithoutExtension().toStdString();
    sample.path = f.getFullPathName().toStdString();
    sample.rate = reader->sampleRate;
    sample.missing = false;
    sample.data.assign ((size_t) frames, 0.0f);
    const float scale = 1.0f / (float) buffer.getNumChannels();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        for (int i = 0; i < frames; ++i)
            sample.data[(size_t) i] += in[i] * scale;
    }
    return true;
}

void PatternspaceProcessor::resolveSamples()
{
    for (auto& s : graph.samples)
    {
        if (! s.data.empty())
            continue;
        const juce::File f (s.path);
        s.missing = s.path.empty() || ! f.existsAsFile() || ! loadSampleFile (f, s);
    }
}

bool PatternspaceProcessor::clippedRecently() { return clipCount.exchange (0) > 0; }

void PatternspaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    const juce::SpinLock::ScopedLockType samplerHeld (samplerLock); // a sample load waits for the block

    // What came in: notes for MIDI in modules and the reset note, controllers for the macros.
    struct Arrived
    {
        int channel, note, velocity;
    };
    std::array<Arrived, 64> arrived {};
    int numArrived = 0;
    const bool thru = thruParam->load() > 0.5f;
    thruBuffer.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (thru)
            thruBuffer.addEvent (m, meta.samplePosition);
        if (m.isNoteOn())
        {
            if (learningReset.exchange (false))
                resetNoteNumber = m.getNoteNumber();
            if (m.getNoteNumber() == resetNoteNumber.load())
                resetRequested = true;
            else if (numArrived < (int) arrived.size())
                arrived[(size_t) numArrived++] = { m.getChannel(), m.getNoteNumber(), m.getVelocity() };
        }
        else if (m.isController())
        {
            if (const int learn = learningMacro.exchange (-1); learn >= 0 && learn < ps::Engine::numMacros)
                macroCcs[(size_t) learn] = m.getControllerNumber();
            for (int i = 0; i < ps::Engine::numMacros; ++i)
                if (macroCcs[(size_t) i].load() == m.getControllerNumber())
                    if (auto* p = params.getParameter ("m" + juce::String (i + 1)))
                        p->setValueNotifyingHost (m.getControllerValue() / 127.0f);
        }
    }
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
            for (int i = 0; i < ps::Engine::numMacros; ++i)
                engine->setMacro (i, macroParams[(size_t) i]->load());
            for (int i = 0; i < numArrived; ++i)
                engine->noteIn (arrived[(size_t) i].channel, arrived[(size_t) i].note, arrived[(size_t) i].velocity);
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
                    for (auto& ctl : stepOut.controls)
                        midi.addEvent (juce::MidiMessage::controllerEvent (ctl.channel, ctl.cc, ctl.value), juce::jlimit (0, n - 1, (int) std::lround (at)));
                }
            }
        }
        wasPlaying = true;
    }
    running = playing;
    followingHost = hostKnown;
    tempo = bpm;
    if (thru)
        midi.addEvents (thruBuffer, 0, n, 0);
    if (! midi.isEmpty())
        midiSent.fetch_add (midi.getNumEvents());

    // The standalone app's own MIDI output: what the MIDI outs made, plus MIDI clock while
    // playing, with start and stop as the transport changes.
    if (wrapperType == wrapperType_Standalone)
    {
        const juce::SpinLock::ScopedTryLockType tryLock (midiOutLock);
        if (tryLock.isLocked() && midiOut != nullptr)
        {
            deviceBuffer.clear();
            deviceBuffer.addEvents (midi, 0, n, 0);
            const bool startedNow = playing && devicePpq < 0;
            if (startedNow)
                deviceBuffer.addEvent (juce::MidiMessage::midiStart(), 0);
            if (! playing && devicePpq >= 0)
                deviceBuffer.addEvent (juce::MidiMessage::midiStop(), 0);
            if (playing)
            {
                // 24 ticks a beat, from the position of this block
                const double from = ppq, to = ppq + n / samplesPerBeat;
                for (auto t = (std::int64_t) std::ceil (from * 24.0 - 1.0e-7); t / 24.0 < to - 1.0e-7; ++t)
                    deviceBuffer.addEvent (juce::MidiMessage::midiClock(), juce::jlimit (0, n - 1, (int) std::lround ((t / 24.0 - from) * samplesPerBeat)));
                devicePpq = to;
            }
            else
                devicePpq = -1;
            if (! deviceBuffer.isEmpty())
                midiOut->sendBlockOfMessages (deviceBuffer, juce::Time::getMillisecondCounterHiRes(), sr);
        }
    }

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
    std::array<float, ps::Sampler::numBuses> gainL {}, gainR {};
    for (size_t b = 0; b < gainL.size(); ++b)
    {
        gainL[b] = busGainL[b].load();
        gainR[b] = busGainR[b].load();
    }
    sampler.render (left, right, n, gainL, gainR);
    for (int b = 0; b < ps::Sampler::numBuses; ++b)
    {
        const float peak = sampler.takePeak (b);
        if (peak > levels[(size_t) b].load())
            levels[(size_t) b] = peak;
    }

    // The master trim and the soft clipper, on every output that is on. The clipper bends what
    // goes past 0 dB back under it (tanh), so 64 voices into one out can't hard-clip.
    const float master = juce::Decibels::decibelsToGain (masterParam ? masterParam->load() : 0.0f);
    const bool clip = clipParam == nullptr || clipParam->load() > 0.5f;
    float peak = 0;
    bool over = false;
    for (int b = 0; b < (standalone ? 1 : getBusCount (false)); ++b)
    {
        auto bus = getBusBuffer (buffer, false, b);
        for (int ch = 0; ch < bus.getNumChannels(); ++ch)
        {
            float* d = bus.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float s = d[i] * master;
                if (std::abs (s) > 1.0f)
                    over = true;
                if (clip)
                    s = std::tanh (s);
                d[i] = s;
                if (b == 0)
                    peak = std::max (peak, std::abs (s));
            }
        }
    }
    if (peak > masterPeak.load())
        masterPeak = peak;
    if (over)
        ++clipCount;
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
    updateSamples();
    auto next = std::make_unique<ps::Engine>();
    next->load (graph);
    next->setViewPath (viewPath);
    const int version = ++editVersion;
    {
        // The audio thread waits on this lock only while the new engine copies the old one's
        // positions: a few integers per module, so microseconds even for a large patch. The old
        // engine is freed below, on this thread, never on the audio thread.
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
    resolveSamples();
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
    masterPeak = masterPeak.load() * 0.7f;
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
    juce::StringArray ccs;
    for (int i = 0; i < ps::Engine::numMacros; ++i)
        ccs.add (juce::String (macroCcs[(size_t) i].load()));
    xml->setAttribute ("macroCcs", ccs.joinIntoString (","));
    xml->setAttribute ("resetNote", resetNoteNumber.load());
    xml->setAttribute ("midiOut", midiOutputId());
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
    {
        const auto ccs = juce::StringArray::fromTokens (xml->getStringAttribute ("macroCcs"), ",", "");
        for (int i = 0; i < ps::Engine::numMacros; ++i)
            macroCcs[(size_t) i] = i < ccs.size() ? juce::jlimit (-1, 127, ccs[i].getIntValue()) : -1;
        resetNoteNumber = juce::jlimit (-1, 127, xml->getIntAttribute ("resetNote", -1));
        if (wrapperType == wrapperType_Standalone)
            setMidiOutput (xml->getStringAttribute ("midiOut"));
    }

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
