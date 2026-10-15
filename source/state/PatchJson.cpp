#include "PatchJson.h"

#include "../core/Modules.h"

#include <cmath>

namespace ps::json
{
namespace
{
    using juce::var;

    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    std::string std8 (const var& v) { return v.toString().toStdString(); }

    var obj()
    {
        return var (new juce::DynamicObject());
    }
    void put (var& o, const char* key, const var& value) { o.getDynamicObject()->setProperty (key, value); }

    var strings (const std::vector<std::string>& v)
    {
        juce::Array<var> a;
        for (auto& s : v)
            a.add (str (s));
        return a;
    }
    var types (const std::vector<PortType>& v)
    {
        juce::Array<var> a;
        for (auto t : v)
            a.add (t == PortType::audio ? "audio" : "gate");
        return a;
    }
    var tags (const std::map<int, std::string>& m)
    {
        auto o = obj();
        for (auto& [k, t] : m)
            put (o, juce::String (k).toRawUTF8(), str (t));
        return o;
    }

    var graphToVar (const Graph& g, bool root);

    var nodeToVar (const Node& n, bool root)
    {
        auto o = obj();
        put (o, "id", str (n.id));
        put (o, "k", str (n.kind));
        put (o, "x", n.x);
        put (o, "y", n.y);
        put (o, "c", n.collapsed ? 1 : 0);
        put (o, "tagIn", tags (n.tagIn));
        put (o, "tagOut", tags (n.tagOut));
        if (n.hasIns)
        {
            put (o, "ins", strings (n.ins));
            put (o, "inT", types (n.inT));
        }
        if (n.hasOuts)
        {
            put (o, "outs", strings (n.outs));
            put (o, "outT", types (n.outT));
        }
        auto p = obj();
        for (auto& [k, v] : n.num)
            put (p, k.c_str(), v);
        if (! n.mode.empty()) put (p, "mode", str (n.mode));
        if (! n.sample.empty()) put (p, "sample", str (n.sample));
        if (n.kind == "step")
        {
            juce::Array<var> cells;
            for (int c : n.cells)
                cells.add (c);
            put (p, "cells", cells);
        }
        if (n.kind == "midi")
        {
            put (p, "name", str (n.name));
            juce::Array<var> lanes;
            for (auto& l : n.lanes)
            {
                auto lane = obj();
                put (lane, "name", str (l.name));
                put (lane, "note", l.note);
                if (l.channel) put (lane, "ch", l.channel);
                if (l.choke) put (lane, "choke", l.choke);
                lanes.add (lane);
            }
            put (p, "lanes", lanes);
        }
        put (o, "p", p);
        if (n.kind == "sub")
        {
            put (o, "name", str (n.name));
            if (! n.inner.empty())
                put (o, "g", graphToVar (n.inner.front(), false));
        }
        if (root && ! n.page.empty())
            put (o, "pg", str (n.page));
        return o;
    }

