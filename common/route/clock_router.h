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
    ROUTED,
    // The net's source wire cannot enter any channel of the network.
    NO_ELIGIBLE_CHANNEL,
    // Every channel that could serve this net was already taken.
    CHANNELS_EXHAUSTED,
    // Channels were available but do not collectively reach every sink.
    SINKS_UNREACHABLE,
};

const char *to_string(ClockRouteStatus status);

struct ClockNetResult
{
    NetInfo *net = nullptr;
    ClockRouteStatus status = ClockRouteStatus::ROUTED;
    // Channel indices assigned to this net, ascending.
    std::vector<int> channels;
    // Sink wires left unserved. Non-empty only when status != ROUTED.
    std::vector<WireId> unreached_sinks;
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

// Route the given candidates onto the network, binding successful ones at
// STRENGTH_LOCKED. Nets that do not fit are left untouched for the general
// router and reported with a reason; nothing is dropped silently.
ClockRouteReport route_clock_nets(Context *ctx, const ClockNetwork &network,
                                  const std::vector<ClockCandidate> &candidates);

NEXTPNR_NAMESPACE_END

#endif
