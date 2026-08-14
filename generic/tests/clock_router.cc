/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  The nextpnr Authors
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "gtest/gtest.h"

#include "clock_router.h"
#include "log.h"
#include "nextpnr.h"
#include "router2.h"

USING_NEXTPNR_NAMESPACE

namespace {

// The two topologies the router must serve with identical code. Every test
// below is parameterised over this: if a topology-specific branch ever appears
// in the router, one of these cases stops passing.
enum class Topology
{
    LADDER,
    MESH,
};

const char *name_of(Topology topology) { return topology == Topology::LADDER ? "ladder" : "mesh"; }

struct FabricSpec
{
    Topology topology = Topology::LADDER;
    int width = 4;
    int height = 4;
    // Nets the network can carry at once: ladders for a ladder fabric, trunks
    // and per-region distribution tracks for a mesh.
    int capacity = 2;
    // Mesh only: tiles along the edge of a clock region.
    int region_size = 2;
};

// A generic-arch fabric with a dedicated clock network laid over it.
struct TestFabric
{
    Context *ctx;
    FabricSpec spec;
    ClockNetwork network;

    std::vector<WireId> source_wires;
    std::vector<CellInfo *> sources;
    // Indexed by slot, then by tile. A tile carries one clock tap per domain,
    // as a real fabric does: a flip-flop's clock pin joins exactly one net, so
    // without this no two domains could reach the same tile.
    std::vector<std::vector<CellInfo *>> ffs;
    std::vector<std::vector<WireId>> tile_clk;
    std::vector<std::vector<WireId>> tile_data;

    IdString id_CLK, id_O, id_Q, id_D;

    int tile_index(int x, int y) const { return y * spec.width + x; }
    int tile_count() const { return spec.width * spec.height; }
    // One more than the network can carry, so oversubscription is a property
    // of the network rather than of the tiles.
    int slots() const { return spec.capacity + 1; }

    int region_of(int x, int y) const
    {
        int cols = spec.width / spec.region_size;
        return (y / spec.region_size) * cols + (x / spec.region_size);
    }
    int region_count() const { return (spec.width / spec.region_size) * (spec.height / spec.region_size); }

    IdStringList wire_name(const std::string &name) { return IdStringList(ctx->id(name)); }

    PipId add_pip(const std::string &name, WireId src, WireId dst)
    {
        return ctx->addPip(wire_name(name), ctx->id("CLK_PIP"), src, dst, 0.1, Loc(0, 0, 0));
    }

    void build(Context *context, const FabricSpec &fabric_spec)
    {
        ctx = context;
        spec = fabric_spec;
        id_CLK = ctx->id("CLK");
        id_O = ctx->id("O");
        id_Q = ctx->id("Q");
        id_D = ctx->id("D");

        build_tiles();
        build_sources();
        if (spec.topology == Topology::LADDER)
            build_ladder();
        else
            build_mesh();
        build_general_routing();
        network.index(ctx);
    }

    // Pips outside every channel, reaching the data pins. The clock router must
    // never use these, and the general router must remain free to.
    void build_general_routing()
    {
        for (size_t s = 0; s < source_wires.size(); s++)
            for (int slot = 0; slot < slots(); slot++)
                for (int i = 0; i < tile_count(); i++)
                    add_pip(stringf("PIP_GENERAL_%zu_%d_%d", s, slot, i), source_wires.at(s), tile_data.at(slot).at(i));
    }