    var graphToVar (const Graph& g, bool root)
    {
        auto o = obj();
        if (root)
        {
            put (o, "v", kPatchFormatVersion);
            juce::Array<var> pages;
            for (auto& p : g.pages)
            {
                auto po = obj();
                put (po, "id", str (p.id));
                put (po, "name", str (p.name));
                pages.add (po);
            }
            put (o, "pages", pages);
        }
        juce::Array<var> nodes, links, blocks, notes;
        for (auto& n : g.nodes)
            nodes.add (nodeToVar (n, root));
        for (auto& l : g.links)
        {
            auto lo = obj();
            put (lo, "a", str (l.a));
            put (lo, "p", l.p);
            put (lo, "b", str (l.b));
            put (lo, "q", l.q);
            links.add (lo);
        }
        for (auto& b : g.blocks)
        {
            auto bo = obj();
            put (bo, "id", str (b.id));
            put (bo, "x", b.x);
            put (bo, "y", b.y);
            put (bo, "w", b.w);
            put (bo, "h", b.h);
            put (bo, "t", str (b.title));
            put (bo, "c", str (b.colour));
            if (root) put (bo, "pg", str (b.page));
            blocks.add (bo);
        }
        for (auto& t : g.notes)
        {
            auto no = obj();
            put (no, "id", str (t.id));
            put (no, "x", t.x);
            put (no, "y", t.y);
            put (no, "text", str (t.text));
            if (t.w > 0) put (no, "w", t.w);
            if (root) put (no, "pg", str (t.page));
            notes.add (no);
        }
        put (o, "nodes", nodes);
        put (o, "links", links);
        put (o, "blocks", blocks);
        put (o, "notes", notes);
        if (root && ! g.tagColours.empty())
        {
            auto tc = obj();
            for (auto& [t, c] : g.tagColours)
                put (tc, str (t).toRawUTF8(), str (c));
            put (o, "tagColors", tc);
        }
        if (root && ! g.settings.empty())
        {
            auto st = obj();
            for (auto& [k, v] : g.settings)
                put (st, k.c_str(), v);
            put (o, "settings", st);
        }
        return o;
    }

    // ---- reading

    int toInt (const var& v, int fallback = 0)
    {
        if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
            return (int) std::lround ((double) v);
        if (v.isString())
            return v.toString().getIntValue();
        return fallback;
    }

    std::vector<std::string> readStrings (const var& v)
    {
        std::vector<std::string> r;
        if (auto* a = v.getArray())
            for (auto& s : *a)
                r.push_back (std8 (s));
        return r;
    }
    std::vector<PortType> readTypes (const var& v)
    {
        std::vector<PortType> r;
        if (auto* a = v.getArray())
            for (auto& s : *a)
                r.push_back (s.toString() == "audio" ? PortType::audio : PortType::gate);
        return r;
    }
    std::map<int, std::string> readTags (const var& v)
    {
        std::map<int, std::string> r;
        if (auto* o = v.getDynamicObject())
            for (auto& p : o->getProperties())
            {
                const auto t = p.value.toString();
                if (t.isNotEmpty() && p.name.toString().containsOnly ("0123456789"))
                    r[p.name.toString().getIntValue()] = t.toStdString();
            }
        return r;
    }

    bool readGraph (const var& v, Graph& g, bool root);

    bool readNode (const var& v, Node& n, bool root)
    {
        if (! v.isObject())
            return false;
        n.id = std8 (v["id"]);
        n.kind = std8 (v["k"]);
        if (n.id.empty() || kindInfo (n.kind) == nullptr)
            return false;
        n.x = (double) v["x"];
        n.y = (double) v["y"];
        n.collapsed = toInt (v["c"]) != 0;
        if (root)
            n.page = std8 (v["pg"]);
        n.tagIn = readTags (v["tagIn"]);
        n.tagOut = readTags (v["tagOut"]);
        if (v.hasProperty ("ins") && v["ins"].isArray())
        {
            n.hasIns = true;
            n.ins = readStrings (v["ins"]);
            n.inT = readTypes (v["inT"]);
        }
        if (v.hasProperty ("outs") && v["outs"].isArray())
        {
            n.hasOuts = true;
            n.outs = readStrings (v["outs"]);
            n.outT = readTypes (v["outT"]);
        }
        std::string legacyNote;
        if (auto* p = v["p"].getDynamicObject())
            for (auto& prop : p->getProperties())
            {
                const auto key = prop.name.toString().toStdString();
                const var& value = prop.value;
                if (key == "mode") n.mode = std8 (value);
                else if (key == "sample") n.sample = std8 (value);
                else if (key == "name") n.name = std8 (value);
                else if (key == "note") legacyNote = std8 (value); // MIDI outs before they had several outputs
                else if (key == "cells")
                {
                    if (auto* a = value.getArray())
                        for (auto& c : *a)
                            n.cells.push_back (juce::jlimit (0, 2, toInt (c)));
                }
                else if (key == "lanes")
                {
                    if (auto* a = value.getArray())
                        for (auto& l : *a)
                            n.lanes.push_back ({ std8 (l["name"]), juce::jlimit (0, 127, toInt (l["note"], 36)), juce::jlimit (0, 16, toInt (l["ch"], 0)),
                                                 juce::jlimit (0, 4, toInt (l["choke"], 0)) });
                }
                else if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
                    n.num[key] = toInt (value);
            }
        if (n.kind == "sub")
        {
            n.name = std8 (v["name"]);
            n.inner.emplace_back();
            if (v["g"].isObject() && ! readGraph (v["g"], n.inner.front(), false))
                return false;
        }
        if (n.kind == "midi" && n.lanes.empty())
            n.lanes.push_back ({ n.name.empty() ? "Note" : n.name, noteNumber (legacyNote) });
        return true;
    }

