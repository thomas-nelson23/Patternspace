// The canvas: what the user does to it (mouse, keys, drops), the view, and every edit to the patch.

#include "PatchCanvasDetail.h"
#include "PluginProcessor.h"

#include "../core/Patterns.h"

#include <cmath>
#include <limits>
#include <utility>

using namespace ps::canvas;

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
    menu.addItem ("Control", true, type == ps::PortType::control, [setType] { setType (ps::PortType::control); });
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
    const auto items = bodyItems (n, x, y + L.bodyTop, w, processor.patch().settings, graph(), processor.patch());
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
                {
                    // the same grid paintCells draws: up to 32 a row, two rows beyond
                    const int count = (int) n.cells.size();
                    const int perRow = count > 32 ? (count + 1) / 2 : std::max (1, count);
                    const int rows = count > 32 ? 2 : 1;
                    const float cw = it.r.getWidth() / perRow, ch = it.r.getHeight() / rows;
                    for (int c = 0; c < count; ++c)
                        L.zones.push_back ({ { it.r.getX() + cw * (c % perRow), it.r.getY() + ch * (c / perRow) - 1, cw, ch + 2 }, ZoneType::cell, {}, c });
                }
                break;
            case ItemType::modes:
            {
                const int count = it.key == "combine" ? (int) ps::combineModes().size() : it.key == "shape" ? 4 : it.key == "follow" || it.key == "rate" ? 3 : 2;
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
                L.zones.push_back ({ it.r.withWidth (18), ZoneType::sample, {}, -1 });
                L.zones.push_back ({ it.r.withTrimmedLeft (it.r.getWidth() - 18), ZoneType::sample, {}, 1 });
                L.zones.push_back ({ it.r.reduced (18, 0), ZoneType::sample, {}, 0 }); // the name: its menu
                break;
            case ItemType::button:
                L.zones.push_back ({ it.r,
                                     it.key == "open"     ? ZoneType::open
                                     : it.key == "reroll" ? ZoneType::reroll
                                     : it.key == "learn"  ? ZoneType::learn
                                     : it.key == "target" ? ZoneType::target
                                                          : ZoneType::addPort,
                                     {},
                                     0 });
                break;
            case ItemType::text:
            case ItemType::bigText:
            case ItemType::drumRows:
            case ItemType::meter:
            case ItemType::switchPos:
            case ItemType::ctlBar:
                break;
        }
    }
    if (n.kind == "midi" || n.kind == "midiin")
        for (int q = 0; q < (int) n.lanes.size(); ++q)
        {
            const float ry = y + ioTop + q * rowH;
            L.zones.push_back ({ { x + 12, ry, w - 80, rowH }, ZoneType::lane, {}, q });
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
    for (int t = (int) g.notes.size() - 1; t >= 0; --t)
    {
        const auto& note = g.notes[(size_t) t];
        if (note.page != currentPage && ! note.page.empty())
            continue;
        if (noteRect (note).expanded (6, 4).contains (w))
        {
            r.what = Hit::note;
            r.block = t;
            return r;
        }
    }
    for (int b = (int) g.blocks.size() - 1; b >= 0; --b)
    {
        const auto& blk = g.blocks[(size_t) b];
        if (blk.page != currentPage && ! blk.page.empty())
            continue;
        const juce::Rectangle<float> area ((float) blk.x, (float) blk.y, (float) blk.w, (float) blk.h);
        if (juce::Rectangle<float> (area.getRight() - 14, area.getBottom() - 14, 14, 14).contains (w))
        {
            r.what = Hit::blockResize;
            r.block = b;
            return r;
        }
        if (area.withHeight (24).contains (w))
        {
            r.what = Hit::blockTitle;
            r.block = b;
            return r;
        }
    }
    return r;
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

    if (! e.mods.isPopupMenu() && ! e.mods.isMiddleButtonDown() && minimapRect().contains (e.position))
    {
        drag = Drag::minimap;
        pressed = {};
        mouseDrag (e);
        return;
    }
    pressed = hitTest (dragWorld);
    menuPress = e.mods.isPopupMenu();
    auto& patch = graph();
    if (pressed.what != Hit::blockTitle && pressed.what != Hit::blockResize)
        selectedBlock = -1;
    if (pressed.what != Hit::note)
        selectedNote = -1;

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
        case Hit::blockResize:
        {
            drag = Drag::blockResize;
            dragBlock = selectedBlock = pressed.block;
            const auto& b = patch.blocks[(size_t) dragBlock];
            blockStart = { (float) b.w, (float) b.h };
            selection.clear();
            break;
        }
        case Hit::note:
        {
            drag = Drag::note;
            selectedNote = pressed.block;
            const auto& t = patch.notes[(size_t) selectedNote];
            blockStart = { (float) t.x, (float) t.y };
            selection.clear();
            break;
        }
        case Hit::blockTitle:
        {
            drag = Drag::block;
            dragBlock = selectedBlock = pressed.block;
            selection.clear();
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
        case Drag::blockResize:
            if (dragBlock >= 0 && dragBlock < (int) patch.blocks.size())
            {
                patch.blocks[(size_t) dragBlock].w = std::max (80.0, (double) std::round (blockStart.x + delta.x));
                patch.blocks[(size_t) dragBlock].h = std::max (48.0, (double) std::round (blockStart.y + delta.y));
            }
            setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor);
            break;
        case Drag::note:
            if (selectedNote >= 0 && selectedNote < (int) patch.notes.size())
            {
                patch.notes[(size_t) selectedNote].x = std::round (blockStart.x + delta.x);
                patch.notes[(size_t) selectedNote].y = std::round (blockStart.y + delta.y);
            }
            break;
        case Drag::minimap:
        {
            // The point under the mouse on the map becomes the centre of the view.
            const auto r = minimapRect();
            const auto world = minimapWorldBounds();
            if (r.isEmpty() || world.isEmpty())
                break;
            const float k = std::min ((r.getWidth() - 8) / world.getWidth(), (r.getHeight() - 8) / world.getHeight());
            const juce::Point<float> at (world.getX() + (e.position.x - r.getX() - 4) / k, world.getY() + (e.position.y - r.getY() - 4) / k);
            offset = getLocalBounds().getCentre().toFloat() - at * scale;
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
        case Drag::blockResize:
        case Drag::note:
            if (dragMoved)
                edited();
            break;
        case Drag::minimap:
            if (onViewChanged)
                onViewChanged();
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
                showTagMenu (pressed.node, pressed.what == Hit::outPort, pressed.port);
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
            if (! dragMoved && menuPress && (pressed.what == Hit::blockTitle || pressed.what == Hit::blockResize))
            {
                drag = Drag::none;
                selectedBlock = pressed.block;
                repaint();
                showBlockMenu (pressed.block);
                return;
            }
            if (! dragMoved && menuPress && pressed.what == Hit::note)
            {
                drag = Drag::none;
                selectedNote = pressed.block;
                repaint();
                showNoteMenu (pressed.block);
                return;
            }
            if (dragMoved && onViewChanged)
                onViewChanged();
            break;
        case Drag::box:
            break;
        case Drag::none:
            if (! dragMoved && (pressed.what == Hit::inTag || pressed.what == Hit::outTag))
                showTagMenu (pressed.node, pressed.what == Hit::outTag, pressed.port);
            break;
    }
    drag = Drag::none;
    repaint();
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTest (toWorld (e.position));
    if (hit.what == Hit::blockTitle)
    {
        editBlockTitle (hit.block);
        return;
    }
    if (hit.what == Hit::note)
    {
        editNote (hit.block);
        return;
    }
    if (hit.what == Hit::none && ! minimapRect().contains (e.position))
    {
        addNote (toWorld (e.position));
        return;
    }
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
        else if (textKey == "swing")
            current = juce::String (std::max (50, settingOf ("swing")));
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
    if (minimapRect().contains (e.position))
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto hit = hitTest (toWorld (e.position));
    const bool point = hit.what == Hit::zone || hit.what == Hit::inPort || hit.what == Hit::outPort || hit.what == Hit::inTag
                       || hit.what == Hit::outTag || hit.what == Hit::wire;
    setMouseCursor (hit.what == Hit::blockResize ? juce::MouseCursor::BottomRightCornerResizeCursor
                    : point                      ? juce::MouseCursor::PointingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
    const int over = hit.what == Hit::wire || hit.what == Hit::blockTitle || hit.what == Hit::blockResize || hit.what == Hit::note ? -1 : hit.node;
    std::string tag;
    if ((hit.what == Hit::inTag || hit.what == Hit::outTag) && hit.node >= 0)
    {
        const auto& n = graph().nodes[(size_t) hit.node];
        const auto& tags = hit.what == Hit::outTag ? n.tagOut : n.tagIn;
        if (auto it = tags.find (hit.port); it != tags.end())
            tag = it->second;
    }
    if (over != hoverNode || tag != hoverTag)
    {
        hoverNode = over;
        hoverTag = tag;
        repaint();
    }
}

void PatchCanvas::mouseExit (const juce::MouseEvent&)
{
    if (hoverNode >= 0 || ! hoverTag.empty())
    {
        hoverNode = -1;
        hoverTag.clear();
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
        auto& patch = graph();
        if (selectedBlock >= 0 && selectedBlock < (int) patch.blocks.size())
        {
            patch.blocks.erase (patch.blocks.begin() + selectedBlock);
            selectedBlock = -1;
            showMessage ("Removed the block. Its modules stay.");
            edited();
        }
        else if (selectedNote >= 0 && selectedNote < (int) patch.notes.size())
        {
            patch.notes.erase (patch.notes.begin() + selectedNote);
            selectedNote = -1;
            edited();
        }
        else
            deleteSelection();
        return true;
    }
    if (key.getTextCharacter() == 'm' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        setMinimapShown (! minimap);
        return true;
    }
    if (cmd ('g'))
    {
        groupSelection();
        return true;
    }
    if (cmd ('b'))
    {
        addBlock (toWorld (lastMouse), ! selection.empty());
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
        selectedBlock = selectedNote = -1;
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
            if (z.key == "follow" || z.key == "rate")
            {
                auto& settings = processor.patch().settings;
                changed = settings[z.key] != z.value;
                settings[z.key] = z.value;
                break;
            }
            if (z.key == "order")
            {
                changed = n.get ("rnd") != z.value;
                n.set ("rnd", z.value);
                break;
            }
            if (z.key == "mono")
            {
                changed = n.get ("mono") != z.value;
                n.set ("mono", z.value);
                break;
            }
            if (z.key == "shape")
            {
                static const char* shapes[] = { "sine", "tri", "saw", "square" };
                changed = n.mode != shapes[juce::jlimit (0, 3, z.value)];
                n.mode = shapes[juce::jlimit (0, 3, z.value)];
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
                n.cells[(size_t) z.value] = (n.cells[(size_t) z.value] + 1) % 3; // off, hit, accent
                changed = true;
            }
            break;
        case ZoneType::lane:
            showLaneMenu (index, z.value);
            break;
        case ZoneType::learn:
        {
            const int macro = juce::jlimit (0, ps::Engine::numMacros - 1, n.get ("macro", 1) - 1);
            if (processor.isLearningMacro (macro))
            {
                processor.learnMacroCc (-1);
                showMessage ("Stopped listening");
            }
            else
            {
                processor.learnMacroCc (macro);
                showMessage ("Move a knob or fader on your controller: the next MIDI CC received drives Macro " + juce::String (macro + 1)
                             + ". Click again to stop.", 8000);
            }
            break;
        }
        case ZoneType::target:
            showTargetMenu (index);
            break;
        case ZoneType::sample:
        {
            if (z.value == 0)
            {
                showSampleMenu (index);
                break;
            }
            const auto names = sampleChoices();
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
    if (key == "swing")
    {
        auto& settings = processor.patch().settings;
        const int before = settings.count ("swing") ? settings["swing"] : 50;
        settings["swing"] = juce::jlimit (50, 75, before + delta);
        changed = settings["swing"] != before;
    }
    else if (key.rfind ("lane", 0) == 0)
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
        showMessage ("A port only connects to its own kind: gate to gate (rings), audio to audio (squares), control to control (diamonds)");
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

void PatchCanvas::addSubsystem (const ps::presets::SubsystemTemplate& t, juce::Point<float> world)
{
    auto& patch = graph();
    const auto centre = world.x != std::numeric_limits<float>::max() ? world + juce::Point<float> (80, 60) : toWorld (getLocalBounds().getCentre().toFloat());
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
    const bool note = textTarget == TextTarget::noteText;
    ed.setMultiLine (note, true);
    ed.setReturnKeyStartsNewLine (note);
    ed.setInputRestrictions (note ? 2000 : textTarget == TextTarget::tag ? 16 : 32);
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
    if (textTarget == TextTarget::blockTitle)
    {
        if (textIndex >= 0 && textIndex < (int) patch.blocks.size() && patch.blocks[(size_t) textIndex].title != text.trim().toStdString())
        {
            patch.blocks[(size_t) textIndex].title = text.trim().toStdString();
            edited();
        }
        return;
    }
    if (textTarget == TextTarget::noteText)
    {
        if (textIndex >= 0 && textIndex < (int) patch.notes.size())
        {
            auto& t = patch.notes[(size_t) textIndex];
            if (text.trim().isEmpty())
            {
                patch.notes.erase (patch.notes.begin() + textIndex);
                selectedNote = -1;
                edited();
            }
            else if (t.text != text.trimEnd().toStdString())
            {
                t.text = text.trimEnd().toStdString();
                edited();
            }
        }
        return;
    }
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
        if (textKey.rfind ("name", 0) == 0)
        {
            const int q = std::stoi (textKey.substr (4));
            if (q < (int) n->lanes.size() && n->lanes[(size_t) q].name != typed.toStdString())
            {
                n->lanes[(size_t) q].name = typed.toStdString();
                edited();
            }
            return;
        }
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
        if (textKey == "swing")
        {
            const auto& settings = processor.patch().settings;
            const int now = settings.count ("swing") ? settings.at ("swing") : 50;
            applyParamDelta (index, textKey, typed.retainCharacters ("0123456789").getIntValue() - now, {});
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

// ---- blocks and notes ------------------------------------------------------------------------

void PatchCanvas::addBlock (juce::Point<float> world, bool aroundSelection)
{
    auto& patch = graph();
    ps::Block b;
    b.id = patch.freshId ("blk");
    b.page = subPath.empty() ? currentPage : std::string();
    b.colour = juce::String::toHexString ((int) ps::theme::swatches()[patch.blocks.size() % 8].rgb).paddedLeft ('0', 6).toStdString();
    b.title = "Section";
    juce::Rectangle<float> r;
    bool any = false;
    if (aroundSelection)
        for (auto& n : patch.nodes)
            if (selection.count (n.id) && onPage (n))
            {
                const auto box = layoutOf (n).box;
                r = any ? r.getUnion (box) : box;
                any = true;
            }
    if (any)
        r = r.expanded (24, 24).withTop (r.getY() - 44);
    else
        r = { world.x, world.y, 260, 170 };
    b.x = std::round (r.getX());
    b.y = std::round (r.getY());
    b.w = std::round (r.getWidth());
    b.h = std::round (r.getHeight());
    patch.blocks.push_back (b);
    selectedBlock = (int) patch.blocks.size() - 1;
    selectedNote = -1;
    edited();
    editBlockTitle (selectedBlock);
}

void PatchCanvas::addNote (juce::Point<float> world)
{
    auto& patch = graph();
    ps::Note t;
    t.id = patch.freshId ("note");
    t.page = subPath.empty() ? currentPage : std::string();
    t.text = "Note";
    t.x = std::round (world.x);
    t.y = std::round (world.y);
    t.w = 210;
    patch.notes.push_back (t);
    selectedNote = (int) patch.notes.size() - 1;
    selectedBlock = -1;
    edited();
    editNote (selectedNote);
}

juce::Rectangle<float> PatchCanvas::noteRect (const ps::Note& t) const
{
    const float w = t.w > 0 ? (float) t.w : 210.0f;
    juce::AttributedString s;
    s.append (u8 (t.text), ps::theme::sans (11.5f), ps::theme::textSoft);
    s.setLineSpacing (3.0f);
    juce::TextLayout layout;
    layout.createLayout (s, w);
    return { (float) t.x, (float) t.y, w, std::max (16.0f, layout.getHeight() + 4) };
}

void PatchCanvas::editBlockTitle (int block)
{
    const auto& blocks = graph().blocks;
    if (block < 0 || block >= (int) blocks.size())
        return;
    const auto& b = blocks[(size_t) block];
    textTarget = TextTarget::blockTitle;
    textIndex = block;
    const juce::Rectangle<float> r ((float) b.x + 24, (float) b.y + 2, std::min ((float) b.w - 30, 260.0f), 22);
    showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (b.title), "Section title");
}

void PatchCanvas::editNote (int note)
{
    const auto& notes = graph().notes;
    if (note < 0 || note >= (int) notes.size())
        return;
    const auto& t = notes[(size_t) note];
    textTarget = TextTarget::noteText;
    textIndex = note;
    auto r = noteRect (t).expanded (4, 4);
    r.setHeight (std::max (r.getHeight(), 72.0f));
    showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (t.text), "Write a note");
    showMessage ("Enter starts a new line. Click outside the note when you're done; Esc keeps the old text.", 6000);
}

void PatchCanvas::showBlockMenu (int block)
{
    auto& patch = graph();
    if (block < 0 || block >= (int) patch.blocks.size())
        return;
    const auto id = patch.blocks[(size_t) block].id;
    auto find = [this, id] (int& index) -> ps::Block* {
        auto& blocks = graph().blocks;
        for (int i = 0; i < (int) blocks.size(); ++i)
            if (blocks[(size_t) i].id == id)
            {
                index = i;
                return &blocks[(size_t) i];
            }
        return nullptr;
    };
    juce::PopupMenu menu, colours;
    menu.addSectionHeader (u8 (patch.blocks[(size_t) block].title));
    menu.addItem ("Rename", [this, find] {
        int i = -1;
        if (find (i))
            editBlockTitle (i);
    });
    const auto hexNow = patch.blocks[(size_t) block].colour;
    for (auto& sw : ps::theme::swatches())
    {
        const auto hex = juce::String::toHexString ((int) sw.rgb).paddedLeft ('0', 6).toStdString();
        colouredItem (colours, sw.name, ps::theme::rgb (sw.rgb), hex == hexNow, [this, find, hex] {
            int i = -1;
            if (auto* b = find (i))
            {
                b->colour = hex;
                edited();
            }
        });
    }
    menu.addSubMenu ("Colour", colours);
    menu.addItem ("Select the modules inside", [this, find] {
        int i = -1;
        if (auto* b = find (i))
        {
            const juce::Rectangle<float> area ((float) b->x, (float) b->y, (float) b->w, (float) b->h);
            selection.clear();
            for (auto& n : graph().nodes)
                if (onPage (n) && area.contains (layoutOf (n).box.getTopLeft()))
                    selection.insert (n.id);
            repaint();
        }
    });
    menu.addItem ("Fit to the modules inside", [this, find] {
        int i = -1;
        if (auto* b = find (i))
        {
            const juce::Rectangle<float> area ((float) b->x, (float) b->y, (float) b->w, (float) b->h);
            juce::Rectangle<float> r;
            bool any = false;
            for (auto& n : graph().nodes)
                if (onPage (n) && area.contains (layoutOf (n).box.getTopLeft()))
                {
                    r = any ? r.getUnion (layoutOf (n).box) : layoutOf (n).box;
                    any = true;
                }
            if (! any)
            {
                showMessage ("No module starts inside this block");
                return;
            }
            r = r.expanded (24, 24).withTop (r.getY() - 44);
            b->x = std::round (r.getX());
            b->y = std::round (r.getY());
            b->w = std::round (r.getWidth());
            b->h = std::round (r.getHeight());
            edited();
        }
    });
    menu.addSeparator();
    menu.addItem ("Delete block", [this, find] {
        int i = -1;
        if (find (i))
        {
            auto& blocks = graph().blocks;
            blocks.erase (blocks.begin() + i);
            selectedBlock = -1;
            edited();
        }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PatchCanvas::showNoteMenu (int note)
{
    auto& patch = graph();
    if (note < 0 || note >= (int) patch.notes.size())
        return;
    const auto id = patch.notes[(size_t) note].id;
    auto find = [this, id] (int& index) -> ps::Note* {
        auto& notes = graph().notes;
        for (int i = 0; i < (int) notes.size(); ++i)
            if (notes[(size_t) i].id == id)
            {
                index = i;
                return &notes[(size_t) i];
            }
        return nullptr;
    };
    juce::PopupMenu menu, widths;
    menu.addItem ("Edit text", [this, find] {
        int i = -1;
        if (find (i))
            editNote (i);
    });
    const double now = patch.notes[(size_t) note].w > 0 ? patch.notes[(size_t) note].w : 210;
    for (auto [name, w] : { std::pair { "Narrow", 150.0 }, std::pair { "Normal", 210.0 }, std::pair { "Wide", 320.0 }, std::pair { "Very wide", 480.0 } })
        widths.addItem (name, true, now == w, [this, find, w] {
            int i = -1;
            if (auto* t = find (i))
            {
                t->w = w;
                edited();
            }
        });
    menu.addSubMenu ("Width", widths);
    menu.addSeparator();
    menu.addItem ("Delete note", [this, find] {
        int i = -1;
        if (find (i))
        {
            auto& notes = graph().notes;
            notes.erase (notes.begin() + i);
            selectedNote = -1;
            edited();
        }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// ---- subsystem from a selection --------------------------------------------------------------

void PatchCanvas::groupSelection()
{
    auto& patch = graph();
    std::set<std::string> moving;
    juce::Rectangle<float> bounds;
    for (auto& n : patch.nodes)
        if (selection.count (n.id) && onPage (n) && n.kind != "subin" && n.kind != "subout")
        {
            const auto box = layoutOf (n).box;
            bounds = moving.empty() ? box : bounds.getUnion (box);
            moving.insert (n.id);
        }
    if (moving.empty())
    {
        showMessage ("Select the modules to group first");
        return;
    }
    juce::String name = "Subsystem";
    for (int i = 2; std::any_of (patch.nodes.begin(), patch.nodes.end(), [&] (auto& m) { return m.kind == "sub" && u8 (m.name) == name; }); ++i)
        name = "Subsystem " + juce::String (i);
    const auto result = ps::groupIntoSubsystem (patch, moving, name.toStdString(), bounds.getX(), bounds.getY(), bounds.getWidth());
    if (auto* sub = patch.find (result.id))
        sub->page = subPath.empty() ? currentPage : std::string();
    selection = { result.id };
    selectedWire = -1;
    edited();
    showMessage ("Grouped " + juce::String ((int) moving.size()) + (moving.size() == 1 ? " module" : " modules") + " into a subsystem with "
                 + juce::String (result.inputs) + (result.inputs == 1 ? " input and " : " inputs and ") + juce::String (result.outputs)
                 + (result.outputs == 1 ? " output" : " outputs")
                 + (result.tags ? ", including " + juce::String (result.tags) + (result.tags == 1 ? " tag" : " tags") : juce::String())
                 + ". Double-click it to look inside.",
                 7000);
}

// ---- library drops ---------------------------------------------------------------------------

bool PatchCanvas::isInterestedInDragSource (const SourceDetails& d) { return d.description.isObject() && onLibraryDrop != nullptr; }

void PatchCanvas::itemDragEnter (const SourceDetails&)
{
    dropHover = true;
    repaint();
}

void PatchCanvas::itemDragExit (const SourceDetails&)
{
    dropHover = false;
    repaint();
}

void PatchCanvas::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    if (onLibraryDrop)
        onLibraryDrop (d.description, toWorld (d.localPosition.toFloat()));
    repaint();
}

