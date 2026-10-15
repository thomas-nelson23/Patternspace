#include "Patch.h"

#include "Modules.h"

#include <set>

namespace ps
{
int Node::get (const std::string& key, int fallback) const
{
    auto it = num.find (key);
    return it == num.end() ? fallback : it->second;
}

int Graph::indexOf (const std::string& id) const
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id)
            return (int) i;
    return -1;
}

Node* Graph::find (const std::string& id)
{
    const int i = indexOf (id);
    return i < 0 ? nullptr : &nodes[(size_t) i];
}

const Node* Graph::find (const std::string& id) const
{
    const int i = indexOf (id);
    return i < 0 ? nullptr : &nodes[(size_t) i];
}

std::string Graph::freshId (const std::string& prefix) const
{
    for (int i = 1;; ++i)
    {
        auto id = prefix + std::to_string (i);
        if (indexOf (id) < 0)
            return id;
    }
}

namespace
{
    void normaliseGraph (Graph& g, bool root)
    {
        if (root)
        {
            if (g.pages.empty())
                g.pages.push_back ({ "p1", "Main" });
            std::set<std::string> ids;
            for (auto& p : g.pages)
                ids.insert (p.id);
            auto fix = [&] (std::string& page) {
                if (! ids.count (page))
                    page = g.pages.front().id;
            };
            for (auto& n : g.nodes) fix (n.page);
            for (auto& b : g.blocks) fix (b.page);
            for (auto& t : g.notes) fix (t.page);
        }
        for (auto& n : g.nodes)
        {
            applyDefaults (n);
            if ((n.kind == "midi" || n.kind == "midiin") && n.lanes.empty())
                n.lanes.push_back ({ n.name.empty() ? "Note" : n.name, 36 });
            if (n.kind == "sub" && n.inner.empty())
                n.inner.emplace_back();
            for (auto& inner : n.inner)
                normaliseGraph (inner, false);
        }
    }
} // namespace

void normalise (Graph& g)
{
    normaliseGraph (g, true);
    // Version 1 files (the prototype's, and 0.2.0's) have no settings. Each later version adds
    // its own upgrade here, keyed on the version the file was written with.
    const int from = g.settings.count ("_v") ? g.settings["_v"] : kPatchFormatVersion;
    g.settings.erase ("_v");
    if (from < 2)
    {
        g.settings.emplace ("follow", 0);
        g.settings.emplace ("seed", 1234);
    }
    g.settings.emplace ("follow", 0);
    g.settings.emplace ("seed", 1234);
}
} // namespace ps
