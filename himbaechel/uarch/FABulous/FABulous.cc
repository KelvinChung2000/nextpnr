#include <iterator>

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "himbaechel_helpers.h"
#include <stack>
#include <unordered_map>
#include "placer_heap.h"
#include "placer1.h"
#include <random>

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/FABulous/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"


#include "FABulous.h"
#include "FABulous_utils.h"
#include "router1.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FABulousImpl : HimbaechelAPI
{

    ~FABulousImpl() {};

    void init_database(Arch *arch) override;
    void init(Context *ctx) override;

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
    

    int context_count = 0;
    int minII = 0;
    int placeTrial = 10;
    dict<IdString, std::vector<IdString>> tile_unique_bel_type;
    dict<BelId, std::vector<BelId>> sharedResource;
};



struct FABulousArch : HimbaechelArch
{
    FABulousArch() : HimbaechelArch("FABulous") {};
    bool match_device(const std::string &device) override { return device == "FABulous"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args) override
    {
        return std::make_unique<FABulousImpl>();
    }
} FABulousArch;


void FABulousImpl::init_database(Arch *arch)
{
    init_uarch_constids(arch);

    if (arch->args.chipdb_override.empty()){
        log_error("FABulous uarch requires a chipdb override to be specified.\n");
    }
    arch->load_chipdb("");
    arch->set_speed_grade("DEFAULT");
}

void FABulousImpl::init(Context *ctx)
{
    const ArchArgs &args = ctx->getArchArgs();

    h.init(ctx);
    fu.init(ctx);
    HimbaechelAPI::init(ctx);

    if (args.options.count("cst")){
        ctx->settings[ctx->id("cst.filename")] = args.options.at("cst");
    }
    context_count = fu.get_context_count();
    if (args.options.count("minII")){
        minII = std::stoi(args.options.at("minII"));
    }
    else{
        minII = std::max(int(ctx->cells.size()) / fu.get_real_bel_count(), 1);
    }

    if (args.options.count("placeTrial")){
        placeTrial = std::stoi(args.options.at("placeTrial"));
    }
    
    if (minII > context_count && context_count > 1){
        log_error("FABulous uarch requires at least %d contexts to be specified.\n", context_count);
    }

    log_info("FABulous uarch initialized with %d contexts and minII=%d\n", context_count, minII);
    tile_unique_bel_type = fu.get_tile_unique_bel_type();
    log_info("%ld unique Tile types found.\n", tile_unique_bel_type.size());
    for (auto &tile : tile_unique_bel_type){
        log_info("   Tile %s has %ld unique bel types.\n", tile.first.c_str(ctx), tile.second.size());
    }

}

void FABulousImpl::assign_resource_shared()
{
    for (auto bel: ctx->getBels()){
        std::vector<BelId> sharedBel;
        Loc loc = ctx->getBelLocation(bel);
        
        for (auto tileBel : ctx->getBelsByTile(loc.x, loc.y)){
            int belContext = fu.get_bel_context(bel);
            for (int i = belContext; i < context_count*minII; i+=minII){
                if (fu.get_bel_context(tileBel) == i % context_count && 
                    ctx->getBelType(tileBel) == ctx->getBelType(bel) &&
                    tileBel != bel){
                    if (std::find(sharedBel.begin(), sharedBel.end(), tileBel) == sharedBel.end() )
                        sharedBel.push_back(tileBel);
                }
            }
        }
        sharedResource[bel] = sharedBel;
        // log_info("Bel %s has shared Bels:\n", ctx->nameOfBel(bel));
        // for (auto shared : sharedBel) {
        //     log_info("   %s\n", ctx->nameOfBel(shared));
        // }
        // log_info("\n");
    }
}

void FABulousImpl::assign_cell_info()
{
    // fast_cell_info.resize(ctx->cells.size());
    // for (auto &cell : ctx->cells) {
    //     CellInfo *ci = cell.second.get();
    //     auto &fc = fast_cell_info.at(ci->flat_index);
    //     if (ci->type == id_LUT4) {
    //         fc.lut_f = ci->getPort(id_F);
    //         fc.lut_i3_used = (ci->getPort(ctx->idf("I[%d]", K - 1)) != nullptr);
    //     } else if (ci->type == id_DFF) {
    //         fc.ff_d = ci->getPort(id_D);
    //     }
    // } 
}

void FABulousImpl::assign_net_info()
{

}

IdString FABulousImpl::getBelBucketForCellType(IdString cell_type) const
{
    return cell_type;
}

// BelBucketId FABulousImpl::getBelBucketForBel(BelId bel) const
// {
//     return BelBucketId(ctx, std::to_string(fu.get_bel_context(bel))+"_"+ctx->getBelType(bel).str(ctx));
// }

bool FABulousImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    return (ctx->getBelType(bel) == cell_type);
    // if (fu.get_bel_context(bel) <= minII){
    // }
    // else{
    //     return false;
    // }

}

bool FABulousImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    CellInfo *boundedCell = ctx->getBoundBelCell(bel);
    // log_info("         isBelLocationValid for bel %s\n", ctx->nameOfBel(bel));
    if (boundedCell == nullptr){
        return true;
    }

    std::vector<bool> foundPaths;
    for (auto p : boundedCell->ports){
        if (p.second.type == PORT_IN || p.second.net == nullptr)
            continue;
        
        NetInfo *outnet = p.second.net;
        
        for (auto user: outnet->users){
            WireId srcWire = ctx->getNetinfoSourceWire(outnet);
            for (auto dstWire : ctx->getNetinfoSinkWires(outnet, user)){
                foundPaths.push_back(have_path(srcWire, dstWire));
            }
        }
    }

    auto sharedBel = sharedResource.at(bel);
    for (auto b : sharedBel){
        CellInfo *sharedCell = ctx->getBoundBelCell(b);
        if (sharedCell == nullptr){
            continue;
        }
        // log_info("         Current bel %s sharing with %s(%s)\n", ctx->nameOfBel(bel), ctx->nameOfBel(b), ctx->nameOf(sharedCell));
        return false;
    }


    for (const auto& elem : foundPaths) {
        if (!elem){
            return false;
        }
    }
    return true;
}

