#include "PatchCanvasDetail.h"

#include "../core/Patterns.h"

#include <cmath>
#include <limits>

namespace ps::canvas
{
Clipboard& clipboard()
{
    static Clipboard c;
    return c;
}

float portY (const ps::Node& n, int i, int count)
{
    return n.collapsed ? 15.0f + ((float) i - (float) (count - 1) / 2.0f) * 11.0f : 39.0f + (float) i * rowH;
}

float nodeWidth (const ps::Node& n)
{
    if (n.collapsed)
        return 130.0f;
    auto* k = ps::kindInfo (n.kind);
    return (float) (k ? k->width : 150);
}

juce::Colour kindColour (const ps::Node& n)
{
    auto* k = ps::kindInfo (n.kind);
    return ps::theme::rgb (k ? k->colour : 0x7c8796);
}

// What a module's body shows, top to bottom. Each item is also where clicks land.


juce::String paramValue (const ps::Node& n, const ps::ParamSpec& spec)
{
    if (spec.key == std::string ("nout"))
        return juce::String ((int) n.lanes.size());
    const int v = n.get (spec.key) * spec.displayScale;
    if (spec.key == std::string ("pitch") && v > 0)
        return "+" + juce::String (v);
    return juce::String (v) + spec.suffix;
}

juce::String sampleLabelIn (const ps::Graph& root, const ps::Node& n)
{
    for (auto& s : root.samples)
        if (s.id == n.sample)
            return u8 (s.name) + (s.missing ? " (missing)" : "");
    return u8 (n.sample);
}

std::vector<Item> bodyItems (const ps::Node& n, float x, float y, float w, const std::map<std::string, int>& patchSettings, const ps::Graph& level, const ps::Graph& root)
{
    std::vector<Item> items;
    auto settings = [&patchSettings] (const char* key) {
        const auto it = patchSettings.find (key);
        return it == patchSettings.end() ? 0 : it->second;
    };
    const float inner = w - padX * 2;
    float cy = y;
    auto add = [&] (ItemType t, float h, std::string key = {}, juce::String text = {}, juce::String value = {}) {
        items.push_back ({ t, { x + padX, cy, inner, h }, std::move (key), std::move (text), std::move (value) });
        cy += h + gapY;
    };
    auto steppers = [&] {
        for (auto& spec : ps::paramsOf (n))
            add (ItemType::stepper, stepperH, spec.key, spec.label, paramValue (n, spec));
    };
    const auto& k = n.kind;
    if (k == "clock")
    {
        add (ItemType::bigText, 18, "bpm");
        add (ItemType::modes, 18, "rate");
        add (ItemType::stepper, stepperH, "swing", "Swing", juce::String (std::max (50, std::min (75, settings ("swing")))) + "%");
        add (ItemType::text, 12, {}, "Jumps line up with:");
        add (ItemType::modes, 18, "follow");
    }
    else if (k == "euclid" || k == "random")
    {
        steppers();
        if (k == "random")
            add (ItemType::button, 18, "reroll", "Reroll");
        add (ItemType::cells, n.get ("len", 16) > 32 ? 21.0f : 10.0f, "pattern");
    }
    else if (k == "ca")
    {
        add (ItemType::text, 12, {}, "Row advances each loop");
        add (ItemType::cells, 10, "pattern");
    }
    else if (k == "step")
    {
        steppers();
        add (ItemType::text, 12, {}, "Click a cell: off, hit, accent");
        add (ItemType::cells, n.get ("len", 16) > 32 ? 25.0f : 12.0f, "edit");
    }
    else if (k == "grids")
    {
        const float pad = 104;
        items.push_back ({ ItemType::xy, { x + padX, cy, pad, pad }, "xy", {}, {} });
        const float sx = x + padX + pad + 10, sw = w - padX - (sx - x);
        float sy = cy + 2;
        for (auto& spec : ps::paramsOf (n))
        {
            items.push_back ({ ItemType::stepper, { sx, sy, sw, stepperH }, spec.key, spec.label, paramValue (n, spec) });
            sy += stepperH + 1;
        }
        cy += pad + 6;
        add (ItemType::drumRows, 3 * 8 + 2 * 3);
    }
    else if (k == "div" || k == "prob" || k == "vel" || k == "shift" || k == "ratchet" || k == "scene")
        steppers();
    else if (k == "fill")
    {
        steppers();
        add (ItemType::text, 12, {}, "Adds hits on the last bar");
    }
    else if (k == "accent")
    {
        steppers();
        add (ItemType::text, 12, {}, "Every " + juce::String (n.get ("every", 8)) + "th step hits harder");
    }
    else if (k == "combine")
        add (ItemType::modes, 18, "combine");
    else if (k == "switch")
    {
        add (ItemType::modes, 18, "switch");
        add (ItemType::modes, 18, "order");
        steppers();
        add (ItemType::switchPos, 14);
    }
    else if (k == "sample")
    {
        add (ItemType::sample, 20, "sample", sampleLabelIn (root, n));
        add (ItemType::modes, 18, "mono");
        steppers();
    }
    else if (k == "audio")
    {
        add (ItemType::buses, 18, "bus");
        const int b = n.get ("bus", 1);
        add (ItemType::text, 12, {}, "Out " + juce::String (b) + ", ch " + juce::String (2 * b - 1) + "-" + juce::String (2 * b));
        add (ItemType::meter, 6, "meter");
        steppers();
    }
    else if (k == "midi")
    {
        steppers();
        add (ItemType::text, 12, {}, "To the host's MIDI out");
    }
    else if (k == "midiin")
    {
        steppers();
        add (ItemType::text, 12, {}, n.get ("ch") ? "Notes on channel " + juce::String (n.get ("ch")) : juce::String ("Notes on any channel"));
    }
    else if (k == "cc")
    {
        steppers();
        add (ItemType::text, 12, {}, "Sends the value as it changes");
    }
    else if (ps::isControlSource (n))
    {
        if (k == "lfo")
            add (ItemType::modes, 18, "shape");
        if (k == "macro")
            add (ItemType::button, 18, "learn", "MIDI CC");
        steppers();
        add (ItemType::ctlBar, 8, "ctl");
        // The target: a module on this level and one of its parameters, with the range swept.
        juce::String target = "Target...";
        std::string id, key;
        if (ps::parseTarget (n.target, id, key))
            if (auto* t = level.find (id))
                for (auto& spec : ps::modulatableParams (*t))
                    if (key == spec.key)
                        target = juce::String::fromUTF8 ("\xE2\x86\x92 ") + u8 (ps::titleOf (*t)) + " \xC2\xB7 " + spec.label;
        add (ItemType::button, 18, "target", target);
        if (target != "Target...")
        {
            add (ItemType::stepper, stepperH, "lo", "From", juce::String (n.get ("lo")));
            add (ItemType::stepper, stepperH, "hi", "To", juce::String (n.get ("hi")));
        }
    }
    else if (k == "sub")
    {
        juce::StringArray names;
        if (! n.inner.empty())
            for (auto& m : n.inner.front().nodes)
                if (m.kind != "subin" && m.kind != "subout")
                    names.add (u8 (ps::titleOf (m)));
        add (ItemType::text, 12, {}, juce::String (names.size()) + (names.size() == 1 ? " module inside" : " modules inside"));
        add (ItemType::text, 24, {}, names.joinIntoString (", "));
        add (ItemType::button, 20, "open", juce::String::fromUTF8 ("Open \xE2\x96\xB8"));
    }
    else if (k == "subin")
    {
        add (ItemType::text, 12, {}, "Inputs from the parent");
        add (ItemType::button, 20, "addPort", "+ Add input");
    }
    else if (k == "subout")
    {
        add (ItemType::text, 12, {}, "Outputs to the parent");
        add (ItemType::button, 20, "addPort", "+ Add output");
    }
    return items;
}

std::vector<int> patternOf (const ps::Node& n, const ps::Engine::View* v)
{
    if (n.kind == "euclid")
        return ps::euclid (n.get ("hits"), n.get ("len", 16), n.get ("rot"));
    if (n.kind == "random")
        return ps::randomPattern (n.get ("len", 8), n.get ("density", 5), n.get ("seed", 90));
    if (n.kind == "step")
        return n.cells;
    if (n.kind == "ca")
    {
        const auto& rows = ps::cellularRows();
        const auto& row = rows[(size_t) ((v ? v->row : 0) % (int) rows.size())];
        return std::vector<int> (row.begin(), row.end());
    }
    return {};
}

// A menu row in a colour, with a tick when it is the current choice.
void colouredItem (juce::PopupMenu& menu, const juce::String& text, juce::Colour colour, bool ticked, std::function<void()> action)
{
    juce::PopupMenu::Item item (text);
    item.colour = colour;
    item.isTicked = ticked;
    item.action = std::move (action);
    menu.addItem (std::move (item));
}
} // namespace ps::canvas
