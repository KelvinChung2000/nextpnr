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

#include "clock_router.h"

#include <algorithm>
#include <deque>

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

const char *to_string(ClockRouteStatus status)
{
    switch (status) {
    case ClockRouteStatus::ROUTED:
        return "routed";
    case ClockRouteStatus::PARTIAL:
        return "partially routed";
    case ClockRouteStatus::REJECTED:
        return "rejected";
    }
    NPNR_ASSERT_FALSE("unknown ClockRouteStatus");
}

const char *to_string(ClockRejectReason reason)
{
    switch (reason) {
    case ClockRejectReason::NONE:
        return "none";
    case ClockRejectReason::NO_ELIGIBLE_CHANNEL:
        return "no channel can be entered from the net source";
    case ClockRejectReason::CHANNELS_EXHAUSTED:
        return "all channels that could serve these sinks are taken";
    case ClockRejectReason::SINKS_UNREACHABLE:
        return "no channel reaches these sinks";
    }
    NPNR_ASSERT_FALSE("unknown ClockRejectReason");
}

namespace {

// Predecessor markers for the channel chain search.
constexpr int UNVISITED = -2;
constexpr int CHAIN_START = -1;

struct ClockRouterWorker
{
    Context *ctx;
    const ClockNetwork &net_desc;
    ClockRouteReport report;

    ClockRouterWorker(Context *ctx, const ClockNetwork &net_desc) : ctx(ctx), net_desc(net_desc)
    {
        report.channel_owner.resize(net_desc.channels.size(), nullptr);
    }

    // Distinct sink wires of a net, first user of each kept for diagnostics.
    std::vector<ClockSink> sinks_of(NetInfo *net) const
    {
        std::vector<ClockSink> sinks;
        pool<WireId> seen;
        for (auto usr : net->users.enumerate()) {
            for (WireId wire : ctx->getNetinfoSinkWires(net, usr.value)) {
                if (wire == WireId() || seen.count(wire))
                    continue;
                seen.insert(wire);
                sinks.push_back(ClockSink{usr.index, wire});
            }
        }
        return sinks;
    }

    bool is_enterable(int channel, const pool<WireId> &from) const
    {
        for (WireId entry : net_desc.channels.at(channel).entries)
            if (from.count(entry))
                return true;
        return false;
    }

