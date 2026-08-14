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

#ifndef CLOCK_ASSIGN_H
#define CLOCK_ASSIGN_H

#include "clock_network.h"
#include "clock_router.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Assignment of nets to clock channels, solved as an integer program over all
// candidate nets at once.
//
// Solving jointly is the point: assigning one net at a time in priority order
// can strand a later net on a channel an earlier one took for no gain, and no
// amount of tuning a per-net rule fixes that.

struct ClockAssignment
{
    // Per candidate, in the order the candidates were given: the channels it
    // may use, ascending.
    std::vector<std::vector<int>> channels;
    // Per candidate: sinks no channel set could serve.
    std::vector<pool<WireId>> dropped;
};

// `sinks` is parallel to `candidates`. Priority is strict: a higher-priority
// net never loses a sink so that lower-priority nets can keep theirs.
ClockAssignment assign_clock_channels(const Context *ctx, const ClockNetwork &network,
                                      const std::vector<ClockCandidate> &candidates,
                                      const std::vector<std::vector<ClockSink>> &sinks);

NEXTPNR_NAMESPACE_END

#endif
