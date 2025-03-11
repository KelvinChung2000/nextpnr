#include <iterator>
#include <fstream>

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
#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"


#include "FABulous.h"
#include "FABulous_utils.h"
#include "router1.h"

NEXTPNR_NAMESPACE_BEGIN


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
    if (args.options.count("constrain-pair")){
        ctx->settings[ctx->id("constrain-pair")] = args.options.at("constrain-pair");
    }

    context_count = fu.get_context_count();
    xUnitSpacing = (0.4 / float(context_count));
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
        log_error("MinII is larger than what the provide uarch can provide at least %d contexts but only %d available.\n", minII, context_count);
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
                    tileBel != bel && 
                    ctx->getBelName(tileBel) == ctx->getBelName(bel)){
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
    if (boundedCell == nullptr){
        return true;
    }

    if (ctx->getBelType(bel) == id_INBUF || ctx->getBelType(bel) == id_OUTBUF){
        return true;
    }

    std::vector<bool> foundPaths;
    for (auto p : boundedCell->ports){
        if (p.second.type == PORT_IN || p.second.net == nullptr)
            continue;
        
        NetInfo *outnet = p.second.net;
        for (auto user: outnet->users){
            WireId srcWire = ctx->getNetinfoSourceWire(outnet);
            if (ctx->getNetinfoSinkWireCount(outnet, user) == 0){
                // log_info("         No sinks wire set for net %s\n", ctx->nameOf(outnet->name));
                return true;
            }
            for (auto dstWire : ctx->getNetinfoSinkWires(outnet, user)){
                bool result = have_path(srcWire, dstWire);
                // log_info("%s -> %s : %d\n", ctx->nameOfWire(srcWire), ctx->nameOfWire(dstWire), result);
                foundPaths.push_back(result);
            }
        }
    }
    // log_info("         Found %ld paths for net %s\n", foundPaths.size(), ctx->nameOf(boundedCell->name));
    
    auto sharedBel = sharedResource.at(bel);
    for (auto b : sharedBel){
        CellInfo *sharedCell = ctx->getBoundBelCell(b);
        if (sharedCell == nullptr){
            continue;
        }
        log_info("         Current bel %s sharing with %s(%s)\n", ctx->nameOfBel(bel), ctx->nameOfBel(b), ctx->nameOf(sharedCell));
        return false;
    }

    for (const auto& elem : foundPaths) {
        if (elem){
            return true;
        }
    }
    // log_info("         No valid paths found for bel %s for net %s\n", ctx->nameOfBel(bel), ctx->nameOf(boundedCell->name));
    return false;
}

bool FABulousImpl::have_path(WireId src, WireId dst) const{
    std::stack<WireId> stack;
    dict<WireId, bool> visited;

    if (pathCache.count(std::make_pair(src, dst))){
        return pathCache.at(std::make_pair(src, dst));
    }
    
    stack.push(src);
    bool foundPath = false;
    while (!stack.empty()){
        WireId currentWire = stack.top();
        
        // if (std::string(ctx->nameOfWire(currentWire)).find("X1Y2") != std::string::npos) {
        //     log_info("            Visiting %s\n", ctx->nameOfWire(currentWire));
        // }
        stack.pop();
        if (currentWire == dst){
            // log_info("         Found path from %s to %s\n", ctx->nameOfWire(src), ctx->nameOfWire(dst));
            pathCache[std::make_pair(src, dst)] = true;
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

    // log_info("         Path from %s to %s: %d\n", ctx->nameOfWire(src), ctx->nameOfWire(dst), foundPath);
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
        write_fasm(args.options.at("fasm"));
    }
}

void FABulousImpl::drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc)
{
    // GraphicElement el;
    // el.type = GraphicElement::TYPE_BOX;
    // el.style = style;

    // el.x1 = loc.x + 0.3 + xUnitSpacing * float((loc.z / std::max(1 ,context_count-1)));
    // el.x2 = el.x1 + xUnitSpacing * 0.5;

    // IdString tileType = fu.get_tile_type(loc.x, loc.y);
    // int uniqueBelCount = tile_unique_bel_type.at(tileType).size();

    // float yUnitSpacing = 0.4 / uniqueBelCount;

    // el.y1 = loc.y + 0.3 + (loc.z % uniqueBelCount) * yUnitSpacing;
    // el.y2 = el.y1 + (yUnitSpacing * 0.25);

    // g.push_back(el);
}

