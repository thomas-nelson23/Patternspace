#include "Patterns.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ps
{
double Rng::next()
{
    state += 0x6D2B79F5u;
    std::uint32_t t = (state ^ (state >> 15)) * (1u | state);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}

std::vector<int> euclid (int k, int n, int r)
{
    if (n <= 0)
        return {};
    k = std::max (0, std::min (k, n));
    std::vector<int> a ((size_t) n), out ((size_t) n);
    for (int i = 0; i < n; ++i)
        a[(size_t) i] = ((i * k) % n) < k ? 1 : 0;
    for (int i = 0; i < n; ++i)
        out[(size_t) i] = a[(size_t) (((i - r) % n + n) % n)];
    return out;
}

void euclidInto (std::vector<int>& out, int k, int r)
{
    const int n = (int) out.size();
    if (n <= 0)
        return;
    k = std::max (0, std::min (k, n));
    for (int i = 0; i < n; ++i)
    {
        const int j = ((i - r) % n + n) % n;
        out[(size_t) i] = ((j * k) % n) < k ? 1 : 0;
    }
}

void randomPatternInto (std::vector<int>& p, int density, int seed)
{
    Rng r ((std::uint32_t) (seed != 0 ? seed : 90));
    bool any = false;
    for (auto& v : p)
    {
        v = r.next() < density / 10.0 ? 1 : 0;
        any = any || v;
    }
    if (density > 0 && ! any && ! p.empty())
        p[0] = 1;
}

std::vector<int> randomPattern (int len, int density, int seed)
{
    len = std::max (1, len);
    Rng r ((std::uint32_t) (seed != 0 ? seed : 90));
    std::vector<int> p ((size_t) len);
    bool any = false;
    for (auto& v : p)
    {
        v = r.next() < density / 10.0 ? 1 : 0;
        any = any || v;
    }
    if (density > 0 && ! any)
        p[0] = 1;
    return p;
}

const std::vector<std::array<int, 16>>& cellularRows()
{
    static const std::vector<std::array<int, 16>> rows = [] {
        const int rule[] = { 0, 1, 1, 1, 1, 0, 0, 0 };
        std::array<int, 16> row {};
        row[8] = 1;
        std::vector<std::array<int, 16>> all;
        for (int y = 0; y < 40; ++y)
        {
            all.push_back (row);
            std::array<int, 16> next {};
            for (int i = 0; i < 16; ++i)
                next[(size_t) i] = rule[row[(size_t) ((i + 15) % 16)] * 4 + row[(size_t) i] * 2 + row[(size_t) ((i + 1) % 16)]];
            row = next;
        }
        return std::vector<std::array<int, 16>> (all.begin() + 6, all.end());
    }();
    return rows;
}

namespace
{
    // Five grooves running straight (top) to broken (bottom). Each is kick, snare and hat over
    // 32 steps, one digit per step, 0 to 9.
    const char* const kGrooves[5][3] = {
        { "90005000900050009000500090005020", "00009000000090000000900000009003", "21712171217121722171217121712172" },
        { "90000005009000009000000050090000", "00009000000090000000900000009060", "50305030503050305030503050307050" },
        { "90700000009500009070000000900050", "00009003000090030300900300009030", "70707070707070707070707070707570" },
        { "90000000009000009000000000700050", "00009002000090000020900000009040", "63636363636363636363636363636373" },
        { "90000000800000009000005000700000", "00000000900000000000000090000040", "84848484848484848484848488888484" },
    };

    // The 5x5 map: columns run sparse (left) to busy (right) by filling in the quieter steps of
    // each groove.
    using MapCell = std::array<std::array<double, 32>, 3>;
    const std::array<std::array<MapCell, 5>, 5>& drumMapCells()
    {
        static const auto map = [] {
            std::array<std::array<MapCell, 5>, 5> m {};
            Rng r (4242);
            for (int i = 0; i < 5; ++i)
                for (int j = 0; j < 5; ++j)
                    for (int k = 0; k < 3; ++k)
                        for (int s = 0; s < 32; ++s)
                        {
                            const double level = (kGrooves[j][k][s] - '0') * 28;
                            double v = level;
                            if (level < 150)
                                v = std::round (std::max (0.0, std::min (200.0, level * (0.5 + 0.25 * i) + r.next() * i * 22)));
                            m[(size_t) i][(size_t) j][(size_t) k][(size_t) s] = v;
                        }
            return m;
        }();
        return map;
    }
} // namespace

std::array<std::array<double, 32>, 3> drumMapLevels (int x, int y)
{
    const auto& m = drumMapCells();
    const double gx = (x / 100.0) * 4, gy = (y / 100.0) * 4;
    const int i = std::min (3, (int) std::floor (gx)), j = std::min (3, (int) std::floor (gy));
    const double fx = gx - i, fy = gy - j;
    std::array<std::array<double, 32>, 3> out {};
    for (size_t k = 0; k < 3; ++k)
        for (size_t s = 0; s < 32; ++s)
            out[k][s] = (m[(size_t) i][(size_t) j][k][s] * (1 - fx) + m[(size_t) i + 1][(size_t) j][k][s] * fx) * (1 - fy)
                        + (m[(size_t) i][(size_t) j + 1][k][s] * (1 - fx) + m[(size_t) i + 1][(size_t) j + 1][k][s] * fx) * fy;
    return out;
}

DrumMapPattern drumMapPattern (int x, int y, int kickFill, int snareFill, int hatFill, int chaos, int loop)
{
    const auto levels = drumMapLevels (x, y);
    const int fills[] = { kickFill, snareFill, hatFill };
    Rng r ((std::uint32_t) ((chaos ? loop : 0) * 7919 + 13));
    DrumMapPattern out {};
    for (size_t k = 0; k < 3; ++k)
        for (size_t s = 0; s < 32; ++s)
        {
            const double v = levels[k][s] + (chaos ? (r.next() - 0.5) * chaos * 25.6 : 0.0);
            const int d = fills[k];
            out[k][s] = d > 0 && v > 255 - d * 25.5 ? (v > 200 ? 2 : 1) : 0;
        }
    return out;
}
} // namespace ps
