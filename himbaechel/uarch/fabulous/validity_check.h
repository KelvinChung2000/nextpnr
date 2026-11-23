/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#ifndef VALIDITY_CHECKING_H
#define VALIDITY_CHECKING_H

#include "FABulous_utils.h"
#include "fab_cfg.h"
#include "fab_defs.h"
#include "sso_array.h"

#include <set>

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// Forward declarations
class CellTagger;

// Single function to register all checkers - implemented in checker.cc
extern void register_default_checkers(CellTagger *tagger);

// The validity checking engine for the fabulous any Bels

// Generic resource usage for maximum extensibility
struct ResourceUsage
{
    IdString resource_type;              // "shared_wire", "capacity_slot", "timing_domain", etc.
    IdString resource_id;                // Specific resource identifier
    IdString net_or_value;               // What's using it (net name, timing value, etc.)
    dict<IdString, IdString> properties; // Extensible properties (invert, etc.)

    ResourceUsage() = default;
    ResourceUsage(IdString type, IdString id, IdString value)
            : resource_type(type), resource_id(id), net_or_value(value)
    {
    }
};

struct CellTags
{
    std::vector<ResourceUsage> resource_usage;
    dict<IdString, IdString> cell_properties; // Future cell-specific data
    std::vector<int> compatible_tile_type;    // Direct tile indices for fast lookup
};

// Extractor interface for modularity
class ResourceExtractor
{
  public:
    virtual ~ResourceExtractor() = default;
    virtual void extract(const Context *ctx, const CellInfo *ci, CellTags &tags) = 0;
};

// Simple base class for all validity checkers
class ValidityChecker
{
  public:
    virtual ~ValidityChecker() = default;
    std::string violation_reason;

    // Default implementation - can be overridden in derived classes
    virtual bool check(Context *ctx, const CellTagger &cell_tagger, CellInfo *source_cell, CellInfo *target_cell)
    {
        return true; // Default: allow packing
    }

    std::string get_violation_reason() const { return violation_reason; }
};

// Modular cell tagger with extensible extractors and packing validation
class CellTagger
{
    Context *ctx;
    std::vector<CellTags> data;
    std::vector<std::unique_ptr<ResourceExtractor>> extractors;
    std::vector<std::unique_ptr<ValidityChecker>> packing_checkers;

  public:
    explicit CellTagger(Context *ctx);
    ~CellTagger() = default;

    const CellTags &get(const CellInfo *ci) const
    {
        if (!ci) {
            static const CellTags empty_tags; // Return empty tags for null cells
            return empty_tags;
        }
        if (ci->flat_index >= (int)data.size()) {
            // Auto-expand if needed during const access
            const_cast<CellTagger *>(this)->ensure_size(ci->flat_index);
        }
        return data.at(ci->flat_index);
    }
    void assign_for(const Context *ctx, const CellInfo *ci);
    void register_extractor(std::unique_ptr<ResourceExtractor> extractor)
    {
        extractors.push_back(std::move(extractor));
    };

    // Packing validation
    bool check_packing_validity(Context *ctx, CellInfo *source_cell, CellInfo *target_cell);
    void add_packing_checker(std::unique_ptr<ValidityChecker> checker)
    {
        packing_checkers.push_back(std::move(checker));
    };

  private:
    void ensure_size(int flat_index);
};

// we need to add some extra data to CLB bels to track what they do, so we can update CLBState accordingly
struct BelFlags
{
    enum BlockType : uint8_t
    {
        BLOCK_OTHER,
        BLOCK_CLB,
        // ...
    } block = BlockType::BLOCK_OTHER;
    enum FuncType : uint8_t
    {
        FUNC_LC_COMB,
        FUNC_FF,
        FUNC_MUX,
        FUNC_OTHER,
    } func;
    uint8_t index;
    // ...
};

// Modular tile state with pluggable validity checkers
struct TileState
{
    Loc tile_loc;
    FABulousUtils fu;
    std::vector<std::unique_ptr<ValidityChecker>> checkers;

    // Track current resource usage in this tile
    dict<IdString, std::set<IdString>> shared_wire_usage; // wire_id -> set of nets using it
    dict<IdString, int> resource_capacity_usage;          // resource_id -> current usage count
    dict<IdString, IdString> timing_domain_usage;         // resource_id -> timing domain

    explicit TileState(Loc loc, const FABulousUtils &utils);
    ~TileState() = default;

    void add_checker(std::unique_ptr<ValidityChecker> checker) { checkers.push_back(std::move(checker)); };

    // Update tile state when cells are placed/removed
    void update_placement(Context *ctx, BelId bel, CellInfo *old_cell, CellInfo *new_cell);

    void load_database_defined_checkers(const TileExtraData &tile_extra);
};

// Core validity checker implementations are defined privately in checker.cc

// Modular fabric-wide tracker
struct FabricTracker
{
    Context *ctx;
    const FabricConfig &cfg;
    const FABulousUtils &utils;

    FabricTracker(Context *ctx, const FabricConfig &cfg, const FABulousUtils &utils) : ctx(ctx), cfg(cfg), utils(utils)
    {
    }

    void update_bel(BelId bel, CellInfo *old_cell, CellInfo *new_cell);

    struct TileData
    {
        std::unique_ptr<TileState> state;
    };
    std::vector<std::vector<TileData>> tiles;

  private:
    TileState *ensure_tile_state(Loc loc);
};

struct PseudoPipTags
{
    BelId bel;
    enum PPType : uint16_t
    {
        NONE,
        LUT_CONST,
        LUT_PERM,
    } type;
    uint16_t data;
    PseudoPipTags(PPType type = NONE, BelId bel = BelId(), uint16_t data = 0x0) : bel(bel), type(type), data(data) {}
};

NEXTPNR_NAMESPACE_END

#endif
