#include "PluginEditor.h"

#include "../core/Modules.h"
#include "../state/PatchJson.h"
#include "Presets.h"
#include "Theme.h"

namespace
{
constexpr int topBarH = 48, tabRowH = 32, libraryW = 184;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void showError (const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, title, message), nullptr);
}

void styleLookAndFeel (juce::LookAndFeel_V4& l)
{
    using namespace ps::theme;
    l.setColourScheme ({ bar, sidebar, panel, lineStrong, text, node, text, selected.withAlpha (0.35f), text });
    l.setColour (juce::ResizableWindow::backgroundColourId, canvas);
    l.setColour (juce::TextButton::buttonColourId, panel);
    l.setColour (juce::TextButton::buttonOnColourId, rgb (0x7bd88f));
    l.setColour (juce::TextButton::textColourOffId, text);
    l.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff121418));
    l.setColour (juce::ComboBox::backgroundColourId, panel);
    l.setColour (juce::ComboBox::outlineColourId, lineStrong);
    l.setColour (juce::ComboBox::textColourId, text);
    l.setColour (juce::ComboBox::arrowColourId, muted);
    l.setColour (juce::PopupMenu::backgroundColourId, panel);
    l.setColour (juce::PopupMenu::textColourId, text);
    l.setColour (juce::PopupMenu::highlightedBackgroundColourId, selected.withAlpha (0.3f));
    l.setColour (juce::PopupMenu::highlightedTextColourId, text);
    l.setColour (juce::Slider::backgroundColourId, panel);
    l.setColour (juce::Slider::trackColourId, rgb (0x3a4250));
    l.setColour (juce::Slider::textBoxTextColourId, text);
    l.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    l.setColour (juce::ToggleButton::textColourId, label);
    l.setColour (juce::ToggleButton::tickColourId, rgb (0x7bd88f));
    l.setColour (juce::ToggleButton::tickDisabledColourId, muted);
    l.setColour (juce::Label::textColourId, label);
    l.setColour (juce::TextEditor::backgroundColourId, panel);
    l.setColour (juce::TextEditor::textColourId, text);
    l.setColour (juce::TextEditor::outlineColourId, lineStrong);
    l.setColour (juce::TextEditor::focusedOutlineColourId, selected);
    l.setColour (juce::ScrollBar::thumbColourId, lineStrong);
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
        rows.push_back ({ "SUBSYSTEMS", {}, {}, -1, true });
        rows.push_back ({ "New subsystem", juce::Colours::transparentBlack, {}, -3, false });
        const auto& subs = ps::presets::subsystems();
        for (int i = 0; i < (int) subs.size(); ++i)
            rows.push_back ({ subs[(size_t) i].name, ps::theme::rgb (0x9fb6ff), {}, i, false });
        rows.push_back ({ "Add from file...", ps::theme::muted, {}, -2, false });

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

    void paint (juce::Graphics& g) override { g.fillAll (ps::theme::sidebar); }

private:
    struct Row
    {
        juce::String text;
        juce::Colour colour;
        std::string kind;
        int subsystem = -1; // index into the factory subsystems, -2 to read one from a file, -3 for a new one
        bool header = false;
    };

    static constexpr int rowH = 24, headerH = 30;

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
                    g.setColour (muted);
                    g.setFont (mono (9.5f, true));
                    g.drawText (r.text, juce::Rectangle<int> (14, top + 12, getWidth() - 20, 14), juce::Justification::centredLeft);
                    top += headerH;
                    continue;
                }
                const juce::Rectangle<float> box (8.0f, (float) top + 1, (float) getWidth() - 16, (float) rowH - 2);
                if (i == hover)
                {
                    g.setColour (panel.brighter (0.06f));
                    g.fillRoundedRectangle (box, 4);
                }
                g.setColour (r.colour);
                g.fillRoundedRectangle (box.getX() + 8, box.getCentreY() - 4, 8, 8, 2);
                g.setColour (r.subsystem == -2 ? muted : textSoft);
                if (r.subsystem == -3)
                {
                    g.setColour (textSoft);
                    g.setFont (sans (11.5f));
                    g.drawText ("+", box.withWidth (32), juce::Justification::centred);
                }
                g.setFont (sans (11.5f));
                g.drawText (r.text, box.withTrimmedLeft (24), juce::Justification::centredLeft, true);
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
        g.fillAll (bar);
        g.setColour (line);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        const auto& pages = processor.patch().pages;
        const auto rects = tabRects();
        for (size_t i = 0; i < pages.size(); ++i)
        {
            const auto r = rects[i];
            const bool active = pages[i].id == canvas.page();
            if (active)
            {
                g.setColour (ps::theme::canvas);
                g.fillRoundedRectangle (r.withTrimmedBottom (-4), 5);
                // While a subsystem is open the tab stays marked, dimmed: clicking it goes back.
                g.setColour (canvas.insideSubsystem() ? muted : selected);
                g.fillRect (r.getX() + 6, r.getBottom() - 2, r.getWidth() - 12, 2.0f);
            }
            g.setColour (active ? text : label);
            g.setFont (sans (11.5f, active));
            g.drawText (u8 (pages[i].name), r.withTrimmedLeft (12).withTrimmedRight (30), juce::Justification::centredLeft, true);
            const int count = moduleCount (pages[i].id);
            g.setColour (cellOff);
            const auto badge = juce::Rectangle<float> (r.getRight() - 28, r.getCentreY() - 8, 20, 16);
            g.fillRoundedRectangle (badge, 8);
            g.setColour (muted);
            g.setFont (mono (9.0f, true));
            g.drawText (juce::String (count), badge, juce::Justification::centred);
        }
        g.setColour (muted);
        g.setFont (sans (16.0f));
        g.drawText ("+", addRect(), juce::Justification::centred);
    }

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

