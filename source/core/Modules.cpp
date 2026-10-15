#include "Modules.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ps
{
const char* const kCombineAMinusB = "A\xE2\x88\x92"
                                    "B";

namespace
{
    using P = PortType;

    const std::vector<KindInfo>& kinds()
    {
        static const std::vector<KindInfo> k {
            { "clock", "Clock", 0x6f858e, {}, { "1/16", "bar", "2 bars" }, {}, {}, 150, false },
            { "euclid", "Euclidean", 0x2ad4ff, { "clk", "rst" }, { "gate" }, {}, {}, 150, true },
            { "random", "Random", 0x2ad4ff, { "clk", "rst" }, { "gate" }, {}, {}, 150, true },
            { "ca", "Cellular", 0x2ad4ff, { "clk", "rst" }, { "gate" }, {}, {}, 150, true },
            { "grids", "Drum map", 0x2ad4ff, { "clk", "rst" }, { "kick", "snare", "hat" }, {}, {}, 220, false },
            { "step", "Step grid", 0x2ad4ff, { "clk", "rst" }, { "gate" }, {}, {}, 150, true },
            { "div", "Clock divider", 0x6f858e, { "clk", "rst" }, { "out" }, {}, {}, 150, false },
            { "accent", "Accent", 0x9cc7cf, { "in" }, { "out" }, {}, {}, 150, false },
            { "prob", "Probability", 0x9cc7cf, { "in" }, { "out" }, {}, {}, 150, false },
            { "combine", "Combine", 0x9cc7cf, { "A", "B" }, { "out" }, {}, {}, 150, false },
            { "switch", "Switch", 0x9cc7cf, {}, {}, {}, {}, 150, false },
            { "sample", "Sample player", 0xff2a4f, { "trig" }, { "audio" }, {}, { P::audio }, 160, false },
            { "audio", "Audio out", 0xff2a4f, { "audio" }, {}, { P::audio }, {}, 150, false },
            { "midi", "MIDI out", 0xff2a4f, {}, {}, {}, {}, 170, false },
            { "sub", "Subsystem", 0xd9eef2, {}, {}, {}, {}, 160, false },
            { "subin", "Inputs", 0x6f858e, {}, {}, {}, {}, 150, false },
            { "subout", "Outputs", 0x6f858e, {}, {}, {}, {}, 150, false },
        };
        return k;
    }

    // General MIDI drums offered for MIDI out outputs, and the order new outputs take them in.
    const std::vector<std::pair<int, const char*>>& gmDrums()
    {
        static const std::vector<std::pair<int, const char*>> d {
            { 36, "Kick" }, { 35, "Kick 2" }, { 37, "Rim" }, { 38, "Snare" }, { 40, "Snare 2" }, { 39, "Clap" },
            { 42, "Hat" }, { 44, "Pedal hat" }, { 46, "Open hat" }, { 41, "Floor tom" }, { 45, "Low tom" },
            { 48, "High tom" }, { 49, "Crash" }, { 51, "Ride" }, { 56, "Cowbell" }, { 54, "Tambourine" },
        };
        return d;
    }
    const int laneAddOrder[] = { 36, 38, 42, 46, 39, 37, 45, 48, 41, 49, 51, 44, 56, 54, 35, 40 };

    struct SwitchPorts
    {
        std::vector<std::string> ins, outs;
    };
    SwitchPorts switchPorts (const Node& n)
    {
        const int ways = std::max (1, n.get ("ways", 4));
        const bool split = n.mode != "merge";
        std::vector<std::string> side;
        for (int i = 0; i < ways; ++i)
            side.push_back ((split ? "out " : "in ") + std::to_string (i + 1));
        if (split)
            return { { "step", "rst", "in" }, side };
        std::vector<std::string> ins { "step", "rst" };
        ins.insert (ins.end(), side.begin(), side.end());
        return { ins, { "out" } };
    }

    int clampInt (int v, int lo, int hi) { return std::max (lo, std::min (hi, v)); }
} // namespace

const KindInfo* kindInfo (const std::string& kind)
{
    for (auto& k : kinds())
        if (kind == k.id)
            return &k;
    return nullptr;
}

const std::vector<LibrarySection>& librarySections()
{
    static const std::vector<LibrarySection> s {
        { "Generators", { "euclid", "random", "ca", "step", "grids" } },
        { "Shapers", { "accent", "prob", "combine", "switch", "div" } },
        { "Sound", { "sample", "audio" } },
        { "Outputs", { "midi" } },
    };
    return s;
}

std::vector<std::string> inputsOf (const Node& n)
{
    if (n.hasIns)
        return n.ins;
    if (n.kind == "switch")
        return switchPorts (n).ins;
    if (n.kind == "midi")
    {
        std::vector<std::string> r;
        for (auto& l : n.lanes)
            r.push_back (l.name);
        return r;
    }
    if (auto* k = kindInfo (n.kind))
        return k->ins;
    return {};
}

std::vector<std::string> outputsOf (const Node& n)
{
    if (n.hasOuts)
        return n.outs;
    if (n.kind == "switch")
        return switchPorts (n).outs;
    if (auto* k = kindInfo (n.kind))
        return k->outs;
    return {};
}

PortType inputType (const Node& n, int q)
{
    const auto& t = n.hasIns ? n.inT : (kindInfo (n.kind) ? kindInfo (n.kind)->inT : n.inT);
    return q >= 0 && q < (int) t.size() ? t[(size_t) q] : PortType::gate;
}

PortType outputType (const Node& n, int p)
{
    const auto& t = n.hasOuts ? n.outT : (kindInfo (n.kind) ? kindInfo (n.kind)->outT : n.outT);
    return p >= 0 && p < (int) t.size() ? t[(size_t) p] : PortType::gate;
}

std::string titleOf (const Node& n)
{
    if (n.kind == "sub" || n.kind == "midi")
        return n.name.empty() ? (kindInfo (n.kind) ? kindInfo (n.kind)->title : n.kind) : n.name;
    if (n.kind == "sample")
        return n.sample;
    if (auto* k = kindInfo (n.kind))
        return k->title;
    return n.kind;
}

std::string subtitleOf (const Node& n)
{
    const auto& k = n.kind;
    auto pct = [] (int v) { return std::to_string (v * 10) + "%"; };
    if (k == "clock") return "host";
    if (k == "euclid") return "E(" + std::to_string (n.get ("hits")) + "," + std::to_string (n.get ("len")) + ")";
    if (k == "random") return pct (n.get ("density"));
    if (k == "ca") return "R30";
    if (k == "step") return "16 steps";
    if (k == "grids") return "X" + std::to_string (n.get ("mx")) + " Y" + std::to_string (n.get ("my"));
    if (k == "div") return "\xC3\xB7" + std::to_string (n.get ("div"));
    if (k == "accent") return "vel";
    if (k == "prob") return pct (n.get ("chance"));
    if (k == "combine") return n.mode;
    if (k == "switch")
    {
        const auto w = std::to_string (n.get ("ways"));
        return n.mode == "merge" ? w + "\xE2\x86\x92" "1" : "1\xE2\x86\x92" + w;
    }
    if (k == "sample") return "sampler";
    if (k == "audio") return "Out " + std::to_string (n.get ("bus", 1));
    if (k == "midi") return "Ch " + std::to_string (n.get ("ch", 10));
    if (k == "subin") return "from parent";
    if (k == "subout") return "to parent";
    return {};
}

void applyDefaults (Node& n)
{
    auto def = [&n] (const char* key, int v) {
        if (n.num.find (key) == n.num.end())
            n.num[key] = v;
    };
    const auto& k = n.kind;
    if (k == "euclid") { def ("hits", 4); def ("len", 16); def ("rot", 0); }
    else if (k == "random") { def ("len", 8); def ("density", 5); def ("seed", 90); }
    else if (k == "grids") { def ("mx", 50); def ("my", 20); def ("dk", 6); def ("ds", 5); def ("dh", 6); def ("chaos", 0); }
    else if (k == "step") { if (n.cells.size() != 16) n.cells.resize (16, 0); }
    else if (k == "div") def ("div", 3);
    else if (k == "prob") def ("chance", 7);
    else if (k == "combine") { if (n.mode.empty()) n.mode = "OR"; if (n.mode == "A-B") n.mode = kCombineAMinusB; }
    else if (k == "switch") { if (n.mode != "merge") n.mode = "split"; def ("ways", 4); }
    else if (k == "sample") { if (n.sample.empty()) n.sample = "909 Kick"; def ("pitch", 0); def ("decay", 6); }
    else if (k == "audio") def ("bus", 1);
    else if (k == "midi")
    {
        def ("ch", 10);
        if (n.name.empty()) n.name = "Drums";
    }
}

Node makeNode (const std::string& kind, const std::string& id, double x, double y)
{
    Node n;
    n.id = id;
    n.kind = kind;
    n.x = x;
    n.y = y;
    if (kind == "step")
        n.cells = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    if (kind == "midi")
        n.lanes = { { "Kick", 36 }, { "Snare", 38 }, { "Hat", 42 }, { "Open hat", 46 } };
    applyDefaults (n);
    return n;
}

std::vector<ParamSpec> paramsOf (const Node& n)
{
    const auto& k = n.kind;
    if (k == "euclid")
        return { { "hits", "Hits", 0, n.get ("len") }, { "len", "Steps", 1, 32 }, { "rot", "Rotate", 0, std::max (0, n.get ("len") - 1) } };
    if (k == "random")
        return { { "density", "Density", 0, 10, 10, "%" }, { "len", "Loop", 1, 16 } };
    if (k == "grids")
        return { { "dk", "Kick fill", 0, 10, 10, "%" }, { "ds", "Snare fill", 0, 10, 10, "%" }, { "dh", "Hat fill", 0, 10, 10, "%" }, { "chaos", "Chaos", 0, 10, 10, "%" } };
    if (k == "div")
        return { { "div", "Divide by", 1, 16 } };
    if (k == "prob")
        return { { "chance", "Chance", 0, 10, 10, "%" } };
    if (k == "switch")
        return { { "ways", "Ways", 2, 8 } };
    if (k == "sample")
        return { { "pitch", "Pitch", -12, 12 }, { "decay", "Decay", 1, 10, 10, "%" } };
    if (k == "midi")
        return { { "nout", "Outputs", 1, 16 }, { "ch", "Channel", 1, 16 } };
    return {};
}

bool stepParam (Graph& g, Node& n, const std::string& key, int delta)
{
    if (key == "nout")
    {
        const int c = clampInt ((int) n.lanes.size() + delta, 1, 16);
        if (c == (int) n.lanes.size())
            return false;
        resizeLanes (n, c);
        prunePorts (g, n);
        return true;
    }
    if (key == "bus" || key == "mx" || key == "my")
    {
        const int lo = key == "bus" ? 1 : 0, hi = key == "bus" ? 8 : 100;
        const int v = clampInt (n.get (key) + delta, lo, hi);
        if (v == n.get (key))
            return false;
        n.set (key, v);
        return true;
    }
    for (auto& spec : paramsOf (n))
    {
        if (key != spec.key)
            continue;
        const int before = n.get (key);
        n.set (key, clampInt (before + delta, spec.minimum, spec.maximum));
        if (n.kind == "euclid")
        {
            n.set ("hits", std::min (n.get ("hits"), n.get ("len")));
            n.set ("rot", std::min (n.get ("rot"), n.get ("len") - 1));
        }
        if (n.kind == "switch")
            prunePorts (g, n);
        return n.get (key) != before;
    }
    return false;
}

void resizeLanes (Node& n, int count)
{
    while ((int) n.lanes.size() > count)
        n.lanes.pop_back();
    while ((int) n.lanes.size() < count)
    {
        std::set<int> used;
        for (auto& l : n.lanes)
            used.insert (l.note);
        MidiLane lane;
        bool found = false;
        for (int note : laneAddOrder)
            if (! used.count (note))
            {
                lane = { gmDrumName (note), note };
                found = true;
                break;
            }
        if (! found)
            lane = { "Out " + std::to_string (n.lanes.size() + 1), std::min (127, (used.empty() ? 35 : *used.rbegin()) + 1) };
        n.lanes.push_back (lane);
    }
}

std::string noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = clampInt (note, 0, 127);
    return std::string (names[note % 12]) + std::to_string (note / 12 - 2);
}

