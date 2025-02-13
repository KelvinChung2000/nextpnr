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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <algorithm>
#include <boost/range/adaptor/reversed.hpp>
#include <fstream>

#include "FABulous.h"

#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FabFasmWriter
{
    Context *ctx;
    FABulousImpl *uarch;
    std::ostream &out;


    FabFasmWriter(Context *context, FABulousImpl *uarchImpl, std::ostream &output) 
        : ctx(context), uarch(uarchImpl), out(output) {}
    
    std::string format_name(IdStringList name)
    {
        std::string result;
        for (IdString entry : name) {
            if (!result.empty())
                result += ".";
            result += entry.str(ctx);
        }
        return result;
    }


    void write_routing(NetInfo *net){
        std::vector<PipId> sorted_pips;
        for (auto &w : net->wires)
            if (w.second.pip != PipId())
                sorted_pips.push_back(w.second.pip);
        std::sort(sorted_pips.begin(), sorted_pips.end());
        out << stringf("# routing for net '%s'\n", ctx->nameOf(net)) << std::endl;
        for (auto pip : sorted_pips)
            out << format_name(ctx->getPipName(pip)) << std::endl;  
        out << std::endl;
    }

    void write_logic(){
        // prefix = format_name(ctx->getBelName(lc->bel)) + ".";
        // if (lc->type.in(id_FABULOUS_LC, id_FABULOUS_COMB)) {
        //     uint64_t init = depermute_lut(lc);
        //     unsigned width = 1U << cfg.clb.lut_k;
        //     write_int_vector(stringf("INIT[%d:0]", width - 1), init, width); // todo lut depermute and thru
        //     if (bool_or_default(lc->params, id_I0MUX, false))
        //         add_feature("IOmux"); // typo in FABulous?
        // }
        // if (lc->type == id_FABULOUS_LC) {
        //     write_bool(lc, "FF");
        // }
        // if (lc->type.in(id_FABULOUS_LC, id_FABULOUS_FF)) {
        //     write_bool(lc, "SET_NORESET");
        //     write_bool(lc, "NEG_CLK");
        //     write_bool(lc, "NEG_EN");
        //     write_bool(lc, "NEG_SR");
        //     write_bool(lc, "ASYNC_SR");
        // }
        // if (lc->type.in(id_FABULOUS_MUX4, id_FABULOUS_MUX8)) {
        //     // TODO: don't hardcode prefix
        //     out << prefix << "I.c0" << std::endl;
        // }
        // if (lc->type == id_FABULOUS_MUX8) {
        //     // TODO: don't hardcode prefix
        //     out << prefix << "I.c1" << std::endl;
        // }
    }

    void write_cell(const CellInfo *ci)
    {
        out << stringf("# config for cell '%s'\n", ctx->nameOf(ci)) << std::endl;
        if (ci->type.in(id_FABULOUS_COMB, id_FABULOUS_FF, id_FABULOUS_LC, id_FABULOUS_MUX2, id_FABULOUS_MUX4,
                        id_FABULOUS_MUX8))
            write_logic(ci);
        else if (ci->type == id_IO_1_bidirectional_frame_config_pass)
            write_io(ci);
        else if (ci->type.in(id_InPass4_frame_config, id_OutPass4_frame_config))
            write_iopass(ci);
        else
            write_generic_cell(ci);
        // TODO: other cell types
        out << std::endl;
    }

    void write_fasm() {
        for (const auto &net : ctx->nets)
            write_routing(net.second.get());
        for (const auto &cell : ctx->cells)
            write_cell(cell.second.get());
    }

};
} // namespace

void FABulousImpl::write_fasm(const std::string &filename)
{
    std::ofstream out(filename);
    if (!out)
        log_error("failed to open file %s for writing (%s)\n", filename.c_str(), strerror(errno));

    FabFasmWriter wr(this->ctx, this, out);
    wr.write_fasm();
}

NEXTPNR_NAMESPACE_END
