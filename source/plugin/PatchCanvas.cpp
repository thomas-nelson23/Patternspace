#include "PatchCanvas.h"

#include "../core/Modules.h"
#include "../core/Patterns.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <cmath>
#include <limits>

namespace
{
constexpr float headerH = 26.0f, ioTop = 30.0f, rowH = 18.0f, padX = 9.0f, stepperH = 16.0f, gapY = 3.0f;
constexpr float minZoom = 0.4f, maxZoom = 2.0f;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

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
enum class ItemType
{
    text,
    bigText,
    stepper,
    cells,
    drumRows,
    modes,
    buses,
    meter,
    xy,
    sample,
    switchPos
};

struct Item
{
    ItemType type;
    juce::Rectangle<float> r;
    std::string key;
    juce::String text, value;
};

juce::String paramValue (const ps::Node& n, const ps::ParamSpec& spec)
{
    if (spec.key == std::string ("nout"))
        return juce::String ((int) n.lanes.size());
    const int v = n.get (spec.key) * spec.displayScale;
    if (spec.key == std::string ("pitch") && v > 0)
        return "+" + juce::String (v);
    return juce::String (v) + spec.suffix;
}

std::vector<Item> bodyItems (const ps::Node& n, float x, float y, float w)
{
    std::vector<Item> items;
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
        add (ItemType::text, 12, {}, "16ths, bars, phrases");
    }
    else if (k == "euclid" || k == "random")
    {
        steppers();
        add (ItemType::cells, 10, "pattern");
    }
    else if (k == "ca")
    {
        add (ItemType::text, 12, {}, "Row advances each loop");
        add (ItemType::cells, 10, "pattern");
    }
    else if (k == "step")
    {
        add (ItemType::text, 12, {}, "Click cells to edit");
        add (ItemType::cells, 12, "edit");
    }
    else if (k == "grids")
    {
        const float pad = 72;
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
    else if (k == "div" || k == "prob")
        steppers();
    else if (k == "accent")
        add (ItemType::text, 12, {}, "Beats 127, the rest 96");
    else if (k == "combine")
        add (ItemType::modes, 18, "combine");
    else if (k == "switch")
    {
        add (ItemType::modes, 18, "switch");
        steppers();
        add (ItemType::switchPos, 14);
    }
    else if (k == "sample")
    {
        add (ItemType::sample, 20, "sample", u8 (n.sample));
        steppers();
    }
    else if (k == "audio")
    {
        add (ItemType::buses, 18, "bus");
        const int b = n.get ("bus", 1);
        add (ItemType::text, 12, {}, "Out " + juce::String (b) + ", ch " + juce::String (2 * b - 1) + "-" + juce::String (2 * b));
        add (ItemType::meter, 6, "meter");
    }
    else if (k == "midi")
    {
        steppers();
        add (ItemType::text, 12, {}, "To the host's MIDI out");
    }
    else if (k == "sub")
    {
        juce::StringArray names;
        if (! n.inner.empty())
            for (auto& m : n.inner.front().nodes)
                if (m.kind != "subin" && m.kind != "subout")
                    names.add (u8 (ps::titleOf (m)));
        add (ItemType::text, 12, {}, juce::String (names.size()) + " modules inside");
        add (ItemType::text, 24, {}, names.joinIntoString (", "));
    }
    else if (k == "subin")
        add (ItemType::text, 12, {}, "Inputs from the parent");
    else if (k == "subout")
        add (ItemType::text, 12, {}, "Outputs to the parent");
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
} // namespace

PatchCanvas::PatchCanvas (PatternspaceProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    patchReplaced();
    startTimerHz (30);
}

PatchCanvas::~PatchCanvas() = default;

void PatchCanvas::patchReplaced()
{
    finishTagEdit (false);
    selection.clear();
    selectedWire = -1;
    hoverNode = -1;
    drag = Drag::none;
    const auto& pages = processor.patch().pages;
    const bool stillThere = std::any_of (pages.begin(), pages.end(), [this] (auto& pg) { return pg.id == currentPage; });
    if (! stillThere)
        currentPage = pages.empty() ? std::string() : pages.front().id;
    viewValid = false;
    repaint();
}

void PatchCanvas::setPage (const std::string& id)
{
    if (id == currentPage)
        return;
    finishTagEdit (true);
    currentPage = id;
    selection.clear();
    selectedWire = -1;
    repaint();
}

bool PatchCanvas::onPage (const ps::Node& n) const { return n.page.empty() || n.page == currentPage; }

void PatchCanvas::resized() {}

void PatchCanvas::timerCallback()
{
    viewValid = processor.copyView (view) || viewValid;
    processor.decayLevels();
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil)
        message = {};
    repaint();
}

void PatchCanvas::edited()
{
    processor.commitPatch();
    viewValid = false;
    repaint();
    if (onEdited)
        onEdited();
}

// ---- geometry --------------------------------------------------------------------------------

PatchCanvas::Layout PatchCanvas::layoutOf (const ps::Node& n) const
{
    Layout L;
    const auto ins = ps::inputsOf (n), outs = ps::outputsOf (n);
    const float w = nodeWidth (n);
    const float x = (float) n.x, y = (float) n.y;
    for (int i = 0; i < (int) ins.size(); ++i)
        L.ins.push_back ({ x, y + portY (n, i, (int) ins.size()) });
    for (int i = 0; i < (int) outs.size(); ++i)
        L.outs.push_back ({ x + w, y + portY (n, i, (int) outs.size()) });

    if (n.collapsed)
    {
        L.box = { x, y, w, 30 };
        L.header = L.box;
        return L;
    }
    const int rows = (int) std::max (ins.size(), outs.size());
    L.bodyTop = rows > 0 ? ioTop + rows * rowH + 4 : headerH + 6;
    const auto items = bodyItems (n, x, y + L.bodyTop, w);
    float bottom = y + L.bodyTop;
    for (auto& it : items)
    {
        bottom = std::max (bottom, it.r.getBottom());
        switch (it.type)
        {
            case ItemType::stepper:
                L.zones.push_back ({ { it.r.getRight() - 52, it.r.getY(), 14, it.r.getHeight() }, ZoneType::param, it.key, -1 });
                L.zones.push_back ({ { it.r.getRight() - 12, it.r.getY(), 14, it.r.getHeight() }, ZoneType::param, it.key, 1 });
                break;
            case ItemType::cells:
                if (it.key == "edit")
                    for (int c = 0; c < 16; ++c)
                        L.zones.push_back ({ { it.r.getX() + it.r.getWidth() * c / 16.0f, it.r.getY() - 2, it.r.getWidth() / 16.0f, it.r.getHeight() + 4 }, ZoneType::cell, {}, c });
                break;
            case ItemType::modes:
            {
                const int count = it.key == "combine" ? (int) ps::combineModes().size() : 2;
                for (int m = 0; m < count; ++m)
                    L.zones.push_back ({ { it.r.getX() + it.r.getWidth() * m / count, it.r.getY(), it.r.getWidth() / count, it.r.getHeight() }, ZoneType::mode, it.key, m });
                break;
            }
            case ItemType::buses:
                for (int b = 0; b < 8; ++b)
                    L.zones.push_back ({ { it.r.getX() + it.r.getWidth() * b / 8.0f, it.r.getY(), it.r.getWidth() / 8.0f, it.r.getHeight() }, ZoneType::bus, {}, b + 1 });
                break;
            case ItemType::xy:
                L.zones.push_back ({ it.r, ZoneType::xy, {}, 0 });
                break;
            case ItemType::sample:
                L.zones.push_back ({ it.r.withWidth (it.r.getWidth() / 2), ZoneType::sample, {}, -1 });
                L.zones.push_back ({ it.r.withTrimmedLeft (it.r.getWidth() / 2), ZoneType::sample, {}, 1 });
                break;
            case ItemType::text:
            case ItemType::bigText:
            case ItemType::drumRows:
            case ItemType::meter:
            case ItemType::switchPos:
                break;
        }
    }
    if (n.kind == "midi")
        for (int q = 0; q < (int) n.lanes.size(); ++q)
        {
            const float ry = y + ioTop + q * rowH;
            L.zones.push_back ({ { x + w - 62, ry, 14, rowH }, ZoneType::laneNote, {}, -(q + 1) });
            L.zones.push_back ({ { x + w - 26, ry, 14, rowH }, ZoneType::laneNote, {}, q + 1 });
        }
    L.box = { x, y, w, bottom - y + 9 };
    L.header = { x, y, w, headerH };
    return L;
}

