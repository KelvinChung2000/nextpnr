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

NEXTPNR_NAMESPACE_END