#include "PluginEditor.h"

#include "../core/Modules.h"
#include "../state/PatchJson.h"
#include "Presets.h"
#include "Theme.h"

namespace
{
constexpr int topBarH = ps::theme::topbarH, tabRowH = 38, libraryW = ps::theme::sidebarW;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void showError (const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, title, message), nullptr);
}

// The wordmark, as the dashboard writes its own: "PATTERN" + "SPACE" in Michroma capitals, with
// "SPACE" in red and a faint red glow.
void drawBrand (juce::Graphics& g, juce::Rectangle<float> area)
{
    using namespace ps::theme;
    const auto f = display (15.0f).withExtraKerningFactor (0.08f);
    g.setFont (f);
    const float w = juce::GlyphArrangement::getStringWidth (f, "PATTERN");
    g.setColour (textBright);
    g.drawText ("PATTERN", area.withWidth (w + 2), juce::Justification::centredLeft, false);
    const auto accent = area.withTrimmedLeft (w);
    for (const auto& [dx, dy] : { std::pair (0.0f, 1.5f), std::pair (0.0f, -1.5f), std::pair (1.5f, 0.0f), std::pair (-1.5f, 0.0f) })
    {
        g.setColour (red.withAlpha (0.14f));
        g.drawText ("SPACE", accent.translated (dx, dy), juce::Justification::centredLeft, false);
    }
    g.setColour (red);
    g.drawText ("SPACE", accent, juce::Justification::centredLeft, false);
}
} // namespace

// ---- the module library ----------------------------------------------------------------------

class PatternspaceEditor::Library : public juce::Component
{
    struct Row
    {
        juce::String text;
        juce::Colour colour;
        std::string kind;
        int subsystem = -1; // index into the factory subsystems, 1000+ the user's, -2 to read one from a file, -3 for a new one
        bool header = false;
    };

public:
    Library (PatchCanvas& c, bool midiEffect, std::function<void()> importFromFile) : canvas (c), noSound (midiEffect), onImport (std::move (importFromFile))
    {
        search.setFont (ps::theme::sans (12.5f));
        search.setColour (juce::TextEditor::backgroundColourId, ps::theme::ink);
        search.setColour (juce::TextEditor::textColourId, ps::theme::text);
        search.setColour (juce::TextEditor::outlineColourId, ps::theme::hair);
        search.setColour (juce::TextEditor::focusedOutlineColourId, ps::theme::cyan);
        search.setTextToShowWhenEmpty ("Search modules", ps::theme::textDim);
        search.setSelectAllWhenFocused (true);
        search.onTextChange = [this] { rebuild(); };
        search.onEscapeKey = [this] {
            search.setText ({});
            canvas.grabKeyboardFocus();
        };
        search.onReturnKey = [this] {
            // Enter adds the first match.
            for (auto& r : rows)
                if (! r.header && (! r.kind.empty() || r.subsystem >= 0))
                {
                    add (r, { std::numeric_limits<float>::max(), 0 });
                    break;
                }
            canvas.grabKeyboardFocus();
        };
        addAndMakeVisible (search);
        rebuild();
        list.owner = this;
        list.setSize (libraryW, height());
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
        canvas.onLibraryDrop = [this] (const juce::var& item, juce::Point<float> world) {
            const int index = (int) item["row"];
            if (index >= 0 && index < (int) rows.size())
                add (rows[(size_t) index], world);
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        search.setBounds (r.removeFromTop (34).reduced (16, 4));
        viewport.setBounds (r);
        list.setSize (viewport.getMaximumVisibleWidth(), height());
    }

    // Adds the row's module or subsystem to the patch, at a world point or wherever there is room.
    void add (const Row& r, juce::Point<float> world)
    {
        if (! r.kind.empty())
            canvas.addModule (r.kind, world);
        else if (r.subsystem == -3)
            canvas.newSubsystem();
        else if (r.subsystem >= 1000)
        {
            const auto& entry = userSubsystems[(size_t) (r.subsystem - 1000)];
            ps::presets::SubsystemTemplate t;
            if (ps::presets::subsystemFromVar (juce::JSON::parse (entry.file.loadFileAsString()), t))
                canvas.addSubsystem (t, world);
            else
                showError ("Couldn't add " + entry.name, "The file in your Subsystems folder isn't a subsystem any more.");
        }
        else if (r.subsystem >= 0)
            canvas.addSubsystem (ps::presets::subsystems()[(size_t) r.subsystem], world);
        else if (onImport)
            onImport();
    }

    void paint (juce::Graphics& g) override { g.fillAll (ps::theme::bg); }

    // Lists the modules, the factory subsystems and the user's saved subsystems again.
    void rebuild()
    {
        rows.clear();
        const auto query = search.getText().trim();
        auto matches = [&query] (const juce::String& text) { return query.isEmpty() || text.containsIgnoreCase (query); };
        for (auto& section : ps::librarySections())
        {
            if (noSound && juce::String (section.title) == "Sound")
                continue; // the MIDI effect has no audio outputs
            rows.push_back ({ section.title, {}, {}, -1, true });
            for (auto& kind : section.kinds)
                if (auto* info = ps::kindInfo (kind))
                    if (matches (info->title) || matches (section.title))
                        rows.push_back ({ info->title, ps::theme::rgb (info->colour), kind, -1, false });
            if (rows.back().header)
                rows.pop_back();
        }
        rows.push_back ({ "Subsystems", {}, {}, -1, true });
        if (query.isEmpty())
            rows.push_back ({ "New subsystem", juce::Colours::transparentBlack, {}, -3, false });
        const auto& subs = ps::presets::subsystems();
        for (int i = 0; i < (int) subs.size(); ++i)
            if (matches (subs[(size_t) i].name))
                rows.push_back ({ subs[(size_t) i].name, ps::theme::text, {}, i, false });
        userSubsystems = ps::library::subsystems();
        for (int i = 0; i < (int) userSubsystems.size(); ++i)
            if (matches (userSubsystems[(size_t) i].name))
                rows.push_back ({ userSubsystems[(size_t) i].name, ps::theme::textSoft, {}, 1000 + i, false });
        if (query.isEmpty())
            rows.push_back ({ "Add from file...", ps::theme::textDim, {}, -2, false });
        else if (rows.back().header)
            rows.pop_back();
        if (rows.empty())
            rows.push_back ({ "Nothing matches", {}, {}, -1, true });
        list.setSize (std::max (1, viewport.getMaximumVisibleWidth()), height());
        list.repaint();
    }

private:
    std::vector<ps::library::Entry> userSubsystems;

    static constexpr int rowH = 28, headerH = 34; // the dashboard's menu rows, tightened to fit the whole library

    int height() const
    {
        int h = 8;
        for (auto& r : rows)
            h += r.header ? headerH : rowH;
        return h + 8;
    }

    int rowAt (int y) const
    {
        int top = 8;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const int h = rows[(size_t) i].header ? headerH : rowH;
            if (y >= top && y < top + h)
                return rows[(size_t) i].header ? -1 : i;
            top += h;
        }
        return -1;
    }