float PatchCanvas::bodyHeight (const ps::Node& n) const { return layoutOf (n).box.getHeight(); }

juce::Rectangle<float> PatchCanvas::tagRect (const ps::Node&, const Layout& L, bool output, int port, const std::string& tag) const
{
    const auto font = ps::theme::mono (8.5f, true);
    const float w = juce::GlyphArrangement::getStringWidth (font, u8 (tag)) + 13;
    const auto p = output ? L.outs[(size_t) port] : L.ins[(size_t) port];
    return output ? juce::Rectangle<float> (p.x + 9, p.y - 7.5f, w, 15) : juce::Rectangle<float> (p.x - 9 - w, p.y - 7.5f, w, 15);
}

juce::Colour PatchCanvas::tagColour (const std::string& tag) const
{
    auto& colours = processor.patch().tagColours;
    auto it = colours.find (tag);
    return it != colours.end() ? ps::theme::fromHex (it->second, ps::theme::hashedTagColour (tag)) : ps::theme::hashedTagColour (tag);
}

juce::Path PatchCanvas::wirePath (juce::Point<float> a, juce::Point<float> b) const
{
    const float dx = std::max (40.0f, std::abs (b.x - a.x) * 0.5f);
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    return p;
}

int PatchCanvas::viewOut (int node, int port) const
{
    if (! viewValid || node < 0 || node >= (int) view.size() || port < 0 || port >= 16)
        return 0;
    return view[(size_t) node].out[(size_t) port];
}

int PatchCanvas::viewIn (int node, int port) const
{
    if (! viewValid || node < 0 || node >= (int) view.size() || port < 0 || port >= 16)
        return 0;
    return view[(size_t) node].in[(size_t) port];
}

PatchCanvas::HitResult PatchCanvas::hitTest (juce::Point<float> w) const
{
    const auto& g = processor.patch();
    HitResult r;
    for (int i = (int) g.nodes.size() - 1; i >= 0; --i)
    {
        const auto& n = g.nodes[(size_t) i];
        if (! onPage (n))
            continue;
        const auto L = layoutOf (n);
        for (int q = 0; q < (int) L.ins.size(); ++q)
            if (L.ins[(size_t) q].getDistanceFrom (w) < 8)
                return { Hit::inPort, i, q };
        for (int p = 0; p < (int) L.outs.size(); ++p)
            if (L.outs[(size_t) p].getDistanceFrom (w) < 8)
                return { Hit::outPort, i, p };
        for (auto& [q, t] : n.tagIn)
            if (q < (int) L.ins.size() && tagRect (n, L, false, q, t).contains (w))
                return { Hit::inTag, i, q };
        for (auto& [p, t] : n.tagOut)
            if (p < (int) L.outs.size() && tagRect (n, L, true, p, t).contains (w))
                return { Hit::outTag, i, p };
        if (L.box.contains (w))
        {
            for (auto& z : L.zones)
                if (z.r.contains (w))
                {
                    r = { Hit::zone, i };
                    r.zone = z;
                    return r;
                }
            return { L.header.contains (w) ? Hit::header : Hit::body, i };
        }
    }
    for (int l = 0; l < (int) g.links.size(); ++l)
    {
        const auto& link = g.links[(size_t) l];
        const auto *a = g.find (link.a), *b = g.find (link.b);
        if (! a || ! b || ! onPage (*a) || ! onPage (*b))
            continue;
        const auto La = layoutOf (*a), Lb = layoutOf (*b);
        if (link.p >= (int) La.outs.size() || link.q >= (int) Lb.ins.size())
            continue;
        juce::Point<float> nearest;
        const auto path = wirePath (La.outs[(size_t) link.p], Lb.ins[(size_t) link.q]);
        path.getNearestPoint (w, nearest);
        if (nearest.getDistanceFrom (w) < 6.0f / scale)
        {
            r.what = Hit::wire;
            r.wire = l;
            return r;
        }
    }
    for (int b = (int) g.blocks.size() - 1; b >= 0; --b)
    {
        const auto& blk = g.blocks[(size_t) b];
        if (blk.page != currentPage && ! blk.page.empty())
            continue;
        if (juce::Rectangle<float> ((float) blk.x, (float) blk.y, (float) blk.w, 24).contains (w))
        {
            r.what = Hit::blockTitle;
            r.block = b;
            return r;
        }
    }
    return r;
}

// ---- painting --------------------------------------------------------------------------------

