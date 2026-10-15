#pragma once

#include "Engine.h"
#include "Patch.h"

#include <array>
#include <vector>

// The Sample player's sounds and voices. The six built-in drum sounds are synthesized at the host's
// sample rate (the same recipes as the prototype), so the plugin ships without sample files; the
// patch's own sounds (Graph::samples) follow them in the bank, at whatever rate they were recorded.
namespace ps
{
class Sampler
{
public:
    static constexpr int numBuses = 8;
    static constexpr int numVoices = 64;
    static constexpr int numTails = 16; // voices fading out after a cut-off or steal

    // Builds the built-in sounds. Allocates, so call it from prepareToPlay, not the audio callback.
    void prepare (double sampleRate);

    struct Sound
    {
        std::vector<float> data;
        double rate = 44100;
    };
    // The patch's own sounds, built on the message thread (allocates), then swapped in while the
    // audio thread is kept out. Playing voices stop, since the bank moves.
    static std::vector<Sound> bankFor (const std::vector<Sample>& samples);
    void setUserSamples (std::vector<Sound>&& sounds);

    // Starts a hit `offset` samples into the next render() call.
    void trigger (const StepOutput::Hit& hit, int offset);

    // Adds every playing voice into its bus. `left` and `right` hold one channel pointer per audio
    // out (1-8); several outs may share pointers to mix them together. `gainL` and `gainR` scale
    // each bus (an Audio out's level and pan), live.
    void render (const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples,
                 const std::array<float, numBuses>& gainL = ones(), const std::array<float, numBuses>& gainR = ones());
    static const std::array<float, numBuses>& ones();

    void stopAll();
    int activeVoices() const;

    // The loudest sample added to an audio out (0-7) since the last call, for meters.
    float takePeak (int bus);

    const std::vector<float>& sound (int index) const { return bank[(size_t) index].data; }
    int numSounds() const { return (int) bank.size(); }
    double sampleRate() const { return rate; }

private:
    struct Voice
    {
        const Sound* sound = nullptr;
        double pos = 0, step = 1;
        float env = 0, decay = 1;   // the exponential fall
        float gainL = 0, gainR = 0; // level and pan
        float lp = 0, lpCoeff = 1;  // one-pole low-pass state and coefficient (1 bypasses)
        float fade = 1, fadeStep = 0; // a cut-off voice ramps down over a few milliseconds
        int bus = 0, delay = 0, remaining = 0;
        int choke = 0, owner = 0;
        bool active = false;
    };

    void cutOff (Voice&); // moves it into a tail that fades out
    void renderVoice (Voice&, const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples,
                      const std::array<float, numBuses>& gainL, const std::array<float, numBuses>& gainR);

    std::vector<Sound> bank;
    size_t numBuiltIn = 0;
    std::array<Voice, numVoices> voices {};
    std::array<Voice, numTails> tails {};
    std::array<float, numBuses> peaks {};
    double rate = 44100;
};
} // namespace ps