    struct List : public juce::Component
    {
        Library* owner = nullptr;
        int hover = -1;

        void paint (juce::Graphics& g) override
        {
            using namespace ps::theme;
            int top = 8;
            for (int i = 0; i < (int) owner->rows.size(); ++i)
            {
                const auto& r = owner->rows[(size_t) i];
                if (r.header)
                {
                    g.setColour (textDim);
                    g.setFont (caps (9.5f, 0.22f));
                    g.drawText (r.text.toUpperCase(), juce::Rectangle<int> (20, top + 14, getWidth() - 30, 16), juce::Justification::centredLeft);
                    top += headerH;
                    continue;
                }
                // Quiet menu rows, as in the dashboard: no fill, the name turns white under the mouse.
                const juce::Rectangle<float> box (20.0f, (float) top, (float) getWidth() - 30, (float) rowH);
                const bool hot = i == hover;
                const auto mark = juce::Point<float> (box.getX() + 4, box.getCentreY());
                if (r.subsystem == -3 || r.subsystem == -2)
                {
                    // Actions take a small red line icon: a plus, or an arrow into a tray.
                    juce::Path icon;
                    const auto m = mark.translated (1, 0);
                    if (r.subsystem == -3)
                    {
                        icon.startNewSubPath (m.translated (-4, 0));
                        icon.lineTo (m.translated (4, 0));
                        icon.startNewSubPath (m.translated (0, -4));
                        icon.lineTo (m.translated (0, 4));
                    }
                    else
                    {
                        icon.startNewSubPath (m.translated (0, -5));
                        icon.lineTo (m.translated (0, 1.5f));
                        icon.startNewSubPath (m.translated (-2.5f, -1));
                        icon.lineTo (m.translated (0, 1.5f));
                        icon.lineTo (m.translated (2.5f, -1));
                        icon.startNewSubPath (m.translated (-5, 1.5f));
                        icon.lineTo (m.translated (-5, 4.5f));
                        icon.lineTo (m.translated (5, 4.5f));
                        icon.lineTo (m.translated (5, 1.5f));
                    }
                    g.setColour (red);
                    g.strokePath (icon, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                    drawLed (g, mark, r.colour, true, 5.0f);
                g.setColour (hot ? textBright : (r.subsystem == -2 ? textDim : textSoft));
                g.setFont (sans (13.0f).withExtraKerningFactor (0.06f));
                g.drawText (r.text, box.withTrimmedLeft (20).withTrimmedRight (6), juce::Justification::centredLeft, true);
                top += rowH;
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = owner->rowAt (e.y);
            if (h != hover)
            {
                hover = h;
                setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hover = -1;
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // Rows drag onto the workspace and land where they are dropped.
            const int i = owner->rowAt (e.getMouseDownY());
            if (dragging || i < 0 || e.getDistanceFromDragStart() < 6)
                return;
            const auto& r = owner->rows[(size_t) i];
            if (r.kind.empty() && r.subsystem < 0)
                return;
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
            if (container == nullptr)
                return;
            dragging = true;
            auto* o = new juce::DynamicObject();
            o->setProperty ("row", i);
            juce::Image ghost (juce::Image::ARGB, 150, 26, true);
            {
                juce::Graphics g (ghost);
                g.setColour (ps::theme::menu.withAlpha (0.92f));
                g.fillRoundedRectangle (ghost.getBounds().toFloat(), 6);
                g.setColour (ps::theme::glassEdge);
                g.drawRoundedRectangle (ghost.getBounds().toFloat().reduced (0.5f), 6, 1);
                ps::theme::drawLed (g, { 13, 13 }, r.colour.isTransparent() ? ps::theme::text : r.colour, true, 5.0f);
                g.setColour (ps::theme::textBright);
                g.setFont (ps::theme::sans (12.5f));
                g.drawText (r.text, 24, 0, 120, 26, juce::Justification::centredLeft, true);
            }
            container->startDragging (juce::var (o), this, juce::ScaledImage (ghost), false, nullptr, &e.source);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool wasDrag = dragging;
            dragging = false;
            const int i = owner->rowAt (e.y);
            if (i < 0 || wasDrag || e.mouseWasDraggedSinceMouseDown())
                return;
            owner->add (owner->rows[(size_t) i], { std::numeric_limits<float>::max(), 0 });
        }

        bool dragging = false;
    };

    PatchCanvas& canvas;
    bool noSound = false;
    std::function<void()> onImport;
    std::vector<Row> rows;
    juce::TextEditor search;
    List list;
    juce::Viewport viewport;
};

// ---- workspace tabs --------------------------------------------------------------------------

class PatternspaceEditor::TabStrip : public juce::Component
{
public:
    TabStrip (PatternspaceProcessor& p, PatchCanvas& c) : processor (p), canvas (c) {}

    void paint (juce::Graphics& g) override
    {
        using namespace ps::theme;
        g.setColour (hair);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        const auto& pages = processor.patch().pages;
        const auto rects = tabRects();
        const auto mouse = getMouseXYRelative().toFloat();
        for (size_t i = 0; i < pages.size(); ++i)
        {
            // Numbered like the dashboard's menu: the current tab lights its number red and its
            // name white. While a subsystem is open the number dims: clicking the tab goes back out.
            const auto r = rects[i];
            const bool active = pages[i].id == canvas.page();
            const bool hot = isMouseOver() && r.contains (mouse);
            if (pages[i].id == dropTarget)
            {
                g.setColour (wash);
                g.fillRoundedRectangle (r, r.getHeight() / 2);
                g.setColour (cyan.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2, 1.0f);
            }
            auto row = r.withTrimmedLeft (12).withTrimmedRight (12);
            const auto number = juce::String (i + 1).paddedLeft ('0', 2);
            g.setFont (mono (10.0f));
            const auto numberArea = row.removeFromLeft (18);
            if (active && ! canvas.insideSubsystem())
            {
                g.setColour (red.withAlpha (0.25f));
                g.drawText (number, numberArea.translated (0, 1), juce::Justification::centredLeft, false);
                g.drawText (number, numberArea.translated (0, -1), juce::Justification::centredLeft, false);
            }
            g.setColour (active && ! canvas.insideSubsystem() ? red : textDim);
            g.drawText (number, numberArea, juce::Justification::centredLeft, false);
            row.removeFromLeft (8);
            const auto count = juce::String (moduleCount (pages[i].id));
            g.setColour (cyan.withAlpha (0.6f));
            g.drawText (count, row.removeFromRight (juce::GlyphArrangement::getStringWidth (mono (10.0f), count) + 2), juce::Justification::centredRight,
                        false);
            row.removeFromRight (8);
            g.setColour (active || hot ? textBright : textDim);
            g.setFont (sans (13.0f, active ? Weight::semibold : Weight::regular).withExtraKerningFactor (0.06f));
            g.drawText (u8 (pages[i].name), row, juce::Justification::centredLeft, true);
        }
        const auto add = addRect();
        const bool addHot = isMouseOver() && add.contains (mouse);
        if (addHot)
        {
            g.setColour (wash);
            g.fillEllipse (add);
        }
        juce::Path plus;
        const auto c = add.getCentre();
        plus.startNewSubPath (c.translated (-5, 0));
        plus.lineTo (c.translated (5, 0));
        plus.startNewSubPath (c.translated (0, -5));
        plus.lineTo (c.translated (0, 5));
        g.setColour (red);
        g.strokePath (plus, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        finishRename (true);
        const auto& pages = processor.patch().pages;
        const auto rects = tabRects();
        for (size_t i = 0; i < pages.size(); ++i)
            if (rects[i].contains (e.position))
            {
                canvas.setPage (pages[i].id);
                repaint();
                if (e.mods.isPopupMenu())
                    showMenu ((int) i);
                return;
            }
        if (addRect().contains (e.position))
            addPage();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto rects = tabRects();
        for (size_t i = 0; i < rects.size(); ++i)
            if (rects[i].contains (e.position))
                startRename ((int) i);
    }

    // The tab under a screen point, so modules dragged on the canvas can be dropped onto it.
    std::string pageAtScreen (juce::Point<int> screenPos) const
    {
        const auto local = getLocalPoint (nullptr, screenPos).toFloat();
        const auto& pages = processor.patch().pages;
        const auto rects = tabRects();
        for (size_t i = 0; i < pages.size(); ++i)
            if (rects[i].expanded (0, 4).contains (local))
                return pages[i].id;
        return {};
    }

    void setDropTarget (const std::string& page)
    {
        dropTarget = page;
        repaint();
    }

private:
    std::string dropTarget;

    int moduleCount (const std::string& page) const
    {
        int count = 0;
        for (auto& n : processor.patch().nodes)
            count += n.page == page ? 1 : 0;
        return count;
    }

    std::vector<juce::Rectangle<float>> tabRects() const
    {
        std::vector<juce::Rectangle<float>> out;
        float x = 8;
        const float h = (float) ps::theme::controlH, y = (float) getHeight() - h - 5;
        const auto& pages = processor.patch().pages;
        for (size_t i = 0; i < pages.size(); ++i)
        {
            const auto count = juce::String (moduleCount (pages[i].id));
            const float w = juce::GlyphArrangement::getStringWidth (ps::theme::sans (13.0f, ps::theme::Weight::semibold).withExtraKerningFactor (0.06f), u8 (pages[i].name))
                            + juce::GlyphArrangement::getStringWidth (ps::theme::mono (10.0f), count) + 62;
            out.push_back ({ x, y, w, h });
            x = out.back().getRight() + 4;
        }
        return out;
    }

    juce::Rectangle<float> addRect() const
    {
        const auto rects = tabRects();
        const float h = (float) ps::theme::controlH;
        const float x = rects.empty() ? 16.0f : rects.back().getRight() + 6;
        return { x, (float) getHeight() - h - 5, h, h };
    }

    void addPage()
    {
        auto& pages = processor.patch().pages;
        std::string id;
        for (int i = 1;; ++i)
        {
            id = "p" + std::to_string (i);
            if (std::none_of (pages.begin(), pages.end(), [&] (auto& p) { return p.id == id; }))
                break;
        }
        pages.push_back ({ id, "Tab " + std::to_string (pages.size() + 1) });
        processor.commitPatch();
        canvas.setPage (id);
        repaint();
        startRename ((int) pages.size() - 1);
    }

    void showMenu (int index)
    {
        auto& pages = processor.patch().pages;
        const auto id = pages[(size_t) index].id;
        const bool empty = moduleCount (id) == 0;
        juce::PopupMenu menu;
        menu.addItem ("Rename", [this, index] { startRename (index); });
        menu.addItem (empty || pages.size() < 2 ? "Delete tab" : "Delete tab (move its modules out first)", empty && pages.size() > 1,
                      false, [this, id] {
                          auto& ps_ = processor.patch().pages;
                          ps_.erase (std::remove_if (ps_.begin(), ps_.end(), [&] (auto& p) { return p.id == id; }), ps_.end());
                          auto& patch = processor.patch();
                          patch.blocks.erase (std::remove_if (patch.blocks.begin(), patch.blocks.end(), [&] (auto& b) { return b.page == id; }),
                                              patch.blocks.end());
                          patch.notes.erase (std::remove_if (patch.notes.begin(), patch.notes.end(), [&] (auto& t) { return t.page == id; }),
                                             patch.notes.end());
                          processor.commitPatch();
                          canvas.setPage (ps_.front().id);
                          repaint();
                      });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void startRename (int index)
    {
        const auto rects = tabRects();
        if (index < 0 || index >= (int) rects.size())
            return;
        renaming = index;
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (ps::theme::sans (13.0f));
        editor->setText (u8 (processor.patch().pages[(size_t) index].name), false);
        editor->onReturnKey = [this] { finishRename (true); };
        editor->onEscapeKey = [this] { finishRename (false); };
        editor->onFocusLost = [this] { finishRename (true); };
        addAndMakeVisible (*editor);
        editor->setBounds (rects[(size_t) index].reduced (2, 2).withWidth (std::max (140.0f, rects[(size_t) index].getWidth())).toNearestInt());
        editor->selectAll();
        editor->grabKeyboardFocus();
    }

    void finishRename (bool keep)
    {
        if (renaming < 0)
            return;
        const int index = renaming;
        renaming = -1;
        const auto name = editor->getText().trim();
        editor->setVisible (false);
        // Deleted later: this can run from inside the editor's own callbacks.
        juce::MessageManager::callAsync ([old = std::shared_ptr<juce::TextEditor> (editor.release())] {});
        auto& pages = processor.patch().pages;
        if (keep && name.isNotEmpty() && index < (int) pages.size())
        {
            pages[(size_t) index].name = name.toStdString();
            processor.commitPatch();
        }
        repaint();
    }

    PatternspaceProcessor& processor;
    PatchCanvas& canvas;
    std::unique_ptr<juce::TextEditor> editor;
    int renaming = -1;
};

// ---- the output strip ------------------------------------------------------------------------

// A button with the master meter drawn along its bottom edge; it turns red for a moment when the
// output went past 0 dB (the soft clipper caught it, but the level is too hot).
class PatternspaceEditor::OutputButton : public juce::TextButton,
                                         private juce::Timer
{
public:
    explicit OutputButton (PatternspaceProcessor& p) : TextButton ("Out"), processor (p) { startTimerHz (20); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        TextButton::paintButton (g, highlighted, down);
        using namespace ps::theme;
        const auto r = getLocalBounds().toFloat().reduced (10, 0).removeFromBottom (7).withHeight (3);
        g.setColour (cellOff);
        g.fillRoundedRectangle (r, 1.5f);
        const float level = juce::jlimit (0.0f, 1.0f, processor.masterLevel());
        if (level > 0)
        {
            g.setColour (clipLeft > 0 ? red : cyan);
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * level), 1.5f);
        }
    }

private:
    void timerCallback() override
    {
        if (processor.clippedRecently())
            clipLeft = 20;
        else if (clipLeft > 0)
            --clipLeft;
        repaint();
    }

    PatternspaceProcessor& processor;
    int clipLeft = 0;
};

// The strip: the master trim, the soft clipper and a meter for each out, in a call-out under the
// Out button.
class PatternspaceEditor::OutputPanel : public juce::Component,
                                        private juce::Timer
{
public:
    explicit OutputPanel (PatternspaceProcessor& p)
        : processor (p), masterAttachment (p.params, "master", master), clipAttachment (p.params, "clip", clip)
    {
        using namespace ps::theme;
        master.setSliderStyle (juce::Slider::LinearBar);
        master.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        master.setTextValueSuffix (" dB");
        master.setNumDecimalPlacesToDisplay (1);
        master.setDoubleClickReturnValue (true, 0.0);
        master.setTooltip ("Master trim on every output. Double-click for 0 dB");
        addAndMakeVisible (master);
        clip.setTooltip ("Bends peaks over 0 dB back under it, so many voices into one out can't hard-clip");
        addAndMakeVisible (clip);
        setSize (300, 62 + 8 * 18 + 10);
        startTimerHz (20);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        auto row = r.removeFromTop (ps::theme::controlH);
        clip.setBounds (row.removeFromRight (96));
        row.removeFromRight (8);
        master.setBounds (row);
        r.removeFromTop (10);
        meters = r;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ps::theme;
        auto r = meters;
        for (int b = 0; b < ps::Sampler::numBuses; ++b)
        {
            auto row = r.removeFromTop (18).toFloat();
            const bool on = b == 0 || (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone && b < processor.getBusCount (false)
                                       && processor.getBus (false, b)->isEnabled());
            g.setColour (on ? textSoft : textDim);
            g.setFont (mono (10.0f));
            g.drawText ("Out " + juce::String (b + 1), row.removeFromLeft (44), juce::Justification::centredLeft);
            const auto track = row.withSizeKeepingCentre (row.getWidth(), 4.0f);
            g.setColour (cellOff);
            g.fillRoundedRectangle (track, 2);
            const float level = juce::jlimit (0.0f, 1.0f, processor.busLevel (b + 1) * 1.6f);
            if (level > 0 && on)
            {
                const auto fill = track.withWidth (track.getWidth() * level);
                g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.5f), fill.getX(), 0, cyan, fill.getRight(), 0, false));
                g.fillRoundedRectangle (fill, 2);
            }
            if (! on && b > 0)
            {
                g.setColour (textDim);
                g.setFont (sans (9.5f));
                g.drawText (b == 0 ? "" : "off in the host, mixed into Out 1", row, juce::Justification::centredRight);
            }
        }
    }

private:
    void timerCallback() override { repaint (meters); }

