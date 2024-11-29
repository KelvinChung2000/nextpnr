#include "log.h"
#include "nextpnr.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/FABulous/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"
#include "FABulous.h"
#include "FABulous_utils.h"

NEXTPNR_NAMESPACE_BEGIN

int FABulousUtils::get_context_count()
{
    const Chip_extra_data_POD *extra = 
            reinterpret_cast<const Chip_extra_data_POD *>(ctx->chip_info->extra_data.get());

    return extra->context;
}

int FABulousUtils::get_real_bel_count()
{
    const Chip_extra_data_POD *extra = 
            reinterpret_cast<const Chip_extra_data_POD *>(ctx->chip_info->extra_data.get());

    return extra->realBelCount;
}

int FABulousUtils::get_bel_context(BelId bel) const
{
    const Bel_extra_data_POD *extra = 
            reinterpret_cast<const Bel_extra_data_POD *>(chip_bel_info(ctx->chip_info, bel).extra_data.get());

    return extra->context;
}

dict<IdString, std::vector<IdString>> FABulousUtils::get_tile_unique_bel_type() const
{   
    dict<IdString, std::vector<IdString>> result;
    for (int x = 0; x < ctx->chip_info->width; x++)
    {
        for (int y = 0; y < ctx->chip_info->height; y++)
        {
            std::vector<IdString> uniqueBelTypes;
            int tile = tile_by_xy(ctx->chip_info, x, y);
            const TileTypePOD &tile_data = chip_tile_info(ctx->chip_info, tile);
            IdString tileType = IdString(tile_data.type_name);

            for (BelId bel : ctx->getBelsByTile(x, y))
            {
                IdString belType = ctx->getBelType(bel);
                if (std::find(uniqueBelTypes.begin(), uniqueBelTypes.end(), belType) == uniqueBelTypes.end())
                    uniqueBelTypes.push_back(belType);
            }
            if(result.find(tileType) == result.end()){
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

NEXTPNR_NAMESPACE_END