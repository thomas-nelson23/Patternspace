#pragma once

#include "../core/Engine.h"
#include "../core/Patch.h"
#include "Presets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>

class PatternspaceProcessor;

// The workspace: modules, cables and tags on the current tab, or inside a subsystem that has been
// opened. Drag empty space to pan, Ctrl/Cmd-scroll or pinch to zoom, drag an output onto an input to
// wire it, click a port to tag it.
class PatchCanvas : public juce::Component,
                    private juce::Timer
{
public:
    explicit PatchCanvas (PatternspaceProcessor& processor);
    ~PatchCanvas() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Called when the processor loads a different patch.
    void patchReplaced();

    const std::string& page() const { return currentPage; }
    void setPage (const std::string& pageId); // also closes any open subsystem

    // Opening a subsystem shows the patch inside it, where it can be edited like any other.
    // The path runs from the root down, one subsystem id per level.
    bool insideSubsystem() const { return ! subPath.empty(); }
    void openSubsystem (const std::string& id);
    void closeSubsystem (size_t depth = 0); // back out until `depth` levels stay open
    void newSubsystem(); // an empty one, opened so its ports and modules can be added

    float zoom() const { return scale; }
    void zoomAround (float newScale, juce::Point<float> screenPoint);
    void zoomStep (int direction);
    void fitView();

    void addModule (const std::string& kind);
    void addSubsystem (const ps::presets::SubsystemTemplate& subsystem);
    void moveSelectionToPage (const std::string& pageId);
    void collapseAll (bool collapse);
    void copySelection();
    void pasteClipboard();

    std::function<void()> onViewChanged;  // zoom changed
    std::function<void()> onEdited;       // after any change to the patch
    std::function<void()> onPagesChanged; // a tab was added, or modules moved between tabs
    std::function<void()> onLevelChanged; // a subsystem was opened or closed
    std::function<void (const ps::Node&)> onExportSubsystem;
    // Dragging modules onto a workspace tab moves them there. The editor says which tab, if any, is
    // under a screen point, and lights the one a drag would drop onto (empty for none).
    std::function<std::string (juce::Point<int> screenPos)> pageAtScreen;
    std::function<void (const std::string& pageId)> onDropTarget;

private:
    enum class ZoneType
    {
        param,
        mode,
        bus,
        cell,
        sample,
        laneNote,
        xy,
        open,
        addPort
    };
    struct Zone
    {
        juce::Rectangle<float> r;
        ZoneType type;
        std::string key;
        int value = 0;
    };
    struct Layout
    {
        juce::Rectangle<float> box, header;
        std::vector<juce::Point<float>> ins, outs;
        std::vector<Zone> zones;
        float bodyTop = 0;
    };
    enum class Hit
    {
        none,
        inPort,
        outPort,
        inTag,
        outTag,
        zone,
        header,
        body,
        wire,
        blockTitle
    };
    struct HitResult
    {
        Hit what = Hit::none;
        int node = -1, port = -1, wire = -1, block = -1;
        Zone zone {};
    };
    enum class Drag
    {
        none,
        pan,
        move,
        wire,
        xy,
        block,
        box // left-drag on empty space draws a selection box
    };

    void timerCallback() override;

    // The graph being edited: the root patch, or the inside of the open subsystem.
    ps::Graph& graph();
    const ps::Graph& graph() const;
    bool pathIsValid() const;
    ps::Node* openNode (ps::Graph** parent = nullptr); // the open subsystem, and the graph it sits in
    void addPort (bool output);
    void showPortMenu (int index, int port);
    void editPortName (int index, int port);
    void renameSubsystem (int index);
    void showTextEditor (juce::Rectangle<int> screenArea, const juce::String& text, const juce::String& placeholder);
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> crumbs() const; // screen space
    void paintCrumbs (juce::Graphics&);

    juce::Point<float> toWorld (juce::Point<float> screen) const { return (screen - offset) / scale; }
    juce::Point<float> toScreen (juce::Point<float> world) const { return world * scale + offset; }

    Layout layoutOf (const ps::Node&) const;
    float bodyHeight (const ps::Node&) const;
    bool onPage (const ps::Node&) const;
    HitResult hitTest (juce::Point<float> world) const;
    juce::Rectangle<float> tagRect (const ps::Node&, const Layout&, bool output, int port, const std::string& tag) const;
    juce::Colour tagColour (const std::string& tag) const;
    juce::Path wirePath (juce::Point<float> from, juce::Point<float> to) const;
    int viewOut (int node, int port) const;
    int viewIn (int node, int port) const;

    void paintBlocks (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintWires (juce::Graphics&);
    void paintNode (juce::Graphics&, int index, const ps::Node&, const Layout&);
    void paintBody (juce::Graphics&, int index, const ps::Node&, const Layout&);
    void paintCells (juce::Graphics&, juce::Rectangle<float>, const std::vector<int>& cells, int now, juce::Colour, bool editable = false);

    void applyZone (int node, const Zone&, juce::Point<float> world);
    void connect (int fromNode, int fromPort, int toNode, int toPort);
    void deleteSelection();
    void editTag (int node, bool output, int port);
    void finishTagEdit (bool keep);
    void showMessage (const juce::String&);
    void showNodeMenu (int node);
    void edited(); // commit the patch to the engine

    PatternspaceProcessor& processor;
    std::vector<ps::Engine::View> view;
    bool viewValid = false;

    float scale = 1.0f;
    juce::Point<float> offset { 0.0f, 0.0f }; // screen = world * scale + offset
    std::string currentPage;
    std::vector<std::string> subPath; // ids of the open subsystems, outermost first
    struct SavedView
    {
        float scale;
        juce::Point<float> offset;
    };
    std::vector<SavedView> savedViews; // the view at each level above, restored on the way out
    std::set<std::string> selection;
    int selectedWire = -1;
    int hoverNode = -1;

    Drag drag = Drag::none;
    juce::Point<float> dragStart, dragWorld;
    juce::Point<float> panStart;
    bool dragMoved = false;
    bool menuPress = false; // right-click (or ctrl-click on a Mac): pans when dragged, opens the module menu when not
    HitResult pressed;
    std::vector<std::pair<std::string, juce::Point<float>>> moveStart;
    std::string dropPage; // the tab a module drag is over, if it is not the current one
    int dragBlock = -1;
    juce::Point<float> blockStart;
    juce::Point<float> lastMouse; // where pasted modules land when the mouse is over the canvas
    int pasteCount = 0;           // without the mouse, each paste lands a step further from the copy
    std::set<std::string> boxBase; // what was selected before a shift box-drag, kept as the box changes

    std::unique_ptr<juce::TextEditor> tagEditor; // made once, shown while a tag is being named
    bool tagEditing = false;
    enum class TextTarget
    {
        tag,
        portName,
        subName
    } textTarget = TextTarget::tag;
    std::string tagNode;
    bool tagOutput = false;
    int tagPort = 0;

    juce::String message;
    juce::uint32 messageUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};
