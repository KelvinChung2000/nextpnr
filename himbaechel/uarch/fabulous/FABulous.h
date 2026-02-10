#ifndef FABULOUS_H
#define FABULOUS_H

#include "nextpnr.h"
#include "himbaechel_helpers.h"
#include "FABulous_utils.h"


NEXTPNR_NAMESPACE_BEGIN

NPNR_PACKED_STRUCT(struct Cell_port_POD {
    int32_t bel_type;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct Packing_rule_POD {
    Cell_port_POD driver;
    Cell_port_POD user;
    int32_t width;
    int32_t base_z;
    int32_t rel_x;
    int32_t rel_y;
    int32_t rel_z;
    int32_t flag;
    static constexpr uint32_t BASE_RULE = 0x01; // base rule, no relative z
    static constexpr uint32_t ABS_RULE = 0x02;  // absolute rule, no relative x/y/z
});

NPNR_PACKED_STRUCT(struct Chip_extra_data_POD {
    int32_t context;
    int32_t realBelCount;
    RelSlice<Packing_rule_POD> packingRules;
});

// Database extension points for architecture-specific validity rules
NPNR_PACKED_STRUCT(struct ValidityRule_POD {
    int32_t rule_type;         // SHARED_WIRE, CAPACITY, CUSTOM, etc.
    int32_t scope;             // TILE, BEL, CHIP_GLOBAL
    int32_t priority;          // Rule execution priority
    RelPtr<uint8_t> rule_data; // Flexible rule-specific data
    
    // Rule type constants
    static constexpr int32_t RULE_SHARED_WIRE = 0x01;
    static constexpr int32_t RULE_CAPACITY = 0x02;
    static constexpr int32_t RULE_TIMING_DOMAIN = 0x03;
    static constexpr int32_t RULE_POWER_DOMAIN = 0x04;
    static constexpr int32_t RULE_CUSTOM = 0x1000; // Base for custom rules
    
    // Scope constants
    static constexpr int32_t SCOPE_TILE = 0x01;
    static constexpr int32_t SCOPE_BEL = 0x02;
    static constexpr int32_t SCOPE_CHIP_GLOBAL = 0x03;
});

// Resource capacity definitions
NPNR_PACKED_STRUCT(struct ResourceCapacity_POD {
    int32_t resource_type;    // Resource type ID (shared_wire, timing_domain, etc.)
    int32_t resource_id;      // Specific resource identifier
    int32_t max_capacity;     // Maximum simultaneous users
    int32_t flags;            // Additional flags for resource behavior
});

NPNR_PACKED_STRUCT(struct Tile_extra_data_POD {
    int32_t uniqueBelCount;
    int32_t northPortCount;
    int32_t eastPortCount;
    int32_t southPortCount;
    int32_t westPortCount;
    
    // New: Extensible validity and resource data
    RelSlice<ValidityRule_POD> validity_rules;        // Architecture-defined rules
    RelSlice<ResourceCapacity_POD> resource_capacities; // Resource capacity overrides
    RelPtr<uint8_t> arch_specific_data;              // Future extensions
});

NPNR_PACKED_STRUCT(struct Bel_pin_flag_POD {
    int32_t pin;
    int32_t flags;
    static constexpr uint32_t FLAG_INTERNAL = 0x01;
    static constexpr uint32_t FLAG_SHARED = 0x02;
});

NPNR_PACKED_STRUCT(struct Bel_extra_data_POD {
    int32_t context;
    RelSlice<Bel_pin_flag_POD> pinFlags;
});

struct FABulousImpl : HimbaechelAPI
{

    ~FABulousImpl() {};

    void init_database(Arch *arch) override;
    void init(Context *ctx) override;

    void pack() override;
    void prePlace() override;
    void postPlace() override;
    // void preRoute() override;
    void postRoute() override;

    bool place() override;

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    // BelBucketId getBelBucketForBel(BelId bel) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    // drawing
    void drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc) override;
    void drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type,
                  int32_t tilewire, IdString tile_type) override;
    void drawPip(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, WireId src, IdString src_type,
                 int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id) override;

  private:
    HimbaechelHelpers h;
    FABulousUtils fu;

    void assign_cell_info();
    void assign_net_info();
    void assign_resource_shared();
    bool have_path(WireId srcWire, WireId destWire) const;

    void write_fasm(const std::string &filename);

    bool fdc_apply(Context *ctx, const std::string &filename);

    int context_count = 1;
    int minII = 0;
    int placeTrial = 1;
    float xUnitSpacing = (0.4 / float(context_count));
    dict<IdString, std::vector<IdString>> tile_unique_bel_type;
    dict<BelId, std::vector<BelId>> sharedResource;
    mutable dict<std::pair<WireId, WireId>, bool> pathCache;
    pool<IdString> ioBufTypes;
};

NEXTPNR_NAMESPACE_END

#endif