    PatternspaceProcessor& processor;
    juce::Slider master;
    juce::ToggleButton clip { "Soft clip" };
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment clipAttachment;
    juce::Rectangle<int> meters;
};

// ---- the window ------------------------------------------------------------------------------

PatternspaceEditor::PatternspaceEditor (PatternspaceProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      canvas (p),
      playAttachment (p.params, "run", playButton),
      syncAttachment (p.params, "sync", syncButton),
      tempoAttachment (p.params, "bpm", tempoSlider)
{
    setLookAndFeel (&look);

    library = std::make_unique<Library> (canvas, processor.isMidiEffect(), [this] { importSubsystem(); });
    tabs = std::make_unique<TabStrip> (p, canvas);
    addAndMakeVisible (canvas);
    addAndMakeVisible (*library);
    addAndMakeVisible (*tabs);

    canvas.onViewChanged = [this] {
        refreshZoom();
        saveViewState();
    };
    canvas.onPagesChanged = [this] { tabs->repaint(); };
    canvas.onLevelChanged = [this] { tabs->repaint(); };
    canvas.onExportSubsystem = [this] (const ps::Node& n) { exportSubsystem (n); };
    canvas.onExportSubsystemToFile = [this] (const ps::Node& n) { exportSubsystemToFile (n); };
    canvas.onEdited = [this] {
        tabs->repaint();
        refreshPresets();
    };
    canvas.pageAtScreen = [this] (juce::Point<int> screenPos) { return tabs->pageAtScreen (screenPos); };
    canvas.onDropTarget = [this] (const std::string& page) { tabs->setDropTarget (page); };

    patchButton.setTooltip ("Presets, New, Open, Save, Rename");
    patchButton.onClick = [this] { showPatchMenu(); };
    addAndMakeVisible (patchButton);

    openButton.onClick = [this] { openPatch(); };
    saveButton.onClick = [this] { savePatch(); };
    openButton.setTooltip ("Open a patch file");
    saveButton.setTooltip ("Save the patch to its file (Save as the first time)");
    addAndMakeVisible (openButton);
    addAndMakeVisible (saveButton);

    playButton.setClickingTogglesState (true);
    playButton.getProperties().set ("accent", true);
    playButton.setTooltip ("Runs the internal clock when the host isn't driving it (and always in the standalone app)");
    addAndMakeVisible (playButton);
    syncButton.setTooltip ("Follow the host's tempo and transport when it has one");
    addAndMakeVisible (syncButton);
    resetButton.setTooltip ("Put every generator back to the start of the phrase, as if a reset arrived on every rst input");
    resetButton.onClick = [this] { processor.requestReset(); };
    addAndMakeVisible (resetButton);
    // The standalone app has no host to follow, so the switch would only confuse.
    syncButton.setVisible (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone);

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setNumDecimalPlacesToDisplay (1);
    tempoSlider.setTooltip ("Tempo of the internal clock");
    addAndMakeVisible (tempoSlider);

    for (auto* b : { &zoomOut, &zoomIn, &fitButton, &collapseButton, &snapButton, &helpButton })
        addAndMakeVisible (*b);
    collapseButton.onClick = [this] { canvas.collapseAll (canvas.anyExpanded()); };
    collapseButton.setTooltip ("Collapse every module on this tab to its header, or expand them all again");
    snapButton.setClickingTogglesState (true);
    snapButton.onClick = [this] { canvas.setSnapToGrid (snapButton.getToggleState()); };
    snapButton.setTooltip ("Snap modules to a 10 px grid while dragging");
    helpButton.onClick = [this] { canvas.setShowHelp (! canvas.showingHelp()); };
    helpButton.setTooltip ("Keyboard and mouse shortcuts (? or F1)");
    zoomOut.onClick = [this] { canvas.zoomStep (-1); };
    zoomIn.onClick = [this] { canvas.zoomStep (1); };
    fitButton.onClick = [this] { canvas.fitView(); };
    zoomLabel.setJustificationType (juce::Justification::centred);
    zoomLabel.setFont (ps::theme::mono (11.0f));
    zoomLabel.setColour (juce::Label::textColourId, ps::theme::textSoft);
    zoomOut.setTooltip ("Zoom out");
    zoomIn.setTooltip ("Zoom in");
    fitButton.setTooltip ("Fit the tab's modules in view");
    addAndMakeVisible (zoomLabel);

    canvas.loadSample = [this] (const juce::File& f, ps::Sample& s) { return processor.loadSampleFile (f, s); };
    if (! processor.isMidiEffect())
    {
        outButton = std::make_unique<OutputButton> (processor);
        outButton->setTooltip ("Output: master level, soft clip and a meter per out");
        outButton->onClick = [this] { showOutputPanel(); };
        addAndMakeVisible (*outButton);
    }

    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        updater = std::make_unique<Updater> (*this);
        updater->onStateChanged = [this] { refreshUpdateButton(); };
        updatesButton.onClick = [this] { showUpdatesMenu(); };
        addAndMakeVisible (updatesButton);
        refreshUpdateButton();
        midiButton.setTooltip ("The MIDI device the app's MIDI outs play to, with MIDI clock and start/stop. The input device is under Options.");
        midiButton.onClick = [this] { showMidiMenu(); };
        addAndMakeVisible (midiButton);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PatternspaceEditor> (this)] {
            if (safe != nullptr)
                safe->updater->checkIfDue();
        });
    }

    processor.addChangeListener (this);
    refreshPresets();
    refreshZoom();
    refreshTransport();
    startTimerHz (4);

    setResizable (true, true);
    setResizeLimits (900, 560, 4000, 3000);
    // Where the window was last time, if the host kept our state.
    int w = 1280, h = 800;
    juce::var saved;
    const auto viewState = processor.getViewState();
    if (viewState.isNotEmpty() && juce::JSON::parse (viewState, saved).wasOk() && saved.isObject())
    {
        w = juce::jlimit (900, 4000, (int) saved.getProperty ("w", w));
        h = juce::jlimit (560, 3000, (int) saved.getProperty ("h", h));
    }
    setSize (w, h);
    if (saved.isObject())
        canvas.restoreViewState (viewState);
    else
        canvas.fitView();
}

