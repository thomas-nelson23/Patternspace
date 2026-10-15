#include "PatchCanvas.h"

#include "../core/Modules.h"
#include "../core/Patterns.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr float headerH = 26.0f, ioTop = 30.0f, rowH = 18.0f, padX = 9.0f, stepperH = 16.0f, gapY = 3.0f;
constexpr float minZoom = 0.4f, maxZoom = 2.0f;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

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
    switchPos,
    button
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
    subPath.clear();
    savedViews.clear();
    if (! processor.isRestoringHistory())
        pageViews.clear();
    processor.setViewPath (subPath);
    const auto& pages = processor.patch().pages;
    const bool stillThere = std::any_of (pages.begin(), pages.end(), [this] (auto& pg) { return pg.id == currentPage; });
    if (! stillThere)
        currentPage = pages.empty() ? std::string() : pages.front().id;
    viewValid = false;
    repaint();
}

void PatchCanvas::setPage (const std::string& id)
{
    if (id == currentPage && subPath.empty())
        return;
    finishTagEdit (true);
    closeSubsystem();
    pageViews[currentPage] = { scale, offset };
    currentPage = id;
    if (auto it = pageViews.find (id); it != pageViews.end())
    {
        scale = it->second.scale;
        offset = it->second.offset;
        if (onViewChanged)
            onViewChanged();
    }
    selection.clear();
    selectedWire = -1;
    repaint();
}

bool PatchCanvas::onPage (const ps::Node& n) const { return n.page.empty() || n.page == currentPage; }

// ---- subsystems ------------------------------------------------------------------------------

const ps::Graph& PatchCanvas::graph() const
{
    const ps::Graph* g = &processor.patch();
    for (auto& id : subPath)
    {
        const auto* n = g->find (id);
        if (! n || n->kind != "sub" || n->inner.empty())
            break;
        g = &n->inner.front();
    }
    return *g;
}

ps::Graph& PatchCanvas::graph() { return const_cast<ps::Graph&> (std::as_const (*this).graph()); }

bool PatchCanvas::pathIsValid() const
{
    const ps::Graph* g = &processor.patch();
    for (auto& id : subPath)
    {
        const auto* n = g->find (id);
        if (! n || n->kind != "sub" || n->inner.empty())
            return false;
        g = &n->inner.front();
    }
    return true;
}

void PatchCanvas::openSubsystem (const std::string& id)
{
    auto* n = graph().find (id);
    if (n == nullptr || n->kind != "sub")
        return;
    finishTagEdit (true);
    if (n->inner.empty())
        n->inner.emplace_back();
    savedViews.push_back ({ scale, offset });
    subPath.push_back (id);
    selection.clear();
    selectedWire = -1;
    hoverNode = -1;
    drag = Drag::none;
    processor.setViewPath (subPath);
    viewValid = false;
    fitView();
    if (onLevelChanged)
        onLevelChanged();
}

void PatchCanvas::closeSubsystem (size_t depth)
{
    if (depth >= subPath.size())
        return;
    finishTagEdit (true);
    // Back out to the subsystem that was opened at `depth`, cyan so it's easy to spot.
    const auto reopened = subPath[depth];
    scale = savedViews[depth].scale;
    offset = savedViews[depth].offset;
    subPath.resize (depth);
    savedViews.resize (depth);
    selection = { reopened };
    selectedWire = -1;
    hoverNode = -1;
    drag = Drag::none;
    processor.setViewPath (subPath);
    viewValid = false;
    repaint();
    if (onViewChanged)
        onViewChanged();
    if (onLevelChanged)
        onLevelChanged();
}

ps::Node* PatchCanvas::openNode (ps::Graph** parent)
{
    if (subPath.empty())
        return nullptr;
    ps::Graph* g = &processor.patch();
    ps::Node* n = nullptr;
    for (auto& id : subPath)
    {
        if (n != nullptr)
            g = &n->inner.front();
        n = g->find (id);
        if (n == nullptr || n->kind != "sub" || n->inner.empty())
            return nullptr;
    }
    if (parent != nullptr)
        *parent = g;
    return n;
}

void PatchCanvas::newSubsystem()
{
    auto& patch = graph();
    juce::String name = "New subsystem";
    for (int i = 2; std::any_of (patch.nodes.begin(), patch.nodes.end(), [&] (auto& m) { return m.kind == "sub" && u8 (m.name) == name; }); ++i)
        name = "New subsystem " + juce::String (i);
    const auto centre = toWorld (getLocalBounds().getCentre().toFloat());
    auto n = ps::makeSubsystem (patch.freshId ("sub"), name.toStdString(), std::round (centre.x - 80), std::round (centre.y - 60));
    n.page = subPath.empty() ? currentPage : std::string();
    const auto id = n.id;
    patch.nodes.push_back (n);
    edited();
    openSubsystem (id);
    showMessage ("Add inputs and outputs with the + buttons, then add modules from the library and wire them up.");
}

void PatchCanvas::addPort (bool output)
{
    ps::Graph* parent = nullptr;
    auto* sub = openNode (&parent);
    if (sub == nullptr)
        return;
    const auto existing = output ? ps::outputsOf (*sub) : ps::inputsOf (*sub);
    std::string name;
    for (int i = (int) existing.size() + 1;; ++i)
    {
        name = (output ? "out " : "in ") + std::to_string (i);
        if (std::find (existing.begin(), existing.end(), name) == existing.end())
            break;
    }
    ps::addSubsystemPort (*sub, output, name, ps::PortType::gate);
    edited();
    const auto& io = graph();
    for (int i = 0; i < (int) io.nodes.size(); ++i)
        if (io.nodes[(size_t) i].kind == (output ? "subout" : "subin"))
            editPortName (i, (int) existing.size());
}

void PatchCanvas::showPortMenu (int index, int port)
{
    const auto& io = graph().nodes[(size_t) index];
    const bool output = io.kind == "subout"; // the subsystem's outputs are the Outputs module's inputs
    const auto type = output ? ps::inputType (io, port) : ps::outputType (io, port);
    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ((output ? ps::inputsOf (io) : ps::outputsOf (io))[(size_t) port]));
    menu.addItem ("Rename", [this, index, port] { editPortName (index, port); });
    auto setType = [this, output, port] (ps::PortType t) {
        ps::Graph* parent = nullptr;
        if (auto* sub = openNode (&parent))
        {
            ps::setSubsystemPortType (*parent, *sub, output, port, t);
            edited();
        }
    };
    menu.addItem ("Gate", true, type == ps::PortType::gate, [setType] { setType (ps::PortType::gate); });
    menu.addItem ("Audio", true, type == ps::PortType::audio, [setType] { setType (ps::PortType::audio); });
    menu.addSeparator();
    menu.addItem (output ? "Remove output" : "Remove input", [this, output, port] {
        ps::Graph* parent = nullptr;
        if (auto* sub = openNode (&parent))
        {
            ps::removeSubsystemPort (*parent, *sub, output, port);
            edited();
        }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

std::vector<std::pair<juce::String, juce::Rectangle<float>>> PatchCanvas::crumbs() const
{
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> out;
    if (subPath.empty())
        return out;
    juce::StringArray names;
    const auto& root = processor.patch();
    auto page = std::find_if (root.pages.begin(), root.pages.end(), [this] (auto& p) { return p.id == currentPage; });
    names.add (juce::String::fromUTF8 ("\xE2\x97\x80 ") + (page != root.pages.end() ? u8 (page->name) : juce::String ("Patch")));
    const ps::Graph* g = &root;
    for (auto& id : subPath)
    {
        const auto* n = g->find (id);
        if (! n || n->inner.empty())
            break;
        names.add (n->name.empty() ? juce::String ("Subsystem") : u8 (n->name));
        g = &n->inner.front();
    }
    const auto font = ps::theme::sans (12.5f);
    float x = 18;
    for (int i = 0; i < names.size(); ++i)
    {
        const float w = juce::GlyphArrangement::getStringWidth (font, names[i]) + 18;
        out.push_back ({ names[i], { x, 14, w, 22 } });
        x += w + 14; // room for the separator
    }
    return out;
}

void PatchCanvas::paintCrumbs (juce::Graphics& g)
{
    using namespace ps::theme;
    const auto items = crumbs();
    if (items.empty())
        return;
    const auto bar = items.front().second.getUnion (items.back().second).expanded (4);
    g.setColour (menu);
    g.fillRoundedRectangle (bar, bar.getHeight() / 2);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (bar.reduced (0.5f), bar.getHeight() / 2, 1);
    g.setFont (sans (12.5f));
    const auto mouse = getMouseXYRelative().toFloat();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& [name, r] = items[i];
        const bool current = i + 1 == items.size();
        if (! current && r.contains (mouse) && isMouseOver())
        {
            g.setColour (wash);
            g.fillRoundedRectangle (r, r.getHeight() / 2);
        }
        g.setColour (current ? textBright : textSoft);
        g.drawText (name, r, juce::Justification::centred, false);
        if (! current)
        {
            g.setColour (textDim);
            g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), juce::Rectangle<float> (r.getRight(), r.getY(), 14, r.getHeight()), juce::Justification::centred, false);
        }
    }
}