    void build_tiles()
    {
        tile_clk.assign(slots(), std::vector<WireId>(tile_count()));
        tile_data.assign(slots(), std::vector<WireId>(tile_count()));
        ffs.assign(slots(), std::vector<CellInfo *>(tile_count()));
        for (int y = 0; y < spec.height; y++) {
            for (int x = 0; x < spec.width; x++) {
                int index = tile_index(x, y);
                for (int slot = 0; slot < slots(); slot++) {
                    WireId clk = ctx->addWire(wire_name(stringf("CLK_%d_%d_%d", x, y, slot)), id_CLK, x, y);
                    BelId bel = ctx->addBel(wire_name(stringf("FF_%d_%d_%d", x, y, slot)), ctx->id("FF"),
                                            Loc(x, y, slot), false, false);
                    ctx->addBelInput(bel, id_CLK, clk);

                    // An ordinary data pin, deliberately off the clock network,
                    // so a net can be given a sink the network cannot serve.
                    WireId data = ctx->addWire(wire_name(stringf("D_%d_%d_%d", x, y, slot)), id_D, x, y);
                    ctx->addBelInput(bel, id_D, data);

                    CellInfo *cell = ctx->createCell(ctx->id(stringf("ff_%d_%d_%d", x, y, slot)), ctx->id("FF"));
                    cell->addInput(id_CLK);
                    cell->addInput(id_D);
                    cell->bel_pins[id_CLK].push_back(id_CLK);
                    cell->bel_pins[id_D].push_back(id_D);
                    if (slot == 0) {
                        WireId out = ctx->addWire(wire_name(stringf("Q_%d_%d", x, y)), id_Q, x, y);
                        ctx->addBelOutput(bel, id_Q, out);
                        cell->addOutput(id_Q);
                        cell->bel_pins[id_Q].push_back(id_Q);
                    }
                    ctx->bindBel(bel, cell, STRENGTH_STRONG);

                    tile_clk.at(slot).at(index) = clk;
                    tile_data.at(slot).at(index) = data;
                    ffs.at(slot).at(index) = cell;
                }
            }
        }
    }

    void build_sources()
    {
        // One driver per net the network can carry, plus one spare so that
        // oversubscription is a property of the network, not of the drivers.
        for (int i = 0; i < spec.capacity + 1; i++) {
            BelId bel = ctx->addBel(wire_name(stringf("CLKSRC_%d", i)), ctx->id("CLKSRC"), Loc(0, 0, slots() + i), true,
                                    false);
            WireId wire = ctx->addWire(wire_name(stringf("GCLK_%d", i)), ctx->id("GCLK"), 0, 0);
            ctx->addBelOutput(bel, id_O, wire);

            CellInfo *cell = ctx->createCell(ctx->id(stringf("clksrc_%d", i)), ctx->id("CLKSRC"));
            cell->addOutput(id_O);
            cell->bel_pins[id_O].push_back(id_O);
            ctx->bindBel(bel, cell, STRENGTH_STRONG);

            source_wires.push_back(wire);
            sources.push_back(cell);
        }
    }

    // Independent daisy chains, each reaching every tile: the FABulous fabric
    // of today, one ladder per clock domain.
    void build_ladder()
    {
        for (int c = 0; c < spec.capacity; c++) {
            ClockChannel channel;
            channel.name = ctx->id(stringf("LADDER_%d", c));
            channel.tier = ClockTier::LEAF;
            channel.entries = source_wires;

            std::vector<WireId> spine;
            for (int i = 0; i < tile_count(); i++)
                spine.push_back(ctx->addWire(wire_name(stringf("SPINE_%d_%d", c, i)), ctx->id("CLK_ROUTE"),
                                             i % spec.width, i / spec.width));

            for (size_t s = 0; s < source_wires.size(); s++)
                channel.resources.insert(add_pip(stringf("PIP_ENTRY_%d_%zu", c, s), source_wires.at(s), spine.at(0)));
            for (int i = 1; i < tile_count(); i++)
                channel.resources.insert(add_pip(stringf("PIP_CHAIN_%d_%d", c, i), spine.at(i - 1), spine.at(i)));
            for (int i = 0; i < tile_count(); i++)
                for (int slot = 0; slot < slots(); slot++)
                    channel.resources.insert(
                            add_pip(stringf("PIP_TAP_%d_%d_%d", c, i, slot), spine.at(i), tile_clk.at(slot).at(i)));

            network.channels.push_back(channel);
        }
    }