void PatchCanvas::paint (juce::Graphics& g)
{
    using namespace ps::theme;
    g.fillAll (canvas);

    // Dot grid, every 20 world units.
    const float step = 20.0f * scale;
    if (step >= 6.0f)
    {
        g.setColour (dots);
        const float sx = std::fmod (offset.x, step), sy = std::fmod (offset.y, step);
        for (float yy = sy; yy < (float) getHeight(); yy += step)
            for (float xx = sx; xx < (float) getWidth(); xx += step)
                g.fillRect (xx, yy, 1.4f, 1.4f);
    }

    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (scale).translated (offset));
        paintBlocks (g);
        paintNotes (g);
        paintWires (g);
        const auto& nodes = processor.patch().nodes;
        for (int i = 0; i < (int) nodes.size(); ++i)
            if (onPage (nodes[(size_t) i]))
                paintNode (g, i, nodes[(size_t) i], layoutOf (nodes[(size_t) i]));

        if (drag == Drag::wire && dragMoved && pressed.node >= 0)
        {
            const auto& n = nodes[(size_t) pressed.node];
            const auto L = layoutOf (n);
            const bool fromOut = pressed.what == Hit::outPort;
            const auto p = fromOut ? L.outs[(size_t) pressed.port] : L.ins[(size_t) pressed.port];
            g.setColour (kindColour (n).withAlpha (0.9f));
            g.strokePath (fromOut ? wirePath (p, dragWorld) : wirePath (dragWorld, p), juce::PathStrokeType (2.2f));
        }
    }

    if (drag == Drag::box && dragMoved)
    {
        const auto area = juce::Rectangle<float> (dragStart, toScreen (dragWorld));
        g.setColour (selected.withAlpha (0.1f));
        g.fillRect (area);
        g.setColour (selected.withAlpha (0.8f));
        g.drawRect (area, 1.0f);
    }

    const auto hint = message.isNotEmpty() ? message
                                           : juce::String ("Drag empty space to select, right-drag to pan, Ctrl/Cmd-scroll to zoom. Drag an output onto an input to wire it, click a port to tag it.");
    g.setFont (mono (10.0f));
    auto hintArea = getLocalBounds().reduced (8, 6).removeFromBottom (20).toFloat();
    hintArea = hintArea.withWidth (std::min (hintArea.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 16));
    g.setColour (canvas.withAlpha (0.85f));
    g.fillRoundedRectangle (hintArea, 4);
    g.setColour (message.isNotEmpty() ? textSoft : muted.withAlpha (0.8f));
    g.drawText (hint, hintArea.reduced (8, 0), juce::Justification::centredLeft, true);
}

void PatchCanvas::paintBlocks (juce::Graphics& g)
{
    for (auto& b : processor.patch().blocks)
    {
        if (! b.page.empty() && b.page != currentPage)
            continue;
        const auto c = ps::theme::fromHex (b.colour, ps::theme::muted);
        const juce::Rectangle<float> r ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
        g.setColour (c.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 8);
        g.setColour (c.withAlpha (0.32f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8, 1);
        g.setColour (c);
        g.fillEllipse (r.getX() + 9, r.getY() + 8, 8, 8);
        g.setFont (ps::theme::mono (10.0f, true));
        g.setColour (c.brighter (0.2f));
        g.drawText (u8 (b.title).toUpperCase(), juce::Rectangle<float> (r.getX() + 24, r.getY(), r.getWidth() - 30, 24),
                    juce::Justification::centredLeft, true);
    }
}

void PatchCanvas::paintNotes (juce::Graphics& g)
{
    g.setFont (ps::theme::sans (11.5f));
    g.setColour (ps::theme::noteText);
    for (auto& t : processor.patch().notes)
    {
        if (! t.page.empty() && t.page != currentPage)
            continue;
        const float w = t.w > 0 ? (float) t.w : 210.0f;
        juce::AttributedString s;
        s.append (u8 (t.text), ps::theme::sans (11.5f), ps::theme::noteText);
        s.setLineSpacing (3.0f);
        s.draw (g, { (float) t.x, (float) t.y, w, 400.0f });
    }
}

void PatchCanvas::paintWires (juce::Graphics& g)
{
    const auto& patch = processor.patch();
    for (int l = 0; l < (int) patch.links.size(); ++l)
    {
        const auto& link = patch.links[(size_t) l];
        const int ia = patch.indexOf (link.a), ib = patch.indexOf (link.b);
        if (ia < 0 || ib < 0)
            continue;
        const auto &a = patch.nodes[(size_t) ia], &b = patch.nodes[(size_t) ib];
        if (! onPage (a) || ! onPage (b))
            continue;
        const auto La = layoutOf (a), Lb = layoutOf (b);
        if (link.p >= (int) La.outs.size() || link.q >= (int) Lb.ins.size())
            continue;
        const bool audio = ps::outputType (a, link.p) == ps::PortType::audio;
        const bool reset = ps::inputsOf (b)[(size_t) link.q] == "rst";
        const bool lit = viewOut (ia, link.p) > 0 && ! (a.kind == "clock" && link.p == 0);
        auto path = wirePath (La.outs[(size_t) link.p], Lb.ins[(size_t) link.q]);
        const float width = (audio ? 4.0f : 2.2f) + (lit ? 1.4f : 0.0f);
        g.setColour (l == selectedWire ? juce::Colours::white : kindColour (a).withAlpha (lit ? 1.0f : (audio ? 0.6f : 0.72f)));
        if (reset)
        {
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (width).createDashedStroke (dashed, path, dashes, 2);
            g.fillPath (dashed);
        }
        else
            g.strokePath (path, juce::PathStrokeType (width));
    }
}

void PatchCanvas::paintCells (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<int>& cells, int now, juce::Colour c, bool editable)
{
    const int count = (int) cells.size();
    if (count == 0)
        return;
    const float gap = count > 20 ? 1.0f : 2.0f;
    const float cw = (r.getWidth() - gap * (count - 1)) / count;
    for (int i = 0; i < count; ++i)
    {
        const juce::Rectangle<float> cell (r.getX() + i * (cw + gap), r.getY(), cw, r.getHeight());
        const int v = cells[(size_t) i];
        if (v > 0)
            g.setColour (c.withAlpha (i == now ? 1.0f : (v > 1 ? 0.8f : 0.5f)));
        else
            g.setColour (editable ? ps::theme::cellOff.brighter (0.1f) : ps::theme::cellOff);
        g.fillRoundedRectangle (cell, 1.5f);
        if (i == now)
        {
            g.setColour (ps::theme::text);
            g.drawRoundedRectangle (cell.expanded (0.5f), 1.5f, 1.0f);
        }
    }
}

void PatchCanvas::paintNode (juce::Graphics& g, int index, const ps::Node& n, const Layout& L)
{
    using namespace ps::theme;
    const auto c = kindColour (n);
    const bool sel = selection.count (n.id) > 0;
    const auto ins = ps::inputsOf (n), outs = ps::outputsOf (n);

    // shadow, body, border
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (L.box.translated (0, 4).expanded (1), 7);
    g.setColour (node);
    g.fillRoundedRectangle (L.box, 6);
    g.setColour (sel ? selected : nodeBorder);
    g.drawRoundedRectangle (L.box.reduced (0.5f), 6, sel ? 2.0f : 1.0f);

    // header
    const auto h = L.header;
    if (! n.collapsed)
    {
        g.setColour (lineStrong.darker (0.2f));
        g.fillRect (h.getX() + 1, h.getBottom() - 1, h.getWidth() - 2, 1.0f);
    }
    g.setColour (muted);
    juce::Path chevron;
    const float cx = h.getX() + 12, cy = h.getY() + (n.collapsed ? 15.0f : 13.0f);
    chevron.addTriangle (cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 2.5f);
    if (n.collapsed)
        chevron.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cx, cy));
    g.fillPath (chevron);
    g.setColour (c);
    g.fillRoundedRectangle (h.getX() + 22, cy - 4, 8, 8, 2);
    g.setColour (text);
    g.setFont (sans (11.0f, true));
    const auto sub = u8 (ps::subtitleOf (n));
    const float subW = n.collapsed ? 0.0f : juce::GlyphArrangement::getStringWidth (mono (9.5f), sub) + 4;
    g.drawText (u8 (ps::titleOf (n)), juce::Rectangle<float> (h.getX() + 36, cy - 8, h.getWidth() - 44 - subW, 16), juce::Justification::centredLeft, true);
    if (! n.collapsed)
    {
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (sub, juce::Rectangle<float> (h.getRight() - 7 - subW, cy - 8, subW, 16), juce::Justification::centredRight, false);
    }

    // port labels (MIDI outs show each output's name and note instead)
    if (! n.collapsed)
    {
        g.setFont (mono (9.0f));
        if (n.kind == "midi")
        {
            for (int q = 0; q < (int) n.lanes.size(); ++q)
            {
                const float ry = (float) n.y + ioTop + q * rowH;
                const auto& lane = n.lanes[(size_t) q];
                g.setColour (textSoft);
                g.drawText (u8 (lane.name), juce::Rectangle<float> ((float) n.x + 12, ry, L.box.getWidth() - 80, rowH), juce::Justification::centredLeft, true);
                g.setColour (muted);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), juce::Rectangle<float> (L.box.getRight() - 62, ry, 14, rowH), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), juce::Rectangle<float> (L.box.getRight() - 26, ry, 14, rowH), juce::Justification::centred);
                g.setColour (c);
                g.drawText (u8 (ps::noteName (lane.note)), juce::Rectangle<float> (L.box.getRight() - 50, ry, 24, rowH), juce::Justification::centred);
                const int v = viewIn (index, q);
                g.setColour (v > 0 ? (v > 1 ? text : c) : cellOff);
                g.fillEllipse (L.box.getRight() - 11, ry + rowH / 2 - 3, 6, 6);
            }
        }
        else
        {
            g.setColour (muted);
            for (int i = 0; i < (int) ins.size(); ++i)
                g.drawText (u8 (ins[(size_t) i]), juce::Rectangle<float> ((float) n.x + 12, (float) n.y + ioTop + i * rowH, L.box.getWidth() / 2 - 12, rowH), juce::Justification::centredLeft, true);
            for (int i = 0; i < (int) outs.size(); ++i)
                g.drawText (u8 (outs[(size_t) i]), juce::Rectangle<float> ((float) n.x + L.box.getWidth() / 2, (float) n.y + ioTop + i * rowH, L.box.getWidth() / 2 - 12, rowH), juce::Justification::centredRight, true);
        }
        paintBody (g, index, n, L);
    }

    // ports: gate ports are rings, audio ports squares; a port lights while a hit passes through
    for (int dir = 0; dir < 2; ++dir)
    {
        const auto& pts = dir == 0 ? L.ins : L.outs;
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const bool audio = dir == 0 ? ps::inputType (n, i) == ps::PortType::audio : ps::outputType (n, i) == ps::PortType::audio;
            const int v = dir == 0 ? viewIn (index, i) : viewOut (index, i);
            const bool lit = v > 0 && ! (n.kind == "clock" && dir == 1 && i == 0) && ! audio;
            const auto r = juce::Rectangle<float> (10, 10).withCentre (pts[(size_t) i]);
            g.setColour (node);
            if (audio)
                g.fillRect (r);
            else
                g.fillEllipse (r);
            g.setColour (lit ? text : c);
            if (audio)
                g.drawRect (r, 2.0f);
            else
                g.drawEllipse (r.reduced (1), 2.0f);
            if (lit)
                g.fillEllipse (r.reduced (3));
        }
    }

    // tags
    g.setFont (mono (8.5f, true));
    for (int dir = 0; dir < 2; ++dir)
    {
        const auto& tags = dir == 0 ? n.tagIn : n.tagOut;
        for (auto& [port, tag] : tags)
        {
            if (port >= (int) (dir == 0 ? L.ins.size() : L.outs.size()))
                continue;
            const auto r = tagRect (n, L, dir == 1, port, tag);
            const auto tc = tagColour (tag);
            const int v = dir == 0 ? viewIn (index, port) : viewOut (index, port);
            const bool lit = v > 0 && ! (n.kind == "clock" && dir == 1 && port == 0);
            g.setColour (tc);
            const auto p = dir == 0 ? L.ins[(size_t) port] : L.outs[(size_t) port];
            g.drawLine (dir == 0 ? r.getRight() : p.x + 5, p.y, dir == 0 ? p.x - 5 : r.getX(), p.y, 1.0f);
            g.setColour (lit ? tc : canvas);
            g.fillRoundedRectangle (r, 3);
            g.setColour (tc);
            g.drawRoundedRectangle (r.reduced (0.5f), 3, 1);
            g.fillRect (r.withWidth (3));
            g.setColour (lit ? juce::Colour (0xff121418) : text);
            g.drawText (u8 (tag), r.withTrimmedLeft (4), juce::Justification::centred, false);
        }
    }
}

