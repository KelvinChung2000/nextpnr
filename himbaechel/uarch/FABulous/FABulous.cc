#include <iterator>

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/FABulous/constids.inc"
#include "himbaechel_constids.h"


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
    // void postPlace() override;
    // void preRoute() override;
    void postRoute() override;

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    // BelBucketId getBelBucketForBel(BelId bel) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;

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
    bool match_device(const std::string &device) override { return true; }
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

void FABulousImpl::postRoute()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("fasm")) {
    }
}


} // namespace

NEXTPNR_NAMESPACE_END
