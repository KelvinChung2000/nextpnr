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
    int32_t northPortCount;
    int32_t eastPortCount;
    int32_t southPortCount;
    int32_t westPortCount;
});



} // namespace

NEXTPNR_NAMESPACE_END

#endif