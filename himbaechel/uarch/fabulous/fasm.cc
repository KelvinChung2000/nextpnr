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
#include <regex>

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
            std::string entryStr = entry.str(ctx);
            entryStr = std::regex_replace(entryStr, std::regex("\\[(\\d+)\\]"), "__$1");
            result += entryStr;
        }
        return result;
    }

    void write_routing(NetInfo *net){
        std::vector<PipId> sorted_pips;
        for (auto &w : net->wires)
            if (w.second.pip != PipId())
                sorted_pips.push_back(w.second.pip);
        std::sort(sorted_pips.begin(), sorted_pips.end());
        out << stringf("# routing for net '%s'\n", ctx->nameOf(net));
        for (auto pip : sorted_pips){

            out << format_name(ctx->getPipName(pip)) << std::endl;  
        }
        out << std::endl;
    }

    // Write a FASM bitvector; optionally inverting the values in the process
    void write_vector(const std::string &name, const std::vector<bool> &value, bool invert = false)
    {
        out << name << " = " << int(value.size()) << "'b";
        for (auto bit : boost::adaptors::reverse(value))
            out << ((bit ^ invert) ? '1' : '0');
        out << std::endl;
    }

    void write_int_vector(const std::string &name, uint64_t value, int width, bool invert = false)
    {
        std::vector<bool> bits(width, false);
        for (int i = 0; i < width; i++)
            bits[i] = (value & (1ULL << unsigned(i))) != 0;
        write_vector(name, bits, invert);
    }

    void write_cell(const CellInfo *ci)
    {
        out << stringf("# config for cell '%s'", ctx->nameOf(ci)) << std::endl;
        std::string name = format_name(ctx->getBelName(ci->bel));
        for (auto &param : ci->params){
            auto paramN = param.first.str(ctx);
            auto value = param.second;
            write_int_vector(name + "." + paramN, value.as_int64(), value.size());
        }
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
