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
            const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
            IdString tileType = IdString(tile_data.type_name);

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
    const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
    return IdString(tile_data.type_name);
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

NEXTPNR_NAMESPACE_END