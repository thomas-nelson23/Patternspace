#pragma once

#include "../core/Engine.h"
#include "../core/Patch.h"
#include "Presets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <limits>
#include <map>
#include <set>

class PatternspaceProcessor;

// The workspace: modules, cables and tags on the current tab, or inside a subsystem that has been
// opened. Drag empty space to pan, Ctrl/Cmd-scroll or pinch to zoom, drag an output onto an input to
// wire it, click a port to tag it.
class PatchCanvas : public juce::Component,
                    public juce::DragAndDropTarget,
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
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Modules and subsystems dragged in from the library land where they are dropped.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    std::function<void (const juce::var& item, juce::Point<float> world)> onLibraryDrop;

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

    void addModule (const std::string& kind, juce::Point<float> world = { std::numeric_limits<float>::max(), 0 });
    void selectAll();
    void duplicateSelection();
    void nudgeSelection (float dx, float dy);
    bool snapToGrid() const { return snap; }
    void setSnapToGrid (bool on);
    bool anyExpanded() const; // on the current tab, for a Collapse all / Expand all toggle
    bool showingHelp() const { return helpShown; }
    void setShowHelp (bool on);

    // The view per tab and the open subsystem, as a string for the plug-in state.
    juce::String viewStateToString() const;
    void restoreViewState (const juce::String& state);
    std::function<void()> onViewStateChanged;
    void addSubsystem (const ps::presets::SubsystemTemplate& subsystem, juce::Point<float> world = { std::numeric_limits<float>::max(), 0 });
    void groupSelection(); // the selected modules become one subsystem; wires across its edge become ports
    void addBlock (juce::Point<float> world, bool aroundSelection = false);
    void addNote (juce::Point<float> world);
    bool minimapShown() const { return minimap; }
    void setMinimapShown (bool on);
    void moveSelectionToPage (const std::string& pageId);
    void collapseAll (bool collapse);
    void copySelection();
    void pasteClipboard();

    std::function<void()> onViewChanged;  // zoom changed
    std::function<void()> onEdited;       // after any change to the patch
    std::function<void()> onPagesChanged; // a tab was added, or modules moved between tabs
    std::function<void()> onLevelChanged; // a subsystem was opened or closed
    std::function<void (const ps::Node&)> onExportSubsystem;       // to the user's library
    std::function<void (const ps::Node&)> onExportSubsystemToFile; // to a chosen file
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
        addPort,
        reroll,
        paramValue, // the value itself: drag up and down, scroll, or double-click to type
        lane        // a MIDI out's output name: opens its menu
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
        blockTitle,
        blockResize, // the handle in a block's bottom-right corner
        note
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
        box,  // left-drag on empty space draws a selection box
        param, // dragging a value up and down
        note,
        blockResize,
        minimap
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
    int settingOf (const char* key) const; // a root patch setting, 0 when unset
    HitResult hitTest (juce::Point<float> world) const;
    juce::Rectangle<float> tagRect (const ps::Node&, const Layout&, bool output, int port, const std::string& tag) const;
    juce::Colour tagColour (const std::string& tag) const;
    juce::Path wirePath (juce::Point<float> from, juce::Point<float> to) const;
    int viewOut (int node, int port) const;
    int viewIn (int node, int port) const;

    void paintBlocks (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintTagLinks (juce::Graphics&); // dashed, from the tag under the mouse to its listeners
    void paintMinimap (juce::Graphics&);
    juce::Rectangle<float> minimapRect() const;        // screen space; empty when hidden
    juce::Rectangle<float> minimapWorldBounds() const; // what the minimap shows
    static void paintKindGlyph (juce::Graphics&, const std::string& kind, juce::Point<float> centre, juce::Colour);
    juce::Rectangle<float> noteRect (const ps::Note&) const;
    void paintWires (juce::Graphics&);
    void paintNode (juce::Graphics&, int index, const ps::Node&, const Layout&);
    void paintBody (juce::Graphics&, int index, const ps::Node&, const Layout&);
    void paintCells (juce::Graphics&, juce::Rectangle<float>, const std::vector<int>& cells, int now, juce::Colour, bool editable = false);
    static void paintSegment (juce::Graphics&, juce::Rectangle<float>, bool on, juce::Colour);
    void renderBackdrop();

    void applyZone (int node, const Zone&, juce::Point<float> world);
    void connect (int fromNode, int fromPort, int toNode, int toPort);
    void deleteSelection();
    void editTag (int node, bool output, int port);
    void showTagMenu (int node, bool output, int port);
    void goToNode (const std::string& id); // select it and bring it into view, switching tabs if needed
    void showBlockMenu (int block);
    void showLaneMenu (int node, int lane);
    void showNoteMenu (int note);
    void editBlockTitle (int block);
    void editNote (int note);
    void finishTagEdit (bool keep);
    void showMessage (const juce::String&, int milliseconds = 2600);
    void showNodeMenu (int node);
    void showCanvasMenu (juce::Point<float> world);
    void paintHelp (juce::Graphics&);
    void applyParamDelta (int node, const std::string& key, int delta, const juce::String& undoGroup);
    void edited (const juce::String& undoGroup = {}); // commit the patch to the engine

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
    int selectedBlock = -1, selectedNote = -1; // the block or note last clicked, for Delete and the menus
    std::string hoverTag;                      // the tag under the mouse: its links show dashed
    bool minimap = true;
    bool dropHover = false; // something from the library is being dragged over the canvas
    juce::Point<float> lastMouse; // where pasted modules land when the mouse is over the canvas
    int pasteCount = 0;           // without the mouse, each paste lands a step further from the copy
    std::set<std::string> boxBase; // what was selected before a shift box-drag, kept as the box changes
    int dragAccum = 0;             // pixels dragged since the last value step
    bool snap = false;
    bool helpShown = false;
    std::map<std::string, SavedView> pageViews; // the view of each tab left and come back to

    std::unique_ptr<juce::TextEditor> tagEditor; // made once, shown while a tag is being named
    bool tagEditing = false;
    enum class TextTarget
    {
        tag,
        portName,
        subName,
        param,
        blockTitle,
        noteText
    } textTarget = TextTarget::tag;
    int textIndex = -1; // the block or note being edited
    std::string textKey; // the parameter being typed
    std::string tagNode;
    bool tagOutput = false;
    int tagPort = 0;

    juce::String message;
    juce::uint32 messageUntil = 0;

    juce::Image backdrop; // the faint colour fields behind the workspace, redrawn when it resizes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};