void PatchCanvas::resized() {}

juce::String PatchCanvas::viewStateToString() const
{
    auto* o = new juce::DynamicObject();
    const juce::var v (o);
    o->setProperty ("page", u8 (currentPage));
    auto* views = new juce::DynamicObject();
    auto record = [&] (const std::string& page, const SavedView& view) {
        auto* pv = new juce::DynamicObject();
        pv->setProperty ("s", view.scale);
        pv->setProperty ("x", view.offset.x);
        pv->setProperty ("y", view.offset.y);
        views->setProperty (u8 (page), juce::var (pv));
    };
    for (auto& [page, view] : pageViews)
        if (page != currentPage)
            record (page, view);
    if (subPath.empty())
        record (currentPage, { scale, offset });
    else if (! savedViews.empty())
        record (currentPage, savedViews.front());
    o->setProperty ("views", juce::var (views));
    return juce::JSON::toString (v, true);
}

void PatchCanvas::restoreViewState (const juce::String& state)
{
    juce::var v;
    if (state.isEmpty() || juce::JSON::parse (state, v).failed() || ! v.isObject())
        return;
    pageViews.clear();
    if (auto* views = v["views"].getDynamicObject())
        for (auto& p : views->getProperties())
        {
            const auto& pv = p.value;
            if (pv.isObject())
                pageViews[p.name.toString().toStdString()] = { (float) (double) pv["s"], { (float) (double) pv["x"], (float) (double) pv["y"] } };
        }
    const auto page = v["page"].toString().toStdString();
    const auto& pages = processor.patch().pages;
    if (std::any_of (pages.begin(), pages.end(), [&] (auto& pg) { return pg.id == page; }))
    {
        closeSubsystem();
        currentPage = page;
    }
    if (auto it = pageViews.find (currentPage); it != pageViews.end())
    {
        scale = juce::jlimit (minZoom, maxZoom, it->second.scale);
        offset = it->second.offset;
    }
    if (onViewChanged)
        onViewChanged();
    repaint();
}

int PatchCanvas::settingOf (const char* key) const
{
    const auto& settings = processor.patch().settings;
    const auto it = settings.find (key);
    return it == settings.end() ? 0 : it->second;
}

void PatchCanvas::timerCallback()
{
    if (! pathIsValid())
        closeSubsystem(); // the subsystem being shown was removed
    viewValid = processor.copyView (view) || viewValid;
    processor.decayLevels();
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil)
        message = {};
    repaint();
}

void PatchCanvas::edited (const juce::String& undoGroup)
{
    processor.commitPatch (undoGroup);
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
                L.zones.push_back ({ { it.r.getX(), it.r.getY(), it.r.getWidth() - 52, it.r.getHeight() }, ZoneType::paramValue, it.key, 0 });
                L.zones.push_back ({ { it.r.getRight() - 38, it.r.getY(), 26, it.r.getHeight() }, ZoneType::paramValue, it.key, 0 });
                break;
            case ItemType::cells:
                if (it.key == "edit")
                    for (int c = 0; c < 16; ++c)
                        L.zones.push_back ({ { it.r.getX() + it.r.getWidth() * c / 16.0f, it.r.getY() - 2, it.r.getWidth() / 16.0f, it.r.getHeight() + 4 }, ZoneType::cell, {}, c });
                break;
            case ItemType::modes:
            {
                const int count = it.key == "combine" ? (int) ps::combineModes().size() : it.key == "follow" ? 3 : 2;
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
            case ItemType::button:
                L.zones.push_back ({ it.r, it.key == "open" ? ZoneType::open : it.key == "reroll" ? ZoneType::reroll : ZoneType::addPort, {}, 0 });
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
            L.zones.push_back ({ { x + w - 48, ry, 22, rowH }, ZoneType::paramValue, "lane" + std::to_string (q), 0 });
        }
    L.box = { x, y, w, bottom - y + 9 };
    L.header = { x, y, w, headerH };
    return L;
}

float PatchCanvas::bodyHeight (const ps::Node& n) const { return layoutOf (n).box.getHeight(); }

juce::Rectangle<float> PatchCanvas::tagRect (const ps::Node&, const Layout& L, bool output, int port, const std::string& tag) const
{
    const float w = juce::GlyphArrangement::getStringWidth (ps::theme::mono (9.5f), u8 (tag)) + 16;
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
    const auto& g = graph();
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
    if (backdrop.getWidth() != getWidth() || backdrop.getHeight() != getHeight())
        renderBackdrop();
    g.drawImageAt (backdrop, 0, 0);

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
        const auto& nodes = graph().nodes;
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
        g.setColour (cyanSoft);
        g.fillRect (area);
        g.setColour (cyan.withAlpha (0.45f));
        g.drawRect (area, 1.0f);
    }

    paintCrumbs (g);
    if (helpShown)
    {
        paintHelp (g);
        return;
    }

    const auto idle = subPath.empty()
                          ? juce::String ("Drag empty space to select, right-drag to pan, Ctrl/Cmd-scroll to zoom. Drag an output onto an input to wire it, click a port to tag it.")
                          : juce::String ("Inside a subsystem: edits change this copy only. Esc or the path at the top left goes back out.");
    const auto hint = message.isNotEmpty() ? message : idle;
    g.setFont (sans (11.0f).withExtraKerningFactor (0.03f));
    auto hintArea = getLocalBounds().reduced (14, 12).removeFromBottom (24).toFloat();
    hintArea = hintArea.withWidth (std::min (hintArea.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 24));
    g.setColour (bg.withAlpha (0.85f));
    g.fillRoundedRectangle (hintArea, hintArea.getHeight() / 2);
    if (message.isNotEmpty())
    {
        g.setColour (glassEdge);
        g.drawRoundedRectangle (hintArea.reduced (0.5f), hintArea.getHeight() / 2, 1.0f);
    }
    g.setColour (message.isNotEmpty() ? textSoft : textDim);
    g.drawText (hint, hintArea.reduced (12, 0), juce::Justification::centredLeft, true);
}

