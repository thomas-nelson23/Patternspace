#pragma once

#include "Patch.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Runs a patch one sixteenth-note step at a time. It follows the prototype's evaluation exactly:
// modules run in dependency order, an input reads 0 from a module that hasn't run yet this step
// (so feedback loops are one step late rather than infinite), and gate values are 0 (rest),
// 1 (hit) or 2 (accent).
//
// Threading: load() allocates and belongs on the message thread. step() and locate() don't
// allocate and are safe on the audio thread.
namespace ps
{
struct StepOutput
{
    struct NoteOn
    {
        int channel; // 1-16
        int note;
        int velocity;
    };
    struct Hit
    {
        int sample; // index into sampleNames()
        int bus;    // audio out 1-8
        bool accent;
        int pitch;  // semitones
        int decay;  // 1-10
    };
    std::vector<NoteOn> notes;
    std::vector<Hit> hits;

    StepOutput();
    void clear()
    {
        notes.clear();
        hits.clear();
    }
};

class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // Compiles a patch. Generator positions start fresh; call adoptState() to carry them over
    // from the engine this one replaces.
    void load (const Graph& patch);
    void adoptState (const Engine& previous);

    // Evaluates step `index`, counted in sixteenths from the start of bar 1, appending what it plays.
    void step (std::int64_t index, StepOutput& out);

    // Jumps to `index` as if the patch had been playing since the last two-bar boundary, so the
    // generators are in phase. Nothing is output for the replayed steps.
    void locate (std::int64_t index);
    void resetState();

    std::int64_t lastStep() const;
    void setRandomSeed (std::uint32_t seed); // the stream the Probability modules draw from

    // What each root-level module is doing after the last step, in patch order, for drawing.
    struct View
    {
        int pos = -1;   // loop position, -1 before the first clock
        int fired = 0;  // reset arrived this step
        int sel = -1;   // Switch position
        int row = 0;    // Cellular row
        int loop = 0;   // Drum map loop count
        std::array<int, 16> out {}, in {};
    };
    const std::vector<View>& view() const;

    // For tests: a module's values after the last step, as the prototype records them. `path` is
    // a module id, with "/" reaching into subsystems ("sn/pr").
    std::string valuesAt (const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ps