int noteNumber (const std::string& s, int fallback)
{
    if (s.size() < 2)
        return fallback;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const char letter = s[0];
    if (letter < 'A' || letter > 'G')
        return fallback;
    size_t i = 1;
    int n = base[letter - 'A'];
    if (s[i] == '#')
    {
        ++n;
        ++i;
    }
    if (i >= s.size())
        return fallback;
    try
    {
        size_t used = 0;
        const int octave = std::stoi (s.substr (i), &used);
        if (i + used != s.size())
            return fallback;
        return clampInt (n + (octave + 2) * 12, 0, 127);
    }
    catch (...)
    {
        return fallback;
    }
}

const char* gmDrumName (int note)
{
    for (auto& d : gmDrums())
        if (d.first == note)
            return d.second;
    return nullptr;
}

void prunePorts (Graph& g, const Node& n)
{
    const int ni = (int) inputsOf (n).size(), no = (int) outputsOf (n).size();
    g.links.erase (std::remove_if (g.links.begin(), g.links.end(), [&] (const Link& l) {
                       return (l.b == n.id && l.q >= ni) || (l.a == n.id && l.p >= no);
                   }),
                   g.links.end());
    if (auto* m = g.find (n.id))
    {
        for (auto it = m->tagIn.begin(); it != m->tagIn.end();)
            it = it->first >= ni ? m->tagIn.erase (it) : std::next (it);
        for (auto it = m->tagOut.begin(); it != m->tagOut.end();)
            it = it->first >= no ? m->tagOut.erase (it) : std::next (it);
    }
}