void FABulousImpl::drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type, int32_t tilewire, IdString tile_type){
    // GraphicElement el;
    // el.type = GraphicElement::TYPE_LINE;
    // el.style = style;
    // int z = tilewire;
    // // if (wire_type == ctx->id("NextCycle")){
    // //     el.x1 = loc.x + 0.1 + xUnitSpacing * float((z / std::max(1 ,context_count-1))) + xUnitSpacing * 0.5;
    // //     el.x2 = el.x1 + xUnitSpacing * 0.25;

    // //     IdString tileType = fu.get_tile_type(loc.x, loc.y);
    // //     int uniqueBelCount = tile_unique_bel_type.at(tileType).size();
    // //     float yUnitSpacing = 0.9 / uniqueBelCount;

    // //     el.y1 = loc.y + 0.1+ (z % uniqueBelCount) * yUnitSpacing + (yUnitSpacing * 0.5);
    // //     el.y2 = el.y1;
    // // }
    // float offset = 0.125;
    // if (wire_type == ctx->id("OutPortNORTH") || wire_type == ctx->id("OutPortNORTH_internal") || wire_type == ctx->id("InPortNORTH")){
    //     float spacing = 0.75 / (float(fu.get_tile_north_port_count(loc.x, loc.y))*context_count);
    //     el.x1 = loc.x + offset + spacing * float((z / 4));
    //     if (wire_type == ctx->id("InPortNORTH"))
    //         el.x1 = loc.x + 1.0 - offset - spacing * (4 - float((z / 4)));
    //     el.x2 = el.x1;
    //     el.y1 = loc.y + 1.0;
    //     el.y2 = el.y1 - 0.05;

    //     if (wire_type == ctx->id("OutPortNORTH_internal")){
    //         el.y1 = el.y2;
    //         el.y2 = el.y1 - 0.05;
    //     }
    //     // log_info("Drawing wire at %f %f %f %f %d\n", el.x1, el.y1, el.x2, el.y2, z);
    // }else if (wire_type == ctx->id("OutPortEAST")|| wire_type == ctx->id("OutPortEAST_internal") || wire_type == ctx->id("InPortEAST")){
    //     float spacing = 0.75 / (float(fu.get_tile_east_port_count(loc.x, loc.y))*context_count);
    //     el.x1 = loc.x+1.0;
    //     el.x2 = el.x1 - 0.05;
    //     if (wire_type == ctx->id("OutPortEAST_internal")){
    //         el.x1 = el.x2;
    //         el.x2 = el.x1 - 0.05;
    //     }

    //     el.y1 = loc.y + offset + spacing * float((z / 4));
    //     if (wire_type == ctx->id("InPortEAST"))
    //         el.y1 = loc.y + 1.0 - offset - spacing * (4 - float((z / 4)));
        
    //     el.y2 = el.y1;

    // }else if (wire_type == ctx->id("OutPortSOUTH")|| wire_type == ctx->id("OutPortSOUTH_internal") || wire_type == ctx->id("InPortSOUTH")){
    //     float spacing = 0.75 / (float(fu.get_tile_south_port_count(loc.x, loc.y))*context_count);
        
    //     el.x1 = loc.x + 1.0 - offset - spacing * (4 - float((z / 4)));
    //     if (wire_type == ctx->id("InPortSOUTH"))
    //         el.x1 = loc.x + offset + spacing * float((z / 4));
        
    //     el.x2 = el.x1;
    //     el.y1 = loc.y;
    //     el.y2 = el.y1 + 0.05;
    //     if (wire_type == ctx->id("OutPortSOUTH_internal")){
    //         el.y1 = el.y2;
    //         el.y2 = el.y1 + 0.05;
    //     }
    //     // log_info("Drawing wire at %f %f %f %f %d\n", el.x1, el.y1, el.x2, el.y2, z);
    // }else if (wire_type == ctx->id("OutPortWEST")|| wire_type == ctx->id("OutPortWEST_internal") || wire_type == ctx->id("InPortWEST")){
    //     float spacing = 0.75 / (float(fu.get_tile_west_port_count(loc.x, loc.y))*context_count);
    //     el.x1 = loc.x;
    //     el.x2 = el.x1 + 0.05;
    //     if (wire_type == ctx->id("OutPortWEST_internal")){
    //         el.x1 = el.x2;
    //         el.x2 = el.x1 + 0.05;
    //     }
        
    //     el.y1 = loc.y + 1.0 - offset - spacing * (4 - float((z / 4)));
    //     if (wire_type == ctx->id("InPortWEST"))
    //         el.y1 = loc.y + offset + spacing * float((z / 4));

    //     el.y2 = el.y1;
    // }
    // g.push_back(el);

}

void FABulousImpl::drawPip(std::vector<GraphicElement> &g,GraphicElement::style_t style, Loc loc,
            WireId src, IdString src_type, int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id)
{
    // GraphicElement el;
    // el.type = GraphicElement::TYPE_ARROW;
    // el.style = style;
    // if (dst_type == ctx->id("OutPortNORTH")){
    //     el.x1 = loc.x;
    //     el.x2 = loc.x + 0.5;
    //     el.y1 = loc.y;
    //     el.y2 = el.y1 + 0.5;
    // }
    // g.push_back(el);

}

NEXTPNR_NAMESPACE_END