void PatternspaceEditor::saveViewState()
{
    juce::var v;
    if (juce::JSON::parse (canvas.viewStateToString(), v).failed() || ! v.isObject())
        return;
    v.getDynamicObject()->setProperty ("w", getWidth());
    v.getDynamicObject()->setProperty ("h", getHeight());
    processor.setViewState (juce::JSON::toString (v, true));
}

PatternspaceEditor::~PatternspaceEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void PatternspaceEditor::paint (juce::Graphics& g)
{
    using namespace ps::theme;
    g.fillAll (bg);
    // The library sits on the field with no panel behind it, just an edge that fades out at both
    // ends, as the dashboard's pinned menu does. The wordmark is at its top.
    const float h = (float) getHeight();
    juce::ColourGradient edge (juce::Colours::white.withAlpha (0.0f), 0, h * 0.1f, juce::Colours::white.withAlpha (0.0f), 0, h * 0.9f, false);
    edge.addColour (0.5, juce::Colours::white.withAlpha (0.12f));
    g.setGradientFill (edge);
    g.fillRect ((float) libraryW, h * 0.1f, 1.0f, h * 0.8f);
    drawBrand (g, juce::Rectangle<float> (20.0f, 6.0f, (float) libraryW - 30, (float) topBarH - 6));
}