Node makeSubsystem (const std::string& id, const std::string& name, double x, double y)
{
    Node n = makeNode ("sub", id, x, y);
    n.name = name;
    n.hasIns = n.hasOuts = true;
    n.inner.assign (1, Graph {});
    subsystemInputs (n);
    subsystemOutputs (n);
    return n;
}

namespace
{
    Node* innerModule (Node& sub, const char* kind, const char* id, double x)
    {
        if (sub.inner.empty())
            sub.inner.emplace_back();
        auto& g = sub.inner.front();
        for (auto& m : g.nodes)
            if (m.kind == kind)
                return &m;
        Node m = makeNode (kind, g.find (id) ? g.freshId (id) : id, x, 60);
        // The module mirrors the subsystem's ports as they are now.
        if (m.kind == "subin")
        {
            m.hasOuts = true;
            m.outs = inputsOf (sub);
            for (int q = 0; q < (int) m.outs.size(); ++q)
                m.outT.push_back (inputType (sub, q));
        }
        else
        {
            m.hasIns = true;
            m.ins = outputsOf (sub);
            for (int p = 0; p < (int) m.ins.size(); ++p)
                m.inT.push_back (outputType (sub, p));
        }
        g.nodes.push_back (m);
        return &g.nodes.back();
    }

    // The two port lists that describe one side of a subsystem: its own, and the matching list on
    // the Inputs or Outputs module inside.
    struct Side
    {
        std::vector<std::string>*names[2];
        std::vector<PortType>* types[2];
    };

