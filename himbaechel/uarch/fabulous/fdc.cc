#include <fstream>

#include "FABulous.h"
#include "FABulous_utils.h"
#include "himbaechel_api.h"
#include "log.h"
#include "util.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FABulousDesignConstraints
{
    Context *ctx;
    std::string filename;
    int lineno = 0;

    FABulousDesignConstraints(Context *ctx, const std::string &filename) : ctx(ctx), filename(filename) {}

    void io_command(std::vector<std::string> &args)
    {
        size_t argsEnd = 1;
        std::string cell = args.at(argsEnd);
        std::string pin = args.at(argsEnd + 1);

        auto fnd_cell = ctx->cells.find(ctx->id(cell));
        if (fnd_cell == ctx->cells.end()) {
            log_warning("unmatched constraint '%s' (on line %d)\n", cell.c_str(), lineno);
        } else {
            BelId pinBel = ctx->get_package_pin_bel(ctx->id(pin));
            if (pinBel == BelId())
                log_error("package does not have a pin named '%s' (on line %d)\n", pin.c_str(), lineno);
            if (fnd_cell->second->attrs.count(id_BEL))
                log_error("duplicate pin constraint on '%s' (on line %d)\n", cell.c_str(), lineno);
            fnd_cell->second->attrs[id_BEL] = ctx->getBelName(pinBel).str(ctx);
            log_info("constrained '%s' to bel '%s'\n", cell.c_str(),
                     fnd_cell->second->attrs[id_BEL].as_string().c_str());
        }
    }

    void frequency_command(std::vector<std::string> &args)
    {
        if (args.size() < 3)
            log_error("expected FDC syntax 'set_frequency net frequency' (on line %d)\n", lineno);
        ctx->addClock(ctx->id(args.at(1)), std::stof(args.at(2)));
    }

    void cell_command(std::vector<std::string> &args)
    {
        size_t argsEnd = 1;
        std::string cell = args.at(argsEnd);
        std::string bel = args.at(argsEnd + 1);

        auto fnd_cell = ctx->cells.find(ctx->id(cell));
        if (fnd_cell == ctx->cells.end()) {
            log_warning("unmatched constraint '%s' (on line %d)\n", cell.c_str(), lineno);
        } else {
            BelId targetBel = ctx->getBelByNameStr(bel);
            if (targetBel == BelId())
                log_error("package does not have a pin named '%s' (on line %d)\n", bel.c_str(), lineno);
            if (fnd_cell->second->attrs.count(id_BEL))
                log_error("duplicate pin constraint on '%s' (on line %d)\n", cell.c_str(), lineno);
            fnd_cell->second->attrs[id_BEL] = ctx->getBelName(targetBel).str(ctx);
            log_info("constrained '%s' to bel '%s'\n", cell.c_str(),
                     fnd_cell->second->attrs[id_BEL].as_string().c_str());
        }
    }

    void apply_constraints()
    {
        std::ifstream in(filename);
        if (!in) {
            log_error("failed to open constraint file\n");
        }

        std::string line;
        lineno = 0;
        while (std::getline(in, line)) {
            lineno++;
            size_t cstart = line.find('#');
            if (cstart != std::string::npos)
                line = line.substr(0, cstart);
            std::stringstream ss(line);
            std::vector<std::string> words;
            std::string tmp;
            while (ss >> tmp)
                words.push_back(tmp);
            if (words.size() == 0)
                continue;
            std::string cmd = words.at(0);
            if (cmd == "set_io") {
                io_command(words);
            } else if (cmd == "set_frequency") {
                frequency_command(words);
            } else if (cmd == "set_cell") {
                cell_command(words);
            } else {
                log_error("unsupported command '%s' (on line %d)\n", cmd.c_str(), lineno);
            }
        }
    }
};
} // namespace

bool FABulousImpl::fdc_apply(Context *ctx, const std::string &filename)
{
    try {
        FABulousDesignConstraints fdc(ctx, filename);
        fdc.apply_constraints();
    } catch (log_execution_error_exception) {
        return false;
    }
    return true;
}

NEXTPNR_NAMESPACE_END