// The shortcuts, on a glass panel over the workspace. ? or F1 toggles it; any key or click closes it.
void PatchCanvas::paintHelp (juce::Graphics& g)
{
    using namespace ps::theme;
    struct Row
    {
        const char* keys;
        const char* what;
    };
    static const Row rows[] = {
        { "Drag empty space", "Select modules in a box (Shift adds)" },
        { "Right or middle drag", "Pan" },
        { "Ctrl/Cmd scroll, pinch", "Zoom. + - 0 step and reset it" },
        { "Drag output to input", "Wire. Drag a wired input to move or remove the wire" },
        { "Click a port", "Give it a tag" },
        { "Drag a value", "Change it. Shift is finer; scroll over it; double-click to type" },
        { "Double-click a header", "Collapse the module. Double-click a subsystem to open it" },
        { "Right-click", "Module actions, or add a module on empty space" },
        { "Ctrl/Cmd Z, Shift-Z", "Undo, redo" },
        { "Ctrl/Cmd C, X, V, D", "Copy, cut, paste at the mouse, duplicate" },
        { "Ctrl/Cmd A", "Select every module on the tab" },
        { "Arrows, Shift arrows", "Nudge the selection 1 or 10 px" },
        { "Delete, Backspace", "Remove the selection or the chosen wire" },
        { "Esc", "Clear the selection, or leave a subsystem" },
        { "?", "This list" },
    };
    const float rowHeight = 22.0f;
    const auto panel = juce::Rectangle<float> (0, 0, 560, 56 + rowHeight * (float) std::size (rows)).withCentre (getLocalBounds().getCentre().toFloat());
    g.setColour (bg.withAlpha (0.6f));
    g.fillAll();
    drawPanel (g, panel, glassEdge);
    g.setColour (textSoft);
    g.setFont (caps (10.0f, 0.2f));
    g.drawText ("KEYBOARD AND MOUSE", panel.reduced (24, 0).withHeight (40), juce::Justification::centredLeft);
    float y = panel.getY() + 44;
    for (auto& row : rows)
    {
        g.setColour (textBright);
        g.setFont (mono (10.5f, true));
        g.drawText (row.keys, juce::Rectangle<float> (panel.getX() + 24, y, 190, rowHeight), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (sans (11.5f));
        g.drawText (row.what, juce::Rectangle<float> (panel.getX() + 220, y, panel.getWidth() - 244, rowHeight), juce::Justification::centredLeft, true);
        y += rowHeight;
    }
}

void PatchCanvas::paintBlocks (juce::Graphics& g)
{
    for (auto& b : graph().blocks)
    {
        if (! b.page.empty() && b.page != currentPage)
            continue;
        const auto c = ps::theme::fromHex (b.colour, ps::theme::textDim);
        const juce::Rectangle<float> r ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
        // A section: a faint wash of its colour and a hairline edge, labelled in tracked capitals.
        g.setColour (c.withAlpha (0.035f));
        g.fillRoundedRectangle (r, ps::theme::radius);
        g.setColour (c.withAlpha (0.22f));
        g.drawRoundedRectangle (r.reduced (0.5f), ps::theme::radius, 1);
        ps::theme::drawLed (g, { r.getX() + 15, r.getY() + 13 }, c, true, 5.0f);
        g.setFont (ps::theme::caps (10.0f, 0.22f));
        g.setColour (c.interpolatedWith (ps::theme::text, 0.45f));
        g.drawText (u8 (b.title).toUpperCase(), juce::Rectangle<float> (r.getX() + 26, r.getY() + 1, r.getWidth() - 32, 24),
                    juce::Justification::centredLeft, true);
    }
}

void PatchCanvas::paintNotes (juce::Graphics& g)
{
    g.setFont (ps::theme::sans (11.5f));
    g.setColour (ps::theme::textSoft);
    for (auto& t : graph().notes)
    {
        if (! t.page.empty() && t.page != currentPage)
            continue;
        const float w = t.w > 0 ? (float) t.w : 210.0f;
        juce::AttributedString s;
        s.append (u8 (t.text), ps::theme::sans (11.5f), ps::theme::textSoft);
        s.setLineSpacing (3.0f);
        s.draw (g, { (float) t.x, (float) t.y, w, 400.0f });
    }
}

void PatchCanvas::paintWires (juce::Graphics& g)
{
    const auto& patch = graph();
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
        g.setColour (l == selectedWire ? ps::theme::textBright : kindColour (a).withAlpha (lit ? 1.0f : (audio ? 0.55f : 0.6f)));
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
    // Up to 32 cells in one row; longer patterns take two rows.
    const int perRow = count > 32 ? (count + 1) / 2 : count;
    const int rows = count > 32 ? 2 : 1;
    const float gap = perRow > 20 ? 1.0f : 2.0f;
    const float cw = (r.getWidth() - gap * (perRow - 1)) / perRow;
    const float ch = (r.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < count; ++i)
    {
        const juce::Rectangle<float> cell (r.getX() + (i % perRow) * (cw + gap), r.getY() + (i / perRow) * (ch + gap), cw, ch);
        const int v = cells[(size_t) i];
        if (v > 0)
            g.setColour (c.withAlpha (i == now ? 1.0f : (v > 1 ? 0.85f : 0.45f)));
        else
            g.setColour (editable ? ps::theme::cellOff.withMultipliedAlpha (1.6f) : ps::theme::cellOff);
        g.fillRoundedRectangle (cell, 1.5f);
        if (i == now)
        {
            g.setColour (ps::theme::textBright);
            g.drawRoundedRectangle (cell.expanded (0.5f), 1.5f, 1.0f);
        }
    }
}

// One option of a segmented control: filled in the module's colour when chosen, otherwise a faint
// outlined cell.
void PatchCanvas::paintSegment (juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour c)
{
    if (on)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r, 4);
        return;
    }
    g.setColour (ps::theme::wash);
    g.fillRoundedRectangle (r, 4);
    g.setColour (ps::theme::hair);
    g.drawRoundedRectangle (r.reduced (0.5f), 4, 1);
}

