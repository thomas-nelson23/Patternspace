#pragma once

#include "Engine.h"

#include <array>
#include <vector>

// The Sample player's sounds and voices. The six drum sounds are synthesized at the host's sample
// rate (the same recipes as the prototype), so the plugin ships without sample files.
namespace ps
{
class Sampler
{
public:
    static constexpr int numBuses = 8;
    static constexpr int numVoices = 64;

    // Builds the sounds. Allocates, so call it from prepareToPlay, not the audio callback.
    void prepare (double sampleRate);

    // Starts a hit `offset` samples into the next render() call.
    void trigger (const StepOutput::Hit& hit, int offset);

    // Adds every playing voice into its bus. `left` and `right` hold one channel pointer per audio
    // out (1-8); several outs may share pointers to mix them together.
    void render (const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples);

    void stopAll();
    int activeVoices() const;

    // The loudest sample added to an audio out (0-7) since the last call, for meters.
    float takePeak (int bus);

    const std::vector<float>& sound (int index) const { return bank[(size_t) index]; }
    double sampleRate() const { return rate; }

private:
    struct Voice
    {
        const std::vector<float>* data = nullptr;
        double pos = 0, step = 1;
        float env = 0, decay = 1;
        int bus = 0, delay = 0, remaining = 0;
        bool active = false;
    };

    std::vector<std::vector<float>> bank;
    std::array<Voice, numVoices> voices {};
    std::array<float, numBuses> peaks {};
    double rate = 44100;
};
} // namespace ps
