#ifndef FABULOUS_UTILS_H
#define FABULOUS_UTILS_H

#include "himbaechel_helpers.h"
#include "design_utils.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include <optional>
#include <vector>


NEXTPNR_NAMESPACE_BEGIN

static constexpr int PACKING_RULE_FLAG_BASE = 0x01; // base rule, no relative z
static constexpr int PACKING_RULE_FLAG_ABS = 0x02;  // absolute rule, no relative x/y/z

// Validity rule constants
static constexpr int VALIDITY_RULE_SHARED_WIRE = 0x01;
static constexpr int VALIDITY_RULE_CAPACITY = 0x02;
static constexpr int VALIDITY_RULE_TIMING_DOMAIN = 0x03;
static constexpr int VALIDITY_RULE_POWER_DOMAIN = 0x04;
static constexpr int VALIDITY_RULE_CUSTOM = 0x1000; // Base for custom rules

static constexpr int VALIDITY_SCOPE_TILE = 0x01;
static constexpr int VALIDITY_SCOPE_BEL = 0x02;
static constexpr int VALIDITY_SCOPE_CHIP_GLOBAL = 0x03;

struct ValidityRule
{
    int rule_type;         // SHARED_WIRE, CAPACITY, CUSTOM, etc.
    int scope;             // TILE, BEL, CHIP_GLOBAL
    int priority;          // Rule execution priority
    std::vector<uint8_t> rule_data; // Flexible rule-specific data
    
    std::string toString(Context *ctx) const
    {
        std::string str = "ValidityRule { ";
        str += "rule_type: " + std::to_string(rule_type) + ", ";
        str += "scope: " + std::to_string(scope) + ", ";
        str += "priority: " + std::to_string(priority) + ", ";
        str += "data_size: " + std::to_string(rule_data.size());
        str += " }";
        return str;
    }
    
    bool isSharedWireRule() const { return rule_type == VALIDITY_RULE_SHARED_WIRE; }
    bool isCapacityRule() const { return rule_type == VALIDITY_RULE_CAPACITY; }
    bool isTimingDomainRule() const { return rule_type == VALIDITY_RULE_TIMING_DOMAIN; }
    bool isPowerDomainRule() const { return rule_type == VALIDITY_RULE_POWER_DOMAIN; }
    bool isCustomRule() const { return rule_type >= VALIDITY_RULE_CUSTOM; }
    
    bool isTileScope() const { return scope == VALIDITY_SCOPE_TILE; }
    bool isBelScope() const { return scope == VALIDITY_SCOPE_BEL; }
    bool isChipGlobalScope() const { return scope == VALIDITY_SCOPE_CHIP_GLOBAL; }
};

struct ResourceCapacity
{
    IdString resource_type;    // Resource type ID (shared_wire, timing_domain, etc.)
    IdString resource_id;      // Specific resource identifier
    int max_capacity;          // Maximum simultaneous users
    int flags;                 // Additional flags for resource behavior
    
    std::string toString(Context *ctx) const
    {
        std::string str = "ResourceCapacity { ";
        str += "resource_type: " + resource_type.str(ctx) + ", ";
        str += "resource_id: " + resource_id.str(ctx) + ", ";
        str += "max_capacity: " + std::to_string(max_capacity) + ", ";
        str += "flags: " + std::to_string(flags);
        str += " }";
        return str;
    }
};

struct TileExtraData
{
    int uniqueBelCount;
    int northPortCount;
    int eastPortCount;
    int southPortCount;
    int westPortCount;
    std::vector<ValidityRule> validity_rules;
    std::vector<ResourceCapacity> resource_capacities;
    std::vector<uint8_t> arch_specific_data;
    
    std::string toString(Context *ctx) const
    {
        std::string str = "TileExtraData { ";
        str += "uniqueBelCount: " + std::to_string(uniqueBelCount) + ", ";
        str += "northPortCount: " + std::to_string(northPortCount) + ", ";
        str += "eastPortCount: " + std::to_string(eastPortCount) + ", ";
        str += "southPortCount: " + std::to_string(southPortCount) + ", ";
        str += "westPortCount: " + std::to_string(westPortCount) + ", ";
        str += "validity_rules: " + std::to_string(validity_rules.size()) + ", ";
        str += "resource_capacities: " + std::to_string(resource_capacities.size());
        str += " }";
        return str;
    }
};

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
    bool is_shared_bel_pin(BelId bel, IdString port) const;
    std::vector<IdString> get_shared_bel_pins(BelId bel) const;
    std::vector<PackingRule> get_packing_rules() const;
    
    // Wire type checking methods
    bool is_shared_wire_type(WireId wire) const;
    std::vector<IdString> get_tile_types_for_cell_type(IdString cell_type) const;
    
    // Optimized method for CellTileTypeExtractor workflow
    std::vector<int> get_compatible_tile_type_for_cell_type(IdString cell_type) const;
    
    // Database access methods - broken down for reusability
    int get_tile_type_index(int tile_index) const;
    dict<IdString, std::vector<IdString>> get_pin_on_same_wire_in_tile_type(int tile_type_index) const;
    
    // Tile extra data methods with full type conversion
    std::optional<TileExtraData> get_tile_extra_data(int x, int y) const;
    std::optional<TileExtraData> get_tile_extra_data(Loc loc) const;
};

NEXTPNR_NAMESPACE_END

#endif