void PatchCanvas::paintNode (juce::Graphics& g, int index, const ps::Node& n, const Layout& L)
{
    using namespace ps::theme;
    const auto c = kindColour (n);
    const bool sel = selection.count (n.id) > 0;
    const auto ins = ps::inputsOf (n), outs = ps::outputsOf (n);

    // A glass panel. Its edge brightens under the mouse; a selected module gets a cyan edge and a
    // soft cyan ring.
    if (sel)
    {
        g.setColour (cyanSoft);
        g.fillRoundedRectangle (L.box.expanded (3), radius + 3);
    }
    drawPanel (g, L.box, sel ? cyan.withAlpha (0.8f) : (index == hoverNode ? edgeHover : glassEdge));

    // header
    const auto h = L.header;
    if (! n.collapsed)
    {
        g.setColour (hair);
        g.fillRect (h.getX() + 1, h.getBottom() - 1, h.getWidth() - 2, 1.0f);
    }
    g.setColour (textDim);
    juce::Path chevron;
    const float cx = h.getX() + 12, cy = h.getY() + (n.collapsed ? 15.0f : 13.0f);
    chevron.addTriangle (cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 2.5f);
    if (n.collapsed)
        chevron.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cx, cy));
    g.fillPath (chevron);
    drawLed (g, { h.getX() + 26, cy }, c, true, 5.0f);
    // The title in tracked capitals, like the dashboard's panel titles.
    g.setColour (text);
    g.setFont (caps (9.5f, 0.1f));
    const auto sub = u8 (ps::subtitleOf (n));
    // The title keeps its room; the subtitle takes what is left.
    const auto title = u8 (ps::titleOf (n)).toUpperCase();
    const float titleW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title);
    const float subW = n.collapsed ? 0.0f
                                   : std::min (juce::GlyphArrangement::getStringWidth (mono (9.5f), sub) + 4,
                                               std::max (0.0f, h.getWidth() - 44 - titleW - 6));
    g.drawText (title, juce::Rectangle<float> (h.getX() + 36, cy - 8, h.getWidth() - 44 - subW, 16), juce::Justification::centredLeft, true);
    if (! n.collapsed && subW > 12)
    {
        g.setColour (textDim);
        g.setFont (mono (9.5f));
        g.drawText (sub, juce::Rectangle<float> (h.getRight() - 7 - subW, cy - 8, subW, 16), juce::Justification::centredRight, true);
    }

    // port labels (MIDI outs show each output's name and note instead)
    if (! n.collapsed)
    {
        g.setFont (mono (10.0f));
        if (n.kind == "midi")
        {
            for (int q = 0; q < (int) n.lanes.size(); ++q)
            {
                const float ry = (float) n.y + ioTop + q * rowH;
                const auto& lane = n.lanes[(size_t) q];
                g.setColour (textSoft);
                g.drawText (u8 (lane.name), juce::Rectangle<float> ((float) n.x + 12, ry, L.box.getWidth() - 80, rowH), juce::Justification::centredLeft, true);
                g.setColour (textDim);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), juce::Rectangle<float> (L.box.getRight() - 62, ry, 14, rowH), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), juce::Rectangle<float> (L.box.getRight() - 26, ry, 14, rowH), juce::Justification::centred);
                g.setColour (c);
                g.drawText (u8 (ps::noteName (lane.note)), juce::Rectangle<float> (L.box.getRight() - 50, ry, 24, rowH), juce::Justification::centred);
                const int v = viewIn (index, q);
                drawLed (g, { L.box.getRight() - 8, ry + rowH / 2 }, v > 1 ? textBright : c, v > 0);
            }
        }
        else
        {
            g.setColour (textDim);
            for (int i = 0; i < (int) ins.size(); ++i)
                g.drawText (u8 (ins[(size_t) i]), juce::Rectangle<float> ((float) n.x + 12, (float) n.y + ioTop + i * rowH, L.box.getWidth() / 2 - 12, rowH), juce::Justification::centredLeft, true);
            for (int i = 0; i < (int) outs.size(); ++i)
                g.drawText (u8 (outs[(size_t) i]), juce::Rectangle<float> ((float) n.x + L.box.getWidth() / 2, (float) n.y + ioTop + i * rowH, L.box.getWidth() / 2 - 12, rowH), juce::Justification::centredRight, true);
        }
        paintBody (g, index, n, L);
    }

    // ports: gate ports are rings, audio ports squares; a port lights while a hit passes through.
    // While a wire is being dragged, the ports it could land on grow and the rest fade.
    const bool wiring = drag == Drag::wire && dragMoved && pressed.node >= 0 && pressed.node < (int) graph().nodes.size();
    const bool fromOut = pressed.what == Hit::outPort;
    const auto wireType = ! wiring ? ps::PortType::gate
                        : fromOut  ? ps::outputType (graph().nodes[(size_t) pressed.node], pressed.port)
                                   : ps::inputType (graph().nodes[(size_t) pressed.node], pressed.port);
    for (int dir = 0; dir < 2; ++dir)
    {
        const auto& pts = dir == 0 ? L.ins : L.outs;
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const bool audio = dir == 0 ? ps::inputType (n, i) == ps::PortType::audio : ps::outputType (n, i) == ps::PortType::audio;
            const int v = dir == 0 ? viewIn (index, i) : viewOut (index, i);
            const bool lit = v > 0 && ! (n.kind == "clock" && dir == 1 && i == 0) && ! audio;
            const bool target = wiring && (dir == 0) == fromOut && index != pressed.node
                                && (audio ? ps::PortType::audio : ps::PortType::gate) == wireType;
            const auto r = juce::Rectangle<float> (10, 10).withCentre (pts[(size_t) i]).expanded (target ? 2.0f : 0.0f);
            if (wiring && ! target)
                g.setOpacity (0.35f);
            else
                g.setOpacity (1.0f);
            g.setColour (panelBottom);
            if (audio)
                g.fillRect (r);
            else
                g.fillEllipse (r);
            g.setColour (lit ? textBright : c);
            if (audio)
                g.drawRect (r, 2.0f);
            else
                g.drawEllipse (r.reduced (1), 2.0f);
            if (lit)
                g.fillEllipse (r.reduced (3));
        }
    }
    g.setOpacity (1.0f);

    // tags
    g.setFont (mono (9.5f));
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
            // A pill, like the dashboard's tags: the tag's colour on the field, filled while a hit passes.
            const float pill = r.getHeight() / 2;
            g.setColour (lit ? tc : bg);
            g.fillRoundedRectangle (r, pill);
            g.setColour (tc.withAlpha (lit ? 1.0f : 0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), pill, 1);
            g.setColour (lit ? onAccent : tc.interpolatedWith (text, 0.25f));
            g.drawText (u8 (tag), r, juce::Justification::centred, false);
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
                g.setColour (textDim);
                g.setFont (sans (10.5f));
                g.drawFittedText (it.text, r.toNearestInt(), juce::Justification::topLeft, r.getHeight() > 14 ? 2 : 1, 1.0f);
                break;
            case ItemType::bigText:
            {
                // The dashboard's tile value: bold Sora, tracked tight, white fading to ice.
                g.setFont (sans (15.0f, Weight::bold).withExtraKerningFactor (-0.03f));
                drawValue (g, juce::String (processor.currentTempo(), 1) + " BPM", r, juce::Justification::centredLeft);
                g.setColour (textDim);
                g.setFont (mono (9.5f));
                g.drawText (juce::String (processor.meterNumerator()) + "/" + juce::String (processor.meterDenominator())
                                + (processor.isFollowingHost() ? "  host" : "  internal"),
                            r, juce::Justification::centredRight);
                break;
            }
            case ItemType::stepper:
                g.setFont (sans (10.5f));
                g.setColour (textDim);
                g.drawText (it.text, r, juce::Justification::centredLeft);
                g.setColour (textDim);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), juce::Rectangle<float> (r.getRight() - 52, r.getY(), 14, r.getHeight()), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), juce::Rectangle<float> (r.getRight() - 12, r.getY(), 12, r.getHeight()), juce::Justification::centred);
                g.setColour (textBright);
                g.setFont (mono (10.5f, true));
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
                    g.setColour (textDim);
                    g.setFont (mono (9.0f));
                    g.drawText (labels[k], row.withWidth (10), juce::Justification::centredLeft);
                    paintCells (g, row.withTrimmedLeft (12), std::vector<int> (pat[(size_t) k].begin(), pat[(size_t) k].end()), v ? v->pos : -1, c);
                }
                break;
            }
            case ItemType::modes:
            {
                const bool combine = it.key == "combine", follow = it.key == "follow";
                const auto& modes = ps::combineModes();
                const int count = combine ? (int) modes.size() : follow ? 3 : 2;
                const int ways = n.get ("ways", 4);
                const int followNow = settingOf ("follow");
                static const char* followNames[] = { "Phrase", "Song", "Free" };
                for (int m = 0; m < count; ++m)
                {
                    const auto b = juce::Rectangle<float> (r.getX() + r.getWidth() * m / count, r.getY(), r.getWidth() / count, r.getHeight()).reduced (1, 0);
                    const bool on = combine ? n.mode == modes[(size_t) m] : follow ? m == followNow : (m == 0) == (n.mode != "merge");
                    paintSegment (g, b, on, c);
                    g.setColour (on ? onAccent : textSoft);
                    g.setFont (sans (9.5f, on ? Weight::semibold : Weight::regular));
                    const juce::String arrow = juce::String::fromUTF8 (" \xE2\x86\x92 ");
                    const auto caption = combine ? u8 (modes[(size_t) m])
                                       : follow  ? juce::String (followNames[m])
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
                    paintSegment (g, b, on, c);
                    g.setColour (on ? onAccent : textDim);
                    g.setFont (mono (9.5f, on));
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
                    paintSegment (g, br, b == bus, c);
                    g.setColour (b == bus ? onAccent : textSoft);
                    g.setFont (mono (10.0f, b == bus));
                    g.drawText (juce::String (b), br, juce::Justification::centred);
                }
                break;
            }
            case ItemType::meter:
            {
                // The dashboard's meter: a 4px track with a fill that brightens toward its end.
                const auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
                g.setColour (cellOff);
                g.fillRoundedRectangle (track, 2);
                const float level = juce::jlimit (0.0f, 1.0f, processor.busLevel (n.get ("bus", 1)) * 1.6f);
                if (level > 0.0f)
                {
                    const auto fill = track.withWidth (track.getWidth() * level);
                    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.5f), fill.getX(), 0, c, fill.getRight(), 0, false));
                    g.fillRoundedRectangle (fill, 2);
                }
                break;
            }
            case ItemType::xy:
            {
                g.setColour (bg);
                g.fillRoundedRectangle (r, radius);
                g.setColour (glassEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1);
                const auto inner = r.reduced (7);
                g.setColour (off);
                for (int i = 0; i < 5; ++i)
                    for (int j = 0; j < 5; ++j)
                        g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre ({ inner.getX() + inner.getWidth() * i / 4, inner.getY() + inner.getHeight() * j / 4 }));
                const juce::Point<float> dot (inner.getX() + inner.getWidth() * n.get ("mx", 50) / 100.0f, inner.getY() + inner.getHeight() * n.get ("my", 20) / 100.0f);
                drawLed (g, dot, c, true, 10.0f);
                break;
            }
            case ItemType::button:
                // A hairline pill.
                g.setColour (pillEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2, 1);
                g.setColour (textSoft);
                g.setFont (sans (10.5f));
                g.drawText (it.text, r, juce::Justification::centred, false);
                break;
            case ItemType::sample:
            {
                g.setColour (bg);
                g.fillRoundedRectangle (r, radiusSm);
                g.setColour (glassEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), radiusSm, 1);
                g.setColour (textDim);
                g.setFont (sans (10.5f));
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), r.withWidth (16), juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("\xE2\x80\xBA"), r.withTrimmedLeft (r.getWidth() - 16), juce::Justification::centred);
                g.setColour (text);
                g.setFont (sans (10.5f));
                g.drawText (it.text, r.reduced (18, 0), juce::Justification::centred, true);
                drawLed (g, { r.getRight() - 21, r.getCentreY() }, c, viewIn (index, 0) > 0);
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
    const auto& g = graph();
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
    if (helpShown)
    {
        setShowHelp (false);
        return;
    }
    dragStart = e.position;
    dragWorld = toWorld (e.position);
    dragMoved = false;
    drag = Drag::none;

    if (! e.mods.isPopupMenu() && ! e.mods.isMiddleButtonDown())
    {
        const auto items = crumbs();
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].second.contains (e.position))
            {
                closeSubsystem (i);
                return;
            }
    }

    pressed = hitTest (dragWorld);
    menuPress = e.mods.isPopupMenu();
    auto& patch = graph();

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
            if (pressed.what == Hit::inPort)
            {
                // A wired input: pick the wire up by its end so it can be moved or dropped away.
                auto& links = patch.links;
                const auto& dest = patch.nodes[(size_t) pressed.node];
                auto it = std::find_if (links.begin(), links.end(), [&] (const ps::Link& l) { return l.b == dest.id && l.q == pressed.port; });
                if (it != links.end())
                {
                    const int from = patch.indexOf (it->a);
                    if (from >= 0)
                    {
                        pressed = { Hit::outPort, from, it->p };
                        links.erase (it);
                        dragMoved = true; // dropping it nowhere leaves it removed
                        edited();
                    }
                }
            }
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
            else if (pressed.zone.type == ZoneType::paramValue)
            {
                drag = Drag::param;
                dragAccum = 0;
                selection = { patch.nodes[(size_t) pressed.node].id };
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
    const auto previousMouse = lastMouse;
    lastMouse = e.position;
    dragWorld = toWorld (e.position);
    auto& patch = graph();
    const auto delta = (e.position - dragStart) / scale;
    switch (drag)
    {
        case Drag::pan:
            offset = panStart + (e.position - dragStart);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        case Drag::move:
            if (pageAtScreen)
            {
                auto page = pageAtScreen (e.getScreenPosition());
                if (page == currentPage)
                    page.clear();
                if (page != dropPage)
                {
                    dropPage = page;
                    if (onDropTarget)
                        onDropTarget (dropPage);
                }
            }
            [[fallthrough]];
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
                    if (snap)
                    {
                        n->x = std::round (n->x / 10.0) * 10.0;
                        n->y = std::round (n->y / 10.0) * 10.0;
                    }
                }
            break;
        case Drag::xy:
            applyZone (pressed.node, pressed.zone, dragWorld);
            break;
        case Drag::param:
        {
            // Every 6 screen pixels up or down is one step; Shift makes it 24.
            const int perStep = e.mods.isShiftDown() ? 24 : 6;
            const int pixels = juce::roundToInt (previousMouse.y - e.position.y) + dragAccum;
            const int delta = pixels / perStep;
            if (delta != 0)
            {
                dragAccum = pixels - delta * perStep;
                applyParamDelta (pressed.node, pressed.zone.key, delta, "drag " + pressed.zone.key + patch.nodes[(size_t) pressed.node].id);
            }
            else
                dragAccum = pixels;
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            break;
        }
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
            if (! dropPage.empty())
            {
                // put the modules back where they started on this tab, then move them across
                auto& patch = processor.patch();
                for (auto& [id, start] : moveStart)
                    if (auto* n = patch.find (id))
                    {
                        n->x = start.x;
                        n->y = start.y;
                    }
                const auto page = dropPage;
                dropPage.clear();
                if (onDropTarget)
                    onDropTarget ({});
                drag = Drag::none;
                moveSelectionToPage (page);
                return;
            }
            if (dragMoved)
                edited();
            break;
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
        case Drag::param:
            if (! dragMoved)
                showMessage ("Drag the value up or down, scroll over it, or double-click to type it", 5000);
            break;
        case Drag::pan:
            if (! dragMoved && menuPress && pressed.node >= 0)
            {
                // A port's row on the Inputs or Outputs module: rename it, change its type or remove it.
                const auto& n = graph().nodes[(size_t) pressed.node];
                const int count = (int) (n.kind == "subin" ? ps::outputsOf (n) : ps::inputsOf (n)).size();
                const int row = (int) std::floor ((dragWorld.y - (float) n.y - ioTop) / rowH);
                if ((n.kind == "subin" || n.kind == "subout") && ! n.collapsed && pressed.what != Hit::header && row >= 0 && row < count)
                {
                    drag = Drag::none;
                    showPortMenu (pressed.node, row);
                    return;
                }
                const auto& id = n.id;
                if (! selection.count (id))
                    selection = { id };
                drag = Drag::none;
                repaint();
                showNodeMenu (pressed.node);
                return;
            }
            if (! dragMoved && menuPress && pressed.what == Hit::none)
            {
                drag = Drag::none;
                showCanvasMenu (dragWorld);
                return;
            }
            if (dragMoved && onViewChanged)
                onViewChanged();
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
        auto& n = graph().nodes[(size_t) hit.node];
        n.collapsed = ! n.collapsed;
        edited();
    }
    else if (hit.what == Hit::body && graph().nodes[(size_t) hit.node].kind == "sub")
        openSubsystem (graph().nodes[(size_t) hit.node].id);
    else if (hit.what == Hit::zone && hit.zone.type == ZoneType::paramValue)
    {
        const auto& n = graph().nodes[(size_t) hit.node];
        tagNode = n.id;
        textKey = hit.zone.key;
        textTarget = TextTarget::param;
        juce::String current;
        if (textKey.rfind ("lane", 0) == 0)
        {
            const int q = std::stoi (textKey.substr (4));
            if (q < (int) n.lanes.size())
                current = u8 (ps::noteName (n.lanes[(size_t) q].note));
        }
        else
            for (auto& spec : ps::paramsOf (n))
                if (textKey == spec.key)
                    current = juce::String (textKey == "nout" ? (int) n.lanes.size() : n.get (textKey) * spec.displayScale);
        const auto r = hit.zone.r.expanded (2, 3);
        showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, current, "Value");
        tagEditor->setJustification (juce::Justification::centred);
    }
}