void PatternspaceEditor::resized()
{
    auto r = getLocalBounds();
    library->setBounds (r.removeFromLeft (libraryW).withTrimmedTop (topBarH - 8));
    r.removeFromLeft (1);

    // Top bar: patch on the left, the clock on the right, all 30px controls.
    auto top = r.removeFromTop (topBarH).reduced (16, 0).withSizeKeepingCentre (r.getWidth() - 32, ps::theme::controlH).translated (0, 4);
    patchButton.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (8);
    openButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (6);
    saveButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (6);
    updatesButton.setBounds (top.removeFromLeft (updater != nullptr && updater->updateAvailable() ? 150 : 86));
    if (midiButton.isVisible())
    {
        top.removeFromLeft (6);
        midiButton.setBounds (top.removeFromLeft (76));
    }
    if (outButton != nullptr)
    {
        top.removeFromLeft (6);
        outButton->setBounds (top.removeFromLeft (60));
    }

    if (syncButton.isVisible())
    {
        syncButton.setBounds (top.removeFromRight (100));
        top.removeFromRight (6);
    }
    tempoSlider.setBounds (top.removeFromRight (124));
    top.removeFromRight (6);
    playButton.setBounds (top.removeFromRight (72));
    top.removeFromRight (6);
    resetButton.setBounds (top.removeFromRight (64));

    auto tabRow = r.removeFromTop (tabRowH);
    auto zoom = tabRow.removeFromRight (200).withTrimmedRight (16).withSizeKeepingCentre (184, ps::theme::controlH).withY (tabRow.getBottom() - ps::theme::controlH - 5);
    fitButton.setBounds (zoom.removeFromRight (48));
    zoom.removeFromRight (6);
    zoomIn.setBounds (zoom.removeFromRight (30));
    zoomLabel.setBounds (zoom.removeFromRight (54));
    zoomOut.setBounds (zoom.removeFromRight (30));
    auto tools = tabRow.removeFromRight (190).withSizeKeepingCentre (174, ps::theme::controlH).withY (tabRow.getBottom() - ps::theme::controlH - 5);
    helpButton.setBounds (tools.removeFromRight (30));
    tools.removeFromRight (6);
    snapButton.setBounds (tools.removeFromRight (56));
    tools.removeFromRight (6);
    collapseButton.setBounds (tools.removeFromRight (76));
    tabs->setBounds (tabRow);
    canvas.setBounds (r);
    if (isShowing())
        saveViewState();
}

void PatternspaceEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    canvas.patchReplaced();
    if (! processor.isRestoringHistory()) // an undo keeps the view where it is
        canvas.fitView();
    tabs->repaint();
    refreshPresets();
}

void PatternspaceEditor::refreshPresets()
{
    // The name, with a dot while there are unsaved changes, like a document window.
    const auto name = processor.patchName().isEmpty() ? juce::String ("Untitled") : processor.patchName();
    patchButton.setButtonText ((processor.isDirty() ? juce::String::fromUTF8 ("\xE2\x97\x8F ") : juce::String()) + name);
}

void PatternspaceEditor::showPatchMenu()
{
    juce::PopupMenu menu, factory, user, recent;
    menu.addItem ("New patch", [this] { newPatch(); });
    menu.addSeparator();
    const auto& presets = ps::presets::factory();
    for (int i = 0; i < (int) presets.size(); ++i)
        factory.addItem (presets[(size_t) i].name, true, presets[(size_t) i].name == processor.patchName(),
                         [this, i] { loadPreset (ps::presets::factory()[(size_t) i]); });
    menu.addSubMenu ("Factory presets", factory);
    const auto mine = ps::library::presets();
    for (auto& entry : mine)
        user.addItem (entry.name, true, entry.name == processor.patchName(), [this, file = entry.file] { openPatchFile (file); });
    if (! mine.empty())
        user.addSeparator();
    user.addItem ("Show the presets folder", [] { ps::library::presetsFolder().createDirectory(); ps::library::presetsFolder().revealToUser(); });
    menu.addSubMenu ("My presets", user);
    menu.addItem ("Save as preset...", [this] { savePreset(); });
    menu.addSeparator();
    menu.addItem ("Open...", [this] { openPatch(); });
    for (auto& path : ps::library::recentFiles())
        recent.addItem (juce::File (path).getFileNameWithoutExtension(), [this, path] { openPatchFile (juce::File (path)); });
    menu.addSubMenu ("Open recent", recent, recent.getNumItems() > 0);
    menu.addItem ("Save", [this] { savePatch(); });
    menu.addItem ("Save as...", [this] { savePatchAs(); });
    menu.addSeparator();
    menu.addItem ("Rename patch...", [this] { renamePatch(); });
    // What the MIDI input does, kept with the plug-in instance rather than the patch.
    juce::PopupMenu midiIn;
    const int rst = processor.resetNote();
    midiIn.addItem (processor.isLearningReset() ? juce::String ("Play the note that resets...")
                                                : rst >= 0 ? "Reset on " + u8 (ps::noteName (rst)) + "  (learn another)" : juce::String ("Reset on a note (learn)"),
                    [this] { processor.learnResetNote(); });
    midiIn.addItem ("No reset note", rst >= 0, false, [this] { processor.setResetNote (-1); });
    midiIn.addSeparator();
    const bool thru = processor.params.getRawParameterValue ("thru")->load() > 0.5f;
    midiIn.addItem ("MIDI thru", true, thru, [this, thru] {
        if (auto* p = processor.params.getParameter ("thru"))
            p->setValueNotifyingHost (thru ? 0.0f : 1.0f);
    });
    midiIn.addSeparator();
    midiIn.addItem ("Macros follow MIDI CCs: click Learn on a Macro module", false, false, nullptr);
    menu.addSubMenu ("MIDI input", midiIn);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patchButton).withMinimumWidth (220));
}

