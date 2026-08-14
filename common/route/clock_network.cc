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

#include "clock_network.h"

#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

void ClockNetwork::index(const Context *ctx)
{
    reach.clear();
    reach.resize(channels.size());
    serving_channels.clear();

    for (size_t i = 0; i < channels.size(); i++) {
        const ClockChannel &ch = channels.at(i);
        if (ch.entries.empty())
            log_error("clock channel '%s' has no entries\n", ch.name.c_str(ctx));

        dict<WireId, std::vector<PipId>> downhill;
        for (PipId pip : ch.resources)
            downhill[ctx->getPipSrcWire(pip)].push_back(pip);

        pool<WireId> &r = reach.at(i);
        std::vector<WireId> queue;
        for (WireId entry : ch.entries) {
            if (r.count(entry))
                continue;
            r.insert(entry);
            queue.push_back(entry);
        }

        while (!queue.empty()) {
            WireId wire = queue.back();
            queue.pop_back();
            auto found = downhill.find(wire);
            if (found == downhill.end())
                continue;
            for (PipId pip : found->second) {
                WireId dst = ctx->getPipDstWire(pip);
                if (r.count(dst))
                    continue;
                r.insert(dst);
                queue.push_back(dst);
            }
        }

        // Outer loop ascends, so each wire's channel list stays sorted by
        // channel index however the reach set happens to iterate.
        for (WireId wire : r)
            serving_channels[wire].push_back(int(i));
    }
}

NEXTPNR_NAMESPACE_END
