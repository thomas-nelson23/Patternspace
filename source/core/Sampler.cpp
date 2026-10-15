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
    numBuiltIn = 0;
    for (auto& name : sampleNames())
    {
        if (name == "909 Kick")
        {
            double ph = 0;
            bank.push_back ({ synth (sr, 0.45, [&] (double t) {
                ph += twoPi * (48 + 140 * std::exp (-t * 28)) / sr;
                return std::sin (ph) * std::exp (-t * 6.5) * 0.95 + (t < 0.004 ? noise() * 0.4 : 0.0);
            }), sr });
        }
        else if (name == "Crack Snare")
            bank.push_back ({ synth (sr, 0.3, [&] (double t) {
                return noise() * std::exp (-t * 16) * 0.6 + std::sin (twoPi * 190 * t) * std::exp (-t * 22) * 0.5;
            }), sr });
        else if (name == "Tight Hat")
        {
            double pv = 0;
            bank.push_back ({ synth (sr, 0.12, [&] (double t) {
                const double x = noise(), y = x - pv;
                pv = x;
                return y * 0.5 * std::exp (-t * 55);
            }), sr });
        }
        else if (name == "Clap")
            bank.push_back ({ synth (sr, 0.35, [&] (double t) {
                double e = 0;
                for (double o : { 0.0, 0.011, 0.022 })
                    e += t >= o ? std::exp (-(t - o) * 180) : 0.0;
                e += t > 0.022 ? std::exp (-(t - 0.022) * 14) * 0.5 : 0.0;
                return noise() * e * 0.45;
            }), sr });
        else if (name == "Rim")
            bank.push_back ({ synth (sr, 0.1, [&] (double t) {
                return std::sin (twoPi * 820 * t) * std::exp (-t * 90) * 0.7 + noise() * std::exp (-t * 160) * 0.3;
            }), sr });
        else // Low Tom
        {
            double ph = 0;
            bank.push_back ({ synth (sr, 0.45, [&] (double t) {
                ph += twoPi * (95 + 70 * std::exp (-t * 18)) / sr;
                return std::sin (ph) * std::exp (-t * 7) * 0.85;
            }), sr });
        }
    }
    numBuiltIn = bank.size();
}

std::vector<Sampler::Sound> Sampler::bankFor (const std::vector<Sample>& samples)
{
    std::vector<Sound> sounds;
    for (auto& s : samples)
        sounds.push_back ({ s.data, s.rate > 0 ? s.rate : 44100 });
    return sounds;
}

void Sampler::setUserSamples (std::vector<Sound>&& sounds)
{
    stopAll(); // the bank moves in memory
    bank.resize (numBuiltIn);
    for (auto& s : sounds)
        bank.push_back (std::move (s));
}

const std::array<float, Sampler::numBuses>& Sampler::ones()
{
    static const std::array<float, numBuses> a { 1, 1, 1, 1, 1, 1, 1, 1 };
    return a;
}

void Sampler::cutOff (Voice& v)
{
    // The voice keeps sounding in a tail slot while it ramps to silence over about 5 ms, so a
    // choke or a stolen long sample doesn't click. A full tail array drops its quietest entry.
    auto t = std::find_if (tails.begin(), tails.end(), [] (const Voice& x) { return ! x.active; });
    if (t == tails.end())
        t = std::min_element (tails.begin(), tails.end(), [] (const Voice& a, const Voice& b) { return a.env * a.fade < b.env * b.fade; });
    *t = v;
    t->fadeStep = (float) (1.0 / std::max (1.0, rate * 0.005));
    v.active = false;
}