    bool readGraph (const var& v, Graph& g, bool root)
    {
        if (! v.isObject() || ! v["nodes"].isArray())
            return false;
        if (root)
            if (auto* a = v["pages"].getArray())
                for (auto& p : *a)
                    g.pages.push_back ({ std8 (p["id"]), std8 (p["name"]) });
        for (auto& nv : *v["nodes"].getArray())
        {
            Node n;
            if (readNode (nv, n, root))
                g.nodes.push_back (std::move (n));
        }
        if (auto* a = v["links"].getArray())
            for (auto& l : *a)
                g.links.push_back ({ std8 (l["a"]), toInt (l["p"]), std8 (l["b"]), toInt (l["q"]) });
        if (auto* a = v["blocks"].getArray())
            for (auto& b : *a)
            {
                Block blk;
                blk.id = std8 (b["id"]);
                blk.x = (double) b["x"];
                blk.y = (double) b["y"];
                blk.w = (double) b["w"];
                blk.h = (double) b["h"];
                blk.title = std8 (b["t"]);
                blk.colour = std8 (b["c"]);
                if (root) blk.page = std8 (b["pg"]);
                g.blocks.push_back (blk);
            }
        if (auto* a = v["notes"].getArray())
            for (auto& t : *a)
            {
                Note nt;
                nt.id = std8 (t["id"]);
                nt.x = (double) t["x"];
                nt.y = (double) t["y"];
                nt.w = t.hasProperty ("w") ? (double) t["w"] : 0.0;
                nt.text = std8 (t["text"]);
                if (root) nt.page = std8 (t["pg"]);
                g.notes.push_back (nt);
            }
        if (root)
        {
            if (auto* tc = v["tagColors"].getDynamicObject())
                for (auto& p : tc->getProperties())
                    g.tagColours[p.name.toString().toStdString()] = std8 (p.value);
            if (auto* st = v["settings"].getDynamicObject())
                for (auto& p : st->getProperties())
                    g.settings[p.name.toString().toStdString()] = toInt (p.value);
            // Files newer than this build may hold things it doesn't know; they load as far as
            // they can rather than failing. Older files are upgraded by normalise().
            g.settings["_v"] = toInt (v["v"], 1);
        }
        return true;
    }
} // namespace

juce::var toVar (const Graph& g) { return graphToVar (g, true); }

juce::String toString (const Graph& g) { return juce::JSON::toString (toVar (g), true); }

bool fromVar (const juce::var& v, Graph& out)
{
    Graph g;
    if (! readGraph (v, g, true))
        return false;
    normalise (g);
    out = std::move (g);
    return true;
}

bool fromString (const juce::String& text, Graph& g)
{
    juce::var v;
    if (juce::JSON::parse (text, v).failed())
        return false;
    return fromVar (v, g);
}

bool presetFromString (const juce::String& text, Preset& preset)
{
    juce::var v;
    if (juce::JSON::parse (text, v).failed() || ! v.isObject())
        return false;
    Graph g;
    if (! fromVar (v["patch"], g))
        return false;
    preset.name = v["name"].toString();
    preset.patch = std::move (g);
    return true;
}
} // namespace ps::json