private:
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
        for (auto& p : processor.patch().pages)
        {
            const float w = juce::GlyphArrangement::getStringWidth (ps::theme::sans (11.5f, true), u8 (p.name)) + 46;
            out.push_back ({ x, 4, std::max (70.0f, w), (float) getHeight() - 4 });
            x = out.back().getRight() + 2;
        }
        return out;
    }

    juce::Rectangle<float> addRect() const
    {
        const auto rects = tabRects();
        const float x = rects.empty() ? 8.0f : rects.back().getRight() + 4;
        return { x, 4, 28, (float) getHeight() - 6 };
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
        editor->setFont (ps::theme::sans (12.0f));
        editor->setText (u8 (processor.patch().pages[(size_t) index].name), false);
        editor->onReturnKey = [this] { finishRename (true); };
        editor->onEscapeKey = [this] { finishRename (false); };
        editor->onFocusLost = [this] { finishRename (true); };
        addAndMakeVisible (*editor);
        editor->setBounds (rects[(size_t) index].reduced (4, 3).withWidth (std::max (120.0f, rects[(size_t) index].getWidth())).toNearestInt());
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
    styleLookAndFeel (look);
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

    presetBox.setTextWhenNothingSelected ("Patch");
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
    playButton.setTooltip ("Runs the internal clock when the host isn't driving it (and always in the standalone app)");
    addAndMakeVisible (playButton);
    syncButton.setTooltip ("Follow the host's tempo and transport when it has one");
    addAndMakeVisible (syncButton);

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
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
    zoomLabel.setFont (ps::theme::mono (10.5f));
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
    g.fillAll (bar);
    auto top = getLocalBounds().removeFromTop (topBarH);
    g.setColour (bar);
    g.fillRect (top);
    g.setColour (line);
    g.fillRect (top.removeFromBottom (1));
    g.fillRect (libraryW, topBarH, 1, getHeight() - topBarH);

    g.setColour (rgb (0x8fa3ff));
    g.fillRoundedRectangle (16, 17, 14, 14, 3);
    g.setColour (text);
    g.setFont (sans (14.0f, true));
    g.drawText ("Patternspace", 38, 0, 120, topBarH, juce::Justification::centredLeft);
}

void PatternspaceEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topBarH).reduced (12, 10);
    top.removeFromLeft (150);
    presetBox.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (8);
    openButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (4);
    saveButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (4);
    updatesButton.setBounds (top.removeFromLeft (updater != nullptr && updater->updateAvailable() ? 124 : 80));

    syncButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (8);
    tempoSlider.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    playButton.setBounds (top.removeFromRight (64));

    library->setBounds (r.removeFromLeft (libraryW));
    r.removeFromLeft (1);
    auto tabRow = r.removeFromTop (tabRowH);
    auto zoom = tabRow.removeFromRight (190).reduced (8, 5);
    fitButton.setBounds (zoom.removeFromRight (44));
    zoom.removeFromRight (6);
    zoomIn.setBounds (zoom.removeFromRight (26));
    zoomLabel.setBounds (zoom.removeFromRight (52));
    zoomOut.setBounds (zoom.removeFromRight (26));
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
