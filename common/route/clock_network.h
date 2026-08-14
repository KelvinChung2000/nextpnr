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

#ifndef CLOCK_NETWORK_H
#define CLOCK_NETWORK_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// A description of an architecture's dedicated clock/global network, expressed
// as a set of independently-assignable channels.
//
// The topology lives entirely in this data. The router asks only "which
// channels reach my sinks, and are they free?", never "am I a ladder or a
// mesh?", so the same code routes both.

// Position of a channel in the distribution hierarchy. This is descriptive
// data used to order candidates during assignment; the router never branches
// on it. A ladder declares a single LEAF channel with fabric-wide reach.
enum class ClockTier : uint8_t
{
    ROUTE = 0,      // source -> root
    DISTRIBUTE = 1, // root -> region
    LEAF = 2,       // region -> taps
};

// One independently-assignable distribution path. Capacity is exactly one net:
// contention on dedicated clock resources is binary, not negotiable.
struct ClockChannel
{
    IdString name;
    ClockTier tier = ClockTier::LEAF;
    // Region this channel serves, or -1 for fabric-wide. Region budgets are
    // applied at the mesh milestone; a ladder leaves this at -1.
    int region = -1;
    // Wires from which this channel may be entered. A net may take this
    // channel once it has reached any one of them.
    std::vector<WireId> entries;
    // The pips this channel owns. Path search is restricted to this set, which
    // is what stops one net from consuming the whole network.
    pool<PipId> resources;
};

struct ClockNetwork
{
    std::vector<ClockChannel> channels;

    // Derived from the channels by index(); do not populate by hand.
    // reach[i] is every wire channel i can deliver to, entries included.
    std::vector<pool<WireId>> reach;
    // Channels that can deliver to a given wire, ascending by channel index.
    dict<WireId, std::vector<int>> serving_channels;

    // Compute the derived reach index. Must be called after the channels are
    // populated and before the network is handed to the router.
    void index(const Context *ctx);

    bool is_indexed() const { return reach.size() == channels.size(); }
};

NEXTPNR_NAMESPACE_END

#endif