    // Shortest chain of free channels leading from the wires already reached to
    // a channel that reaches `target`, by breadth-first search over channels.
    // A chain of one is the ladder case; a mesh needs a trunk channel that
    // covers no sink at all before a channel that covers many, which is why
    // channels are searched rather than scored by coverage.
    std::vector<int> find_chain(const pool<WireId> &reached, const std::vector<bool> &taken, WireId target) const
    {
        int count = int(net_desc.channels.size());
        std::vector<int> prev(count, UNVISITED);
        std::deque<int> queue;
        for (int i = 0; i < count; i++) {
            if (taken.at(i) || !is_enterable(i, reached))
                continue;
            prev.at(i) = CHAIN_START;
            queue.push_back(i);
        }

        int found = -1;
        while (!queue.empty() && found < 0) {
            int current = queue.front();
            queue.pop_front();
            if (net_desc.reach.at(current).count(target)) {
                found = current;
                break;
            }
            for (int i = 0; i < count; i++) {
                if (taken.at(i) || prev.at(i) != UNVISITED)
                    continue;
                if (!is_enterable(i, net_desc.reach.at(current)))
                    continue;
                prev.at(i) = current;
                queue.push_back(i);
            }
        }

        std::vector<int> chain;
        for (int cursor = found; cursor >= 0; cursor = prev.at(cursor))
            chain.push_back(cursor);
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    // Minimum channel-set cover, approached one uncovered sink at a time. Reach
    // is an optimistic filter here; path planning below is the authority on
    // what is actually connected.
    std::vector<int> select_channels(WireId src_wire, const std::vector<ClockSink> &sinks) const
    {
        pool<WireId> reached;
        reached.insert(src_wire);
        pool<WireId> uncovered;
        for (const ClockSink &sink : sinks)
            uncovered.insert(sink.wire);

        std::vector<bool> taken(net_desc.channels.size());
        for (size_t i = 0; i < net_desc.channels.size(); i++)
            taken.at(i) = report.channel_owner.at(i) != nullptr;

        std::vector<int> chosen;
        while (!uncovered.empty()) {
            // Sinks are visited in net order, so the chain committed first is
            // the same on every run.
            WireId target;
            for (const ClockSink &sink : sinks) {
                if (uncovered.count(sink.wire)) {
                    target = sink.wire;
                    break;
                }
            }

            std::vector<int> chain = find_chain(reached, taken, target);
            if (chain.empty())
                break;

            for (int channel : chain) {
                chosen.push_back(channel);
                taken.at(channel) = true;
                for (WireId wire : net_desc.reach.at(channel)) {
                    reached.insert(wire);
                    uncovered.erase(wire);
                }
            }
        }
        return chosen;
    }

    // Backward search from a sink to the net's existing tree, restricted to
    // the pips the assigned channels own. Appends the pips of the new branch.
    bool plan_sink(WireId sink, const pool<PipId> &allowed, pool<WireId> &tree, std::vector<PipId> &pips) const
    {
        if (tree.count(sink))
            return true;

        dict<WireId, PipId> toward_sink;
        pool<WireId> visited;
        visited.insert(sink);
        std::deque<WireId> queue;
        queue.push_back(sink);
        WireId meet;

        while (!queue.empty() && meet == WireId()) {
            WireId wire = queue.front();
            queue.pop_front();
            for (PipId pip : ctx->getPipsUphill(wire)) {
                if (!allowed.count(pip) || !ctx->checkPipAvail(pip))
                    continue;
                WireId src = ctx->getPipSrcWire(pip);
                if (visited.count(src))
                    continue;
                visited.insert(src);
                toward_sink[src] = pip;
                if (tree.count(src)) {
                    meet = src;
                    break;
                }
                queue.push_back(src);
            }
        }

        if (meet == WireId())
            return false;

        WireId cursor = meet;
        while (cursor != sink) {
            PipId pip = toward_sink.at(cursor);
            pips.push_back(pip);
            cursor = ctx->getPipDstWire(pip);
            tree.insert(cursor);
        }
        return true;
    }

    void bind_net(NetInfo *net, WireId src_wire, const std::vector<PipId> &pips) const
    {
        NetInfo *bound = ctx->getBoundWireNet(src_wire);
        if (bound != nullptr && bound != net)
            log_error("clock source wire '%s' of net '%s' is already bound to net '%s'\n", ctx->nameOfWire(src_wire),
                      ctx->nameOf(net), ctx->nameOf(bound));
        if (bound == net)
            ctx->unbindWire(src_wire);
        ctx->bindWire(src_wire, net, STRENGTH_LOCKED);
        for (PipId pip : pips) {
            // Overlapping channel resource sets would otherwise corrupt the
            // binding silently on arches whose bindPip overwrites.
            NPNR_ASSERT(ctx->getBoundWireNet(ctx->getPipDstWire(pip)) == nullptr);
            ctx->bindPip(pip, net, STRENGTH_LOCKED);
        }
    }

    void route(NetInfo *net, ClockNetResult &result)
    {
        WireId src_wire = ctx->getNetinfoSourceWire(net);
        if (src_wire == WireId())
            log_error("net '%s' has no source wire\n", ctx->nameOf(net));

        std::vector<ClockSink> sinks = sinks_of(net);
        std::vector<int> chosen = select_channels(src_wire, sinks);

        pool<PipId> allowed;
        for (int channel : chosen)
            for (PipId pip : net_desc.channels.at(channel).resources)
                allowed.insert(pip);

        // router2 adopts pre-routed arcs one at a time, so a net whose sinks
        // are not all on the network can still take the network for the ones
        // that are. A clock that also feeds ordinary logic needs this.
        pool<WireId> tree;
        tree.insert(src_wire);
        std::vector<PipId> pips;
        for (const ClockSink &sink : sinks) {
            if (!plan_sink(sink.wire, allowed, tree, pips))
                result.unreached_sinks.push_back(sink);
        }

        if (pips.empty()) {
            result.status = ClockRouteStatus::REJECTED;
            result.reason = classify_failure(src_wire, chosen, result.unreached_sinks);
            return;
        }

        bind_net(net, src_wire, pips);
        result.status = result.unreached_sinks.empty() ? ClockRouteStatus::ROUTED : ClockRouteStatus::PARTIAL;
        if (!result.unreached_sinks.empty())
            result.reason = classify_failure(src_wire, chosen, result.unreached_sinks);

        // Claim only the channels the bound path actually runs through, so a
        // channel selected but then unused stays available to the next net.
        pool<PipId> bound(pips.begin(), pips.end());
        for (int channel : chosen) {
            bool used = false;
            for (PipId pip : net_desc.channels.at(channel).resources)
                if (bound.count(pip))
                    used = true;
            if (!used)
                continue;
            result.channels.push_back(channel);
            report.channel_owner.at(channel) = net;
        }
        std::sort(result.channels.begin(), result.channels.end());
    }

    ClockRejectReason classify_failure(WireId src_wire, const std::vector<int> &chosen,
                                       const std::vector<ClockSink> &unreached) const
    {
        if (chosen.empty()) {
            bool any_entry = false;
            for (const ClockChannel &ch : net_desc.channels)
                for (WireId entry : ch.entries)
                    if (entry == src_wire)
                        any_entry = true;
            if (!any_entry)
                return ClockRejectReason::NO_ELIGIBLE_CHANNEL;
        }
        // A taken channel that would have served one of these sinks means the
        // net lost a contest, not that the fabric cannot reach it.
        for (int i = 0; i < int(net_desc.channels.size()); i++) {
            if (report.channel_owner.at(i) == nullptr)
                continue;
            for (const ClockSink &sink : unreached)
                if (net_desc.reach.at(i).count(sink.wire))
                    return ClockRejectReason::CHANNELS_EXHAUSTED;
        }
        return ClockRejectReason::SINKS_UNREACHABLE;
    }
};

} // namespace

ClockRouteReport route_clock_nets(Context *ctx, const ClockNetwork &network,
                                  const std::vector<ClockCandidate> &candidates)
{
    if (!network.is_indexed())
        log_error("clock network was not indexed before routing\n");

    // Priority, then fanout, then name. Never hash iteration order: the ice40
    // promoter picks its losers that way and the choice is unexplainable.
    std::vector<ClockCandidate> ordered(candidates);
    std::stable_sort(ordered.begin(), ordered.end(), [ctx](const ClockCandidate &a, const ClockCandidate &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        size_t a_fanout = a.net->users.entries(), b_fanout = b.net->users.entries();
        if (a_fanout != b_fanout)
            return a_fanout > b_fanout;
        return a.net->name.str(ctx) < b.net->name.str(ctx);
    });

    ClockRouterWorker worker(ctx, network);
    for (const ClockCandidate &candidate : ordered) {
        ClockNetResult result;
        result.net = candidate.net;
        worker.route(candidate.net, result);
        worker.report.nets.push_back(result);
    }

    for (const ClockNetResult &result : worker.report.nets) {
        if (result.status != ClockRouteStatus::REJECTED) {
            std::string channels;
            for (int channel : result.channels)
                channels += stringf("%s%s", channels.empty() ? "" : ", ", network.channels.at(channel).name.c_str(ctx));
            log_info("    net '%s' %s on clock channel%s %s\n", ctx->nameOf(result.net), to_string(result.status),
                     result.channels.size() == 1 ? "" : "s", channels.c_str());
        }
        if (result.unreached_sinks.empty())
            continue;
        log_warning("    net '%s': %d sink%s left to the general router, %s\n", ctx->nameOf(result.net),
                    int(result.unreached_sinks.size()), result.unreached_sinks.size() == 1 ? "" : "s",
                    to_string(result.reason));
        for (const ClockSink &sink : result.unreached_sinks) {
            const PortRef &user = result.net->users.at(sink.user);
            log_warning("        %s.%s\n", ctx->nameOf(user.cell), ctx->nameOf(user.port));
        }
    }
    log_info("    %d of %d clock channels used\n", worker.report.used_channels(), int(network.channels.size()));

    return worker.report;
}

NEXTPNR_NAMESPACE_END