void PatchCanvas::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const auto items = crumbs();
    if (std::any_of (items.begin(), items.end(), [&] (auto& c) { return c.second.contains (e.position); }))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }
    const auto hit = hitTest (toWorld (e.position));
    const bool point = hit.what == Hit::zone || hit.what == Hit::inPort || hit.what == Hit::outPort || hit.what == Hit::inTag
                       || hit.what == Hit::outTag || hit.what == Hit::wire;
    setMouseCursor (point ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    const int over = hit.what == Hit::wire || hit.what == Hit::blockTitle ? -1 : hit.node;
    if (over != hoverNode)
    {
        hoverNode = over;
        repaint();
    }
}

void PatchCanvas::mouseExit (const juce::MouseEvent&)
{
    if (hoverNode >= 0)
    {
        hoverNode = -1;
        repaint();
    }
}

// One flat deep-blue field with the dashboard's wireframe cube in the top corner: a cube with its
// faces split into a 3 x 3 grid, turned to show three faces, its back edges fainter. Drawn once
// per size, since the workspace repaints while the patch plays.
void PatchCanvas::renderBackdrop()
{
    using namespace ps::theme;
    const int w = std::max (1, getWidth()), h = std::max (1, getHeight());
    backdrop = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (backdrop);
    g.fillAll (bg);
    if (w < 520 || h < 360)
        return;

    struct V3
    {
        float x, y, z;
    };
    const float ay = 0.62f, ax = 0.52f, az = 0.18f; // yaw, pitch and roll, in radians
    const float size = 46.0f;
    const juce::Point<float> centre ((float) w - 104.0f, 96.0f);
    const auto project = [&] (V3 p) {
        float x = p.x * std::cos (ay) + p.z * std::sin (ay), z = -p.x * std::sin (ay) + p.z * std::cos (ay);
        float y = p.y * std::cos (ax) - z * std::sin (ax);
        z = p.y * std::sin (ax) + z * std::cos (ax);
        const float rx = x * std::cos (az) - y * std::sin (az), ry = x * std::sin (az) + y * std::cos (az);
        const float persp = 7.0f / (7.0f + z);
        return std::pair<juce::Point<float>, float> (centre + juce::Point<float> (rx, ry) * size * persp, z);
    };
    // Each line of the grid on each face, split into front and back by depth.
    juce::Path front, back;
    for (int axis = 0; axis < 3; ++axis)
        for (int side = -1; side <= 1; side += 2)
            for (int k = 0; k <= 3; ++k)
                for (int dir = 0; dir < 2; ++dir)
                {
                    const float t = -1.0f + k * 2.0f / 3.0f;
                    V3 a {}, b {};
                    float* ca = &a.x;
                    float* cb = &b.x;
                    ca[axis] = cb[axis] = (float) side;
                    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                    ca[dir == 0 ? u : v] = cb[dir == 0 ? u : v] = t;
                    ca[dir == 0 ? v : u] = -1.0f;
                    cb[dir == 0 ? v : u] = 1.0f;
                    const auto pa = project (a), pb = project (b);
                    auto& path = (pa.second + pb.second) * 0.5f > 0.2f ? back : front;
                    path.startNewSubPath (pa.first);
                    path.lineTo (pb.first);
                }
    g.setColour (cyan.withAlpha (0.3f * 0.22f));
    g.strokePath (back, juce::PathStrokeType (1.0f));
    g.setColour (cyan.withAlpha (0.3f));
    g.strokePath (front, juce::PathStrokeType (1.0f));
}

void PatchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (scale * std::pow (1.0016f, wheel.deltaY * 300.0f), e.position);
        return;
    }
    const auto hit = hitTest (toWorld (e.position));
    if (hit.what == Hit::zone && (hit.zone.type == ZoneType::paramValue || hit.zone.type == ZoneType::param))
    {
        const int delta = wheel.deltaY > 0 ? 1 : wheel.deltaY < 0 ? -1 : 0;
        if (delta != 0)
            applyParamDelta (hit.node, hit.zone.key, delta, "wheel " + hit.zone.key + graph().nodes[(size_t) hit.node].id);
        return;
    }
    offset += juce::Point<float> (wheel.deltaX, wheel.deltaY) * 300.0f;
    repaint();
}

void PatchCanvas::mouseMagnify (const juce::MouseEvent& e, float factor) { zoomAround (scale * factor, e.position); }

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = [&key] (char c) { return key == juce::KeyPress (c, juce::ModifierKeys::commandModifier, 0); };
    if (helpShown && key != juce::KeyPress::escapeKey)
        setShowHelp (false);
    if (cmd ('z'))
    {
        processor.undo();
        return true;
    }
    if (cmd ('y') || key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        processor.redo();
        return true;
    }
    if (cmd ('a'))
    {
        selectAll();
        return true;
    }
    if (cmd ('d'))
    {
        duplicateSelection();
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey) || key.isKeyCode (juce::KeyPress::upKey)
        || key.isKeyCode (juce::KeyPress::downKey))
    {
        const float step = key.getModifiers().isShiftDown() ? 10.0f : 1.0f;
        nudgeSelection (key.isKeyCode (juce::KeyPress::leftKey) ? -step : key.isKeyCode (juce::KeyPress::rightKey) ? step : 0.0f,
                        key.isKeyCode (juce::KeyPress::upKey) ? -step : key.isKeyCode (juce::KeyPress::downKey) ? step : 0.0f);
        return true;
    }
    if (key.getTextCharacter() == '?' || key == juce::KeyPress::F1Key)
    {
        setShowHelp (! helpShown);
        return true;
    }
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
        if (helpShown)
        {
            setShowHelp (false);
            return true;
        }
        if (selection.empty() && selectedWire < 0 && ! subPath.empty())
        {
            closeSubsystem (subPath.size() - 1);
            return true;
        }
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
    auto& patch = graph();
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
            if (z.key == "follow")
            {
                auto& settings = processor.patch().settings;
                changed = settings["follow"] != z.value;
                settings["follow"] = z.value;
                break;
            }
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
        case ZoneType::open:
            openSubsystem (n.id);
            return;
        case ZoneType::reroll:
        {
            // A fresh seed that isn't the current one.
            const int before = n.get ("seed", 90);
            int seed = before;
            while (seed == before)
                seed = juce::Random::getSystemRandom().nextInt (juce::Range<int> (1, 10000));
            n.set ("seed", seed);
            changed = true;
            break;
        }
        case ZoneType::addPort:
            addPort (n.kind == "subout");
            return;
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

