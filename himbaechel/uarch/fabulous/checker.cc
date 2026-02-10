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

#include <set>
#include "FABulous.h"
#include "log.h"
#include "util.h"
#include "validity_check.h"

#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellTileTypeExtractor : ResourceExtractor
{
    void extract(const Context *ctx, const CellInfo *ci, CellTags &tags) override
    {
        FABulousUtils fu;
        fu.init(const_cast<Context *>(ctx));

        // Get compatible tile indices for this cell type
        tags.compatible_tile_type = fu.get_compatible_tile_type_for_cell_type(ci->type);

        if (ctx->debug) {
            // Join compatible_tile_type into a comma-separated string for logging
            std::string joinedTypes;
            for (size_t i = 0; i < tags.compatible_tile_type.size(); ++i) {
                joinedTypes += std::to_string(tags.compatible_tile_type[i]);
                joinedTypes += " (" +
                               IdString(ctx->chip_info->tile_types[tags.compatible_tile_type[i]].type_name).str(ctx) +
                               ")";
                if (i + 1 < tags.compatible_tile_type.size())
                    joinedTypes += ", ";
            }
            log_info("Cell %s has compatible tile types: %s\n", ci->name.c_str(ctx), joinedTypes.c_str());
        }
    }
};

struct SharedWireExtractor : ResourceExtractor
{
    void extract(const Context *ctx, const CellInfo *ci, CellTags &tags) override
    {
        FABulousUtils fu;
        fu.init(const_cast<Context *>(ctx));

        // Get unique tile types to avoid processing duplicates

        // Extract shared wire information using modular database access
        std::set<IdString> processed_ports;

        for (int tile_type_index : tags.compatible_tile_type) {
            // Get all shared wires in this tile type

            auto shared_pins = fu.get_pin_on_same_wire_in_tile_type(tile_type_index);

            for (auto &[wire, pins] : shared_pins) {
                for (IdString pin : pins) {
                    if (ci->ports.count(pin) && processed_ports.find(pin) == processed_ports.end()) {
                        const NetInfo *net = ci->getPort(pin);
                        if (net) {
                            ResourceUsage usage(ctx->id("shared_wire"), wire, net->name);
                            IdString neg_param = ctx->idf("NEG_%s", pin.c_str(ctx));
                            if (ci->params.count(neg_param) && ci->params.at(neg_param).as_bool())
                                usage.properties[ctx->id("invert")] = ctx->id("true");
                            tags.resource_usage.push_back(usage);
                            processed_ports.insert(pin);
                        }
                    }
                }
            }
        }
    };
};
struct SharedWireChecker : ValidityChecker
{
    bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *base_cell, CellInfo *new_cell) override
    {
        if (!base_cell || !new_cell)
            return false;
        IdString shared_wire_id = ctx->id("shared_wire");
        dict<IdString, IdString> new_shared_wires, base_shared_wires;

        const auto &source_tags = cell_tagger.get(new_cell);
        for (const auto &usage : source_tags.resource_usage) {
            if (usage.resource_type == shared_wire_id)
                new_shared_wires[usage.resource_id] = usage.net_or_value;
        }

        if (base_cell->cluster == ClusterId()) {
            const auto &target_tags = cell_tagger.get(base_cell);
            for (const auto &usage : target_tags.resource_usage) {
                if (usage.resource_type == shared_wire_id)
                    base_shared_wires[usage.resource_id] = usage.net_or_value;
            }
        } else {
            auto root = ctx->getClusterRootCell(base_cell->cluster);
            for (const auto &usage : cell_tagger.get(root).resource_usage) {
                if (usage.resource_type == shared_wire_id)
                    base_shared_wires[usage.resource_id] = usage.net_or_value;
            }

            for (auto child : root->constr_children) {
                const auto &child_tags = cell_tagger.get(child);
                for (const auto &usage : child_tags.resource_usage) {
                    if (usage.resource_type == shared_wire_id)
                        base_shared_wires[usage.resource_id] = usage.net_or_value;
                }
            }
        }

        for (const auto &[sw, net] : new_shared_wires) {
            auto it = base_shared_wires.find(sw);
            if (it != base_shared_wires.end() && net != it->second) {
                if (ctx->debug){
                    log_info("Shared wire %s conflict between cells %s(%s) and %s(%s)\n", sw.c_str(ctx),
                             base_cell->name.c_str(ctx), ctx->nameOf(net), new_cell->name.c_str(ctx),
                             ctx->nameOf(it->second));
                }

                violation_reason = "Shared wire " + sw.str(ctx) + " conflict between cells";
                return false;
            }
        }
        return true;
    };
};
struct ResourceCapacityChecker : ValidityChecker
{
    bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *source_cell, CellInfo *target_cell) override
    {
        if (!source_cell || !target_cell)
            return false;
        const auto &source_tags = cell_tagger.get(source_cell);
        const auto &target_tags = cell_tagger.get(target_cell);
        IdString capacity_slot_id = ctx->id("capacity_slot");
        dict<IdString, int> combined;
        for (const auto &u : source_tags.resource_usage)
            if (u.resource_type == capacity_slot_id)
                combined[u.resource_id]++;
        for (const auto &u : target_tags.resource_usage)
            if (u.resource_type == capacity_slot_id)
                combined[u.resource_id]++;
        for (auto &kv : combined) {
            const int BASIC_PACKING_LIMIT = 4;
            if (kv.second > BASIC_PACKING_LIMIT) {
                violation_reason = "Combined resource usage for " + kv.first.str(ctx) + " exceeds packing limit";
                return false;
            }
        }
        return true;
    };
};
struct WireConflictChecker : ValidityChecker
{
    bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *source_cell, CellInfo *target_cell) override
    {
        if (!source_cell || !target_cell)
            return false;
        return true;
    };
};
struct TimingDomainChecker : ValidityChecker
{
    bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *source_cell, CellInfo *target_cell) override
    {
        if (!source_cell || !target_cell)
            return false;
        const auto &source_tags = cell_tagger.get(source_cell);
        const auto &target_tags = cell_tagger.get(target_cell);
        IdString timing_domain_id = ctx->id("timing_domain");
        dict<IdString, IdString> src, dst;
        for (const auto &u : source_tags.resource_usage)
            if (u.resource_type == timing_domain_id)
                src[u.resource_id] = u.net_or_value;
        for (const auto &u : target_tags.resource_usage)
            if (u.resource_type == timing_domain_id)
                dst[u.resource_id] = u.net_or_value;
        for (const auto &kv : src) {
            auto it = dst.find(kv.first);
            if (it != dst.end() && kv.second != it->second) {
                violation_reason = "Timing domain conflict on resource " + kv.first.str(ctx);
                return false;
            }
        }
        return true;
    };
};

// Example of adding a new checker with minimal boilerplate
struct ExampleCustomChecker : ValidityChecker
{
    bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *source_cell, CellInfo *target_cell) override
    {
        if (!source_cell || !target_cell)
            return false;

        // Example: Don't allow packing cells with names starting with "SPECIAL_"
        if (source_cell->name.str(ctx).find("SPECIAL_") == 0 || target_cell->name.str(ctx).find("SPECIAL_") == 0) {
            violation_reason = "Cannot pack SPECIAL_ cells together";
            return false;
        }
        return true;
    }
};

CellTagger::CellTagger(Context *ctx) : ctx(ctx)
{
    // Register extractors in order - CellTileTypeExtractor must run first
    this->register_extractor(std::make_unique<CellTileTypeExtractor>());
    this->register_extractor(std::make_unique<SharedWireExtractor>());

    // Register default checkers
    this->add_packing_checker(std::make_unique<SharedWireChecker>());
    this->add_packing_checker(std::make_unique<ResourceCapacityChecker>());
    this->add_packing_checker(std::make_unique<TimingDomainChecker>());
}

NEXTPNR_NAMESPACE_END