void PatchCanvas::paintBody (juce::Graphics& g, int index, const ps::Node& n, const Layout& L)
{
    using namespace ps::theme;
    const auto c = kindColour (n);
    const ps::Engine::View* v = viewValid && index < (int) view.size() ? &view[(size_t) index] : nullptr;
    for (auto& it : bodyItems (n, L.box.getX(), L.box.getY() + L.bodyTop, L.box.getWidth()))
    {
        const auto r = it.r;
        switch (it.type)
        {
            case ItemType::text:
                g.setColour (label);
                g.setFont (mono (9.5f));
                g.drawFittedText (it.text, r.toNearestInt(), juce::Justification::topLeft, r.getHeight() > 14 ? 2 : 1, 1.0f);
                break;
            case ItemType::bigText:
            {
                g.setColour (text);
                g.setFont (mono (13.0f, true));
                g.drawText (juce::String (processor.currentTempo(), 1) + " BPM", r, juce::Justification::centredLeft);
                g.setColour (muted);
                g.setFont (mono (9.5f));
                g.drawText (processor.isFollowingHost() ? "host" : "internal", r, juce::Justification::centredRight);
                break;
            }
            case ItemType::stepper:
                g.setFont (mono (10.0f));
                g.setColour (label);
                g.drawText (it.text, r, juce::Justification::centredLeft);
                g.setColour (muted);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), juce::Rectangle<float> (r.getRight() - 52, r.getY(), 14, r.getHeight()), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), juce::Rectangle<float> (r.getRight() - 12, r.getY(), 12, r.getHeight()), juce::Justification::centred);
                g.setColour (text);
                g.setFont (mono (10.0f, true));
                g.drawText (it.value, juce::Rectangle<float> (r.getRight() - 40, r.getY(), 28, r.getHeight()), juce::Justification::centred);
                break;
            case ItemType::cells:
                paintCells (g, r, patternOf (n, v), v ? v->pos : -1, c, it.key == "edit");
                break;
            case ItemType::drumRows:
            {
                const int chaos = n.get ("chaos");
                const auto pat = ps::drumMapPattern (n.get ("mx", 50), n.get ("my", 20), n.get ("dk"), n.get ("ds"), n.get ("dh"), chaos,
                                                     chaos && v ? v->loop : 0);
                const char* labels[] = { "K", "S", "H" };
                for (int k = 0; k < 3; ++k)
                {
                    const juce::Rectangle<float> row (r.getX(), r.getY() + k * 11.0f, r.getWidth(), 8);
                    g.setColour (muted);
                    g.setFont (mono (8.5f, true));
                    g.drawText (labels[k], row.withWidth (10), juce::Justification::centredLeft);
                    paintCells (g, row.withTrimmedLeft (12), std::vector<int> (pat[(size_t) k].begin(), pat[(size_t) k].end()), v ? v->pos : -1, c);
                }
                break;
            }
            case ItemType::modes:
            {
                const bool combine = it.key == "combine";
                const auto& modes = ps::combineModes();
                const int count = combine ? (int) modes.size() : 2;
                const int ways = n.get ("ways", 4);
                for (int m = 0; m < count; ++m)
                {
                    const auto b = juce::Rectangle<float> (r.getX() + r.getWidth() * m / count, r.getY(), r.getWidth() / count, r.getHeight()).reduced (1, 0);
                    const bool on = combine ? n.mode == modes[(size_t) m] : (m == 0) == (n.mode != "merge");
                    g.setColour (on ? c : cellOff);
                    g.fillRoundedRectangle (b, 3);
                    g.setColour (on ? juce::Colour (0xff121418) : label);
                    g.setFont (mono (9.5f, true));
                    const juce::String arrow = juce::String::fromUTF8 (" \xE2\x86\x92 ");
                    const auto caption = combine ? u8 (modes[(size_t) m])
                                                 : (m == 0 ? "1" + arrow + juce::String (ways) : juce::String (ways) + arrow + "1");
                    g.drawText (caption, b, juce::Justification::centred);
                }
                break;
            }
            case ItemType::switchPos:
            {
                const int ways = n.get ("ways", 4), now = v ? std::max (0, v->sel) : 0;
                for (int i = 0; i < ways; ++i)
                {
                    const auto b = juce::Rectangle<float> (r.getX() + r.getWidth() * i / ways, r.getY(), r.getWidth() / ways, r.getHeight()).reduced (1, 0);
                    const bool on = v && v->sel >= 0 && i == now;
                    g.setColour (on ? c : cellOff);
                    g.fillRoundedRectangle (b, 2);
                    g.setColour (on ? juce::Colour (0xff121418) : muted);
                    g.setFont (mono (9.0f, true));
                    g.drawText (juce::String (i + 1), b, juce::Justification::centred);
                }
                break;
            }
            case ItemType::buses:
            {
                const int bus = n.get ("bus", 1);
                for (int b = 1; b <= 8; ++b)
                {
                    const auto br = juce::Rectangle<float> (r.getX() + r.getWidth() * (b - 1) / 8.0f, r.getY(), r.getWidth() / 8.0f, r.getHeight()).reduced (1, 0);
                    g.setColour (b == bus ? c : cellOff);
                    g.fillRoundedRectangle (br, 3);
                    g.setColour (b == bus ? juce::Colour (0xff121418) : label);
                    g.setFont (mono (9.5f, true));
                    g.drawText (juce::String (b), br, juce::Justification::centred);
                }
                break;
            }
            case ItemType::meter:
            {
                g.setColour (cellOff);
                g.fillRoundedRectangle (r, 3);
                const float level = juce::jlimit (0.0f, 1.0f, processor.busLevel (n.get ("bus", 1)) * 1.6f);
                g.setColour (c);
                g.fillRoundedRectangle (r.withWidth (r.getWidth() * level), 3);
                break;
            }
            case ItemType::xy:
            {
                g.setColour (juce::Colour (0xff181b20));
                g.fillRoundedRectangle (r, 5);
                g.setColour (lineStrong);
                g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
                const auto inner = r.reduced (7);
                g.setColour (juce::Colour (0xff3d4450));
                for (int i = 0; i < 5; ++i)
                    for (int j = 0; j < 5; ++j)
                        g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre ({ inner.getX() + inner.getWidth() * i / 4, inner.getY() + inner.getHeight() * j / 4 }));
                const juce::Point<float> dot (inner.getX() + inner.getWidth() * n.get ("mx", 50) / 100.0f, inner.getY() + inner.getHeight() * n.get ("my", 20) / 100.0f);
                g.setColour (c.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (dot));
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (dot));
                break;
            }
            case ItemType::sample:
            {
                g.setColour (juce::Colour (0xff181b20));
                g.fillRoundedRectangle (r, 4);
                g.setColour (lineStrong);
                g.drawRoundedRectangle (r.reduced (0.5f), 4, 1);
                g.setColour (muted);
                g.setFont (mono (10.0f));
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), r.withWidth (16), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), r.withTrimmedLeft (r.getWidth() - 16), juce::Justification::centred);
                g.setColour (text);
                g.setFont (sans (10.5f, true));
                g.drawText (it.text, r.reduced (18, 0), juce::Justification::centred, true);
                const bool hit = viewIn (index, 0) > 0;
                g.setColour (hit ? c : cellOff);
                g.fillEllipse (r.getRight() - 24, r.getCentreY() - 3, 6, 6);
                break;
            }
        }
    }
}

