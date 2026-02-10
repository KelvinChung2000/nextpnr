#include "log.h"
#include "nextpnr.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#include "FABulous.h"
#include "FABulous_utils.h"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

int FABulousUtils::get_context_count()
{
    const Chip_extra_data_POD *extra = reinterpret_cast<const Chip_extra_data_POD *>(ctx->chip_info->extra_data.get());

    return extra->context;
}

int FABulousUtils::get_real_bel_count()
{
    const Chip_extra_data_POD *extra = reinterpret_cast<const Chip_extra_data_POD *>(ctx->chip_info->extra_data.get());

    return extra->realBelCount;
}

int FABulousUtils::get_bel_context(BelId bel) const
{
    const Bel_extra_data_POD *extra =
            reinterpret_cast<const Bel_extra_data_POD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());

    return extra->context;
}

int FABulousUtils::get_tile_north_port_count(int x, int y) const
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
    const Tile_extra_data_POD *extra = reinterpret_cast<const Tile_extra_data_POD *>(tile_data.extra_data.get());
    return extra->northPortCount;
}

int FABulousUtils::get_tile_east_port_count(int x, int y) const
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
    const Tile_extra_data_POD *extra = reinterpret_cast<const Tile_extra_data_POD *>(tile_data.extra_data.get());
    return extra->eastPortCount;
}

int FABulousUtils::get_tile_south_port_count(int x, int y) const
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
    const Tile_extra_data_POD *extra = reinterpret_cast<const Tile_extra_data_POD *>(tile_data.extra_data.get());
    return extra->southPortCount;
}

int FABulousUtils::get_tile_west_port_count(int x, int y) const
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
    const Tile_extra_data_POD *extra = reinterpret_cast<const Tile_extra_data_POD *>(tile_data.extra_data.get());
    return extra->westPortCount;
}

dict<IdString, std::vector<IdString>> FABulousUtils::get_tile_unique_bel_type() const
{
    dict<IdString, std::vector<IdString>> result;
    for (int x = 0; x < ctx->chip_info->width; x++) {
        for (int y = 0; y < ctx->chip_info->height; y++) {
            std::vector<IdString> uniqueBelTypes;
            int tile = tile_by_xy(ctx->chip_info, x, y);
            IdString tileType = ctx->get_tile_type(tile);

            for (BelId bel : ctx->getBelsByTile(x, y)) {
                IdString belType = ctx->getBelType(bel);
                if (std::find(uniqueBelTypes.begin(), uniqueBelTypes.end(), belType) == uniqueBelTypes.end())
                    uniqueBelTypes.push_back(belType);
            }
            if (result.find(tileType) == result.end()) {
                result[tileType] = uniqueBelTypes;
            }
        }
    }
    return result;
}

IdString FABulousUtils::get_tile_type(int x, int y) const
{
    int tile = tile_by_xy(ctx->chip_info, x, y);
    return ctx->get_tile_type(tile);
}

int FABulousUtils::get_bel_pin_flags(BelId bel, IdString port) const
{
    const Bel_extra_data_POD *extra =
            reinterpret_cast<const Bel_extra_data_POD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());

    for (auto &bel_pin_flag : extra->pinFlags) {
        if (IdString(bel_pin_flag.pin) == port) {
            return bel_pin_flag.flags;
        }
    }
    log_error("Given pin %s does not exists in bel %s\n", ctx->nameOf(port), ctx->nameOfBel(bel));
}

bool FABulousUtils::is_tile_internal_bel_pin(BelId bel, IdString port) const
{
    int flag = get_bel_pin_flags(bel, port);
    return flag == Bel_pin_flag_POD::FLAG_INTERNAL;
}

bool FABulousUtils::is_shared_bel_pin(BelId bel, IdString port) const
{
    int flag = get_bel_pin_flags(bel, port);
    return flag == Bel_pin_flag_POD::FLAG_SHARED;
}

std::vector<IdString> FABulousUtils::get_shared_bel_pins(BelId bel) const
{
    std::vector<IdString> shared_pins;
    for (const auto &port : ctx->getBelPins(bel)) {
        if (is_shared_bel_pin(bel, port)) {
            shared_pins.push_back(port);
        }
    }
    return shared_pins;
}

std::vector<PackingRule> FABulousUtils::get_packing_rules() const
{
    std::vector<PackingRule> rules;
    const Chip_extra_data_POD *extra = reinterpret_cast<const Chip_extra_data_POD *>(ctx->chip_info->extra_data.get());

    log_info("Packing rules found: %ld\n", extra->packingRules.size());
    for (const Packing_rule_POD &rule : extra->packingRules) {

        PackingRule packingRule;
        packingRule.base_z = rule.base_z;
        packingRule.rel_x = rule.rel_x;
        packingRule.rel_y = rule.rel_y;
        packingRule.rel_z = rule.rel_z;
        packingRule.flag = rule.flag;
        if (rule.width > 1) {
            for (int i = 0; i < rule.width; i++) {
                auto driver_port = IdString(rule.driver.port).c_str(ctx);
                auto user_port = IdString(rule.user.port).c_str(ctx);
                packingRule.driver = CellTypePort(IdString(rule.driver.bel_type), ctx->idf("%s[%d]", driver_port, i));
                packingRule.user = CellTypePort(IdString(rule.user.bel_type), ctx->idf("%s[%d]", user_port, i));
                rules.push_back(packingRule);
            }
        } else {
            packingRule.driver = CellTypePort(IdString(rule.driver.bel_type), IdString(rule.driver.port));
            packingRule.user = CellTypePort(IdString(rule.user.bel_type), IdString(rule.user.port));
            rules.push_back(packingRule);
        }
    }
    return rules;
}