void Sampler::trigger (const StepOutput::Hit& hit, int offset)
{
    if (hit.sample < 0 || hit.sample >= (int) bank.size() || hit.bus < 1 || hit.bus > numBuses || bank[(size_t) hit.sample].data.size() < 2)
        return;

    // A mono player's own earlier hits, and the rest of its choke group, stop now.
    for (auto& v : voices)
        if (v.active && ((hit.mono && v.owner == hit.owner) || (hit.choke && v.choke == hit.choke)))
            cutOff (v);

    auto v = std::find_if (voices.begin(), voices.end(), [] (const Voice& x) { return ! x.active; });
    if (v == voices.end()) // steal the quietest, fading it out rather than cutting it
    {
        v = std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.env < b.env; });
        cutOff (*v);
    }

    const auto& sound = bank[(size_t) hit.sample];
    // Same envelope as the prototype: an exponential fall to -60 dB over 60 ms plus 60 ms per decay
    // step, so a user sample's length is set by Decay too (10 lets about 0.66 s through; the
    // sound's own end stops it sooner). Velocity sets the level: a plain hit (96) is the
    // prototype's 0.4, an accent (127) its 0.55; Level scales that.
    const double gain = (0.4 + (std::max (1, std::min (127, hit.velocity)) - 96) * (0.15 / 31.0)) * std::max (0, std::min (100, hit.level)) / 100.0;
    const double seconds = 0.06 + std::max (1, std::min (10, hit.decay)) * 0.06;
    const double pan = std::max (-50, std::min (50, hit.pan)) / 100.0 + 0.5; // 0 left, 1 right
    const double cut = std::max (0, std::min (100, hit.cutoff)) / 100.0;
    v->sound = &sound;
    v->pos = (double) sound.data.size() * std::max (0, std::min (100, hit.start)) / 100.0;
    v->step = std::pow (2.0, std::max (-12, std::min (12, hit.pitch)) / 12.0) * sound.rate / rate;
    v->env = (float) gain;
    v->decay = (float) std::pow (0.001 / std::max (gain, 1.0e-4), 1.0 / (seconds * rate));
    v->gainL = (float) std::cos (pan * twoPi / 4);
    v->gainR = (float) std::sin (pan * twoPi / 4);
    // The filter: a one-pole low-pass from about 100 Hz at 0 to open at 100.
    if (cut >= 0.999)
        v->lpCoeff = 1;
    else
    {
        const double hz = 100.0 * std::pow (200.0, cut); // 100 Hz .. 20 kHz
        v->lpCoeff = (float) (1.0 - std::exp (-twoPi * std::min (hz, rate * 0.45) / rate));
    }
    v->lp = 0;
    v->fade = 1;
    v->fadeStep = 0;
    v->bus = hit.bus - 1;
    v->delay = std::max (0, offset);
    v->remaining = (int) (seconds * rate);
    v->choke = hit.choke;
    v->owner = hit.owner;
    v->active = true;
}

void Sampler::renderVoice (Voice& v, const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples,
                           const std::array<float, numBuses>& gainL, const std::array<float, numBuses>& gainR)
{
    float* l = left[(size_t) v.bus];
    float* r = right[(size_t) v.bus];
    const float gl = v.gainL * gainL[(size_t) v.bus], gr = v.gainR * gainR[(size_t) v.bus];
    const auto& d = v.sound->data;
    const int size = (int) d.size();
    int i = std::min (v.delay, numSamples);
    v.delay -= i;
    for (; i < numSamples; ++i)
    {
        const int idx = (int) v.pos;
        if (idx + 2 >= size || v.remaining <= 0 || v.fade <= 0.0f)
        {
            v.active = false;
            break;
        }
        // Four-point Hermite interpolation, so pitched-up samples don't fizz like linear would.
        const float frac = (float) (v.pos - idx);
        const float y0 = d[(size_t) std::max (0, idx - 1)], y1 = d[(size_t) idx], y2 = d[(size_t) idx + 1], y3 = d[(size_t) idx + 2];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        float s = ((c3 * frac + c2) * frac + c1) * frac + y1;
        if (v.lpCoeff < 1.0f)
        {
            v.lp += v.lpCoeff * (s - v.lp);
            s = v.lp;
        }
        s *= v.env * v.fade;
        if (l) l[i] += s * gl;
        if (r) r[i] += s * gr;
        peaks[(size_t) v.bus] = std::max (peaks[(size_t) v.bus], std::abs (s) * std::max (gl, gr));
        v.pos += v.step;
        v.env *= v.decay;
        v.fade -= v.fadeStep;
        --v.remaining;
    }
}

void Sampler::render (const std::array<float*, numBuses>& left, const std::array<float*, numBuses>& right, int numSamples,
                      const std::array<float, numBuses>& gainL, const std::array<float, numBuses>& gainR)
{
    for (auto& v : voices)
        if (v.active)
            renderVoice (v, left, right, numSamples, gainL, gainR);
    for (auto& t : tails)
        if (t.active)
            renderVoice (t, left, right, numSamples, gainL, gainR);
}

void Sampler::stopAll()
{
    for (auto& v : voices)
        v.active = false;
    for (auto& t : tails)
        t.active = false;
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
