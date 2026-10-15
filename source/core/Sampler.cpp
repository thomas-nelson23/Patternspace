#include "Sampler.h"

#include "Modules.h"
#include "Patterns.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ps
{
namespace
{
    constexpr double twoPi = 6.283185307179586;

    std::vector<float> synth (double sr, double seconds, const std::function<double (double)>& f)
    {
        std::vector<float> a ((size_t) std::floor (sr * seconds));
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = (float) f ((double) i / sr);
        return a;
    }
} // namespace

void Sampler::prepare (double sampleRate)
{
    rate = sampleRate > 0 ? sampleRate : 44100;
    stopAll();
    Rng nz (7);
    auto noise = [&nz] { return nz.next() * 2 - 1; };
    const double sr = rate;

    bank.clear();
    for (auto& name : sampleNames())
    {
        if (name == "909 Kick")
        {
            double ph = 0;
            bank.push_back (synth (sr, 0.45, [&] (double t) {
                ph += twoPi * (48 + 140 * std::exp (-t * 28)) / sr;
                return std::sin (ph) * std::exp (-t * 6.5) * 0.95 + (t < 0.004 ? noise() * 0.4 : 0.0);
            }));
        }
        else if (name == "Crack Snare")
            bank.push_back (synth (sr, 0.3, [&] (double t) {
                return noise() * std::exp (-t * 16) * 0.6 + std::sin (twoPi * 190 * t) * std::exp (-t * 22) * 0.5;
            }));
        else if (name == "Tight Hat")
        {
            double pv = 0;
            bank.push_back (synth (sr, 0.12, [&] (double t) {
                const double x = noise(), y = x - pv;
                pv = x;
                return y * 0.5 * std::exp (-t * 55);
            }));
        }
        else if (name == "Clap")
            bank.push_back (synth (sr, 0.35, [&] (double t) {
                double e = 0;
                for (double o : { 0.0, 0.011, 0.022 })
                    e += t >= o ? std::exp (-(t - o) * 180) : 0.0;
                e += t > 0.022 ? std::exp (-(t - 0.022) * 14) * 0.5 : 0.0;
                return noise() * e * 0.45;
            }));
        else if (name == "Rim")
            bank.push_back (synth (sr, 0.1, [&] (double t) {
                return std::sin (twoPi * 820 * t) * std::exp (-t * 90) * 0.7 + noise() * std::exp (-t * 160) * 0.3;
            }));
        else // Low Tom
        {
            double ph = 0;
            bank.push_back (synth (sr, 0.45, [&] (double t) {
                ph += twoPi * (95 + 70 * std::exp (-t * 18)) / sr;
                return std::sin (ph) * std::exp (-t * 7) * 0.85;
            }));
        }
    }
}

void Sampler::trigger (const StepOutput::Hit& hit, int offset)
{
    if (hit.sample < 0 || hit.sample >= (int) bank.size() || hit.bus < 1 || hit.bus > numBuses)
        return;

    auto v = std::find_if (voices.begin(), voices.end(), [] (const Voice& x) { return ! x.active; });
    if (v == voices.end()) // steal the quietest
        v = std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.env < b.env; });

    // Same envelope as the prototype: an exponential fall to -60 dB over 60 ms plus 60 ms per decay step.
    // Velocity sets the level: a plain hit (96) is the prototype's 0.4, an accent (127) its 0.55.
    const double gain = 0.4 + (std::max (1, std::min (127, hit.velocity)) - 96) * (0.15 / 31.0);
    const double seconds = 0.06 + std::max (1, std::min (10, hit.decay)) * 0.06;
    v->data = &bank[(size_t) hit.sample];
    v->pos = 0;
    v->step = std::pow (2.0, std::max (-12, std::min (12, hit.pitch)) / 12.0);
    v->env = (float) gain;
    v->decay = (float) std::pow (0.001 / gain, 1.0 / (seconds * rate));
    v->bus = hit.bus - 1;
    v->delay = std::max (0, offset);
    v->remaining = (int) (seconds * rate);
    v->active = true;
}

void Sampler::render (const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        float* l = left[(size_t) v.bus];
        float* r = right[(size_t) v.bus];
        const auto& d = *v.data;
        const int size = (int) d.size();
        int i = std::min (v.delay, numSamples);
        v.delay -= i;
        for (; i < numSamples; ++i)
        {
            const int idx = (int) v.pos;
            if (idx + 1 >= size || v.remaining <= 0)
            {
                v.active = false;
                break;
            }
            const float frac = (float) (v.pos - idx);
            const float s = (d[(size_t) idx] + (d[(size_t) idx + 1] - d[(size_t) idx]) * frac) * v.env;
            if (l) l[i] += s;
            if (r) r[i] += s;
            peaks[(size_t) v.bus] = std::max (peaks[(size_t) v.bus], std::abs (s));
            v.pos += v.step;
            v.env *= v.decay;
            --v.remaining;
        }
    }
}

void Sampler::stopAll()
{
    for (auto& v : voices)
        v.active = false;
}

float Sampler::takePeak (int bus)
{
    if (bus < 0 || bus >= numBuses)
        return 0;
    const float p = peaks[(size_t) bus];
    peaks[(size_t) bus] = 0;
    return p;
}

int Sampler::activeVoices() const
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; });
}
} // namespace ps