std::optional<TileExtraData> FABulousUtils::get_tile_extra_data(int x, int y) const
{
    try {
        int tile = tile_by_xy(ctx->chip_info, x, y);
        const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);

        if (tile_data.extra_data.is_null()) {
            return std::nullopt; // No extra data available
        }

        const Tile_extra_data_POD *extra = reinterpret_cast<const Tile_extra_data_POD *>(tile_data.extra_data.get());

        TileExtraData result;
        result.uniqueBelCount = extra->uniqueBelCount;
        result.northPortCount = extra->northPortCount;
        result.eastPortCount = extra->eastPortCount;
        result.southPortCount = extra->southPortCount;
        result.westPortCount = extra->westPortCount;

        // Convert validity rules from POD to typed structs
        for (const ValidityRule_POD &rule_pod : extra->validity_rules) {
            ValidityRule rule;
            rule.rule_type = rule_pod.rule_type;
            rule.scope = rule_pod.scope;
            rule.priority = rule_pod.priority;

            // Copy rule-specific data if available
            if (!rule_pod.rule_data.is_null()) {
                // Note: The size of rule_data would need to be known from context
                // For now, we'll leave it empty unless the POD format includes size
                // This can be enhanced when the actual rule data format is defined
            }

            result.validity_rules.push_back(rule);
        }

        // Convert resource capacities from POD to typed structs
        for (const ResourceCapacity_POD &capacity_pod : extra->resource_capacities) {
            ResourceCapacity capacity;
            capacity.resource_type = IdString(capacity_pod.resource_type);
            capacity.resource_id = IdString(capacity_pod.resource_id);
            capacity.max_capacity = capacity_pod.max_capacity;
            capacity.flags = capacity_pod.flags;

            result.resource_capacities.push_back(capacity);
        }

        // Copy architecture-specific data if available
        if (!extra->arch_specific_data.is_null()) {
            // Note: Similar to rule_data, the size would need to be known
            // This can be enhanced when the actual format is defined
        }

        return result;
    } catch (...) {
        // Return nullopt if any error occurs (e.g., invalid tile coordinates)
        return std::nullopt;
    }
}

std::optional<TileExtraData> FABulousUtils::get_tile_extra_data(Loc loc) const
{
    return get_tile_extra_data(loc.x, loc.y);
}

bool FABulousUtils::is_shared_wire_type(WireId wire) const
{
    IdString wire_type = ctx->getWireType(wire);
    std::string wire_type_str = wire_type.str(ctx);
    return wire_type_str.find("SHARED_") == 0;
}

std::vector<IdString> FABulousUtils::get_tile_types_for_cell_type(IdString cell_type) const
{
    std::vector<IdString> compatible_tile_types;
    dict<IdString, std::vector<IdString>> tile_bel_types = get_tile_unique_bel_type();

    for (const auto &entry : tile_bel_types) {
        const IdString &tile_type = entry.first;
        const std::vector<IdString> &bel_types = entry.second;

        // Check if this tile type supports the given cell type
        if (std::find(bel_types.begin(), bel_types.end(), cell_type) != bel_types.end()) {
            compatible_tile_types.push_back(tile_type);
        }
    }

    return compatible_tile_types;
}

std::vector<int> FABulousUtils::get_compatible_tile_type_for_cell_type(IdString cell_type) const
{
    std::vector<int> compatible_tile_indices;
    compatible_tile_indices.reserve(ctx->chip_info->tile_insts.ssize() / 4); // Reasonable initial capacity

    // First pass: identify which tile types support this cell type
    for (int tile_type_idx = 0; tile_type_idx < ctx->chip_info->tile_types.ssize(); ++tile_type_idx) {

        const TileTypePOD &tile_type = ctx->chip_info->tile_types[tile_type_idx];
        // Check if this tile type has BELs that match the cell type
        for (int bel_idx = 0; bel_idx < tile_type.bels.ssize(); bel_idx++) {
            const BelDataPOD &bel_data = tile_type.bels[bel_idx];
            if (IdString(bel_data.bel_type) == cell_type) {
                compatible_tile_indices.push_back(tile_type_idx);
                break; // Found match in this tile type, move to next
            }
        }
    }

    return compatible_tile_indices;
}

int FABulousUtils::get_tile_type_index(int tile_index) const { return ctx->chip_info->tile_insts[tile_index].type; }

dict<IdString, std::vector<IdString>> FABulousUtils::get_pin_on_same_wire_in_tile_type(int tile_type_index) const
{
    dict<IdString, std::vector<IdString>> shared;
    const TileTypePOD &tile_data = ctx->chip_info->tile_types[tile_type_index];

    for (int wire_idx = 0; wire_idx < tile_data.wires.ssize(); wire_idx++) {
        const TileWireDataPOD &wire_data = tile_data.wires[wire_idx];
        std::vector<IdString> bel_pins;
        if (IdString(wire_data.wire_type).str(ctx).find("SHARED_") == std::string::npos)
            continue; // Skip non-shared wires
        for (int i = 0; i < wire_data.bel_pins.ssize(); i++){
            bel_pins.push_back(IdString(wire_data.bel_pins[i].pin));
        }
        shared[IdString(wire_data.name)] = bel_pins;
    }

    return shared;
}


NEXTPNR_NAMESPACE_END