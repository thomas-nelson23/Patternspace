#pragma once

#include "Patch.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Runs a patch one step at a time (a sixteenth unless the Clock's rate says otherwise). It follows
// the prototype's evaluation exactly: modules run in dependency order, an input reads 0 from a
// module that hasn't run yet this step (so feedback loops are one step late rather than infinite).
//
// A gate carries its velocity: 0 is a rest, a plain hit is 96 and an accent 127, and modules such
// as Velocity set any value between. Bits above the velocity carry a ratchet count, so a hit can
// repeat within its step on the way to a MIDI out or Sample player. levelOf() folds a value back
// to the prototype's 0, 1 or 2 for drawing and for the parity test.
//
// Threading: load() allocates and belongs on the message thread. step() and locate() don't
// allocate and are safe on the audio thread.
namespace ps
{
constexpr int kHit = 96, kAccent = 127;
inline int velocityOf (int v) { return v & 0x7f; }
inline bool isAccent (int v) { return velocityOf (v) > 112; }
inline int levelOf (int v) { return velocityOf (v) == 0 ? 0 : isAccent (v) ? 2 : 1; }
inline int ratchetsOf (int v) { return velocityOf (v) == 0 ? 1 : ((v >> 8) & 7) + 1; }
inline int withVelocity (int v, int velocity) { return velocity <= 0 ? 0 : (v & ~0x7f) | (velocity > 127 ? 127 : velocity); }
inline int withRatchets (int v, int n) { return velocityOf (v) == 0 ? 0 : (v & 0xff) | (((n < 1 ? 1 : n > 8 ? 8 : n) - 1) << 8); }

struct StepOutput
{
    struct NoteOn
    {
        int channel; // 1-16
        int note;
        int velocity;
        double length = 0.5; // in steps
        int repeats = 1;     // ratchets within the step
        int choke = 0;       // notes with the same non-zero group cut each other off
        double offset = 0;   // in steps, the module's nudge
    };
    struct Hit
    {
        int sample; // index into sampleNames()
        int bus;    // audio out 1-8
        bool accent;
        int pitch;  // semitones
        int decay;  // 1-10
        int velocity = kHit;
        int repeats = 1;
        double offset = 0; // in steps
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

    // Jumps to `index` so the generators are in phase, replaying silently from where the patch's
    // follow setting says: the last two-bar phrase (the default), the start of the song, or, when
    // free-running, nowhere (the generators keep their state across the jump).
    void locate (std::int64_t index);
    void resetState();

    // How the patch lines up with the song position when playback starts or jumps.
    enum class Follow
    {
        phrase, // replay from the last two-bar boundary
        song,   // replay from bar 1, so every step is a pure function of the song position
        free    // never reset: the generators run on from wherever they were
    };
    Follow follow() const;
    static Follow followFromSetting (int value);

    // The host's meter: sixteenths per bar and the step bar 1 starts on. The Clock's bar and
    // 2-bar outputs follow it. Safe on the audio thread.
    void setMeter (int stepsPerBar, std::int64_t barStartStep);

    std::int64_t lastStep() const;

    // Probability modules decide from the step index, the module and the patch's seed, so a song
    // position always makes the same choice (and a bounce matches playback). The prototype's
    // single running stream is kept for the parity test.
    enum class ProbabilityMode
    {
        position,
        stream
    };
    void setProbabilityMode (ProbabilityMode mode);
    void setRandomSeed (std::uint32_t seed); // seeds both modes

    // What each module of the graph being shown is doing after the last step, in patch order, for
    // drawing. That graph is the root patch unless setViewPath() has opened a subsystem.
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

    // Shows the modules inside a subsystem: ids from the root down, {"sn"} for the subsystem sn,
    // {"sn", "in2"} for one nested in it. An empty path, or one that no longer leads to a
    // subsystem, shows the root patch. Allocates, so call it on the message thread.
    void setViewPath (const std::vector<std::string>& path);

    // For tests: a module's values after the last step, as the prototype records them. `path` is
    // a module id, with "/" reaching into subsystems ("sn/pr").
    std::string valuesAt (const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ps
