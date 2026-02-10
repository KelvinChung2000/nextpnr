/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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

#include "validity_check.h"
#include <set>
#include "FABulous.h"
#include "FABulous_utils.h"
#include "log.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

TileState::TileState(Loc loc, const FABulousUtils &utils) : tile_loc(loc), fu(utils)
{
    // TileState is now used only for placement-stage tracking
    // Packing validation is handled by CellTagger

    // Initialize tile state tracking structures
    shared_wire_usage.clear();
    resource_capacity_usage.clear();
    timing_domain_usage.clear();

    // Load additional checkers based on database tile type and rules if needed
    try {
        auto tile_extra_opt = utils.get_tile_extra_data(tile_loc);
        if (tile_extra_opt) {
            const TileExtraData &tile_extra_data = *tile_extra_opt;
            // Process tile-specific rules for future placement validation
            // For now, we'll focus on resource tracking
        }
    } catch (...) {
        // Gracefully handle cases where tile extra data is not available
    }
}

void CellTagger::assign_for(const Context *ctx, const CellInfo *ci)
{
    if (!ci || !ctx)
        return; // Safety check

    ensure_size(ci->flat_index);
    auto &tags = data.at(ci->flat_index);

    // Clear previous data
    tags.resource_usage.clear();
    tags.cell_properties.clear();

    // Use all registered extractors
    for (auto &extractor : extractors) {
        extractor->extract(ctx, ci, tags);
    }
}

void CellTagger::ensure_size(int flat_index)
{
    if (int(data.size()) <= flat_index) {
        data.resize(flat_index + 1);
    }
}

bool CellTagger::check_packing_validity(Context *ctx, CellInfo *source_cell, CellInfo *target_cell)
{
    if (!source_cell || !target_cell) {
        return false;
    }

    // Use all registered packing checkers to validate cell compatibility
    for (auto &checker : packing_checkers) {
        if (!checker->check(ctx, *this, source_cell, target_cell)) {
            if (ctx->debug) {
                log_info("Packing validation failed: %s\n", checker->get_violation_reason().c_str());
            }
            return false;
        }
    }
    return true;
}

void TileState::update_placement(Context *ctx, BelId bel, CellInfo *old_cell, CellInfo *new_cell)
{
    // Remove old cell's resource usage
    if (old_cell) {
        // Remove shared wire usage
        for (const auto &port : ctx->getBelPins(bel)) {
            NetInfo *net = ctx->getBoundWireNet(ctx->getBelPinWire(bel, port));
            if (net && fu.is_shared_bel_pin(bel, port)) {
                auto &wire_nets = shared_wire_usage[port];
                wire_nets.erase(net->name);
                if (wire_nets.empty()) {
                    shared_wire_usage.erase(port);
                }
            }
        }

        // Remove capacity usage
        IdString bel_type = ctx->getBelType(bel);
        auto it = resource_capacity_usage.find(bel_type);
        if (it != resource_capacity_usage.end()) {
            it->second--;
            if (it->second <= 0) {
                resource_capacity_usage.erase(it);
            }
        }
    }

    // Add new cell's resource usage
    if (new_cell) {
        // Add shared wire usage
        for (const auto &port : ctx->getBelPins(bel)) {
            NetInfo *net = ctx->getBoundWireNet(ctx->getBelPinWire(bel, port));
            if (net && fu.is_shared_bel_pin(bel, port)) {
                shared_wire_usage[port].insert(net->name);
            }
        }

        // Add capacity usage
        IdString bel_type = ctx->getBelType(bel);
        resource_capacity_usage[bel_type]++;
    }
}

TileState *FabricTracker::ensure_tile_state(Loc loc)
{
    if (int(tiles.size()) <= loc.y)
        tiles.resize(loc.y + 1);
    auto &row = tiles.at(loc.y);
    if (int(row.size()) <= loc.x)
        row.resize(loc.x + 1);
    auto &tile_data = row.at(loc.x);

    if (!tile_data.state) {
        tile_data.state = std::make_unique<TileState>(loc, utils);
    }

    return tile_data.state.get();
}

void FabricTracker::update_bel(BelId bel, CellInfo *old_cell, CellInfo *new_cell)
{
    Loc loc = ctx->getBelLocation(bel);

    // Ensure tile state exists
    TileState *tile_state = ensure_tile_state(loc);

    // Update tile state with the placement change
    tile_state->update_placement(ctx, bel, old_cell, new_cell);
}

// Placeholder implementation for database-defined checkers
void TileState::load_database_defined_checkers(const TileExtraData &tile_extra)
{
    // Future: Load checkers based on tile database rules
    // For now, TileState focuses on placement-stage resource tracking
}

// Core validity checker implementations are now in checker.cc

NEXTPNR_NAMESPACE_END