    // Trunks feeding per-region distribution tracks. A net needs a trunk plus
    // one distribution channel per region it has sinks in, so covering the
    // fabric takes a channel *set* rather than a single channel.
    void build_mesh()
    {
        std::vector<std::vector<WireId>> roots(region_count());
        for (int j = 0; j < region_count(); j++)
            for (int k = 0; k < spec.capacity; k++)
                roots.at(j).push_back(ctx->addWire(wire_name(stringf("ROOT_%d_%d", j, k)), ctx->id("CLK_ROOT"), 0, 0));

        for (int t = 0; t < spec.capacity; t++) {
            ClockChannel channel;
            channel.name = ctx->id(stringf("TRUNK_%d", t));
            channel.tier = ClockTier::ROUTE;
            channel.entries = source_wires;

            WireId trunk = ctx->addWire(wire_name(stringf("HROUTE_%d", t)), ctx->id("CLK_ROUTE"), 0, 0);
            for (size_t s = 0; s < source_wires.size(); s++)
                channel.resources.insert(add_pip(stringf("PIP_TRUNK_%d_%zu", t, s), source_wires.at(s), trunk));
            for (int j = 0; j < region_count(); j++)
                for (int k = 0; k < spec.capacity; k++)
                    channel.resources.insert(add_pip(stringf("PIP_ROOT_%d_%d_%d", t, j, k), trunk, roots.at(j).at(k)));

            network.channels.push_back(channel);
        }

        for (int j = 0; j < region_count(); j++) {
            for (int k = 0; k < spec.capacity; k++) {
                ClockChannel channel;
                channel.name = ctx->id(stringf("DISTR_%d_%d", j, k));
                channel.tier = ClockTier::DISTRIBUTE;
                channel.region = j;
                channel.entries.push_back(roots.at(j).at(k));

                WireId distr = ctx->addWire(wire_name(stringf("VDISTR_%d_%d", j, k)), ctx->id("CLK_ROUTE"), 0, 0);
                channel.resources.insert(add_pip(stringf("PIP_DISTR_%d_%d", j, k), roots.at(j).at(k), distr));
                for (int y = 0; y < spec.height; y++)
                    for (int x = 0; x < spec.width; x++)
                        if (region_of(x, y) == j)
                            for (int slot = 0; slot < slots(); slot++)
                                channel.resources.insert(add_pip(stringf("PIP_LEAF_%d_%d_%d_%d_%d", j, k, x, y, slot),
                                                                 distr, tile_clk.at(slot).at(tile_index(x, y))));

                network.channels.push_back(channel);
            }
        }
    }

    // A net from clock source `index` to one flip-flop in every tile. Each
    // domain uses its own slot, so all of them span the whole fabric.
    NetInfo *make_clock_net(int index)
    {
        NetInfo *net = ctx->createNet(ctx->id(stringf("clk_%d", index)));
        sources.at(index)->connectPort(id_O, net);
        for (CellInfo *ff : ffs.at(index))
            ff->connectPort(id_CLK, net);
        return net;
    }

    std::vector<WireId> sinks_of_clock_net(int index) const { return tile_clk.at(index); }

    // The same clock, but also feeding one ordinary data pin. Real designs do
    // this with sampled clocks and with high-fanout resets.
    NetInfo *make_mixed_net(int index)
    {
        NetInfo *net = make_clock_net(index);
        ffs.at(slots() - 1).at(0)->connectPort(id_D, net);
        return net;
    }

    // A net driven by fabric logic rather than a clock source: nothing on the
    // network can be entered from it.
    NetInfo *make_logic_net(const std::string &name)
    {
        NetInfo *net = ctx->createNet(ctx->id(name));
        ffs.at(0).at(0)->connectPort(id_Q, net);
        for (size_t i = 1; i < ffs.at(0).size(); i++)
            ffs.at(0).at(i)->connectPort(id_CLK, net);
        return net;
    }
};

std::unique_ptr<Context> make_context()
{
    ArchArgs args;
    std::unique_ptr<Context> ctx(new Context(args));
    // Settings the command-line flow normally establishes, which router2 and
    // the timing analyser read without a default.
    ctx->setting<bool>("timing_driven", false);
    ctx->setting<bool>("router/tmg_ripup", false);
    ctx->setting<float>("target_freq", 12e6f);
    return ctx;
}

std::vector<ClockCandidate> candidates_of(const std::vector<NetInfo *> &nets)
{
    std::vector<ClockCandidate> candidates;
    for (size_t i = 0; i < nets.size(); i++)
        candidates.push_back(ClockCandidate{nets.at(i), int(nets.size() - i)});
    return candidates;
}

class ClockRouterTest : public ::testing::TestWithParam<Topology>
{
  protected:
    void SetUp() override
    {
        ctx = make_context();
        spec.topology = GetParam();
    }