// ---- view ------------------------------------------------------------------------------------

void PatchCanvas::zoomAround (float newScale, juce::Point<float> screen)
{
    newScale = juce::jlimit (minZoom, maxZoom, newScale);
    const auto world = toWorld (screen);
    scale = newScale;
    offset = screen - world * scale;
    repaint();
    if (onViewChanged)
        onViewChanged();
}

void PatchCanvas::zoomStep (int direction)
{
    static const float levels[] = { 0.4f, 0.5f, 0.67f, 0.8f, 1.0f, 1.25f, 1.5f, 2.0f };
    float next = scale;
    if (direction > 0)
    {
        for (float l : levels)
            if (l > scale + 0.01f)
            {
                next = l;
                break;
            }
    }
    else
        for (int i = 7; i >= 0; --i)
            if (levels[i] < scale - 0.01f)
            {
                next = levels[i];
                break;
            }
    zoomAround (next, getLocalBounds().getCentre().toFloat());
}

void PatchCanvas::fitView()
{
    juce::Rectangle<float> box;
    bool any = false;
    const auto& g = processor.patch();
    for (auto& n : g.nodes)
        if (onPage (n))
        {
            auto L = layoutOf (n);
            box = any ? box.getUnion (L.box.expanded (60, 20)) : L.box.expanded (60, 20);
            any = true;
        }
    for (auto& b : g.blocks)
        if (b.page.empty() || b.page == currentPage)
        {
            juce::Rectangle<float> r ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
            box = any ? box.getUnion (r) : r;
            any = true;
        }
    if (! any)
    {
        scale = 1;
        offset = {};
    }
    else
    {
        const auto area = getLocalBounds().toFloat().reduced (20, 24);
        scale = juce::jlimit (minZoom, 1.0f, std::min (area.getWidth() / box.getWidth(), area.getHeight() / box.getHeight()));
        offset = area.getCentre() - box.getCentre() * scale;
    }
    repaint();
    if (onViewChanged)
        onViewChanged();
}

