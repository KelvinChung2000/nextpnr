#ifndef FABULOUS_UTILS_H
#define FABULOUS_UTILS_H

#include "design_utils.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct FABulousUtils
{
    Context *ctx;

    FABulousUtils() {}

    void init(Context *ctx) { this->ctx = ctx; }

    int get_context_count();
    int get_real_bel_count();
    int get_bel_context(BelId bel) const;
    int get_tile_north_port_count(int x, int y) const;
    int get_tile_east_port_count(int x, int y) const;
    int get_tile_south_port_count(int x, int y) const;
    int get_tile_west_port_count(int x, int y) const;
    dict<IdString, std::vector<IdString>> get_tile_unique_bel_type() const;
    IdString get_tile_type(int x, int y) const;
    int get_bel_pin_flags(BelId bel, IdString port) const;
    bool is_tile_internal_bel_pin(BelId bel, IdString port) const;
};

NEXTPNR_NAMESPACE_END

#endif