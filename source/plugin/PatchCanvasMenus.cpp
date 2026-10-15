// The canvas's menus: the module and empty-space menus, tags, MIDI out outputs, sounds and
// control targets.

#include "PatchCanvasDetail.h"
#include "PluginProcessor.h"

#include "../core/Patterns.h"

#include <cmath>
#include <limits>
#include <utility>

using namespace ps::canvas;

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
    if (n.kind == "sample")
        menu.addItem ("Load a sample file...", loadSample != nullptr, false, [this, index] { chooseSampleFile (index); });
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
    {
        menu.addItem ("Save to my subsystems...", [this, id = n.id] {
            if (auto* m = graph().find (id))
                onExportSubsystem (*m);
        });
        menu.addItem ("Save subsystem to a file...", [this, id = n.id] {
            if (auto* m = graph().find (id))
                if (onExportSubsystemToFile)
                    onExportSubsystemToFile (*m);
        });
    }
    if (n.kind != "subin" && n.kind != "subout")
    {
        menu.addSeparator();
        const auto what = selection.size() > 1 ? " " + juce::String ((int) selection.size()) + " modules" : juce::String();
        menu.addItem ("Group" + what + " into a subsystem", [this] { groupSelection(); });
        menu.addItem ("Draw a block around" + (what.isEmpty() ? juce::String (" it") : what), [this] { addBlock ({}, true); });
        menu.addSeparator();
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
    menu.addItem ("Add a block here", [this, world] { addBlock (world); });
    menu.addItem ("Add a note here", [this, world] { addNote (world); });
    if (! selection.empty())
        menu.addItem ("Group the selection into a subsystem", [this] { groupSelection(); });
    menu.addSeparator();
    menu.addItem ("Paste", ! clipboard().nodes.empty(), false, [this] { pasteClipboard(); });
    menu.addItem ("Select all", [this] { selectAll(); });
    menu.addItem (anyExpanded() ? "Collapse all" : "Expand all", [this] { collapseAll (anyExpanded()); });
    menu.addItem ("Fit to view", [this] { fitView(); });
    menu.addSeparator();
    menu.addItem ("Snap to grid", true, snap, [this] { setSnapToGrid (! snap); });
    menu.addItem ("Minimap", true, minimap, [this] { setMinimapShown (! minimap); });
    menu.addItem ("Keyboard shortcuts", [this] { setShowHelp (true); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// ---- tag menu --------------------------------------------------------------------------------

void PatchCanvas::showTagMenu (int index, bool output, int port)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    const auto& n = patch.nodes[(size_t) index];
    const auto& tags = output ? n.tagOut : n.tagIn;
    const std::string current = tags.count (port) ? tags.at (port) : std::string();
    const auto type = output ? ps::outputType (n, port) : ps::inputType (n, port);
    const auto id = n.id;
    auto setTag = [this, id, output, port] (const std::string& tag) {
        auto* m = graph().find (id);
        if (m == nullptr)
            return;
        auto& t = output ? m->tagOut : m->tagIn;
        if (tag.empty())
            t.erase (port);
        else
            t[port] = tag;
        edited();
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (current.empty() ? juce::String (output ? "Tag this output" : "Tag this input") : u8 (current));
    menu.addItem (current.empty() ? "Type a tag..." : "Rename...", [this, index, output, port] { editTag (index, output, port); });

    // The tags already in the patch: an input can listen to any owned tag of its type; an output
    // can take over a tag that inputs listen to but nothing owns.
    std::map<std::string, std::pair<std::string, int>> owned; // tag -> owner id, port
    std::set<std::string> heard;
    for (auto& m : patch.nodes)
    {
        for (auto& [p, t] : m.tagOut)
            owned[t] = { m.id, p };
        for (auto& [q, t] : m.tagIn)
            heard.insert (t);
    }
    if (! output)
    {
        juce::PopupMenu listen;
        for (auto& [tag, src] : owned)
        {
            const auto* owner = patch.find (src.first);
            if (! owner || owner->id == n.id || ps::outputType (*owner, src.second) != type)
                continue;
            colouredItem (listen, u8 (tag) + "  (" + u8 (ps::titleOf (*owner)) + ")", tagColour (tag), tag == current, [setTag, tag] { setTag (tag); });
        }
        menu.addSubMenu ("Listen to", listen, listen.getNumItems() > 0);
    }
    else
    {
        juce::PopupMenu take;
        for (auto& tag : heard)
            if (! owned.count (tag))
                colouredItem (take, u8 (tag), tagColour (tag), false, [setTag, tag] { setTag (tag); });
        if (take.getNumItems() > 0)
            menu.addSubMenu ("Tags waiting for a source", take);
    }

    if (! current.empty())
    {
        juce::PopupMenu colours;
        const auto& set = processor.patch().tagColours;
        const bool automatic = ! set.count (current);
        colours.addItem ("Automatic", true, automatic, [this, current] {
            processor.patch().tagColours.erase (current);
            edited();
        });
        for (auto& sw : ps::theme::swatches())
        {
            const auto hex = juce::String::toHexString ((int) sw.rgb).paddedLeft ('0', 6).toStdString();
            colouredItem (colours, sw.name, ps::theme::rgb (sw.rgb), ! automatic && set.at (current) == hex, [this, current, hex] {
                processor.patch().tagColours[current] = hex;
                edited();
            });
        }
        menu.addSubMenu ("Colour", colours);
        if (! output)
        {
            const auto src = owned.find (current);
            if (src != owned.end() && src->second.first != n.id)
                menu.addItem ("Go to " + u8 (ps::titleOf (*patch.find (src->second.first))), [this, to = src->second.first] { goToNode (to); });
            else
                menu.addItem ("No output owns this tag yet", false, false, nullptr);
        }
        else
        {
            juce::PopupMenu used;
            for (auto& m : patch.nodes)
                for (auto& [q, t] : m.tagIn)
                    if (t == current && m.id != n.id)
                    {
                        const auto ins = ps::inputsOf (m);
                        used.addItem (u8 (ps::titleOf (m)) + " " + (q < (int) ins.size() ? u8 (ins[(size_t) q]) : juce::String()), [this, to = m.id] { goToNode (to); });
                    }
            menu.addSubMenu (used.getNumItems() ? "Used by " + juce::String (used.getNumItems()) : juce::String ("Nothing listens yet"), used, used.getNumItems() > 0);
        }
        menu.addSeparator();
        menu.addItem ("Remove tag", [setTag] { setTag ({}); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PatchCanvas::goToNode (const std::string& id)
{
    const auto* n = graph().find (id);
    if (n == nullptr)
        return;
    if (! onPage (*n) && subPath.empty())
    {
        setPage (n->page);
        if (onPagesChanged)
            onPagesChanged();
        n = graph().find (id);
        if (n == nullptr)
            return;
    }
    selection = { id };
    selectedWire = -1;
    const auto box = layoutOf (*n).box;
    if (! getLocalBounds().toFloat().reduced (40).contains (toScreen (box.getCentre())))
    {
        offset = getLocalBounds().getCentre().toFloat() - box.getCentre() * scale;
        if (onViewChanged)
            onViewChanged();
    }
    repaint();
}

// ---- MIDI out lanes --------------------------------------------------------------------------

// ---- samples -------------------------------------------------------------------------------

std::vector<std::string> PatchCanvas::sampleChoices() const
{
    auto names = ps::sampleNames();
    for (auto& s : processor.patch().samples)
        names.push_back (s.id);
    return names;
}

juce::String PatchCanvas::sampleLabel (const ps::Node& n) const { return sampleLabelIn (processor.patch(), n); }

bool PatchCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (loadSample == nullptr)
        return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void PatchCanvas::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHover = true;
    repaint();
}

void PatchCanvas::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaint();
}

void PatchCanvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHover = false;
    juce::Array<juce::File> audio;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3"))
            audio.add (juce::File (f));
    const auto world = toWorld (juce::Point<float> ((float) x, (float) y));
    const auto hit = hitTest (world);
    const int node = hit.node >= 0 && hit.node < (int) graph().nodes.size() && graph().nodes[(size_t) hit.node].kind == "sample" ? hit.node : -1;
    loadSampleFiles (audio, node, world);
    repaint();
}

void PatchCanvas::loadSampleFiles (const juce::Array<juce::File>& files, int node, juce::Point<float> world)
{
    if (files.isEmpty() || loadSample == nullptr)
        return;
    auto& root = processor.patch();
    int loaded = 0;
    juce::StringArray failed;
    std::string lastId;
    for (auto& f : files)
    {
        ps::Sample s;
        if (! loadSample (f, s))
        {
            failed.add (f.getFileName());
            continue;
        }
        // A file already in the patch is reused rather than stored twice.
        auto same = std::find_if (root.samples.begin(), root.samples.end(), [&] (const ps::Sample& x) { return x.path == s.path && ! x.data.empty(); });
        if (same != root.samples.end())
            lastId = same->id;
        else
        {
            int next = 1;
            for (auto& x : root.samples)
                if (x.id.size() > 1 && x.id[0] == 's')
                    next = std::max (next, std::atoi (x.id.c_str() + 1) + 1);
            s.id = "s" + std::to_string (next);
            lastId = s.id;
            root.samples.push_back (std::move (s));
        }
        ++loaded;
        if (node < 0)
        {
            // A new Sample player for each file, stepping down so they don't pile up.
            addModule ("sample", world);
            world += juce::Point<float> (0, 40);
            graph().nodes.back().sample = lastId;
        }
    }
    if (node >= 0 && ! lastId.empty() && node < (int) graph().nodes.size())
        graph().nodes[(size_t) node].sample = lastId;
    // Sounds no player uses any more leave the patch, so a file that was replaced doesn't
    // stay embedded.
    std::set<std::string> used;
    std::function<void (const ps::Graph&)> collect = [&] (const ps::Graph& g) {
        for (auto& n : g.nodes)
        {
            if (n.kind == "sample")
                used.insert (n.sample);
            for (auto& inner : n.inner)
                collect (inner);
        }
    };
    collect (root);
    root.samples.erase (std::remove_if (root.samples.begin(), root.samples.end(), [&] (const ps::Sample& x) { return ! used.count (x.id); }), root.samples.end());
    if (loaded)
        edited();
    if (! failed.isEmpty())
        showMessage ("Couldn't read " + failed.joinIntoString (", ") + ". WAV, AIFF, FLAC, Ogg and MP3 files work.", 5000);
}

void PatchCanvas::chooseSampleFile (int node)
{
    sampleChooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    const auto id = node >= 0 && node < (int) graph().nodes.size() ? graph().nodes[(size_t) node].id : std::string();
    sampleChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this, id] (const juce::FileChooser& fc) {
                                    const auto files = fc.getResults();
                                    if (files.isEmpty())
                                        return;
                                    const int index = graph().indexOf (id);
                                    loadSampleFiles (files, index, toWorld (getLocalBounds().getCentre().toFloat()));
                                });
}

void PatchCanvas::showSampleMenu (int index)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size() || patch.nodes[(size_t) index].kind != "sample")
        return;
    const auto id = patch.nodes[(size_t) index].id;
    const auto current = patch.nodes[(size_t) index].sample;
    auto pick = [this, id] (const std::string& sample) {
        return [this, id, sample] {
            if (auto* m = graph().find (id))
            {
                m->sample = sample;
                edited();
            }
        };
    };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Built-in");
    for (auto& name : ps::sampleNames())
        menu.addItem (u8 (name), true, name == current, pick (name));
    const auto& samples = processor.patch().samples;
    if (! samples.empty())
    {
        menu.addSectionHeader ("In this patch");
        for (auto& s : samples)
            menu.addItem (u8 (s.name) + (s.missing ? " (missing)" : ""), true, s.id == current, pick (s.id));
    }
    menu.addSeparator();
    menu.addItem ("Load a file...", loadSample != nullptr, false, [this, id] { chooseSampleFile (graph().indexOf (id)); });
    for (auto& s : samples)
        if (s.id == current)
        {
            menu.addItem ("Show " + juce::File (s.path).getFileName() + " in " + juce::File (s.path).getParentDirectory().getFileName(), juce::File (s.path).existsAsFile(), false,
                          [path = s.path] { juce::File (path).revealToUser(); });
            const bool embedded = (double) s.data.size() <= ps::kEmbedSeconds * s.rate;
            menu.addItem (embedded ? "Saved inside the patch" : "Longer than " + juce::String ((int) ps::kEmbedSeconds) + " s: saved as a path, reloaded on open", false, false, nullptr);
        }
    menu.addItem ("Drop audio files on the module, or on empty space", false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (1)));
}

