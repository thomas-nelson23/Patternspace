#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Pattern maths shared by the generators. Each function matches the prototype bit for bit, so a
// patch plays the same notes in the plugin as it does in the browser.
namespace ps
{
// mulberry32, the prototype's seeded random number generator.
class Rng
{
public:
    explicit Rng (std::uint32_t seed = 1) : state (seed) {}
    double next(); // in [0, 1)

private:
    std::uint32_t state;
};

// Euclidean rhythm: hits spread as evenly as possible over len steps, rotated by rot.
std::vector<int> euclid (int hits, int len, int rot);
// The same, written into a pattern that already has the length (no allocation, for the audio thread).
void euclidInto (std::vector<int>& pattern, int hits, int rot);
void randomPatternInto (std::vector<int>& pattern, int density, int seed);

// A seeded random loop. Density is 0 to 10 (tenths); a non-zero density always has a hit.
std::vector<int> randomPattern (int len, int density, int seed);

// Rows of the rule 30 cellular automaton, 16 cells wide. The Cellular generator plays one row
// per loop.
const std::vector<std::array<int, 16>>& cellularRows();

// Drum map (modelled on Mutable Instruments Grids, with our own pattern data). x and y are 0 to 100,
// the fills and chaos 0 to 10. Returns kick, snare and hat over 32 steps: 0 rest, 1 hit, 2 accent.
// Chaos varies the levels, re-rolled for every loop.
using DrumMapPattern = std::array<std::array<int, 32>, 3>;
DrumMapPattern drumMapPattern (int x, int y, int kickFill, int snareFill, int hatFill, int chaos, int loop);

// The interpolated 0-255 level of each step before fills and chaos, for drawing the map.
std::array<std::array<double, 32>, 3> drumMapLevels (int x, int y);
} // namespace ps
