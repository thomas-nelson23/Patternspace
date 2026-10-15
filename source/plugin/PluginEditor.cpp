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
public:
    Library (PatchCanvas& c, std::function<void()> importFromFile) : canvas (c), onImport (std::move (importFromFile))
    {
        for (auto& section : ps::librarySections())
        {
            rows.push_back ({ section.title, {}, {}, -1, true });
            for (auto& kind : section.kinds)
                if (auto* info = ps::kindInfo (kind))
                    rows.push_back ({ info->title, ps::theme::rgb (info->colour), kind, -1, false });
        }
        rows.push_back ({ "Subsystems", {}, {}, -1, true });
        rows.push_back ({ "New subsystem", juce::Colours::transparentBlack, {}, -3, false });
        const auto& subs = ps::presets::subsystems();
        for (int i = 0; i < (int) subs.size(); ++i)
            rows.push_back ({ subs[(size_t) i].name, ps::theme::text, {}, i, false });
        rows.push_back ({ "Add from file...", ps::theme::textDim, {}, -2, false });

        list.owner = this;
        list.setSize (libraryW, height());
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        list.setSize (viewport.getMaximumVisibleWidth(), height());
    }

    void paint (juce::Graphics& g) override { g.fillAll (ps::theme::bg); }

private:
    struct Row
    {
        juce::String text;
        juce::Colour colour;
        std::string kind;
        int subsystem = -1; // index into the factory subsystems, -2 to read one from a file, -3 for a new one
        bool header = false;
    };

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

        void mouseUp (const juce::MouseEvent& e) override
        {
            const int i = owner->rowAt (e.y);
            if (i < 0 || e.mouseWasDraggedSinceMouseDown())
                return;
            const auto& r = owner->rows[(size_t) i];
            if (! r.kind.empty())
                owner->canvas.addModule (r.kind);
            else if (r.subsystem == -3)
                owner->canvas.newSubsystem();
            else if (r.subsystem >= 0)
                owner->canvas.addSubsystem (ps::presets::subsystems()[(size_t) r.subsystem]);
            else if (owner->onImport)
                owner->onImport();
        }
    };

    PatchCanvas& canvas;
    std::function<void()> onImport;
    std::vector<Row> rows;
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

    library = std::make_unique<Library> (canvas, [this] { importSubsystem(); });
    tabs = std::make_unique<TabStrip> (p, canvas);
    addAndMakeVisible (canvas);
    addAndMakeVisible (*library);
    addAndMakeVisible (*tabs);

    canvas.onViewChanged = [this] { refreshZoom(); };
    canvas.onPagesChanged = [this] { tabs->repaint(); };
    canvas.onLevelChanged = [this] { tabs->repaint(); };
    canvas.onEdited = [this] { tabs->repaint(); };
    canvas.onExportSubsystem = [this] (const ps::Node& n) { exportSubsystem (n); };
    canvas.pageAtScreen = [this] (juce::Point<int> screenPos) { return tabs->pageAtScreen (screenPos); };
    canvas.onDropTarget = [this] (const std::string& page) { tabs->setDropTarget (page); };

    presetBox.setTextWhenNothingSelected ("Patch");
    presetBox.setTooltip ("Factory patches");
    presetBox.onChange = [this] {
        const int i = presetBox.getSelectedId() - 1;
        const auto& factory = ps::presets::factory();
        if (i >= 0 && i < (int) factory.size())
            processor.loadPatch (factory[(size_t) i].patch, factory[(size_t) i].name);
    };
    addAndMakeVisible (presetBox);

    openButton.onClick = [this] { openPatch(); };
    saveButton.onClick = [this] { savePatch(); };
    addAndMakeVisible (openButton);
    addAndMakeVisible (saveButton);

    playButton.setClickingTogglesState (true);
    playButton.getProperties().set ("accent", true);
    playButton.setTooltip ("Runs the internal clock when the host isn't driving it (and always in the standalone app)");
    addAndMakeVisible (playButton);
    syncButton.setTooltip ("Follow the host's tempo and transport when it has one");
    addAndMakeVisible (syncButton);

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setNumDecimalPlacesToDisplay (1);
    tempoSlider.setTooltip ("Tempo of the internal clock");
    addAndMakeVisible (tempoSlider);

    for (auto* b : { &zoomOut, &zoomIn, &fitButton })
        addAndMakeVisible (*b);
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

    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        updater = std::make_unique<Updater> (*this);
        updater->onStateChanged = [this] { refreshUpdateButton(); };
        updatesButton.onClick = [this] { showUpdatesMenu(); };
        addAndMakeVisible (updatesButton);
        refreshUpdateButton();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PatternspaceEditor> (this)] {
            if (safe != nullptr)
                safe->updater->checkIfDue();
        });
    }

    processor.addChangeListener (this);
    refreshPresets();
    refreshZoom();

    setResizable (true, true);
    setResizeLimits (900, 560, 4000, 3000);
    setSize (1280, 800);
    canvas.fitView();
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
    presetBox.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (8);
    openButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (6);
    saveButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (6);
    updatesButton.setBounds (top.removeFromLeft (updater != nullptr && updater->updateAvailable() ? 150 : 86));

    syncButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (6);
    tempoSlider.setBounds (top.removeFromRight (124));
    top.removeFromRight (6);
    playButton.setBounds (top.removeFromRight (72));

    auto tabRow = r.removeFromTop (tabRowH);
    auto zoom = tabRow.removeFromRight (200).withTrimmedRight (16).withSizeKeepingCentre (184, ps::theme::controlH).withY (tabRow.getBottom() - ps::theme::controlH - 5);
    fitButton.setBounds (zoom.removeFromRight (48));
    zoom.removeFromRight (6);
    zoomIn.setBounds (zoom.removeFromRight (30));
    zoomLabel.setBounds (zoom.removeFromRight (54));
    zoomOut.setBounds (zoom.removeFromRight (30));
    tabs->setBounds (tabRow);
    canvas.setBounds (r);
}

void PatternspaceEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    canvas.patchReplaced();
    canvas.fitView();
    tabs->repaint();
    refreshPresets();
}

void PatternspaceEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& factory = ps::presets::factory();
    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, i + 1);
    for (int i = 0; i < (int) factory.size(); ++i)
        if (factory[(size_t) i].name == processor.patchName())
        {
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }
    presetBox.setText (processor.patchName(), juce::dontSendNotification);
}

void PatternspaceEditor::refreshZoom() { zoomLabel.setText (juce::String (juce::roundToInt (canvas.zoom() * 100)) + "%", juce::dontSendNotification); }

void PatternspaceEditor::openPatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        const auto text = file.loadFileAsString();
        ps::json::Preset preset;
        ps::Graph graph;
        if (ps::json::presetFromString (text, preset))
            processor.loadPatch (preset.patch, preset.name.isNotEmpty() ? preset.name : file.getFileNameWithoutExtension());
        else if (ps::json::fromString (text, graph))
            processor.loadPatch (graph, file.getFileNameWithoutExtension());
        else
            showError ("Couldn't open " + file.getFileName(), "It isn't a Patternspace patch. Subsystem files are added from the library instead.");
    });
}

void PatternspaceEditor::savePatch()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save the patch", dir.getChildFile (juce::File::createLegalFileName (processor.patchName()) + ".json"), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("json");
                              auto* obj = new juce::DynamicObject();
                              const juce::var preset (obj);
                              obj->setProperty ("name", file.getFileNameWithoutExtension());
                              obj->setProperty ("patch", ps::json::toVar (processor.patch()));
                              if (! file.replaceWithText (juce::JSON::toString (preset, false)))
                                  showError ("Couldn't save", "Patternspace couldn't write " + file.getFullPathName() + ".");
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