void PatternspaceEditor::showMidiMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MIDI output");
    const auto current = processor.midiOutputId();
    menu.addItem ("None", true, current.isEmpty(), [this] {
        processor.setMidiOutput ({});
        refreshTransport();
    });
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        menu.addItem (d.name, true, d.identifier == current, [this, id = d.identifier] {
            processor.setMidiOutput (id);
            refreshTransport();
        });
    menu.addSeparator();
    menu.addItem ("Sends the MIDI outs' notes, MIDI clock and start/stop", false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton));
}

void PatternspaceEditor::showOutputPanel()
{
    if (outButton == nullptr)
        return;
    auto panel = std::make_unique<OutputPanel> (processor);
    panel->setLookAndFeel (&look);
    juce::CallOutBox::launchAsynchronously (std::move (panel), outButton->getScreenBounds(), nullptr);
}

void PatternspaceEditor::confirmDiscard (const juce::String& action, std::function<void()> go)
{
    if (! processor.isDirty())
    {
        go();
        return;
    }
    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Unsaved changes")
                       .withMessage ("\"" + processor.patchName() + "\" has changes that haven't been saved. " + action + " anyway?")
                       .withButton ("Save first")
                       .withButton ("Discard")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [this, go, safe = juce::Component::SafePointer<PatternspaceEditor> (this)] (int result) {
        if (safe == nullptr)
            return;
        // JUCE numbers the buttons 1, 2 and then 0 for the last one, so Cancel is 0.
        if (result == 2) // Discard
            go();
        else if (result == 1) // Save first
        {
            // Save first, then carry on: a save that needs a dialog finishes asynchronously, so
            // the action waits for it.
            pendingAfterSave = go;
            savePatch();
        }
    });
}

void PatternspaceEditor::askForText (const juce::String& title, const juce::String& prompt, const juce::String& initial,
                                     std::function<void (const juce::String&)> done)
{
    auto* window = new juce::AlertWindow (title, prompt, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("text", initial);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->getTextEditor ("text")->selectAll();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, done] (int result) {
                                 const auto text = window->getTextEditorContents ("text").trim();
                                 if (result == 1 && text.isNotEmpty())
                                     done (text);
                             }),
                             true);
}

void PatternspaceEditor::newPatch()
{
    confirmDiscard ("Start a new patch", [this] {
        ps::Graph g;
        auto clk = ps::makeNode ("clock", "clk", 14, 250);
        clk.tagOut = { { 0, "CLK" }, { 1, "BAR" }, { 2, "2BAR" } };
        g.nodes.push_back (clk);
        ps::normalise (g);
        processor.loadPatch (g, "Untitled");
    });
}

void PatternspaceEditor::loadPreset (const ps::json::Preset& preset)
{
    confirmDiscard ("Load \"" + preset.name + "\"", [this, preset] { processor.loadPatch (preset.patch, preset.name); });
}

void PatternspaceEditor::openPatchFile (const juce::File& file)
{
    confirmDiscard ("Open \"" + file.getFileNameWithoutExtension() + "\"", [this, file] {
        const auto text = file.loadFileAsString();
        ps::json::Preset preset;
        ps::Graph graph;
        if (ps::json::presetFromString (text, preset))
            processor.loadPatch (preset.patch, preset.name.isNotEmpty() ? preset.name : file.getFileNameWithoutExtension());
        else if (ps::json::fromString (text, graph))
            processor.loadPatch (graph, file.getFileNameWithoutExtension());
        else
        {
            showError ("Couldn't open " + file.getFileName(), "It isn't a Patternspace patch. Subsystem files are added from the library instead.");
            return;
        }
        processor.setPatchFile (file);
        ps::library::addRecentFile (file);
        refreshPresets();
    });
}