// ---- mouse -----------------------------------------------------------------------------------

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    finishTagEdit (true);
    dragStart = e.position;
    dragWorld = toWorld (e.position);
    dragMoved = false;
    drag = Drag::none;

    pressed = hitTest (dragWorld);
    menuPress = e.mods.isPopupMenu();
    auto& patch = processor.patch();

    // the middle and right buttons pan from anywhere; a right-click that doesn't move opens a module's menu on release
    if (e.mods.isMiddleButtonDown() || menuPress)
    {
        drag = Drag::pan;
        panStart = offset;
        return;
    }

    if (pressed.what != Hit::wire)
        selectedWire = -1;

    switch (pressed.what)
    {
        case Hit::inPort:
        case Hit::outPort:
            drag = Drag::wire;
            break;
        case Hit::inTag:
        case Hit::outTag:
            break; // tags open for editing on release
        case Hit::zone:
            if (pressed.zone.type == ZoneType::xy)
            {
                drag = Drag::xy;
                applyZone (pressed.node, pressed.zone, dragWorld);
            }
            else
                applyZone (pressed.node, pressed.zone, dragWorld);
            break;
        case Hit::header:
        case Hit::body:
        {
            auto& n = patch.nodes[(size_t) pressed.node];
            const auto& id = n.id;
            if (pressed.what == Hit::header && dragWorld.x < (float) n.x + 20)
            {
                n.collapsed = ! n.collapsed; // the chevron
                edited();
                return;
            }
            if (e.mods.isShiftDown())
            {
                if (! selection.erase (id))
                    selection.insert (id);
            }
            else if (! selection.count (id))
                selection = { id };
            if (pressed.what == Hit::header)
            {
                drag = Drag::move;
                moveStart.clear();
                for (auto& m : patch.nodes)
                    if (selection.count (m.id))
                        moveStart.push_back ({ m.id, { (float) m.x, (float) m.y } });
            }
            break;
        }
        case Hit::wire:
            selectedWire = pressed.wire;
            selection.clear();
            break;
        case Hit::blockTitle:
        {
            drag = Drag::block;
            dragBlock = pressed.block;
            const auto& b = patch.blocks[(size_t) dragBlock];
            blockStart = { (float) b.x, (float) b.y };
            const juce::Rectangle<float> area ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
            moveStart.clear();
            for (auto& n : patch.nodes)
                if (onPage (n) && area.contains (layoutOf (n).box.getTopLeft()))
                    moveStart.push_back ({ n.id, { (float) n.x, (float) n.y } });
            break;
        }
        case Hit::none:
            boxBase = e.mods.isShiftDown() ? selection : std::set<std::string> {};
            selection = boxBase;
            drag = Drag::box;
            break;
    }
    repaint();
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (dragStart) > 3.0f)
        dragMoved = true;
    dragWorld = toWorld (e.position);
    auto& patch = processor.patch();
    const auto delta = (e.position - dragStart) / scale;
    switch (drag)
    {
        case Drag::pan:
            offset = panStart + (e.position - dragStart);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        case Drag::move:
        case Drag::block:
            if (drag == Drag::block && dragBlock >= 0 && dragBlock < (int) patch.blocks.size())
            {
                patch.blocks[(size_t) dragBlock].x = std::round (blockStart.x + delta.x);
                patch.blocks[(size_t) dragBlock].y = std::round (blockStart.y + delta.y);
            }
            for (auto& [id, start] : moveStart)
                if (auto* n = patch.find (id))
                {
                    n->x = std::round (start.x + delta.x);
                    n->y = std::round (start.y + delta.y);
                }
            break;
        case Drag::xy:
            applyZone (pressed.node, pressed.zone, dragWorld);
            break;
        case Drag::box:
        {
            const auto area = juce::Rectangle<float> (toWorld (dragStart), dragWorld);
            selection = boxBase;
            for (auto& n : patch.nodes)
                if (onPage (n) && layoutOf (n).box.intersects (area))
                    selection.insert (n.id);
            break;
        }
        case Drag::wire:
        case Drag::none:
            break;
    }
    repaint();
}

void PatchCanvas::mouseUp (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    switch (drag)
    {
        case Drag::move:
        case Drag::block:
            if (dragMoved)
                edited();
            break;
        case Drag::wire:
            if (dragMoved)
            {
                const auto target = hitTest (dragWorld);
                const bool fromOut = pressed.what == Hit::outPort;
                if (fromOut && (target.what == Hit::inPort || target.what == Hit::inTag))
                    connect (pressed.node, pressed.port, target.node, target.port);
                else if (! fromOut && (target.what == Hit::outPort || target.what == Hit::outTag))
                    connect (target.node, target.port, pressed.node, pressed.port);
                else if (target.what == Hit::inPort || target.what == Hit::outPort)
                    showMessage ("Connect an output to an input");
            }
            else
                editTag (pressed.node, pressed.what == Hit::outPort, pressed.port);
            break;
        case Drag::xy:
            edited();
            break;
        case Drag::pan:
            if (! dragMoved && menuPress && pressed.node >= 0)
            {
                const auto& id = processor.patch().nodes[(size_t) pressed.node].id;
                if (! selection.count (id))
                    selection = { id };
                drag = Drag::none;
                repaint();
                showNodeMenu (pressed.node);
                return;
            }
            break;
        case Drag::box:
            break;
        case Drag::none:
            if (! dragMoved && (pressed.what == Hit::inTag || pressed.what == Hit::outTag))
                editTag (pressed.node, pressed.what == Hit::outTag, pressed.port);
            break;
    }
    drag = Drag::none;
    repaint();
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTest (toWorld (e.position));
    if (hit.what == Hit::header)
    {
        auto& n = processor.patch().nodes[(size_t) hit.node];
        n.collapsed = ! n.collapsed;
        edited();
    }
}

void PatchCanvas::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const auto hit = hitTest (toWorld (e.position));
    const bool point = hit.what == Hit::zone || hit.what == Hit::inPort || hit.what == Hit::outPort || hit.what == Hit::inTag
                       || hit.what == Hit::outTag || hit.what == Hit::wire;
    setMouseCursor (point ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PatchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (scale * std::pow (1.0016f, wheel.deltaY * 300.0f), e.position);
        return;
    }
    offset += juce::Point<float> (wheel.deltaX, wheel.deltaY) * 300.0f;
    repaint();
}

void PatchCanvas::mouseMagnify (const juce::MouseEvent& e, float factor) { zoomAround (scale * factor, e.position); }

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = [&key] (char c) { return key == juce::KeyPress (c, juce::ModifierKeys::commandModifier, 0); };
    if (cmd ('c'))
    {
        copySelection();
        return true;
    }
    if (cmd ('x'))
    {
        copySelection();
        deleteSelection();
        return true;
    }
    if (cmd ('v'))
    {
        pasteClipboard();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
    {
        zoomStep (1);
        return true;
    }
    if (key.getTextCharacter() == '-')
    {
        zoomStep (-1);
        return true;
    }
    if (key.getTextCharacter() == '0')
    {
        zoomAround (1.0f, getLocalBounds().getCentre().toFloat());
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        selection.clear();
        selectedWire = -1;
        repaint();
        return true;
    }
    return false;
}

// ---- editing ---------------------------------------------------------------------------------

