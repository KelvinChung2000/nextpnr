
#ifndef FABULOUS_UTILS_H
#define FABULOUS_UTILS_H

#include "FABulous.h"
#include "design_utils.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

static constexpr int PACKING_RULE_FLAG_BASE = 0x01; // base rule, no relative z
static constexpr int PACKING_RULE_FLAG_ABS = 0x02; // absolute rule, no relative x/y/z
struct PackingRule
{
    CellTypePort driver;
    CellTypePort user;
    int base_z = 0; // absolute z for driver, relative to user
    int rel_x;
    int rel_y;
    int rel_z;
    int flag;

    std::string toString(Context *ctx) const
    {
        std::string str = "PackingRule { ";
        str += "driver: (" + driver.cell_type.str(ctx) + ", " + driver.port.str(ctx) + "), ";
        str += "user: (" + user.cell_type.str(ctx) + ", " + user.port.str(ctx) + "), ";
        str += "base_z: " + std::to_string(base_z) + ", ";
        str += "rel_x: " + std::to_string(rel_x) + ", ";
        str += "rel_y: " + std::to_string(rel_y) + ", ";
        str += "rel_z: " + std::to_string(rel_z) + ", ";
        str += "flag: " + std::to_string(flag);
        str += " }";
        return str;
    }

    bool isBaseRule() const { return (flag & PACKING_RULE_FLAG_BASE) != 0; }
    bool isAbsoluteRule() const { return (flag & PACKING_RULE_FLAG_ABS) != 0; }
};

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
    std::vector<PackingRule> get_packing_rules() const;
};

NEXTPNR_NAMESPACE_END

#endif