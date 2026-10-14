#include "Engine.h"

#include "Modules.h"
#include "Patterns.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace ps
{
StepOutput::StepOutput()
{
    notes.reserve (512);
    hits.reserve (512);
}

namespace
{
    enum class K
    {
        clock, euclid, random, ca, grids, step, div, accent, prob, combine, sw,
        sample, audio, midi, sub, subin, subout, other
    };

    K kindOf (const std::string& k)
    {
        static const std::map<std::string, K> m {
            { "clock", K::clock }, { "euclid", K::euclid }, { "random", K::random }, { "ca", K::ca },
            { "grids", K::grids }, { "step", K::step }, { "div", K::div }, { "accent", K::accent },
            { "prob", K::prob }, { "combine", K::combine }, { "switch", K::sw }, { "sample", K::sample },
            { "audio", K::audio }, { "midi", K::midi }, { "sub", K::sub }, { "subin", K::subin },
            { "subout", K::subout },
        };
        auto it = m.find (k);
        return it == m.end() ? K::other : it->second;
    }

    enum class Combine { orMode, andMode, xorMode, aMinusB };

    struct Src
    {
        int node = -1;
        int port = 0;
    };

    struct CGraph;

    struct CNode
    {
        K k = K::other;
        std::string id;
        int nIn = 0;
        std::vector<Src> src;  // per input
        std::vector<int> out;  // output values this step
        std::vector<int> in;   // input values this step (MIDI outs and Audio outs keep them)
        bool done = false;     // evaluated this step

        // parameters
        std::vector<int> pat; // Euclidean, Random, Step grid
        int mx = 50, my = 20, fills[3] {}, chaos = 0;
        int div = 3, chance = 7, ways = 4;
        bool split = true;
        Combine mode = Combine::orMode;
        int sample = 0, pitch = 0, decay = 6, bus = 1;
        std::vector<int> buses; // Audio outs a Sample player feeds
        int channel = 10;
        std::vector<int> notes;
        std::unique_ptr<CGraph> inner;
        int subout = -1; // index of the Outputs module inside a subsystem

        // state
        int pos = -1, row = 0, loop = 0, cnt = 0, sel = -1, fired = 0, trig = 0;
        DrumMapPattern gp {};
        int gpLoop = -1;

        void reset()
        {
            pos = -1;
            row = loop = cnt = fired = trig = 0;
            sel = -1;
            gpLoop = -1;
        }
    };

    struct CGraph
    {
        std::vector<CNode> nodes;
        std::vector<int> order;
        std::unordered_map<std::string, int> byId;
    };

    int nonNegMod (std::int64_t a, int m) { return (int) (((a % m) + m) % m); }

    void compile (const Graph& g, CGraph& c)
    {
        const size_t n = g.nodes.size();
        c.nodes.resize (n);
        for (size_t i = 0; i < n; ++i)
            c.byId[g.nodes[i].id] = (int) i;

        // Sources per input: wires first (a later wire into the same input wins), then tags for
        // inputs that have no wire. A tag connects only matching port types, and never to itself.
        std::vector<std::map<int, Src>> src (n);
        std::map<std::string, Src> outTag;
        for (size_t i = 0; i < n; ++i)
            for (auto& [p, t] : g.nodes[i].tagOut)
                if (! t.empty())
                    outTag[t] = { (int) i, p };
        for (auto& l : g.links)
        {
            auto a = c.byId.find (l.a), b = c.byId.find (l.b);
            if (a != c.byId.end() && b != c.byId.end())
                src[(size_t) b->second][l.q] = { a->second, l.p };
        }
        for (size_t i = 0; i < n; ++i)
            for (auto& [q, t] : g.nodes[i].tagIn)
            {
                if (t.empty() || src[i].count (q))
                    continue;
                auto o = outTag.find (t);
                if (o == outTag.end() || o->second.node == (int) i)
                    continue;
                if (outputType (g.nodes[(size_t) o->second.node], o->second.port) == inputType (g.nodes[i], q))
                    src[i][q] = o->second;
            }

        // Dependency order, repeating passes over the patch in its own order; modules caught in a
        // loop go last.
        std::vector<std::set<int>> deps (n);
        for (size_t i = 0; i < n; ++i)
            for (auto& [q, s] : src[i])
                deps[i].insert (s.node);
        std::vector<bool> done (n, false);
        bool progress = true;
        while (c.order.size() < n && progress)
        {
            progress = false;
            for (size_t i = 0; i < n; ++i)
            {
                if (done[i])
                    continue;
                if (std::all_of (deps[i].begin(), deps[i].end(), [&] (int d) { return done[(size_t) d]; }))
                {
                    c.order.push_back ((int) i);
                    done[i] = true;
                    progress = true;
                }
            }
        }
        for (size_t i = 0; i < n; ++i)
            if (! done[i])
                c.order.push_back ((int) i);

        for (size_t i = 0; i < n; ++i)
        {
            const Node& m = g.nodes[i];
            CNode& x = c.nodes[i];
            x.k = kindOf (m.kind);
            x.id = m.id;
            x.nIn = (int) inputsOf (m).size();
            int maxQ = x.nIn;
            for (auto& [q, s] : src[i])
                maxQ = std::max (maxQ, q + 1);
            x.src.assign ((size_t) maxQ, Src {});
            for (auto& [q, s] : src[i])
                if (q >= 0)
                    x.src[(size_t) q] = s;
            x.in.assign ((size_t) std::max (x.nIn, 1), 0);

            switch (x.k)
            {
                case K::euclid: x.pat = euclid (m.get ("hits", 4), m.get ("len", 16), m.get ("rot")); break;
                case K::random: x.pat = randomPattern (m.get ("len", 8), m.get ("density", 5), m.get ("seed", 90)); break;
                case K::step:
                    x.pat = m.cells;
                    if (x.pat.empty())
                        x.pat.assign (16, 0);
                    break;
                case K::grids:
                    x.mx = m.get ("mx", 50);
                    x.my = m.get ("my", 20);
                    x.fills[0] = m.get ("dk", 6);
                    x.fills[1] = m.get ("ds", 5);
                    x.fills[2] = m.get ("dh", 6);
                    x.chaos = m.get ("chaos");
                    break;
                case K::div: x.div = std::max (1, m.get ("div", 3)); break;
                case K::prob: x.chance = m.get ("chance", 7); break;
                case K::combine:
                    x.mode = m.mode == "AND" ? Combine::andMode
                           : m.mode == "XOR" ? Combine::xorMode
                           : (m.mode == kCombineAMinusB || m.mode == "A-B") ? Combine::aMinusB
                                                                            : Combine::orMode;
                    break;
                case K::sw:
                    x.ways = std::max (1, m.get ("ways", 4));
                    x.split = m.mode != "merge";
                    break;
                case K::sample:
                {
                    const auto& names = sampleNames();
                    auto it = std::find (names.begin(), names.end(), m.sample);
                    x.sample = it == names.end() ? 0 : (int) (it - names.begin());
                    x.pitch = m.get ("pitch");
                    x.decay = m.get ("decay", 6);
                    break;
                }
                case K::audio: x.bus = std::max (1, std::min (8, m.get ("bus", 1))); break;
                case K::midi:
                    x.channel = std::max (1, std::min (16, m.get ("ch", 10)));
                    for (auto& l : m.lanes)
                        x.notes.push_back (std::max (0, std::min (127, l.note)));
                    break;
                case K::sub:
                    x.inner = std::make_unique<CGraph>();
                    if (! m.inner.empty())
                    {
                        compile (m.inner.front(), *x.inner);
                        for (size_t j = 0; j < m.inner.front().nodes.size(); ++j)
                            if (m.inner.front().nodes[j].kind == "subout")
                            {
                                x.subout = (int) j;
                                break;
                            }
                    }
                    break;
                default: break;
            }

            int nOut = (int) outputsOf (m).size();
            if (x.k == K::sub)
                nOut = x.subout >= 0 ? x.inner->nodes[(size_t) x.subout].nIn : 0;
            x.out.assign ((size_t) std::max (nOut, 0), 0);
        }

        // A Sample player sounds on every Audio out its output reaches.
        for (auto& x : c.nodes)
            if (x.k == K::audio && ! x.src.empty() && x.src[0].node >= 0)
            {
                auto& s = c.nodes[(size_t) x.src[0].node];
                if (s.k == K::sample && x.src[0].port == 0)
                    s.buses.push_back (x.bus);
            }
    }

    void adopt (CGraph& to, const CGraph& from)
    {
        for (auto& x : to.nodes)
        {
            auto it = from.byId.find (x.id);
            if (it == from.byId.end())
                continue;
            const CNode& y = from.nodes[(size_t) it->second];
            if (y.k != x.k)
                continue;
            x.pos = y.pos;
            x.row = y.row;
            x.loop = y.loop;
            x.cnt = y.cnt;
            x.sel = y.sel;
            if (x.inner && y.inner)
                adopt (*x.inner, *y.inner);
        }
    }

    void resetGraph (CGraph& c)
    {
        for (auto& x : c.nodes)
        {
            x.reset();
            std::fill (x.out.begin(), x.out.end(), 0);
            std::fill (x.in.begin(), x.in.end(), 0);
            if (x.inner)
                resetGraph (*x.inner);
        }
    }

    const CNode* findPath (const CGraph& c, const std::string& path)
    {
        const auto slash = path.find ('/');
        auto it = c.byId.find (path.substr (0, slash));
        if (it == c.byId.end())
            return nullptr;
        const CNode& x = c.nodes[(size_t) it->second];
        if (slash == std::string::npos)
            return &x;
        return x.inner ? findPath (*x.inner, path.substr (slash + 1)) : nullptr;
    }
} // namespace

struct Engine::Impl
{
    CGraph root;
    Rng random { 1234 };
    std::int64_t last = -1;
    std::vector<View> view;
    StepOutput scratch;
    std::int64_t g = 0; // the step being evaluated

    void evalGraph (CGraph& c, const std::vector<int>* ext, StepOutput* out)
    {
        for (auto& x : c.nodes)
            x.done = false;
        for (int i : c.order)
        {
            CNode& x = c.nodes[(size_t) i];
            evalNode (c, x, ext, out);
            x.done = true;
        }
    }

    static int read (const CGraph& c, const CNode& x, int q)
    {
        if (q < 0 || q >= (int) x.src.size())
            return 0;
        const Src& s = x.src[(size_t) q];
        if (s.node < 0)
            return 0;
        const CNode& from = c.nodes[(size_t) s.node];
        if (! from.done || s.port < 0 || s.port >= (int) from.out.size())
            return 0;
        return from.out[(size_t) s.port];
    }

    void evalNode (CGraph& c, CNode& x, const std::vector<int>* ext, StepOutput* out)
    {
        auto inv = [&] (int q) { return read (c, x, q); };
        for (int q = 0; q < x.nIn && q < (int) x.in.size(); ++q)
            x.in[(size_t) q] = inv (q);

        switch (x.k)
        {
            case K::clock:
                if (x.out.size() >= 3)
                {
                    x.out[0] = 1;
                    x.out[1] = nonNegMod (g, 16) == 0 ? 1 : 0;
                    x.out[2] = nonNegMod (g, 32) == 0 ? 1 : 0;
                }
                break;

            case K::subin:
                for (size_t p = 0; p < x.out.size(); ++p)
                    x.out[p] = ext && p < ext->size() ? (*ext)[p] : 0;
                break;

            case K::euclid:
            case K::random:
            case K::ca:
            case K::step:
            {
                const int clk = inv (0), rst = inv (1);
                const int len = x.k == K::ca ? 16 : (int) x.pat.size();
                x.fired = 0;
                if (len <= 0 || x.out.empty())
                    break;
                if (rst)
                {
                    x.pos = 0;
                    x.fired = 1;
                    x.row = 0;
                }
                else if (clk)
                {
                    if (x.pos < 0)
                        x.pos = 0;
                    else
                    {
                        x.pos = (x.pos + 1) % len;
                        if (x.k == K::ca && x.pos == 0)
                            x.row = (x.row + 1) % (int) cellularRows().size();
                    }
                }
                if (x.pos >= len)
                    x.pos %= len;
                int cell = 0;
                if (x.pos >= 0)
                    cell = x.k == K::ca ? cellularRows()[(size_t) (x.row % (int) cellularRows().size())][(size_t) x.pos]
                                        : x.pat[(size_t) x.pos];
                x.out[0] = (clk || rst) && x.pos >= 0 && cell ? 1 : 0;
                break;
            }

            case K::grids:
            {
                const int clk = inv (0), rst = inv (1);
                x.fired = 0;
                if (rst)
                {
                    x.pos = 0;
                    x.fired = 1;
                    ++x.loop;
                }
                else if (clk)
                {
                    if (x.pos < 0)
                        x.pos = 0;
                    else
                    {
                        x.pos = (x.pos + 1) % 32;
                        if (x.pos == 0)
                            ++x.loop;
                    }
                }
                const int loop = x.chaos ? x.loop : 0;
                if (x.gpLoop != loop)
                {
                    x.gp = drumMapPattern (x.mx, x.my, x.fills[0], x.fills[1], x.fills[2], x.chaos, loop);
                    x.gpLoop = loop;
                }
                for (size_t k = 0; k < 3; ++k)
                    x.out[k] = (clk || rst) && x.pos >= 0 ? x.gp[k][(size_t) x.pos] : 0;
                break;
            }

            case K::div:
            {
                const int clk = inv (0);
                if (inv (1))
                    x.cnt = 0;
                int v = 0;
                if (clk)
                {
                    v = x.cnt % x.div == 0 ? 1 : 0;
                    ++x.cnt;
                }
                x.out[0] = v;
                break;
            }

            case K::accent:
            {
                const int v = inv (0);
                x.out[0] = v ? (nonNegMod (g, 8) == 0 ? 2 : 1) : 0;
                break;
            }

            case K::prob:
                x.out[0] = (inv (0) && random.next() < x.chance / 10.0) ? 1 : 0;
                break;

            case K::combine:
            {
                const bool a = inv (0) != 0, b = inv (1) != 0;
                bool r = false;
                switch (x.mode)
                {
                    case Combine::orMode: r = a || b; break;
                    case Combine::andMode: r = a && b; break;
                    case Combine::xorMode: r = a != b; break;
                    case Combine::aMinusB: r = a && ! b; break;
                }
                x.out[0] = r ? 1 : 0;
                break;
            }

            case K::sw:
            {
                const int w = x.ways, st = inv (0), rs = inv (1);
                if (rs)
                    x.sel = 0;
                else if (st)
                    x.sel = x.sel < 0 ? 0 : (x.sel + 1) % w;
                if (x.sel >= w)
                    x.sel %= w;
                const int k = std::max (0, x.sel);
                if (x.split)
                {
                    const int v = inv (2);
                    for (int i = 0; i < (int) x.out.size(); ++i)
                        x.out[(size_t) i] = i == k ? v : 0;
                }
                else if (! x.out.empty())
                    x.out[0] = inv (2 + k);
                break;
            }

            case K::sample:
                x.trig = inv (0);
                if (! x.out.empty())
                    x.out[0] = x.trig;
                if (x.trig && out)
                    for (int bus : x.buses)
                        if (out->hits.size() < out->hits.capacity())
                            out->hits.push_back ({ x.sample, bus, x.trig > 1, x.pitch, x.decay });
                break;

            case K::midi:
                if (out)
                    for (int q = 0; q < (int) x.notes.size(); ++q)
                    {
                        const int v = q < (int) x.in.size() ? x.in[(size_t) q] : 0;
                        if (v && out->notes.size() < out->notes.capacity())
                            out->notes.push_back ({ x.channel, x.notes[(size_t) q], v > 1 ? 127 : 96 });
                    }
                break;

            case K::sub:
            {
                if (! x.inner)
                    break;
                // The subsystem's inputs become its Inputs module's outputs.
                std::vector<int>& ext2 = x.in;
                evalGraph (*x.inner, &ext2, out);
                if (x.subout >= 0)
                {
                    const CNode& so = x.inner->nodes[(size_t) x.subout];
                    for (int q = 0; q < (int) x.out.size(); ++q)
                    {
                        const Src s = q < (int) so.src.size() ? so.src[(size_t) q] : Src {};
                        int v = 0;
                        if (s.node >= 0)
                        {
                            const CNode& from = x.inner->nodes[(size_t) s.node];
                            if (from.done && s.port >= 0 && s.port < (int) from.out.size())
                                v = from.out[(size_t) s.port];
                        }
                        x.out[(size_t) q] = v;
                    }
                }
                break;
            }

            case K::audio:
            case K::subout:
            case K::other:
                break;
        }
    }

    void runStep (std::int64_t index, StepOutput* out)
    {
        g = index;
        evalGraph (root, nullptr, out);
        last = index;
    }

    void updateView()
    {
        for (size_t i = 0; i < root.nodes.size() && i < view.size(); ++i)
        {
            const CNode& x = root.nodes[i];
            View& v = view[i];
            v.pos = x.pos;
            v.fired = x.fired;
            v.sel = x.sel;
            v.row = x.row;
            v.loop = x.loop;
            v.out.fill (0);
            v.in.fill (0);
            for (size_t p = 0; p < x.out.size() && p < v.out.size(); ++p)
                v.out[p] = x.out[p];
            for (size_t q = 0; q < x.in.size() && q < v.in.size(); ++q)
                v.in[q] = x.in[q];
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::load (const Graph& patch)
{
    impl->root = CGraph {};
    compile (patch, impl->root);
    impl->view.assign (impl->root.nodes.size(), View {});
    impl->last = -1;
}

void Engine::adoptState (const Engine& previous)
{
    adopt (impl->root, previous.impl->root);
    impl->last = previous.impl->last;
    impl->random = previous.impl->random;
}

void Engine::step (std::int64_t index, StepOutput& out)
{
    impl->runStep (index, &out);
    impl->updateView();
}

void Engine::locate (std::int64_t index)
{
    resetState();
    const std::int64_t from = index - nonNegMod (index, 32);
    for (std::int64_t s = from; s < index; ++s)
    {
        impl->scratch.clear();
        impl->runStep (s, &impl->scratch);
    }
    impl->scratch.clear();
    impl->last = index - 1;
    impl->updateView();
}

void Engine::resetState()
{
    resetGraph (impl->root);
    impl->last = -1;
}

std::int64_t Engine::lastStep() const { return impl->last; }

void Engine::setRandomSeed (std::uint32_t seed) { impl->random = Rng (seed); }

const std::vector<Engine::View>& Engine::view() const { return impl->view; }

std::string Engine::valuesAt (const std::string& path) const
{
    const CNode* x = findPath (impl->root, path);
    if (! x)
        return "?";
    auto join = [] (const std::vector<int>& v, size_t n) {
        std::string s;
        for (size_t i = 0; i < n && i < v.size(); ++i)
            s += (i ? "," : "") + std::to_string (v[i]);
        return s;
    };
    if (x->k == K::midi)
        return join (x->in, x->notes.size());
    if (x->k == K::sample)
        return std::to_string (x->trig);
    if (x->k == K::subout || x->k == K::audio || x->k == K::other)
        return {};
    return join (x->out, x->out.size());
}
} // namespace ps
