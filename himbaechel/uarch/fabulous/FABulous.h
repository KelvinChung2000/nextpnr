#ifndef FABULOUS_H
#define FABULOUS_H

#include "nextpnr.h"
#include "himbaechel_helpers.h"
#include "FABulous_utils.h"


NEXTPNR_NAMESPACE_BEGIN

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
    void drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type, int32_t tilewire, IdString tile_type) override;
    void drawPip(std::vector<GraphicElement> &g,GraphicElement::style_t style, Loc loc,
        WireId src, IdString src_type, int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id) override;
        
        private:
        HimbaechelHelpers h;
        FABulousUtils fu;
        
        void assign_cell_info();
        void assign_net_info();
        void assign_resource_shared();
        bool have_path(WireId srcWire, WireId destWire) const;
        
        void write_fasm(const std::string &filename);
                
        // pack functions 
        void remove_undrive_constant();
        void fold_bit_const();
        void fold_clock_drive();
        void remove_fabulous_iob();
        int get_macro_cell_z(const CellInfo *ci);
        void rel_constr_cells(CellInfo *a, CellInfo *b, int dz);

        int context_count = 1;
        int minII = 0;
        int placeTrial = 1;
        float xUnitSpacing = (0.4 / float(context_count));
        dict<IdString, std::vector<IdString>> tile_unique_bel_type;
        dict<BelId, std::vector<BelId>> sharedResource;
        mutable dict<std::pair<WireId, WireId>, bool> pathCache;



};


NEXTPNR_NAMESPACE_END

#endif