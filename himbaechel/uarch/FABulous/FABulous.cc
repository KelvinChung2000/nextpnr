#include <iterator>

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/FABulous/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"


#include "FABulous.h"
#include "FABulous_utils.h"

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

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    // BelBucketId getBelBucketForBel(BelId bel) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;

    // drawing
    void drawBel(std::vector<GraphicElement> &g, GraphicElement::style_t style, IdString bel_type, Loc loc) override;
    void drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type, int32_t tilewire, IdString tile_type) override;
    void drawPip(std::vector<GraphicElement> &g,GraphicElement::style_t style, Loc loc,
            WireId src, IdString src_type, int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id) override;

  private:
    HimbaechelHelpers h;
    FABulousUtils fu;

    int context_count = 0;
    int minII = 0;

    // Validity checking
    struct ExampleCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
        bool lut_i3_used = false;
    };
    std::vector<ExampleCellInfo> fast_cell_info;
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
    
    if (minII > context_count && context_count > 1){
        log_error("FABulous uarch requires at least %d contexts to be specified.\n", context_count);
    }

    
    log_info("FABulous uarch initialized with %d contexts and minII=%d\n", context_count, minII);

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
    return true;
}

void FABulousImpl::prePlace()
{
    
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
    el.y1 = loc.y + 0.85 - (loc.z / 2) * 0.1;
    el.y2 = el.y1 - 0.05;
    g.push_back(el);
}

void FABulousImpl::drawWire(std::vector<GraphicElement> &g, GraphicElement::style_t style, Loc loc, IdString wire_type, int32_t tilewire, IdString tile_type){
    GraphicElement el;
    el.type = GraphicElement::TYPE_LINE;
    el.style = style;
    int z;
    z = (tilewire - 9) / 4;
    el.x1 = loc.x + 0.10;
    el.x2 = el.x1 + 0.05;
    el.y1 = loc.y + 0.85 - z * 0.1  - ((tilewire - GFX_WIRE_L0_I0) % 4 + 1) * 0.01;
    el.y2 = el.y1;
    g.push_back(el);
}

void FABulousImpl::drawPip(std::vector<GraphicElement> &g,GraphicElement::style_t style, Loc loc,
            WireId src, IdString src_type, int32_t src_id, WireId dst, IdString dst_type, int32_t dst_id)
{
    GraphicElement el;
    el.type = GraphicElement::TYPE_ARROW;
    el.style = style;
    int z;
    // if (src_type == id_LUT_OUT && dst_type == id_FF_DATA) {
        z = src_id - GFX_WIRE_L0_O;
        el.x1 = loc.x + 0.45;
        el.y1 = loc.y + 0.85 - z * 0.1 - 0.025;
        el.x2 = loc.x + 0.50;
        el.y2 = el.y1;
        g.push_back(el);

    // }
}


} // namespace

NEXTPNR_NAMESPACE_END
