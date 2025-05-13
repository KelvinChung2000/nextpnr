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
#include "himbaechel_constids.h"


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
    arch->set_package("FABulous");
}

void FABulousImpl::init(Context *ctx)
{
    const ArchArgs &args = ctx->getArchArgs();

    h.init(ctx);
    fu.init(ctx);
    HimbaechelAPI::init(ctx);

    if (args.options.count("fdc")){
        ctx->settings[ctx->id("fdc.filename")] = args.options.at("fdc");
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
    dict<std::pair<WireId, WireId>, bool> pathCacheExplain;
    CellInfo *boundedCell = ctx->getBoundBelCell(bel);
    if (boundedCell == nullptr){
        return true;
    }
    // TODO: make this generic
    if (ctx->getBelType(bel) == ctx->id("IO") || ctx->getBelType(bel) == ctx->id("IO_WIDTH_1"))
        return true;

    bool newPath = false;
    std::vector<std::pair<std::pair<WireId, WireId>, bool>> foundPaths;
    for (auto p : boundedCell->ports){
        if (p.second.type == PORT_IN || p.second.net == nullptr)
            continue;
        
        NetInfo *outnet = p.second.net;
        WireId srcWire = ctx->getNetinfoSourceWire(outnet);
        if (srcWire == WireId()){
            // return true;
            // log_info("outnet %s\n", outnet->name.c_str(ctx));
            log_warning("No source wire for net %s\n", ctx->nameOf(outnet));
            for (auto user: outnet->users){
                log_warning("   user %s\n", ctx->nameOf(user.cell));
            }
            log_error("A bel with no source wire %s\n", ctx->nameOfBel(bel));
        }

        for (auto user: outnet->users){
            if (ctx->getNetinfoSinkWireCount(outnet, user) == 0){
                return true;
            }
            for (auto dstWire : ctx->getNetinfoSinkWires(outnet, user)){
                if (ctx->getWireFlags(dstWire) < ctx->getWireFlags(srcWire)){
                    return false;
                }
                auto src_dst = std::make_pair(srcWire, dstWire);
                if (pathCache.count(src_dst)){
                    // foundPaths.push_back(std::make_pair(src_dst, pathCache.at(src_dst)));
                    if (explain_invalid){
                        pathCacheExplain[std::make_pair(srcWire, dstWire)] = pathCache.at(std::make_pair(srcWire, dstWire));
                    }
                    continue;
                }
                bool result = have_path(srcWire, dstWire);
                foundPaths.push_back(std::make_pair(src_dst, result));
                if (explain_invalid){
                    pathCacheExplain[src_dst] = result;
                }
                newPath = true;
            }
        }
    }

    // TODO: need better way to handle this since now will yield false positive
    if (foundPaths.size() == 0 && !newPath){
        // No paths found for this cell, but no new paths were found
        // This means that the cell is not connected to any other cells
        // and can be placed anywhere
        return false;
    }
    
    auto sharedBel = sharedResource.at(bel);
    for (auto b : sharedBel){
        CellInfo *sharedCell = ctx->getBoundBelCell(b);
        if (sharedCell == nullptr){
            continue;
        }
        if (explain_invalid){
            log_info("Current bel %s sharing with %s(%s)\n", ctx->nameOfBel(bel), ctx->nameOfBel(b), ctx->nameOf(sharedCell));
        }
        return false;
    }

    for (const auto& elem : foundPaths) {
        if (elem.second){
#if 0
            log_info("      Found valid paths for bel %s for cell %s\n", ctx->nameOfBel(bel), ctx->nameOf(boundedCell->name));
            log_info("        src wire: %s\n", ctx->nameOfWire(elem.first.first));
            log_info("        dest wire: %s\n\n", ctx->nameOfWire(elem.first.second));
#endif
            return true;
        }
    }
    if (explain_invalid){
        for (auto p : pathCacheExplain){
            log_info("   %s -> %s : %s\n", ctx->nameOfWire(p.first.first), ctx->nameOfWire(p.first.second), p.second ? "true" : "false");
        }
    }
    return false;
}

bool FABulousImpl::have_path(WireId src, WireId dst) const{
    std::stack<WireId> stack;
    dict<WireId, bool> visited;
    
    stack.push(src);
    bool foundPath = false;
    while (!stack.empty()){
        WireId currentWire = stack.top();
        stack.pop();
        if (currentWire == dst){
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
    pathCache[std::make_pair(src, dst)] = foundPath;
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
            // ctx->rngstate = distrib(randDev);
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
        log_info("Writing FASM file to %s\n", args.options.at("fasm").c_str());
        write_fasm(args.options.at("fasm"));
    }
}



NEXTPNR_NAMESPACE_END