void PatchCanvas::applyZone (int index, const Zone& z, juce::Point<float> world)
{
    auto& patch = processor.patch();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    auto& n = patch.nodes[(size_t) index];
    selection = { n.id };
    bool changed = false;
    switch (z.type)
    {
        case ZoneType::param:
            changed = ps::stepParam (patch, n, z.key, z.value);
            break;
        case ZoneType::mode:
            if (z.key == "combine")
                n.mode = ps::combineModes()[(size_t) z.value];
            else
            {
                n.mode = z.value == 0 ? "split" : "merge";
                ps::prunePorts (patch, n);
            }
            changed = true;
            break;
        case ZoneType::bus:
            n.set ("bus", z.value);
            changed = true;
            break;
        case ZoneType::cell:
            if (z.value < (int) n.cells.size())
            {
                n.cells[(size_t) z.value] = n.cells[(size_t) z.value] ? 0 : 1;
                changed = true;
            }
            break;
        case ZoneType::sample:
        {
            const auto& names = ps::sampleNames();
            auto it = std::find (names.begin(), names.end(), n.sample);
            int i = it == names.end() ? 0 : (int) (it - names.begin());
            i = (i + z.value + (int) names.size()) % (int) names.size();
            n.sample = names[(size_t) i];
            changed = true;
            break;
        }
        case ZoneType::laneNote:
        {
            const int q = std::abs (z.value) - 1;
            if (q < (int) n.lanes.size())
            {
                auto& lane = n.lanes[(size_t) q];
                const int before = lane.note;
                lane.note = juce::jlimit (0, 127, lane.note + (z.value > 0 ? 1 : -1));
                // A lane still named after its GM drum follows the note.
                if (auto* old = ps::gmDrumName (before); old && lane.name == old)
                    if (auto* now = ps::gmDrumName (lane.note))
                        lane.name = now;
                changed = lane.note != before;
            }
            break;
        }
        case ZoneType::xy:
        {
            const auto inner = z.r.reduced (7);
            const int mx = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (world.x - inner.getX()) / inner.getWidth()) * 100);
            const int my = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (world.y - inner.getY()) / inner.getHeight()) * 100);
            changed = mx != n.get ("mx") || my != n.get ("my");
            n.set ("mx", mx);
            n.set ("my", my);
            break;
        }
    }
    if (changed)
        edited();
}

void PatchCanvas::connect (int fromNode, int fromPort, int toNode, int toPort)
{
    auto& patch = processor.patch();
    const auto& a = patch.nodes[(size_t) fromNode];
    const auto& b = patch.nodes[(size_t) toNode];
    if (fromNode == toNode)
        return;
    if (ps::outputType (a, fromPort) != ps::inputType (b, toPort))
    {
        showMessage ("Audio ports only connect to audio ports");
        return;
    }
    // An input takes one wire: a new one replaces the old.
    patch.links.erase (std::remove_if (patch.links.begin(), patch.links.end(),
                                       [&] (const ps::Link& l) { return l.b == b.id && l.q == toPort; }),
                       patch.links.end());
    patch.links.push_back ({ a.id, fromPort, b.id, toPort });
    edited();
}

void PatchCanvas::deleteSelection()
{
    auto& patch = processor.patch();
    if (selectedWire >= 0 && selectedWire < (int) patch.links.size())
    {
        patch.links.erase (patch.links.begin() + selectedWire);
        selectedWire = -1;
        edited();
        return;
    }
    if (selection.empty())
        return;
    const auto count = selection.size();
    patch.nodes.erase (std::remove_if (patch.nodes.begin(), patch.nodes.end(), [this] (const ps::Node& n) { return selection.count (n.id) > 0; }),
                       patch.nodes.end());
    patch.links.erase (std::remove_if (patch.links.begin(), patch.links.end(),
                                       [this] (const ps::Link& l) { return selection.count (l.a) || selection.count (l.b); }),
                       patch.links.end());
    selection.clear();
    showMessage ("Removed " + juce::String ((int) count) + (count == 1 ? " module" : " modules"));
    edited();
}

namespace
{
    // Shared by every open editor, so modules can be copied from one plugin instance into another.
    struct Clipboard
    {
        std::vector<ps::Node> nodes;
        std::vector<ps::Link> links; // only the wires between copied modules
    };
    Clipboard& clipboard()
    {
        static Clipboard c;
        return c;
    }
} // namespace

void PatchCanvas::copySelection()
{
    const auto& patch = processor.patch();
    auto& clip = clipboard();
    if (selection.empty())
        return;
    clip.nodes.clear();
    clip.links.clear();
    for (const auto& n : patch.nodes)
        if (selection.count (n.id))
            clip.nodes.push_back (n);
    for (const auto& l : patch.links)
        if (selection.count (l.a) && selection.count (l.b))
            clip.links.push_back (l);
    pasteCount = 0;
    const auto count = (int) clip.nodes.size();
    showMessage ("Copied " + juce::String (count) + (count == 1 ? " module" : " modules"));
}