bool FABulousImpl::have_path(WireId src, WireId dst) const{
    std::stack<WireId> stack;
    dict<WireId, bool> visited;
    stack.push(src);
    bool foundPath = false;
    
    while (!stack.empty()){
        WireId currentWire = stack.top();
        // log_info("Visiting %s\n", ctx->nameOfWire(currentWire));
        stack.pop();
        if (currentWire == dst){
            // log_info("         Found path from %s to %s\n", ctx->nameOfWire(srcWire), ctx->nameOfWire(dstWire));
            foundPath = true;
            break;
        }
        for (auto pip : ctx->getPipsDownhill(currentWire)){
            WireId nextWire = ctx->getPipDstWire(pip);
            if (!visited[nextWire]){
                stack.push(nextWire);
                visited[nextWire] = true;
            }
        }
    }
    return foundPath;
}



void FABulousImpl::prePlace()
{
    assign_net_info();
    assign_resource_shared();
    // assign_cell_info();
}

bool FABulousImpl::place()
{
    bool retVal = false;
    int tryCount = 0;
    std::string placer = str_or_default(ctx->settings, ctx->id("placer"), defaultPlacer);
    if (placer == "heap") {
        PlacerHeapCfg cfg(ctx);
        configurePlacerHeap(cfg);
        cfg.ioBufTypes.insert(ctx->id("GENERIC_IOB"));
        auto run_startt = std::chrono::high_resolution_clock::now();
        while (tryCount < placeTrial){
            log_info("Trying placer heap with seed:%lu \n", ctx->rngstate);
            retVal = placer_heap(ctx, cfg);
            if (retVal){
                break;
            }
            for (auto &cell : ctx->cells) {
                auto ci = cell.second.get();
                if (ci->bel != BelId()) {
                    ctx->unbindBel(ci->bel);
                }
            }
            tryCount++;
            std::random_device randDev{};
            std::uniform_int_distribution<uint64_t> distrib{1};
            ctx->rngstate = distrib(randDev);
        }
        auto run_stopt = std::chrono::high_resolution_clock::now();
        log_info("Heap placer took %d iterations and %.02fs seconds\n", tryCount, std::chrono::duration<double>(run_stopt - run_startt).count());
        log_break();
    } else if (placer == "sa") {
        while (tryCount < placeTrial){
            log_info("Trying placer sa with seed:%lu \n", ctx->rngstate);
            retVal = placer1(ctx, Placer1Cfg(ctx));
            if (retVal){
                break;
            }
            for (auto &cell_entry : ctx->cells) {
                CellInfo *cell = cell_entry.second.get();
                auto loc = cell->attrs.find(ctx->id("BEL"));
                if (loc != cell->attrs.end()) {
                    cell->attrs.erase(ctx->id("BEL"));
                }
            }

            tryCount++;
            std::random_device randDev{};
            std::uniform_int_distribution<uint64_t> distrib{1};
            ctx->rngstate = distrib(randDev);
        }
    } else {
        log_error("FABulous uarch does not support placer '%s'\n", placer.c_str());
    }
    return retVal;
}

void FABulousImpl::postPlace()
{
    log_info("================== Final Placement ===================\n");
    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (ci->bel != BelId()) {
            log_info("%s: %s\n", ctx->nameOfBel(ci->bel), ctx->nameOf(ci));
        } else {
            log_info("unknown: %s\n", ctx->nameOf(ci));
        }
    }
    log_break();
}

void FABulousImpl::postRoute()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("fasm")) {
    }
}

void FABulousImpl::drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_BOX;
    el.style = style;
    el.x1 = loc.x + 0.15;
    el.x2 = el.x1 + 0.25;
    el.y1 = loc.y + 0.85 - loc.z * 0.1;
    el.y2 = el.y1 - 0.05;
    g.push_back(el);
}

void FABulousImpl::drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type, int32_t tilewire, IdString tile_type){
    // GraphicElement el;
    // el.type = GraphicElement::TYPE_LINE;
    // el.style = style;
    // int z;
    // z = (tilewire - 9) / 4;
    // el.x1 = loc.x + 0.10;
    // el.x2 = el.x1 + 0.05;
    // el.y1 = loc.y + 0.85 - z * 0.1  - ((tilewire - GFX_WIRE_L0_I0) % 4 + 1) * 0.01;
    // el.y2 = el.y1;
    // g.push_back(el);
}

void FABulousImpl::drawPip(std::vector<GraphicElement> &g,GraphicElement::style_t style, Loc loc,
            WireId src, IdString src_type, int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id)
{
    // GraphicElement el;
    // el.type = GraphicElement::TYPE_ARROW;
    // el.style = style;
    // int z;
    // // if (src_type == id_LUT_OUT && dst_type == id_FF_DATA) {
    //     z = src_id - GFX_WIRE_L0_O;
    //     el.x1 = loc.x + 0.45;
    //     el.y1 = loc.y + 0.85 - z * 0.1 - 0.025;
    //     el.x2 = loc.x + 0.50;
    //     el.y2 = el.y1;
    //     g.push_back(el);

    // // }
}


} // namespace

NEXTPNR_NAMESPACE_END