    void build() { fabric.build(ctx.get(), spec); }

    std::unique_ptr<Context> ctx;
    FabricSpec spec;
    TestFabric fabric;
};

TEST_P(ClockRouterTest, single_net_covers_every_sink)
{
    build();
    NetInfo *net = fabric.make_clock_net(0);

    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of({net}));

    ASSERT_EQ(report.nets.size(), 1u);
    EXPECT_EQ(report.nets.at(0).status, ClockRouteStatus::ROUTED);
    EXPECT_TRUE(report.all_routed());

    // A ladder is covered by one channel; a mesh needs a trunk plus one
    // distribution channel per region, which is the whole point of solving
    // this as a channel-set cover rather than a channel choice.
    size_t expected = spec.topology == Topology::LADDER ? 1u : 1u + size_t(fabric.region_count());
    EXPECT_EQ(report.nets.at(0).channels.size(), expected);
    if (spec.topology == Topology::MESH) {
        int trunks = 0;
        for (int channel : report.nets.at(0).channels)
            if (fabric.network.channels.at(channel).tier == ClockTier::ROUTE)
                ++trunks;
        EXPECT_EQ(trunks, 1);
    }

    // Every flip-flop clock pin is bound to this net, at locked strength so
    // the general router cannot take it back.
    for (WireId clk : fabric.sinks_of_clock_net(0)) {
        EXPECT_EQ(ctx->getBoundWireNet(clk), net);
        ASSERT_TRUE(net->wires.count(clk));
        EXPECT_EQ(net->wires.at(clk).strength, STRENGTH_LOCKED);
    }
}

TEST_P(ClockRouterTest, distinct_nets_take_distinct_channels)
{
    build();
    std::vector<NetInfo *> nets;
    for (int i = 0; i < spec.capacity; i++)
        nets.push_back(fabric.make_clock_net(i));

    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of(nets));

    EXPECT_TRUE(report.all_routed());
    pool<int> used;
    for (const ClockNetResult &result : report.nets) {
        EXPECT_EQ(result.status, ClockRouteStatus::ROUTED);
        for (int channel : result.channels) {
            EXPECT_FALSE(used.count(channel)) << "channel " << channel << " assigned twice";
            used.insert(channel);
        }
    }
}

// The behaviour this whole design exists to fix: ice40 drops surplus clocks
// with no diagnostic at all.
TEST_P(ClockRouterTest, oversubscription_is_reported_not_silent)
{
    build();
    std::vector<NetInfo *> nets;
    for (int i = 0; i < spec.capacity + 1; i++)
        nets.push_back(fabric.make_clock_net(i));

    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of(nets));

    EXPECT_FALSE(report.all_routed());
    int rejected = 0;
    for (const ClockNetResult &result : report.nets) {
        if (result.status == ClockRouteStatus::ROUTED)
            continue;
        ++rejected;
        EXPECT_EQ(result.status, ClockRouteStatus::REJECTED);
        EXPECT_EQ(result.reason, ClockRejectReason::CHANNELS_EXHAUSTED);
        EXPECT_FALSE(result.unreached_sinks.empty());
        // The loser is the lowest-priority candidate, not whichever net the
        // hash table happened to yield last.
        EXPECT_EQ(result.net, nets.back());
    }
    EXPECT_EQ(rejected, 1);
}

TEST_P(ClockRouterTest, logic_driven_net_is_rejected_with_a_reason)
{
    build();
    NetInfo *net = fabric.make_logic_net("from_logic");

    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of({net}));

    ASSERT_EQ(report.nets.size(), 1u);
    EXPECT_EQ(report.nets.at(0).status, ClockRouteStatus::REJECTED);
    EXPECT_EQ(report.nets.at(0).reason, ClockRejectReason::NO_ELIGIBLE_CHANNEL);
    EXPECT_EQ(report.used_channels(), 0);
}