void PatchCanvas::applyParamDelta (int index, const std::string& key, int delta, const juce::String& undoGroup)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size() || delta == 0)
        return;
    auto& n = patch.nodes[(size_t) index];
    selection = { n.id };
    bool changed = false;
    if (key.rfind ("lane", 0) == 0)
    {
        const int q = std::stoi (key.substr (4));
        if (q < (int) n.lanes.size())
        {
            auto& lane = n.lanes[(size_t) q];
            const int before = lane.note;
            lane.note = juce::jlimit (0, 127, lane.note + delta);
            if (auto* old = ps::gmDrumName (before); old && lane.name == old)
                if (auto* now = ps::gmDrumName (lane.note))
                    lane.name = now;
            changed = lane.note != before;
        }
    }
    else
        changed = ps::stepParam (patch, n, key, delta);
    if (changed)
        edited (undoGroup);
    else
        repaint();
}

void PatchCanvas::selectAll()
{
    selection.clear();
    for (auto& n : graph().nodes)
        if (onPage (n))
            selection.insert (n.id);
    selectedWire = -1;
    repaint();
}

void PatchCanvas::duplicateSelection()
{
    if (selection.empty())
        return;
    copySelection();
    pasteCount = 0;
    const bool over = isMouseOver();
    if (over)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    // Land the copy a step down and right, as a paste without the mouse does.
    auto& clip = clipboard();
    juce::Point<float> topLeft { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    for (const auto& n : clip.nodes)
        topLeft = { std::min (topLeft.x, (float) n.x), std::min (topLeft.y, (float) n.y) };
    lastMouse = toScreen (topLeft + juce::Point<float> (30, 30));
    pasteClipboard();
}

void PatchCanvas::nudgeSelection (float dx, float dy)
{
    if (selection.empty())
        return;
    for (auto& n : graph().nodes)
        if (selection.count (n.id))
        {
            n.x += dx;
            n.y += dy;
        }
    edited ("nudge");
}

void PatchCanvas::setSnapToGrid (bool on)
{
    snap = on;
    showMessage (on ? "Modules snap to a 10 px grid while dragging" : "Snapping off");
}

bool PatchCanvas::anyExpanded() const
{
    for (auto& n : graph().nodes)
        if (onPage (n) && ! n.collapsed)
            return true;
    return false;
}

void PatchCanvas::setShowHelp (bool on)
{
    helpShown = on;
    repaint();
}

void PatchCanvas::connect (int fromNode, int fromPort, int toNode, int toPort)
{
    auto& patch = graph();
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
    auto& patch = graph();
    if (selectedWire >= 0 && selectedWire < (int) patch.links.size())
    {
        patch.links.erase (patch.links.begin() + selectedWire);
        selectedWire = -1;
        edited();
        return;
    }
    // A subsystem's Inputs and Outputs modules are its ports, so they stay.
    std::set<std::string> doomed;
    for (auto& n : patch.nodes)
        if (selection.count (n.id) && n.kind != "subin" && n.kind != "subout")
            doomed.insert (n.id);
    if (doomed.empty())
    {
        if (! selection.empty())
            showMessage ("The Inputs and Outputs modules are the subsystem's ports, so they can't be removed");
        return;
    }
    const auto count = doomed.size();
    patch.nodes.erase (std::remove_if (patch.nodes.begin(), patch.nodes.end(), [&] (const ps::Node& n) { return doomed.count (n.id) > 0; }),
                       patch.nodes.end());
    patch.links.erase (std::remove_if (patch.links.begin(), patch.links.end(),
                                       [&] (const ps::Link& l) { return doomed.count (l.a) || doomed.count (l.b); }),
                       patch.links.end());
    selection.clear();
    showMessage ("Removed " + juce::String ((int) count) + (count == 1 ? " module" : " modules"));
    edited();
}

void PatchCanvas::copySelection()
{
    const auto& patch = graph();
    auto& clip = clipboard();
    if (selection.empty())
        return;
    clip.nodes.clear();
    clip.links.clear();
    for (const auto& n : patch.nodes)
        if (selection.count (n.id) && n.kind != "subin" && n.kind != "subout") // a subsystem's ports stay put
            clip.nodes.push_back (n);
    std::set<std::string> copied;
    for (const auto& n : clip.nodes)
        copied.insert (n.id);
    for (const auto& l : patch.links)
        if (copied.count (l.a) && copied.count (l.b))
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
    auto& patch = graph();

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
        n.page = subPath.empty() ? currentPage : std::string();
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

void PatchCanvas::addModule (const std::string& kind, juce::Point<float> world)
{
    auto& patch = graph();
    auto centre = toWorld (getLocalBounds().getCentre().toFloat());
    if (world.x != std::numeric_limits<float>::max())
        centre = world + juce::Point<float> (75, 60);
    else
    {
        // Step down and right until the spot is clear of other modules on this tab.
        auto clear = [&] (juce::Point<float> at) {
            const juce::Rectangle<float> r (at.x - 75, at.y - 60, 150, 60);
            for (auto& m : patch.nodes)
                if (onPage (m) && layoutOf (m).box.intersects (r))
                    return false;
            return true;
        };
        for (int tries = 0; tries < 12 && ! clear (centre); ++tries)
            centre += juce::Point<float> (30, 30);
    }
    auto prefix = kind.substr (0, std::min<size_t> (2, kind.size()));
    auto n = ps::makeNode (kind, patch.freshId (prefix), std::round (centre.x - 75), std::round (centre.y - 60));
    n.page = subPath.empty() ? currentPage : std::string(); // tabs only exist at the top
    selection = { n.id };
    patch.nodes.push_back (n);
    edited();
}

void PatchCanvas::addSubsystem (const ps::presets::SubsystemTemplate& t)
{
    auto& patch = graph();
    const auto centre = toWorld (getLocalBounds().getCentre().toFloat());
    auto n = t.node;
    n.id = patch.freshId ("sub");
    n.x = std::round (centre.x - 80);
    n.y = std::round (centre.y - 60);
    n.page = subPath.empty() ? currentPage : std::string();
    selection = { n.id };
    patch.nodes.push_back (n);
    edited();
}

void PatchCanvas::collapseAll (bool collapse)
{
    for (auto& n : graph().nodes)
        if (onPage (n))
            n.collapsed = collapse;
    edited();
}

void PatchCanvas::moveSelectionToPage (const std::string& pageId)
{
    auto& patch = graph();
    if (selection.empty() || pageId == currentPage || ! subPath.empty())
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
    auto& patch = graph();
    const auto& n = patch.nodes[(size_t) index];
    juce::PopupMenu menu, pages;
    const auto& rootPages = processor.patch().pages;
    for (auto& p : rootPages)
        pages.addItem (u8 (p.name), p.id != currentPage, false, [this, id = p.id] { moveSelectionToPage (id); });
    if (n.kind == "sub")
    {
        menu.addItem ("Open", [this, id = n.id] { openSubsystem (id); });
        menu.addItem ("Rename", [this, index] { renameSubsystem (index); });
    }
    menu.addItem (n.collapsed ? "Expand" : "Collapse", [this, id = n.id] {
        if (auto* m = graph().find (id))
        {
            m->collapsed = ! m->collapsed;
            edited();
        }
    });
    if (subPath.empty())
        menu.addSubMenu ("Move to tab", pages, rootPages.size() > 1);
    if (n.kind == "sub" && onExportSubsystem)
        menu.addItem ("Save subsystem...", [this, id = n.id] {
            if (auto* m = graph().find (id))
                onExportSubsystem (*m);
        });
    if (n.kind != "subin" && n.kind != "subout")
    {
        menu.addSeparator();
        const auto what = selection.size() > 1 ? " " + juce::String ((int) selection.size()) + " modules" : juce::String();
        menu.addItem ("Copy" + what, [this] { copySelection(); });
        menu.addItem ("Cut" + what, [this] {
            copySelection();
            deleteSelection();
        });
        menu.addItem ("Duplicate" + what, [this] { duplicateSelection(); });
        menu.addItem ("Paste", ! clipboard().nodes.empty(), false, [this] { pasteClipboard(); });
        menu.addSeparator();
        menu.addItem ("Delete" + what, [this] { deleteSelection(); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// Right-click on empty space: add a module right there, or paste.
void PatchCanvas::showCanvasMenu (juce::Point<float> world)
{
    juce::PopupMenu menu;
    for (auto& section : ps::librarySections())
    {
        juce::PopupMenu sub;
        for (auto& kind : section.kinds)
            if (auto* info = ps::kindInfo (kind))
                sub.addItem (info->title, [this, kind, world] { addModule (kind, world); });
        menu.addSubMenu (section.title, sub);
    }
    menu.addSeparator();
    menu.addItem ("Paste", ! clipboard().nodes.empty(), false, [this] { pasteClipboard(); });
    menu.addItem ("Select all", [this] { selectAll(); });
    menu.addItem (anyExpanded() ? "Collapse all" : "Expand all", [this] { collapseAll (anyExpanded()); });
    menu.addItem ("Fit to view", [this] { fitView(); });
    menu.addSeparator();
    menu.addItem ("Snap to grid", true, snap, [this] { setSnapToGrid (! snap); });
    menu.addItem ("Keyboard shortcuts", [this] { setShowHelp (true); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PatchCanvas::showMessage (const juce::String& text, int milliseconds)
{
    message = text;
    messageUntil = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
    repaint();
}

// ---- tags ------------------------------------------------------------------------------------

void PatchCanvas::editTag (int index, bool output, int port)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    const auto& n = patch.nodes[(size_t) index];
    const auto L = layoutOf (n);
    if (port < 0 || port >= (int) (output ? L.outs.size() : L.ins.size()))
        return;
    tagNode = n.id;
    tagOutput = output;
    tagPort = port;
    textTarget = TextTarget::tag;
    const auto& tags = output ? n.tagOut : n.tagIn;
    const auto it = tags.find (port);
    const auto p = toScreen (output ? L.outs[(size_t) port] : L.ins[(size_t) port]);
    const int w = 150, h = 24;
    showTextEditor ({ output ? juce::roundToInt (p.x) + 12 : juce::roundToInt (p.x) - 12 - w, juce::roundToInt (p.y) - h / 2, w, h },
                    it != tags.end() ? u8 (it->second) : juce::String(), output ? "Name this output" : "Tag to listen to");
    showMessage (output ? "Inputs with the same tag receive this output without a wire. Leave it empty to remove the tag."
                        : "This input receives the output that owns the tag. Leave it empty to remove the tag.",
                 6000);
}

void PatchCanvas::editPortName (int index, int port)
{
    const auto& n = graph().nodes[(size_t) index];
    const auto names = n.kind == "subin" ? ps::outputsOf (n) : ps::inputsOf (n);
    if (port < 0 || port >= (int) names.size())
        return;
    tagNode = n.id;
    tagPort = port;
    tagOutput = n.kind == "subout"; // which side of the subsystem
    textTarget = TextTarget::portName;
    const auto r = juce::Rectangle<float> ((float) n.x + 8, (float) n.y + ioTop + port * rowH - 3, nodeWidth (n) - 16, rowH + 6);
    showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (names[(size_t) port]), "Port name");
    showMessage ("Name this port. It shows on the subsystem outside too.");
}

void PatchCanvas::renameSubsystem (int index)
{
    const auto& n = graph().nodes[(size_t) index];
    tagNode = n.id;
    textTarget = TextTarget::subName;
    const auto L = layoutOf (n);
    const auto r = L.header.reduced (30, 2);
    showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (n.name), "Subsystem name");
}

void PatchCanvas::showTextEditor (juce::Rectangle<int> r, const juce::String& text, const juce::String& placeholder)
{
    if (! tagEditor)
    {
        tagEditor = std::make_unique<juce::TextEditor>();
        auto& ed = *tagEditor;
        ed.setFont (ps::theme::sans (12.0f));
        ed.setColour (juce::TextEditor::backgroundColourId, ps::theme::ink);
        ed.setColour (juce::TextEditor::textColourId, ps::theme::text);
        ed.setColour (juce::TextEditor::outlineColourId, ps::theme::cyan);
        ed.setColour (juce::TextEditor::focusedOutlineColourId, ps::theme::cyan);
        ed.onReturnKey = [this] { finishTagEdit (true); };
        ed.onEscapeKey = [this] { finishTagEdit (false); };
        ed.onFocusLost = [this] { finishTagEdit (true); };
        addChildComponent (ed);
    }
    auto& ed = *tagEditor;
    ed.setInputRestrictions (textTarget == TextTarget::tag ? 16 : 32);
    ed.setTextToShowWhenEmpty (placeholder, ps::theme::textDim);
    ed.setText (text, false);
    ed.setBounds (r.constrainedWithin (getLocalBounds().reduced (4)));
    tagEditing = true;
    ed.setVisible (true);
    ed.selectAll();
    ed.grabKeyboardFocus();
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
    auto& patch = graph();
    auto* n = patch.find (tagNode);
    if (! n)
        return;
    if (textTarget == TextTarget::subName)
    {
        const auto name = text.trim().toStdString();
        if (! name.empty() && name != n->name)
        {
            n->name = name;
            edited();
        }
        return;
    }
    if (textTarget == TextTarget::param)
    {
        const auto typed = text.trim();
        if (typed.isEmpty())
            return;
        const int index = patch.indexOf (tagNode);
        if (textKey.rfind ("lane", 0) == 0)
        {
            const int q = std::stoi (textKey.substr (4));
            if (q < (int) n->lanes.size())
            {
                const int note = typed.containsOnly ("0123456789") ? typed.getIntValue() : ps::noteNumber (typed.toUpperCase().toStdString(), -1);
                if (note >= 0)
                    applyParamDelta (index, textKey, juce::jlimit (0, 127, note) - n->lanes[(size_t) q].note, {});
                else
                    showMessage ("Type a note like C1 or F#2, or a number 0 to 127");
            }
            return;
        }
        for (auto& spec : ps::paramsOf (*n))
            if (textKey == spec.key)
            {
                const int now = textKey == "nout" ? (int) n->lanes.size() : n->get (textKey);
                const int want = juce::roundToInt (typed.retainCharacters ("-0123456789.").getDoubleValue() / spec.displayScale);
                applyParamDelta (index, textKey, want - now, {});
            }
        return;
    }
    if (textTarget == TextTarget::portName)
    {
        const auto name = text.trim().toStdString();
        if (auto* sub = openNode(); sub != nullptr && ! name.empty())
        {
            ps::renameSubsystemPort (*sub, tagOutput, tagPort, name);
            edited();
        }
        return;
    }
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