void PatternspaceEditor::savePreset()
{
    askForText ("Save as preset", "A name for the preset. It goes in your presets folder and shows under My presets.", processor.patchName(),
                [this] (const juce::String& name) {
                    const auto file = ps::library::savePreset (name, processor.patch());
                    if (file == juce::File())
                    {
                        showError ("Couldn't save", "Patternspace couldn't write to " + ps::library::presetsFolder().getFullPathName() + ".");
                        return;
                    }
                    processor.setPatchName (file.getFileNameWithoutExtension());
                    processor.setPatchFile (file);
                    processor.markClean();
                    refreshPresets();
                });
}

void PatternspaceEditor::renamePatch()
{
    askForText ("Rename patch", "The patch's name. Saving writes it into the file.", processor.patchName(),
                [this] (const juce::String& name) { processor.setPatchName (name); });
}

void PatternspaceEditor::timerCallback() { refreshTransport(); }

void PatternspaceEditor::refreshTransport()
{
    const bool host = processor.isFollowingHost();
    for (auto* c : { static_cast<juce::Component*> (&playButton), static_cast<juce::Component*> (&tempoSlider) })
    {
        if (c->isEnabled() == ! host)
            continue;
        c->setEnabled (! host);
        c->setAlpha (host ? 0.45f : 1.0f);
    }
    const auto tip = host ? "The host's transport and tempo drive the clock while Host sync is on" : "Tempo of the internal clock";
    if (tempoSlider.getTooltip() != tip)
    {
        tempoSlider.setTooltip (tip);
        playButton.setTooltip (host ? "The host's transport drives the clock while Host sync is on"
                                    : "Runs the internal clock when the host isn't driving it (and always in the standalone app)");
    }
    if (midiButton.isVisible())
    {
        // A dot lights while notes are going out to the chosen device.
        const int sent = processor.midiSentCount();
        const bool device = processor.midiOutputId().isNotEmpty();
        const bool active = device && sent != midiSeen;
        midiSeen = sent;
        const auto label = juce::String (active ? "\xE2\x97\x8F MIDI" : device ? "MIDI" : "MIDI off");
        if (midiButton.getButtonText() != label)
            midiButton.setButtonText (label);
    }
}

void PatternspaceEditor::refreshZoom() { zoomLabel.setText (juce::String (juce::roundToInt (canvas.zoom() * 100)) + "%", juce::dontSendNotification); }

void PatternspaceEditor::openPatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file != juce::File())
            openPatchFile (file);
    });
}

void PatternspaceEditor::savePatch()
{
    const auto file = processor.patchFile();
    if (file == juce::File() || ! file.getParentDirectory().isDirectory())
    {
        savePatchAs();
        return;
    }
    auto* obj = new juce::DynamicObject();
    const juce::var preset (obj);
    obj->setProperty ("name", processor.patchName());
    obj->setProperty ("patch", ps::json::toVar (processor.patch()));
    if (! file.replaceWithText (juce::JSON::toString (preset, false)))
    {
        showError ("Couldn't save", "Patternspace couldn't write " + file.getFullPathName() + ".");
        pendingAfterSave = nullptr;
        return;
    }
    processor.markClean();
    ps::library::addRecentFile (file);
    refreshPresets();
    if (auto go = std::exchange (pendingAfterSave, nullptr))
        go();
}

void PatternspaceEditor::savePatchAs()
{
    const auto dir = processor.patchFile() != juce::File() ? processor.patchFile().getParentDirectory()
                                                           : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save the patch", dir.getChildFile (juce::File::createLegalFileName (processor.patchName()) + ".json"), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                              {
                                  pendingAfterSave = nullptr;
                                  return;
                              }
                              file = file.withFileExtension ("json");
                              processor.setPatchName (file.getFileNameWithoutExtension());
                              processor.setPatchFile (file);
                              savePatch();
                          });
}

void PatternspaceEditor::importSubsystem()
{
    chooser = std::make_unique<juce::FileChooser> ("Add a subsystem", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        ps::presets::SubsystemTemplate t;
        if (ps::presets::subsystemFromVar (juce::JSON::parse (file.loadFileAsString()), t))
            canvas.addSubsystem (t);
        else
            showError ("Couldn't add " + file.getFileName(), "It isn't a subsystem file. Save one from a subsystem's right-click menu.");
    });
}

void PatternspaceEditor::exportSubsystem (const ps::Node& node)
{
    const auto title = node.name.empty() ? juce::String ("Subsystem") : u8 (node.name);
    askForText ("Save subsystem", "A name for it in your library. It shows under Subsystems in every patch.", title,
                [this, node] (const juce::String& name) {
                    if (ps::library::saveSubsystem (name, node) == juce::File())
                        showError ("Couldn't save", "Patternspace couldn't write to " + ps::library::subsystemsFolder().getFullPathName() + ".");
                    else
                        library->rebuild();
                });
}

void PatternspaceEditor::exportSubsystemToFile (const ps::Node& node)
{
    const auto data = ps::presets::subsystemToVar (node);
    const auto title = node.name.empty() ? juce::String ("Subsystem") : u8 (node.name);
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save the subsystem", dir.getChildFile (juce::File::createLegalFileName (title) + ".json"), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [data] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("json");
                              if (! file.replaceWithText (juce::JSON::toString (data, false)))
                                  showError ("Couldn't save", "Patternspace couldn't write " + file.getFullPathName() + ".");
                          });
}

void PatternspaceEditor::refreshUpdateButton()
{
    const bool available = updater->updateAvailable();
    updatesButton.setButtonText (available ? "Update available" : "Updates");
    updatesButton.setToggleState (available, juce::dontSendNotification);
    updatesButton.setTooltip (available ? "A newer Patternspace is out" : "Check for a newer Patternspace");
    resized();
}

void PatternspaceEditor::showUpdatesMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Patternspace " + Updater::currentVersion());
    menu.addItem ("Check for updates...", [this] { updater->checkNow(); });
    menu.addItem ("Check at launch", true, updater->checksAtLaunch(), [this] { updater->setChecksAtLaunch (! updater->checksAtLaunch()); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&updatesButton));
}
