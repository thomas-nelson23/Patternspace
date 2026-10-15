// How the canvas is drawn: modules and their bodies, cables, tags, blocks, notes, the minimap and
// the glyph each kind of module wears in its header.

#include "PatchCanvasDetail.h"
#include "PluginProcessor.h"

#include "../core/Patterns.h"

#include <cmath>
#include <limits>
#include <utility>

using namespace ps::canvas;

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
        paintTagLinks (g);
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

    paintMinimap (g);
    if (dropHover)
    {
        g.setColour (cyan.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3), radius, 2.0f);
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
        { "Click a port", "Tag it: pick a tag, colour it, see who listens" },
        { "Click a MIDI output's name", "Its menu: note, channel, choke group, audition" },
        { "Drop an audio file", "On a Sample player to load it there, or on empty space for a new one" },
        { "Target... on a control", "Pick the module and parameter a Macro, LFO, walk or S&H steers" },
        { "Double-click empty space", "Write a note. Right-click for a block" },
        { "Ctrl/Cmd G, B", "Group the selection into a subsystem; draw a block around it" },
        { "M", "Minimap on or off. Click it to move around" },
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
    const auto& blocks = graph().blocks;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        const auto& b = blocks[(size_t) i];
        if (! b.page.empty() && b.page != currentPage)
            continue;
        const auto c = ps::theme::fromHex (b.colour, ps::theme::textDim);
        const juce::Rectangle<float> r ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
        const bool sel = i == selectedBlock;
        // A section: a faint wash of its colour and a hairline edge, labelled in tracked capitals.
        g.setColour (c.withAlpha (0.035f));
        g.fillRoundedRectangle (r, ps::theme::radius);
        g.setColour (c.withAlpha (sel ? 0.6f : 0.22f));
        g.drawRoundedRectangle (r.reduced (0.5f), ps::theme::radius, sel ? 1.5f : 1.0f);
        if (sel || (drag == Drag::blockResize && dragBlock == i))
        {
            // the resize handle: two short diagonal lines in the corner
            juce::Path grip;
            for (float k = 4; k <= 10; k += 3)
            {
                grip.startNewSubPath (r.getRight() - k, r.getBottom() - 3);
                grip.lineTo (r.getRight() - 3, r.getBottom() - k);
            }
            g.setColour (c.withAlpha (0.8f));
            g.strokePath (grip, juce::PathStrokeType (1.2f));
        }
        ps::theme::drawLed (g, { r.getX() + 15, r.getY() + 13 }, c, true, 5.0f);
        g.setFont (ps::theme::caps (10.0f, 0.22f));
        g.setColour (c.interpolatedWith (ps::theme::text, 0.45f));
        g.drawText (u8 (b.title).toUpperCase(), juce::Rectangle<float> (r.getX() + 26, r.getY() + 1, r.getWidth() - 32, 24),
                    juce::Justification::centredLeft, true);
    }
}