// router2 adopts pre-routed arcs individually, so a net with one sink off the
// network still gets the network for the rest of them.
TEST_P(ClockRouterTest, mixed_sink_net_keeps_the_clock_sinks_on_the_network)
{
    build();
    NetInfo *net = fabric.make_mixed_net(0);

    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of({net}));

    ASSERT_EQ(report.nets.size(), 1u);
    const ClockNetResult &result = report.nets.at(0);
    EXPECT_EQ(result.status, ClockRouteStatus::PARTIAL);
    EXPECT_EQ(result.reason, ClockRejectReason::SINKS_UNREACHABLE);
    ASSERT_EQ(result.unreached_sinks.size(), 1u);
    EXPECT_EQ(net->users.at(result.unreached_sinks.at(0).user).port, fabric.id_D);

    // The clock sinks are bound; the data sink is left for the general router.
    for (WireId clk : fabric.sinks_of_clock_net(0))
        EXPECT_EQ(ctx->getBoundWireNet(clk), net);
    EXPECT_EQ(ctx->getBoundWireNet(result.unreached_sinks.at(0).wire), nullptr);

    // Two-way exclusion: the clock router binds only pips its channels own,
    // even where general routing would have reached the sink. This is the trap
    // for anyone who later widens the set of pips the path search may use.
    for (const auto &wire : net->wires) {
        if (wire.second.pip == PipId())
            continue;
        EXPECT_EQ(ctx->getPipName(wire.second.pip).str(ctx.get()).rfind("PIP_GENERAL", 0), std::string::npos)
                << "clock router used a general routing pip";
    }
}

TEST_P(ClockRouterTest, assignment_is_deterministic)
{
    build();
    std::vector<NetInfo *> nets;
    for (int i = 0; i < spec.capacity; i++)
        nets.push_back(fabric.make_clock_net(i));
    ClockRouteReport first = route_clock_nets(ctx.get(), fabric.network, candidates_of(nets));

    // Rebuild an identical fabric and design in a fresh context.
    std::unique_ptr<Context> other_ctx = make_context();
    TestFabric other;
    other.build(other_ctx.get(), spec);
    std::vector<NetInfo *> other_nets;
    for (int i = 0; i < spec.capacity; i++)
        other_nets.push_back(other.make_clock_net(i));
    ClockRouteReport second = route_clock_nets(other_ctx.get(), other.network, candidates_of(other_nets));

    ASSERT_EQ(first.nets.size(), second.nets.size());
    for (size_t i = 0; i < first.nets.size(); i++) {
        EXPECT_EQ(first.nets.at(i).net->name.str(ctx.get()), second.nets.at(i).net->name.str(other_ctx.get()));
        EXPECT_EQ(first.nets.at(i).channels, second.nets.at(i).channels);
    }
}

// The integration contract with the general router: a fully bound path is
// adopted as pre-routed, so router2 leaves it exactly as it found it.
TEST_P(ClockRouterTest, router2_adopts_the_bound_clock_tree)
{
    build();
    // A mixed net, so this also proves adoption is per-arc: the general router
    // must complete the data sink without disturbing the bound clock tree.
    NetInfo *net = fabric.make_mixed_net(0);
    ClockRouteReport report = route_clock_nets(ctx.get(), fabric.network, candidates_of({net}));
    ASSERT_EQ(report.nets.at(0).status, ClockRouteStatus::PARTIAL);
    WireId data_sink = report.nets.at(0).unreached_sinks.at(0).wire;

    dict<WireId, PipId> before;
    for (const auto &wire : net->wires)
        before[wire.first] = wire.second.pip;

    router2(ctx.get(), Router2Cfg(ctx.get()));

    for (const auto &entry : before) {
        ASSERT_TRUE(net->wires.count(entry.first)) << "router2 dropped a pre-routed wire";
        EXPECT_EQ(net->wires.at(entry.first).pip, entry.second);
        EXPECT_EQ(net->wires.at(entry.first).strength, STRENGTH_LOCKED);
    }
    // The sink the network could not serve is routed, by the general router.
    ASSERT_TRUE(net->wires.count(data_sink));
    EXPECT_FALSE(before.count(data_sink));
    EXPECT_LT(net->wires.at(data_sink).strength, STRENGTH_LOCKED);
}

INSTANTIATE_TEST_SUITE_P(Topologies, ClockRouterTest, ::testing::Values(Topology::LADDER, Topology::MESH),
                         [](const ::testing::TestParamInfo<Topology> &info) { return name_of(info.param); });

} // namespace