    Side sideOf (Node& sub, Node& io, bool output)
    {
        if (output)
        {
            sub.hasOuts = io.hasIns = true;
            return { { &sub.outs, &io.ins }, { &sub.outT, &io.inT } };
        }
        sub.hasIns = io.hasOuts = true;
        return { { &sub.ins, &io.outs }, { &sub.inT, &io.outT } };
    }

    // Moves or drops what refers to `port` on one side of a node: wires and tags. With `remove`,
    // the port goes and the ones after it shift down; otherwise only its wires go.
    void detach (Graph& g, const std::string& id, bool output, int port, bool remove)
    {
        for (auto it = g.links.begin(); it != g.links.end();)
        {
            const bool mine = output ? it->a == id : it->b == id;
            int& at = output ? it->p : it->q;
            if (mine && at == port)
            {
                it = g.links.erase (it);
                continue;
            }
            if (mine && remove && at > port)
                --at;
            ++it;
        }
        if (! remove)
            return;
        if (auto* n = g.find (id))
        {
            auto& tags = output ? n->tagOut : n->tagIn;
            std::map<int, std::string> moved;
            for (auto& [k, t] : tags)
                if (k != port)
                    moved[k > port ? k - 1 : k] = t;
            tags = std::move (moved);
        }
    }
} // namespace

