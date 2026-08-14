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

#ifndef CLOCK_ROUTER_H
#define CLOCK_ROUTER_H

#include "clock_network.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Assigns nets to dedicated clock channels and pre-binds their paths at
// STRENGTH_LOCKED, so the general router adopts them as pre-routed rather than
// routing them itself.
//
// Nets are supplied by the caller with a priority; this pass does not decide
// what a clock is. Higher priority wins a contended channel. Ties break on
// fanout, then on net name, so the result never depends on hash iteration
// order.

struct ClockCandidate
{
    NetInfo *net = nullptr;
    int priority = 0;
};

enum class ClockRouteStatus : uint8_t
{
    // Every sink of the net is on the network.
    ROUTED,
    // Some sinks are on the network, the rest are left to the general router.
    // A clock that also feeds ordinary logic lands here.
    PARTIAL,
    // No sink is on the network; the net was not touched.
    REJECTED,
};

enum class ClockRejectReason : uint8_t
{
    NONE,
    // The net's source wire cannot enter any channel of the network.
    NO_ELIGIBLE_CHANNEL,
    // Every channel that could serve the remaining sinks was already taken.
    CHANNELS_EXHAUSTED,
    // Channels were available but do not reach the remaining sinks.
    SINKS_UNREACHABLE,
};

const char *to_string(ClockRouteStatus status);
const char *to_string(ClockRejectReason reason);

struct ClockSink
{
    store_index<PortRef> user;
    WireId wire;
};

struct ClockNetResult
{
    NetInfo *net = nullptr;
    ClockRouteStatus status = ClockRouteStatus::ROUTED;
    // Why the sinks in unreached_sinks were left off; NONE iff ROUTED.
    ClockRejectReason reason = ClockRejectReason::NONE;
    // Channel indices actually used by the bound path, ascending.
    std::vector<int> channels;
    // Sinks left to the general router. Empty iff ROUTED.
    std::vector<ClockSink> unreached_sinks;
};

struct ClockRouteReport
{
    // One entry per candidate, in the order the router considered them.
    std::vector<ClockNetResult> nets;
    // Owner of each channel, or nullptr if unused. Indexed as the network.
    std::vector<NetInfo *> channel_owner;

    bool all_routed() const
    {
        for (const auto &n : nets)
            if (n.status != ClockRouteStatus::ROUTED)
                return false;
        return true;
    }

    int used_channels() const
    {
        int used = 0;
        for (auto *owner : channel_owner)
            if (owner != nullptr)
                ++used;
        return used;
    }
};

// Route the given candidates onto the network, binding what fits at
// STRENGTH_LOCKED. Sinks that do not fit are left to the general router and
// reported with a reason; nothing is dropped silently.
ClockRouteReport route_clock_nets(Context *ctx, const ClockNetwork &network,
                                  const std::vector<ClockCandidate> &candidates);

NEXTPNR_NAMESPACE_END

#endif
