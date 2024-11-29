#ifndef FABULOUS_H
#define FABULOUS_H

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace
{
NPNR_PACKED_STRUCT(struct Chip_extra_data_POD {
    int32_t context;
    int32_t realBelCount;
});

NPNR_PACKED_STRUCT(struct Bel_extra_data_POD {
    int32_t context;
});

NPNR_PACKED_STRUCT(struct Tile_extra_data_POD {
    int32_t uniqueBelCount;
});



} // namespace

NEXTPNR_NAMESPACE_END

#endif