Node* subsystemInputs (Node& sub) { return innerModule (sub, "subin", "in", 40); }
Node* subsystemOutputs (Node& sub) { return innerModule (sub, "subout", "out", 560); }

void addSubsystemPort (Node& sub, bool output, const std::string& name, PortType type)
{
    auto* io = output ? subsystemOutputs (sub) : subsystemInputs (sub);
    auto side = sideOf (sub, *io, output);
    for (int i = 0; i < 2; ++i)
    {
        side.types[i]->resize (side.names[i]->size(), PortType::gate);
        side.names[i]->push_back (name);
        side.types[i]->push_back (type);
    }
}

void removeSubsystemPort (Graph& parent, Node& sub, bool output, int port)
{
    auto* io = output ? subsystemOutputs (sub) : subsystemInputs (sub);
    auto side = sideOf (sub, *io, output);
    const std::string subId = sub.id, ioId = io->id;
    for (int i = 0; i < 2; ++i)
    {
        if (port < (int) side.names[i]->size())
            side.names[i]->erase (side.names[i]->begin() + port);
        if (port < (int) side.types[i]->size())
            side.types[i]->erase (side.types[i]->begin() + port);
    }
    detach (sub.inner.front(), ioId, ! output, port, true); // inside, the Inputs module's outputs
    detach (parent, subId, output, port, true);
}

void renameSubsystemPort (Node& sub, bool output, int port, const std::string& name)
{
    auto* io = output ? subsystemOutputs (sub) : subsystemInputs (sub);
    auto side = sideOf (sub, *io, output);
    for (int i = 0; i < 2; ++i)
        if (port >= 0 && port < (int) side.names[i]->size())
            (*side.names[i])[(size_t) port] = name;
}

void setSubsystemPortType (Graph& parent, Node& sub, bool output, int port, PortType type)
{
    auto* io = output ? subsystemOutputs (sub) : subsystemInputs (sub);
    auto side = sideOf (sub, *io, output);
    const std::string subId = sub.id, ioId = io->id;
    for (int i = 0; i < 2; ++i)
    {
        side.types[i]->resize (side.names[i]->size(), PortType::gate);
        if (port >= 0 && port < (int) side.types[i]->size())
            (*side.types[i])[(size_t) port] = type;
    }
    detach (sub.inner.front(), ioId, ! output, port, false);
    detach (parent, subId, output, port, false);
}

const std::vector<std::string>& combineModes()
{
    static const std::vector<std::string> m { "OR", "AND", "XOR", kCombineAMinusB };
    return m;
}

const std::vector<std::string>& sampleNames()
{
    static const std::vector<std::string> s { "909 Kick", "Crack Snare", "Tight Hat", "Clap", "Rim", "Low Tom" };
    return s;
}
} // namespace ps