void PatchCanvas::paintNotes (juce::Graphics& g)
{
    const auto& notes = graph().notes;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& t = notes[(size_t) i];
        if (! t.page.empty() && t.page != currentPage)
            continue;
        const auto r = noteRect (t);
        if (i == selectedNote)
        {
            g.setColour (ps::theme::cyan.withAlpha (0.45f));
            g.drawRoundedRectangle (r.expanded (6, 4), 4, 1.0f);
        }
        juce::AttributedString s;
        s.append (u8 (t.text), ps::theme::sans (11.5f), ps::theme::textSoft);
        s.setLineSpacing (3.0f);
        s.draw (g, r.withHeight (r.getHeight() + 8));
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
        const auto type = ps::outputType (a, link.p);
        const bool audio = type == ps::PortType::audio, control = type == ps::PortType::control;
        const bool reset = ps::inputsOf (b)[(size_t) link.q] == "rst";
        const bool lit = ! control && viewOut (ia, link.p) > 0 && ! (a.kind == "clock" && link.p == 0);
        auto path = wirePath (La.outs[(size_t) link.p], Lb.ins[(size_t) link.q]);
        const float width = (audio ? 4.0f : control ? 1.8f : 2.2f) + (lit ? 1.4f : 0.0f);
        g.setColour (l == selectedWire ? ps::theme::textBright : kindColour (a).withAlpha (lit ? 1.0f : (audio ? 0.55f : control ? 0.8f : 0.6f)));
        if (reset || control)
        {
            // reset wires are dashed, control wires dotted
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            const float dots[] = { 2.0f, 3.0f };
            juce::PathStrokeType (width).createDashedStroke (dashed, path, control ? dots : dashes, 2);
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
    {
        // The kind's glyph; a collapsed module lights it while any output fires, so a closed box
        // still shows signal moving.
        bool active = false;
        if (n.collapsed)
            for (int p = 0; p < (int) outs.size() && ! active; ++p)
                active = viewOut (index, p) > 0 && ! (n.kind == "clock" && p == 0);
        paintKindGlyph (g, n.kind, { h.getX() + 26, cy }, active ? textBright : c);
    }
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

    // port labels (MIDI outs and ins show each output's name and note instead)
    if (! n.collapsed)
    {
        g.setFont (mono (10.0f));
        if (n.kind == "midi" || n.kind == "midiin")
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
                const int v = n.kind == "midiin" ? viewOut (index, q) : viewIn (index, q);
                if (n.kind == "midi")
                    drawLed (g, { L.box.getRight() - 8, ry + rowH / 2 }, ps::isAccent (v) ? textBright : c, v > 0);
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
            const auto type = dir == 0 ? ps::inputType (n, i) : ps::outputType (n, i);
            const bool audio = type == ps::PortType::audio, control = type == ps::PortType::control;
            const int v = dir == 0 ? viewIn (index, i) : viewOut (index, i);
            const bool lit = v > 0 && ! (n.kind == "clock" && dir == 1 && i == 0) && ! audio && ! control;
            const bool target = wiring && (dir == 0) == fromOut && index != pressed.node && type == wireType;
            const auto r = juce::Rectangle<float> (10, 10).withCentre (pts[(size_t) i]).expanded (target ? 2.0f : 0.0f);
            if (wiring && ! target)
                g.setOpacity (0.35f);
            else
                g.setOpacity (1.0f);
            // gates are rings, audio ports squares, controls diamonds
            juce::Path diamond;
            if (control)
                diamond.addQuadrilateral (r.getCentreX(), r.getY() - 1, r.getRight() + 1, r.getCentreY(), r.getCentreX(), r.getBottom() + 1, r.getX() - 1, r.getCentreY());
            g.setColour (panelBottom);
            if (audio)
                g.fillRect (r);
            else if (control)
                g.fillPath (diamond);
            else
                g.fillEllipse (r);
            g.setColour (lit ? textBright : c);
            if (audio)
                g.drawRect (r, 2.0f);
            else if (control)
                g.strokePath (diamond, juce::PathStrokeType (1.8f));
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
            const bool hot = tag == hoverTag;
            g.setColour (tc.withAlpha (lit || hot ? 1.0f : 0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), pill, hot ? 1.8f : 1.0f);
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
    for (auto& it : bodyItems (n, L.box.getX(), L.box.getY() + L.bodyTop, L.box.getWidth(), processor.patch().settings, graph(), processor.patch()))
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
                const bool combine = it.key == "combine", follow = it.key == "follow", rate = it.key == "rate", order = it.key == "order", shape = it.key == "shape",
                           mono = it.key == "mono";
                const auto& modes = ps::combineModes();
                const int count = combine ? (int) modes.size() : shape ? 4 : follow || rate ? 3 : 2;
                static const char* shapeNames[] = { "Sine", "Tri", "Saw", "Square" };
                static const char* shapeModes[] = { "sine", "tri", "saw", "square" };
                const int ways = n.get ("ways", 4);
                const int followNow = settingOf ("follow");
                const int rateNow = juce::jlimit (0, 2, settingOf ("rate"));
                static const char* followNames[] = { "Phrase", "Song", "Free" };
                static const char* rateNames[] = { "1/16", "1/32", "1/8" };
                static const char* orderNames[] = { "In turn", "Random" };
                static const char* monoNames[] = { "Poly", "Mono" };
                for (int m = 0; m < count; ++m)
                {
                    const auto b = juce::Rectangle<float> (r.getX() + r.getWidth() * m / count, r.getY(), r.getWidth() / count, r.getHeight()).reduced (1, 0);
                    const bool on = combine ? n.mode == modes[(size_t) m]
                                  : follow  ? m == followNow
                                  : rate    ? m == rateNow
                                  : order   ? m == (n.get ("rnd") ? 1 : 0)
                                  : mono    ? m == (n.get ("mono") ? 1 : 0)
                                  : shape   ? n.mode == shapeModes[m]
                                            : (m == 0) == (n.mode != "merge");
                    paintSegment (g, b, on, c);
                    g.setColour (on ? onAccent : textSoft);
                    g.setFont (sans (9.5f, on ? Weight::semibold : Weight::regular));
                    const juce::String arrow = juce::String::fromUTF8 (" \xE2\x86\x92 ");
                    const auto caption = combine ? u8 (modes[(size_t) m])
                                       : follow  ? juce::String (followNames[m])
                                       : rate    ? juce::String (rateNames[m])
                                       : order   ? juce::String (orderNames[m])
                                       : mono    ? juce::String (monoNames[m])
                                       : shape   ? juce::String (shapeNames[m])
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
            {
                // A hairline pill. A Macro's CC button shows the controller it follows, or that it
                // is waiting for one to move.
                auto label = it.text;
                bool hot = false;
                if (it.key == "learn")
                {
                    const int macro = juce::jlimit (0, ps::Engine::numMacros - 1, n.get ("macro", 1) - 1);
                    hot = processor.isLearningMacro (macro);
                    const int cc = processor.macroCc (macro);
                    label = hot ? "Move a controller..." : cc >= 0 ? "MIDI CC " + juce::String (cc) + "  \xC2\xB7  Learn" : "Learn MIDI CC";
                }
                g.setColour (hot ? c : pillEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2, 1);
                g.setColour (hot ? c : textSoft);
                g.setFont (sans (10.5f));
                g.drawText (label, r, juce::Justification::centred, true);
                break;
            }
            case ItemType::ctlBar:
            {
                // the control's value now, on a track like the audio meter's
                const auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
                g.setColour (cellOff);
                g.fillRoundedRectangle (track, 2);
                const float level = juce::jlimit (0.0f, 1.0f, (float) viewOut (index, 0) / 1000.0f);
                if (level > 0.0f)
                {
                    g.setColour (c);
                    g.fillRoundedRectangle (track.withWidth (std::max (3.0f, track.getWidth() * level)), 2);
                }
                break;
            }
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

void PatchCanvas::paintTagLinks (juce::Graphics& g)
{
    if (hoverTag.empty())
        return;
    const auto& patch = graph();
    const ps::Node* owner = nullptr;
    int ownerPort = 0;
    for (auto& m : patch.nodes)
        for (auto& [p, t] : m.tagOut)
            if (t == hoverTag && onPage (m))
            {
                owner = &m;
                ownerPort = p;
            }
    if (owner == nullptr)
        return;
    const auto La = layoutOf (*owner);
    if (ownerPort >= (int) La.outs.size())
        return;
    const auto c = tagColour (hoverTag);
    const float dashes[] = { 4.0f, 5.0f };
    for (auto& m : patch.nodes)
    {
        if (! onPage (m) || m.id == owner->id)
            continue;
        const auto Lb = layoutOf (m);
        for (auto& [q, t] : m.tagIn)
            if (t == hoverTag && q < (int) Lb.ins.size())
            {
                juce::Path dashed;
                juce::PathStrokeType (1.6f).createDashedStroke (dashed, wirePath (La.outs[(size_t) ownerPort], Lb.ins[(size_t) q]), dashes, 2);
                g.setColour (c.withAlpha (0.75f));
                g.fillPath (dashed);
            }
    }
}

// ---- minimap ---------------------------------------------------------------------------------

juce::Rectangle<float> PatchCanvas::minimapWorldBounds() const
{
    juce::Rectangle<float> box;
    bool any = false;
    const auto& g = graph();
    for (auto& n : g.nodes)
        if (onPage (n))
        {
            const auto b = layoutOf (n).box;
            box = any ? box.getUnion (b) : b;
            any = true;
        }
    for (auto& b : g.blocks)
        if (b.page.empty() || b.page == currentPage)
        {
            const juce::Rectangle<float> r ((float) b.x, (float) b.y, (float) b.w, (float) b.h);
            box = any ? box.getUnion (r) : r;
            any = true;
        }
    if (! any)
        return {};
    // The view counts too, so the map never loses the viewport.
    const juce::Rectangle<float> view (toWorld ({ 0, 0 }), toWorld (getLocalBounds().getBottomRight().toFloat()));
    return box.expanded (40).getUnion (view);
}

juce::Rectangle<float> PatchCanvas::minimapRect() const
{
    if (! minimap || getWidth() < 420 || getHeight() < 300 || minimapWorldBounds().isEmpty())
        return {};
    return { (float) getWidth() - 176, (float) getHeight() - 158, 160, 104 };
}

void PatchCanvas::paintMinimap (juce::Graphics& g)
{
    using namespace ps::theme;
    const auto r = minimapRect();
    if (r.isEmpty())
        return;
    const auto world = minimapWorldBounds();
    const float k = std::min ((r.getWidth() - 8) / world.getWidth(), (r.getHeight() - 8) / world.getHeight());
    const auto map = [&] (juce::Rectangle<float> w) {
        return juce::Rectangle<float> (r.getX() + 4 + (w.getX() - world.getX()) * k, r.getY() + 4 + (w.getY() - world.getY()) * k, w.getWidth() * k, w.getHeight() * k);
    };
    g.setColour (bg.withAlpha (0.88f));
    g.fillRoundedRectangle (r, 6);
    g.setColour (glassEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6, 1);
    const auto& patch = graph();
    for (auto& b : patch.blocks)
        if (b.page.empty() || b.page == currentPage)
        {
            g.setColour (fromHex (b.colour, textDim).withAlpha (0.18f));
            g.fillRect (map ({ (float) b.x, (float) b.y, (float) b.w, (float) b.h }));
        }
    for (auto& n : patch.nodes)
        if (onPage (n))
        {
            g.setColour (kindColour (n).withAlpha (selection.count (n.id) ? 1.0f : 0.7f));
            auto box = map (layoutOf (n).box);
            g.fillRect (box.withSizeKeepingCentre (std::max (2.0f, box.getWidth()), std::max (2.0f, box.getHeight())));
        }
    const juce::Rectangle<float> view (toWorld ({ 0, 0 }), toWorld (getLocalBounds().getBottomRight().toFloat()));
    g.setColour (cyan.withAlpha (0.9f));
    g.drawRect (map (view), 1.0f);
}

void PatchCanvas::setMinimapShown (bool on)
{
    minimap = on;
    showMessage (on ? "Minimap on. Click or drag on it to move around" : "Minimap off");
}

// ---- header glyphs ---------------------------------------------------------------------------

// A small sign per kind of module, drawn in its colour: the shape the mockups gave each one.
void PatchCanvas::paintKindGlyph (juce::Graphics& g, const std::string& k, juce::Point<float> c, juce::Colour colour)
{
    using namespace ps::theme;
    juce::Path p;
    const juce::PathStrokeType thin (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colour);
    if (k == "clock")
    {
        g.drawEllipse (juce::Rectangle<float> (10, 10).withCentre (c), 1.2f);
        p.startNewSubPath (c);
        p.lineTo (c.translated (0, -3.5f));
        p.startNewSubPath (c);
        p.lineTo (c.translated (2.5f, 1.5f));
        g.strokePath (p, thin);
    }
    else if (k == "euclid")
    {
        for (int i = 0; i < 8; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const auto d = c.translated (std::sin (a) * 4.5f, -std::cos (a) * 4.5f);
            if (i % 3 == 0)
                g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre (d));
            else
                g.fillEllipse (juce::Rectangle<float> (1.4f, 1.4f).withCentre (d));
        }
    }
    else if (k == "random")
    {
        g.drawRoundedRectangle (juce::Rectangle<float> (10, 10).withCentre (c), 2, 1.2f);
        for (auto d : { c.translated (-2.5f, -2.5f), c, c.translated (2.5f, 2.5f) })
            g.fillEllipse (juce::Rectangle<float> (2, 2).withCentre (d));
    }
    else if (k == "ca")
    {
        for (int i = 0; i < 9; ++i)
        {
            const auto cell = juce::Rectangle<float> (2.6f, 2.6f).withCentre (c.translated ((float) (i % 3 - 1) * 3.4f, (float) (i / 3 - 1) * 3.4f));
            if ((0x1a5 >> i) & 1)
                g.fillRect (cell);
            else
                g.drawRect (cell, 0.6f);
        }
    }
    else if (k == "step")
    {
        for (int i = 0; i < 4; ++i)
        {
            const float h = i == 1 ? 9.0f : i == 3 ? 7.0f : 4.0f;
            g.fillRect (juce::Rectangle<float> (2.0f, h).withCentre (c.translated ((float) i * 3.0f - 4.5f, 0)).withBottomY (c.y + 4.5f));
        }
    }
    else if (k == "grids")
    {
        g.drawRect (juce::Rectangle<float> (10, 10).withCentre (c), 1.0f);
        p.startNewSubPath (c.translated (-5, 0));
        p.lineTo (c.translated (5, 0));
        p.startNewSubPath (c.translated (0, -5));
        p.lineTo (c.translated (0, 5));
        g.strokePath (p, juce::PathStrokeType (0.7f));
        g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (c.translated (1.5f, -1.5f)));
    }
    else if (k == "div")
    {
        p.startNewSubPath (c.translated (-4.5f, 0));
        p.lineTo (c.translated (4.5f, 0));
        g.strokePath (p, thin);
        g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre (c.translated (0, -3.5f)));
        g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre (c.translated (0, 3.5f)));
    }
    else if (k == "accent")
    {
        p.startNewSubPath (c.translated (-4, -4));
        p.lineTo (c.translated (4, 0));
        p.lineTo (c.translated (-4, 4));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
    else if (k == "prob")
    {
        p.startNewSubPath (c.translated (4, -4.5f));
        p.lineTo (c.translated (-4, 4.5f));
        g.strokePath (p, thin);
        g.drawEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (c.translated (-3, -3)), 1.0f);
        g.drawEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (c.translated (3, 3)), 1.0f);
    }
    else if (k == "combine")
    {
        g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre (c.translated (-2, 0)), 1.1f);
        g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre (c.translated (2, 0)), 1.1f);
    }
    else if (k == "switch")
    {
        p.startNewSubPath (c.translated (-5, 0));
        p.lineTo (c.translated (-1, 0));
        p.lineTo (c.translated (4, -4));
        p.startNewSubPath (c.translated (-1, 0));
        p.lineTo (c.translated (4, 4));
        g.strokePath (p, thin);
        g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre (c.translated (4, -4)));
    }
    else if (k == "sample")
    {
        p.startNewSubPath (c.translated (-5, 0));
        const float ys[] = { -1, 4, -5, 3, -2, 1 };
        for (int i = 0; i < 6; ++i)
            p.lineTo (c.translated (-5 + 2 * (float) (i + 1) - 1, ys[i]));
        p.lineTo (c.translated (5, 0));
        g.strokePath (p, thin);
    }
    else if (k == "audio")
    {
        p.startNewSubPath (c.translated (-5, -2));
        p.lineTo (c.translated (-2, -2));
        p.lineTo (c.translated (1.5f, -5));
        p.lineTo (c.translated (1.5f, 5));
        p.lineTo (c.translated (-2, 2));
        p.lineTo (c.translated (-5, 2));
        p.closeSubPath();
        g.fillPath (p);
        g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre (c.translated (1, 0)).withLeft (c.x + 3.5f), 1.0f);
    }
    else if (k == "midi")
    {
        g.drawEllipse (juce::Rectangle<float> (10.5f, 10.5f).withCentre (c), 1.1f);
        for (int i = -2; i <= 2; ++i)
        {
            const float a = (float) i * 0.62f;
            g.fillEllipse (juce::Rectangle<float> (1.8f, 1.8f).withCentre (c.translated (std::sin (a) * 3.2f, -std::cos (a) * 3.2f)));
        }
    }
    else if (k == "sub")
    {
        g.drawRoundedRectangle (juce::Rectangle<float> (11, 9).withCentre (c), 2, 1.1f);
        g.fillRoundedRectangle (juce::Rectangle<float> (5, 3.5f).withCentre (c), 1);
    }
    else if (k == "vel")
    {
        // three bars rising, like a velocity lane
        for (int i = 0; i < 3; ++i)
            g.fillRect (juce::Rectangle<float> (2.4f, 3.0f + i * 3.0f).withCentre (c.translated ((float) i * 4.0f - 4.0f, 0)).withBottomY (c.y + 5));
    }
    else if (k == "shift")
    {
        p.startNewSubPath (c.translated (-5, -3));
        p.lineTo (c.translated (2, -3));
        p.startNewSubPath (c.translated (-2, 3));
        p.lineTo (c.translated (5, 3));
        g.strokePath (p, thin);
        g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre (c.translated (-5, -3)));
        g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre (c.translated (-2, 3)));
    }
    else if (k == "ratchet")
    {
        for (int i = 0; i < 4; ++i)
            g.fillRect (juce::Rectangle<float> (1.6f, 9.0f).withCentre (c.translated ((float) i * 3.0f - 4.5f, 0)));
    }
    else if (k == "fill")
    {
        p.startNewSubPath (c.translated (-5, 4));
        p.lineTo (c.translated (-2, 1));
        p.lineTo (c.translated (0, 3));
        p.lineTo (c.translated (2, -2));
        p.lineTo (c.translated (5, -5));
        g.strokePath (p, thin);
    }
    else if (k == "scene")
    {
        for (int i = 0; i < 3; ++i)
            g.drawRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c.translated ((float) i * 4.5f - 4.5f, 0)), i == 1 ? 2.0f : 0.8f);
    }
    else if (k == "macro")
    {
        // a knob: a ring with its pointer
        g.drawEllipse (juce::Rectangle<float> (10, 10).withCentre (c), 1.3f);
        p.startNewSubPath (c);
        p.lineTo (c.translated (-3.0f, -3.5f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (k == "lfo")
    {
        p.startNewSubPath (c.translated (-5.5f, 0));
        p.cubicTo (c.translated (-4, -6), c.translated (-1.5f, -6), c.translated (0, 0));
        p.cubicTo (c.translated (1.5f, 6), c.translated (4, 6), c.translated (5.5f, 0));
        g.strokePath (p, thin);
    }
    else if (k == "walk")
    {
        p.startNewSubPath (c.translated (-5.5f, 2));
        const float ys[] = { -3, 1, -4, 3, -1, 2 };
        for (int i = 0; i < 6; ++i)
            p.lineTo (c.translated (-5.5f + 2.0f * (float) (i + 1), ys[i]));
        g.strokePath (p, thin);
    }
    else if (k == "sh")
    {
        // steps: a value held, then another
        p.startNewSubPath (c.translated (-5.5f, 3));
        p.lineTo (c.translated (-2, 3));
        p.lineTo (c.translated (-2, -4));
        p.lineTo (c.translated (2, -4));
        p.lineTo (c.translated (2, 0));
        p.lineTo (c.translated (5.5f, 0));
        g.strokePath (p, thin);
    }
    else if (k == "cc")
    {
        // a dial's arc with its pointer
        p.addCentredArc (c.x, c.y + 1, 5, 5, 0, -2.4f, 2.4f, true);
        g.strokePath (p, thin);
        g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre (c.translated (0, 1)));
    }
    else if (k == "midiin")
    {
        g.drawEllipse (juce::Rectangle<float> (10.5f, 10.5f).withCentre (c), 1.1f);
        p.startNewSubPath (c.translated (-2.5f, 0));
        p.lineTo (c.translated (2.5f, 0));
        p.startNewSubPath (c.translated (0.5f, -2.2f));
        p.lineTo (c.translated (2.7f, 0));
        p.lineTo (c.translated (0.5f, 2.2f));
        g.strokePath (p, thin);
    }
    else if (k == "subin" || k == "subout")
    {
        const float dir = k == "subin" ? 1.0f : 1.0f;
        p.startNewSubPath (c.translated (-5 * dir, 0));
        p.lineTo (c.translated (3 * dir, 0));
        p.startNewSubPath (c.translated (0, -3.5f));
        p.lineTo (c.translated (3.5f * dir, 0));
        p.lineTo (c.translated (0, 3.5f));
        g.strokePath (p, thin);
        g.fillRect (juce::Rectangle<float> (1.4f, 10).withCentre (c.translated (k == "subin" ? -5.5f : 5.5f, 0)));
    }
    else
        drawLed (g, c, colour, true, 5.0f);
}