void PatchCanvas::pasteClipboard()
{
    const auto& clip = clipboard();
    if (clip.nodes.empty())
        return;
    auto& patch = processor.patch();

    // Land the copy's top-left corner at the mouse, or a step down and right of the original.
    juce::Point<float> topLeft { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    for (const auto& n : clip.nodes)
        topLeft = { std::min (topLeft.x, (float) n.x), std::min (topLeft.y, (float) n.y) };
    juce::Point<float> shift;
    if (isMouseOver())
        shift = toWorld (lastMouse) - topLeft;
    else
    {
        ++pasteCount;
        shift = { 30.0f * (float) pasteCount, 30.0f * (float) pasteCount };
    }

    std::map<std::string, std::string> ids;
    selection.clear();
    for (auto n : clip.nodes)
    {
        const auto old = n.id;
        n.id = patch.freshId (n.kind.substr (0, std::min<size_t> (2, n.kind.size())));
        n.x = std::round (n.x + shift.x);
        n.y = std::round (n.y + shift.y);
        n.page = currentPage;
        n.tagOut.clear(); // each tag has one source, so the copy's outputs start untagged; its inputs keep listening
        ids[old] = n.id;
        selection.insert (n.id);
        patch.nodes.push_back (std::move (n));
    }
    for (auto l : clip.links)
    {
        l.a = ids[l.a];
        l.b = ids[l.b];
        patch.links.push_back (l);
    }
    selectedWire = -1;
    const auto count = (int) clip.nodes.size();
    showMessage ("Pasted " + juce::String (count) + (count == 1 ? " module" : " modules"));
    edited();
}

void PatchCanvas::addModule (const std::string& kind)
{
    auto& patch = processor.patch();
    const auto centre = toWorld (getLocalBounds().getCentre().toFloat());
    auto prefix = kind.substr (0, std::min<size_t> (2, kind.size()));
    auto n = ps::makeNode (kind, patch.freshId (prefix), std::round (centre.x - 75), std::round (centre.y - 60));
    n.page = currentPage;
    selection = { n.id };
    patch.nodes.push_back (n);
    edited();
}

void PatchCanvas::addSubsystem (const ps::presets::SubsystemTemplate& t)
{
    auto& patch = processor.patch();
    const auto centre = toWorld (getLocalBounds().getCentre().toFloat());
    auto n = t.node;
    n.id = patch.freshId ("sub");
    n.x = std::round (centre.x - 80);
    n.y = std::round (centre.y - 60);
    n.page = currentPage;
    selection = { n.id };
    patch.nodes.push_back (n);
    edited();
}

void PatchCanvas::collapseAll (bool collapse)
{
    for (auto& n : processor.patch().nodes)
        if (onPage (n))
            n.collapsed = collapse;
    edited();
}

void PatchCanvas::moveSelectionToPage (const std::string& pageId)
{
    auto& patch = processor.patch();
    if (selection.empty() || pageId == currentPage)
        return;
    // Wires between moved modules and ones left behind become tags, so the patch keeps playing.
    int converted = 0;
    for (auto it = patch.links.begin(); it != patch.links.end();)
    {
        const bool aMoves = selection.count (it->a) > 0, bMoves = selection.count (it->b) > 0;
        if (aMoves == bMoves)
        {
            ++it;
            continue;
        }
        auto* a = patch.find (it->a);
        auto* b = patch.find (it->b);
        if (! a || ! b)
        {
            ++it;
            continue;
        }
        auto tag = a->tagOut.count (it->p) ? a->tagOut[it->p] : std::string();
        if (tag.empty())
        {
            auto base = juce::String (u8 (ps::titleOf (*a))).toUpperCase().removeCharacters (" -").substring (0, 10).toStdString();
            tag = base;
            for (int i = 2; std::any_of (patch.nodes.begin(), patch.nodes.end(), [&] (auto& m) {
                     return std::any_of (m.tagOut.begin(), m.tagOut.end(), [&] (auto& kv) { return kv.second == tag; });
                 });
                 ++i)
                tag = base + std::to_string (i);
            a->tagOut[it->p] = tag;
        }
        b->tagIn[it->q] = tag;
        it = patch.links.erase (it);
        ++converted;
    }
    for (auto& n : patch.nodes)
        if (selection.count (n.id))
            n.page = pageId;
    const auto count = selection.size();
    selection.clear();
    showMessage ("Moved " + juce::String ((int) count) + (count == 1 ? " module" : " modules")
                 + (converted ? ", and " + juce::String (converted) + (converted == 1 ? " wire became a tag" : " wires became tags") : juce::String()));
    edited();
    if (onPagesChanged)
        onPagesChanged();
}

void PatchCanvas::showNodeMenu (int index)
{
    auto& patch = processor.patch();
    const auto& n = patch.nodes[(size_t) index];
    juce::PopupMenu menu, pages;
    for (auto& p : patch.pages)
        pages.addItem (u8 (p.name), p.id != currentPage, false, [this, id = p.id] { moveSelectionToPage (id); });
    menu.addItem (n.collapsed ? "Expand" : "Collapse", [this, id = n.id] {
        if (auto* m = processor.patch().find (id))
        {
            m->collapsed = ! m->collapsed;
            edited();
        }
    });
    menu.addSubMenu ("Move to tab", pages, patch.pages.size() > 1);
    if (n.kind == "sub" && onExportSubsystem)
        menu.addItem ("Save subsystem...", [this, id = n.id] {
            if (auto* m = processor.patch().find (id))
                onExportSubsystem (*m);
        });
    menu.addSeparator();
    menu.addItem (selection.size() > 1 ? "Delete selected modules" : "Delete", [this] { deleteSelection(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PatchCanvas::showMessage (const juce::String& text)
{
    message = text;
    messageUntil = juce::Time::getMillisecondCounter() + 2600;
    repaint();
}

// ---- tags ------------------------------------------------------------------------------------

void PatchCanvas::editTag (int index, bool output, int port)
{
    auto& patch = processor.patch();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    const auto& n = patch.nodes[(size_t) index];
    const auto L = layoutOf (n);
    if (port < 0 || port >= (int) (output ? L.outs.size() : L.ins.size()))
        return;
    tagNode = n.id;
    tagOutput = output;
    tagPort = port;

    if (! tagEditor)
    {
        tagEditor = std::make_unique<juce::TextEditor>();
        auto& ed = *tagEditor;
        ed.setFont (ps::theme::mono (12.0f, true));
        ed.setInputRestrictions (16);
        ed.setColour (juce::TextEditor::backgroundColourId, ps::theme::panel);
        ed.setColour (juce::TextEditor::textColourId, ps::theme::text);
        ed.setColour (juce::TextEditor::outlineColourId, ps::theme::selected);
        ed.setColour (juce::TextEditor::focusedOutlineColourId, ps::theme::selected);
        ed.onReturnKey = [this] { finishTagEdit (true); };
        ed.onEscapeKey = [this] { finishTagEdit (false); };
        ed.onFocusLost = [this] { finishTagEdit (true); };
        addChildComponent (ed);
    }
    auto& ed = *tagEditor;
    const auto& tags = output ? n.tagOut : n.tagIn;
    const auto it = tags.find (port);
    ed.setTextToShowWhenEmpty (output ? "Name this output" : "Tag to listen to", ps::theme::muted);
    ed.setText (it != tags.end() ? u8 (it->second) : juce::String(), false);
    const auto p = toScreen (output ? L.outs[(size_t) port] : L.ins[(size_t) port]);
    const int w = 150, h = 24;
    juce::Rectangle<int> r (output ? juce::roundToInt (p.x) + 12 : juce::roundToInt (p.x) - 12 - w, juce::roundToInt (p.y) - h / 2, w, h);
    ed.setBounds (r.constrainedWithin (getLocalBounds().reduced (4)));
    tagEditing = true;
    ed.setVisible (true);
    ed.selectAll();
    ed.grabKeyboardFocus();
    showMessage (output ? "Inputs with the same tag receive this output without a wire. Leave it empty to remove the tag."
                        : "This input receives the output that owns the tag. Leave it empty to remove the tag.");
}

void PatchCanvas::finishTagEdit (bool keep)
{
    if (! tagEditing)
        return;
    tagEditing = false; // hiding the editor moves focus, which calls this again
    const auto text = tagEditor->getText();
    tagEditor->setVisible (false);
    grabKeyboardFocus();
    if (! keep)
        return;
    auto& patch = processor.patch();
    auto* n = patch.find (tagNode);
    if (! n)
        return;
    const auto tag = text.trim().toUpperCase().replaceCharacter (' ', '_').toStdString();
    auto& tags = tagOutput ? n->tagOut : n->tagIn;
    const auto old = tags.count (tagPort) ? tags[tagPort] : std::string();
    if (tag == old)
        return;
    if (tag.empty())
        tags.erase (tagPort);
    else
    {
        if (tagOutput)
            for (auto& m : patch.nodes)
                for (auto& [p, t] : m.tagOut)
                    if (t == tag && ! (m.id == n->id && p == tagPort))
                    {
                        showMessage (u8 (tag) + " already belongs to " + u8 (ps::titleOf (m)) + ". One output per tag.");
                        return;
                    }
        tags[tagPort] = tag;
    }
    edited();
}