void PatchCanvas::showLaneMenu (int index, int lane)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    const auto& n = patch.nodes[(size_t) index];
    if ((n.kind != "midi" && n.kind != "midiin") || lane < 0 || lane >= (int) n.lanes.size())
        return;
    const auto id = n.id;
    const auto current = n.lanes[(size_t) lane];
    const int moduleChannel = n.get ("ch", 10);
    // Each action finds the lane again, since the menu runs after this call returns.
    auto with = [this, id, lane] (std::function<void (ps::MidiLane&)> change) {
        return [this, id, lane, change] {
            if (auto* m = graph().find (id))
                if (lane < (int) m->lanes.size())
                {
                    change (m->lanes[(size_t) lane]);
                    edited();
                }
        };
    };
    juce::PopupMenu menu, notes, channels, chokes;
    const bool input = n.kind == "midiin";
    menu.addSectionHeader (u8 (current.name) + "  " + u8 (ps::noteName (current.note)));
    if (! input)
        menu.addItem ("Audition", [this, current, moduleChannel] {
            processor.audition (current.channel > 0 ? current.channel : moduleChannel, current.note);
        });
    menu.addItem ("Rename...", [this, index, lane] {
        const auto& m = graph().nodes[(size_t) index];
        tagNode = m.id;
        textKey = "name" + std::to_string (lane);
        textTarget = TextTarget::param;
        const juce::Rectangle<float> r ((float) m.x + 8, (float) m.y + ioTop + lane * rowH - 3, nodeWidth (m) - 90, rowH + 6);
        showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (m.lanes[(size_t) lane].name), "Output name");
    });
    for (auto& [note, name] : ps::gmDrumList())
        notes.addItem (juce::String (name) + "  " + u8 (ps::noteName (note)), true, note == current.note, with ([note, name = juce::String (name)] (ps::MidiLane& l) {
                           // a lane still named after its drum follows the note
                           if (auto* old = ps::gmDrumName (l.note); old == nullptr || l.name == old)
                               l.name = name.toStdString();
                           l.note = note;
                       }));
    notes.addSeparator();
    notes.addItem ("Type a note...", [this, index, lane] {
        const auto& m = graph().nodes[(size_t) index];
        tagNode = m.id;
        textKey = "lane" + std::to_string (lane);
        textTarget = TextTarget::param;
        const juce::Rectangle<float> r ((float) m.x + nodeWidth (m) - 52, (float) m.y + ioTop + lane * rowH - 3, 30, rowH + 6);
        showTextEditor ({ toScreen (r.getTopLeft()).toInt(), toScreen (r.getBottomRight()).toInt() }, u8 (ps::noteName (m.lanes[(size_t) lane].note)), "C1");
        tagEditor->setJustification (juce::Justification::centred);
    });
    menu.addSubMenu ("Note", notes);
    if (! input)
    {
        channels.addItem ("Module's (" + juce::String (moduleChannel) + ")", true, current.channel == 0, with ([] (ps::MidiLane& l) { l.channel = 0; }));
        for (int ch = 1; ch <= 16; ++ch)
            channels.addItem (juce::String (ch), true, current.channel == ch, with ([ch] (ps::MidiLane& l) { l.channel = ch; }));
        menu.addSubMenu ("Channel", channels);
        chokes.addItem ("None", true, current.choke == 0, with ([] (ps::MidiLane& l) { l.choke = 0; }));
        for (int grp = 1; grp <= 4; ++grp)
            chokes.addItem ("Group " + juce::String (grp), true, current.choke == grp, with ([grp] (ps::MidiLane& l) { l.choke = grp; }));
        menu.addSubMenu ("Choke group", chokes);
    }
    menu.addSeparator();
    menu.addItem ("Remove output", (int) n.lanes.size() > 1, false, [this, id, lane] {
        auto& p = graph();
        if (auto* m = p.find (id))
            if (lane < (int) m->lanes.size() && m->lanes.size() > 1)
            {
                // the wires and tags below it move up with the rows they belong to
                for (auto it = p.links.begin(); it != p.links.end();)
                {
                    if (it->b == id && it->q == lane)
                        it = p.links.erase (it);
                    else
                    {
                        if (it->b == id && it->q > lane)
                            --it->q;
                        ++it;
                    }
                }
                std::map<int, std::string> tags;
                for (auto& [q, t] : m->tagIn)
                    if (q != lane)
                        tags[q > lane ? q - 1 : q] = t;
                m->tagIn = tags;
                m->lanes.erase (m->lanes.begin() + lane);
                edited();
            }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// ---- control targets -------------------------------------------------------------------------

void PatchCanvas::showTargetMenu (int index)
{
    auto& patch = graph();
    if (index < 0 || index >= (int) patch.nodes.size())
        return;
    const auto& n = patch.nodes[(size_t) index];
    const auto id = n.id;
    std::string currentId, currentKey;
    ps::parseTarget (n.target, currentId, currentKey);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Steer");
    int offered = 0;
    for (auto& m : patch.nodes)
    {
        if (m.id == id || ! onPage (m))
            continue;
        const auto params = ps::modulatableParams (m);
        if (params.empty())
            continue;
        juce::PopupMenu sub;
        for (auto& spec : params)
            sub.addItem (juce::String (spec.label) + "  " + juce::String (spec.minimum) + " to " + juce::String (spec.maximum), true,
                         m.id == currentId && spec.key == currentKey,
                         [this, id, target = m.id + ":" + spec.key, lo = spec.minimum, hi = spec.maximum] {
                             if (auto* c = graph().find (id))
                             {
                                 c->target = target;
                                 c->set ("lo", lo);
                                 c->set ("hi", hi);
                                 edited();
                                 showMessage ("From and To set the range swept as the control goes from 0 to 1; swap them to run it backwards");
                             }
                         });
        menu.addSubMenu (u8 (ps::titleOf (m)), sub);
        ++offered;
    }
    if (offered == 0)
        menu.addItem ("No module on this tab has a parameter to steer", false, false, nullptr);
    menu.addSeparator();
    menu.addItem ("No target", true, n.target.empty(), [this, id] {
        if (auto* c = graph().find (id))
        {
            c->target.clear();
            edited();
        }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

