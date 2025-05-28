#include <fstream>

#include "FABulous.h"
#include "FABulous_utils.h"

#include "himbaechel_api.h"
#include "log.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/fabulous/constids.inc"
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_gfxids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FABulousPacker
{
    Context *ctx;
    std::string filename;
    FABulousPacker(Context *ctx, const std::string &filename) : ctx(ctx), filename(filename) {}

    void pack()
    {
        remove_fabulous_iob();
        // h.replace_constants(CellTypePort(id_VCC_DRV, id_VCC), CellTypePort(id_GND_DRV, id_GND), {}, {}, id_VCC,
        // id_GND);
        fold_bit_const();
        fold_clock_drive();
        remove_undrive_constant();

        std::string filename = ctx->settings[ctx->id("constrain-pair")].as_string();
        log_info("Reading constrain pair file '%s'...\n", filename.c_str());

        std::ifstream file(filename);
        if (!file) {
            log_error("Failed to open constrain pair file '%s'.\n", filename.c_str());
        }

        std::string line;
        int constrain_count = 0;

        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream iss(line);
            std::string src, src_wire, sink, sink_wire;
            std::string delta_z;

            if (iss >> src >> src_wire >> sink >> sink_wire >> delta_z) {
                // Split cell1_spec into cell name and port name
                size_t colon_pos1 = 0;
                while (colon_pos1 < src.size() && src[colon_pos1] != ':') {
                    colon_pos1++;
                }

                // Split cell2_spec into cell name and port name
                size_t colon_pos2 = 0;
                while (colon_pos2 < sink.size() && sink[colon_pos2] != ':') {
                    colon_pos2++;
                }

                if (colon_pos1 == src.size() || colon_pos2 == sink.size()) {
                    log_error("Invalid constraint format '%s %s', expected 'cellName:portName cellName:portName'.\n",
                              src.c_str(), sink.c_str());
                }

                // Verify delta_z is a valid integer
                if (delta_z.empty()) {
                    log_error("Missing delta_z value for constraint '%s %s'.\n", src.c_str(), sink.c_str());
                }
                if (src_wire.empty() || sink_wire.empty()) {
                    log_error("Missing wire name for constraint '%s %s'.\n", src.c_str(), sink.c_str());
                }

                int delta_z_val = std::stoi(delta_z);
                int src_wire_count = std::stoi(src_wire);
                int sink_wire_count = std::stoi(sink_wire);
                pool<CellTypePort> main_cell_ports;
                pool<CellTypePort> child_cell_ports;

                IdString main_cell = ctx->id(src.substr(0, colon_pos1));
                std::string main_cell_port = src.substr(colon_pos1 + 1);
                std::vector<IdString> src_wires;
                if (src_wire_count == 1) {
                    src_wires.push_back(ctx->id(main_cell_port));
                } else {
                    for (int i = 0; i < src_wire_count; i++) {
                        src_wires.push_back(ctx->id(main_cell_port + "[" + std::to_string(i) + "]"));
                    }
                }

                for (IdString src_wire : src_wires) {
                    main_cell_ports.insert(CellTypePort(main_cell, src_wire));
                }

                IdString child_cell = ctx->id(sink.substr(0, colon_pos2));
                std::string child_cell_port = sink.substr(colon_pos2 + 1);
                std::vector<IdString> sink_wires;
                if (sink_wire_count == 1) {
                    sink_wires.push_back(ctx->id(child_cell_port));
                } else {
                    for (int i = 0; i < sink_wire_count; i++) {
                        sink_wires.push_back(ctx->id(child_cell_port + "[" + std::to_string(i) + "]"));
                    }
                }
                for (IdString sink_wire : sink_wires) {
                    child_cell_ports.insert(CellTypePort(child_cell, sink_wire));
                }

                for (auto &cell : ctx->cells) {
                    auto &ci = *cell.second;

                    // clock is special case and handle separately
                    if (ci.type == id_CLK_DRV) {
                        // for (auto &usr : ci.ports.at(ctx->id("CLK_O")).net->users){
                        //     auto &usr_ci = *usr.cell;
                        //     rel_constr_cells(&ci, &usr_ci, 0xFFFF - get_macro_cell_z(&ci));
                        // }
                        continue;
                    }

                    for (auto &port : ci.ports) {
                        if (port.second.net == nullptr)
                            continue;
                        if (port.second.net->driver.cell == nullptr)
                            continue;

                        if (main_cell_ports.count(CellTypePort(ci.type, port.first))) {
                            if (port.second.type == PORT_OUT) {
                                for (auto &usr : port.second.net->users) {
                                    auto &usr_ci = *usr.cell;
                                    if (child_cell_ports.count(CellTypePort(usr_ci.type, usr.port))) {
                                        rel_constr_cells(&ci, &usr_ci, delta_z_val);
                                        constrain_count++;
                                        break;
                                    }
                                }
                            } else {
                                auto &c = port.second.net->driver.cell;
                                for (auto &dport : c->ports) {
                                    if (child_cell_ports.count(CellTypePort(c->type, dport.first))) {
                                        rel_constr_cells(&ci, c, delta_z_val);
                                        constrain_count++;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        log_info("Constrained %d pairs\n", constrain_count);
        file.close();
    }

    void remove_undrive_constant()
    {
        std::vector<NetInfo *> to_remove_net;

        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            if (!ni.driver.cell)
                continue;
            if (ni.driver.cell->type != ctx->id("const_unit"))
                continue;
            for (auto &param : ni.driver.cell->params) {
                if (!param.second.is_fully_def()) {
                    to_remove_net.push_back(&ni);
                }
            }
        }

        for (auto net_name : to_remove_net) {
            for (auto &usr : net_name->users) {
                auto &ci = *usr.cell;
                ci.disconnectPort(usr.port);
            }
            // log_info("Removing net %s as the value is not important\n", net_name->driver.cell->name.c_str(ctx));
            ctx->cells.erase(net_name->driver.cell->name);
            ctx->nets.erase(net_name->name);
        }
    }

    void fold_bit_const()
    {
        std::vector<IdString> trim_cells;
        std::vector<IdString> trim_nets;
        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            if (!ni.driver.cell)
                continue;
            if (ni.driver.cell->type != ctx->id("GND") && ni.driver.cell->type != ctx->id("VCC"))
                continue;

            for (auto &usr : ni.users) {
                usr.cell->disconnectPort(usr.port);
                usr.cell->params[usr.port] = ni.driver.cell->type == ctx->id("GND") ? Property(0, 1) : Property(1, 1);
            }
            trim_cells.push_back(ni.driver.cell->name);
            trim_nets.push_back(ni.name);
        }
        for (IdString cell_name : trim_cells)
            ctx->cells.erase(cell_name);
        for (IdString net_name : trim_nets)
            ctx->nets.erase(net_name);
    }

    void fold_clock_drive()
    {
        std::vector<IdString> trim_cells;
        std::vector<IdString> trim_nets;
        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            if (!ni.driver.cell)
                continue;
            if (ni.driver.cell->type != ctx->id("CLK_DRV"))
                continue;

            for (auto &usr : ni.users) {
                usr.cell->disconnectPort(usr.port);
                usr.cell->params[ctx->id("CLK_DRV")] = Property(1, 1);
            }
            trim_cells.push_back(ni.driver.cell->name);
            trim_nets.push_back(ni.name);
        }
        for (IdString cell_name : trim_cells)
            ctx->cells.erase(cell_name);
        for (IdString net_name : trim_nets)
            ctx->nets.erase(net_name);
    }

    void remove_fabulous_iob()
    {

        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!ci.type.in(ctx->id("INBUF"), ctx->id("OBUF")))
                continue;
            NetInfo *i = ci.getPort(ctx->id("I"));
            if (i && i->driver.cell) {
                for (auto &attr : ci.attrs) {
                    i->driver.cell->setAttr(attr.first, attr.second);
                }
            }
            NetInfo *o = ci.getPort(ctx->id("O"));
            if (o) {
                for (auto &usr : o->users) {
                    for (auto &attr : ci.attrs) {
                        usr.cell->setAttr(attr.first, attr.second);
                    }
                }
            }
        }

        std::vector<NetInfo *> net_to_remove;
        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            if (!ni.driver.cell)
                continue;
            if (ni.driver.cell->type != ctx->id("INBUF"))
                continue;

            ni.driver.cell->disconnectPort(ctx->id("PAD"));
            net_to_remove.push_back(&ni);
        }
        for (auto net_name : net_to_remove) {
            for (auto &usr : net_name->users) {
                auto &ci = *usr.cell;
                ci.disconnectPort(usr.port);
            }
            ctx->cells.erase(net_name->driver.cell->name);
            ctx->nets.erase(net_name->name);
        }
        net_to_remove.clear();
        for (auto &net : ctx->nets) {
            auto &ni = *net.second;
            for (auto &usr : ni.users) {
                if (usr.cell->type != ctx->id("OUTBUF"))
                    continue;
                usr.cell->disconnectPort(ctx->id("PAD"));
                net_to_remove.push_back(&ni);
            }
        }
        for (auto net_name : net_to_remove) {
            auto driver = net_name->driver;
            driver.cell->disconnectPort(driver.port);
            for (auto &usr : net_name->users) {
                auto &ci = *usr.cell;
                ctx->cells.erase(ci.name);
            }
            ctx->nets.erase(net_name->name);
        }
    }

    int get_macro_cell_z(const CellInfo *ci)
    {
        if (ci->constr_abs_z)
            return ci->constr_z;
        else if (ci->cluster != ClusterId() && ctx->getClusterRootCell(ci->cluster) != ci)
            return ci->constr_z + get_macro_cell_z(ctx->getClusterRootCell(ci->cluster));
        else
            return 0;
    }

    void rel_constr_cells(CellInfo *a, CellInfo *b, int dz)
    {
        NPNR_ASSERT(a != nullptr);
        NPNR_ASSERT(b != nullptr);

        // Check if b is already in a's constraint children, which would create a circular dependency
        if (std::find(a->constr_children.begin(), a->constr_children.end(), b) != a->constr_children.end()) {
#if 0
            log_info("Skipping constraint: cell %s is already a child of %s\n", 
                    b->name.c_str(ctx), a->name.c_str(ctx));
#endif
            return;
        }

        // Also check for the reverse relationship to prevent circular dependencies
        if (std::find(b->constr_children.begin(), b->constr_children.end(), a) != b->constr_children.end()) {
#if 0
            log_info("Skipping constraint: cell %s is already a child of %s\n", 
                    a->name.c_str(ctx), b->name.c_str(ctx));
#endif
            return;
        }

        if (a->cluster != ClusterId() && b->cluster != ClusterId())
            return;

        // if (a->cluster != ClusterId() && b->cluster != ClusterId()) {
        //     // both a and b is in a cluster
        //     CellInfo *a_root = ctx->getClusterRootCell(a->cluster);
        //     CellInfo *b_root = ctx->getClusterRootCell(b->cluster);
        //     if (ctx->getClusterRootCell(a->cluster) == a ||
        //         (ctx->getClusterRootCell(a->cluster) != a && ctx->getClusterRootCell(b->cluster) != b)) {
        //         // a is the root of the cluster
        //         for (auto i : b_root->constr_children) {
        //             a_root->constr_children.push_back(i);
        //             i->cluster = a_root->cluster;
        //             i->constr_x = a_root->constr_x;
        //             i->constr_y = a_root->constr_y;
        //             i->constr_z = (i->constr_z - get_macro_cell_z(b)) + get_macro_cell_z(a) + dz;
        //             i->constr_abs_z = a_root->constr_abs_z;
        //         }
        //         b_root->constr_children.clear();
        //         b->cluster = a_root->cluster;
        //         b->constr_x = a->constr_x;
        //         b->constr_y = a->constr_y;
        //         b->constr_z = get_macro_cell_z(a) + dz;
        //         b->constr_abs_z = a->constr_abs_z;
        //         if (ctx->getClusterRootCell(a->cluster) == a && ctx->getClusterRootCell(a->cluster) == a)
        //             log_info("Packed cluster %s into cluster %s\n", ctx->nameOf(b->cluster), ctx->nameOf(a->cluster));
        //         else
        //             log_info("Packed cluster %s into cluster %s as %s is part of %s\n", ctx->nameOf(b_root),
        //                      ctx->nameOf(a->cluster), ctx->nameOf(b), ctx->nameOf(b_root));
        //     } else if (ctx->getClusterRootCell(b->cluster) == b) {
        //         // b is the root of the cluster
        //         for (auto i : a_root->constr_children) {
        //             b_root->constr_children.push_back(i);
        //             i->cluster = b_root->cluster;
        //             i->constr_x = b_root->constr_x;
        //             i->constr_y = b_root->constr_y;
        //             i->constr_z = (i->constr_z - get_macro_cell_z(a)) + get_macro_cell_z(b) + dz;
        //             i->constr_abs_z = b_root->constr_abs_z;
        //         }
        //         a_root->constr_children.clear();
        //         a->cluster = a_root->cluster;
        //         a->constr_x = b->constr_x;
        //         a->constr_y = b->constr_y;
        //         a->constr_z = get_macro_cell_z(b) + dz;
        //         a->constr_abs_z = b->constr_abs_z;
        //         log_info("Packed cluster %s into cluster %s\n", ctx->nameOf(a_root), ctx->nameOf(b_root));
        //     }

        // }
        if (a->cluster != ClusterId() && ctx->getClusterRootCell(a->cluster) != a) {
            // a is part of a cluster
            NPNR_ASSERT(b->cluster == ClusterId());
            NPNR_ASSERT(b->constr_children.empty());
            CellInfo *root = ctx->getClusterRootCell(a->cluster);
            root->constr_children.push_back(b);
            b->cluster = root->cluster;
            b->constr_x = a->constr_x;
            b->constr_y = a->constr_y;
            b->constr_z = get_macro_cell_z(a) + dz;
            b->constr_abs_z = a->constr_abs_z;
            log_info("Packed cell %s into cell %s via %s (z=%d)\n", ctx->nameOf(b), ctx->nameOf(root), ctx->nameOf(a), b->constr_z);
        } else if (b->cluster != ClusterId() && ctx->getClusterRootCell(b->cluster) != b) {
            // b is part of a cluster
            NPNR_ASSERT(a->constr_children.empty());
            CellInfo *root = ctx->getClusterRootCell(b->cluster);
            if (get_macro_cell_z(b) - dz < 0){
                log_info("Skipping constraint: cell %s is already at the bottom of the cluster\n", ctx->nameOf(b));
                return;
            }
            root->constr_children.push_back(a);
            a->cluster = root->cluster;
            a->constr_x = b->constr_x;
            a->constr_y = b->constr_y;
            a->constr_z = get_macro_cell_z(b) - dz;
            a->constr_abs_z = b->constr_abs_z;
            log_info("Packed cell %s into cell %s via %s (z=%d)\n", ctx->nameOf(a), ctx->nameOf(root), ctx->nameOf(b), a->constr_z);
        } else if (!b->constr_children.empty()) {
            // b is a parent cell
            NPNR_ASSERT(a->constr_children.empty());
            if (get_macro_cell_z(b) - dz < 0){
                log_info("Skipping constraint: cell %s is already at the bottom of the cluster\n", ctx->nameOf(b));
                return;
            }
            b->constr_children.push_back(a);
            a->cluster = b->cluster;
            a->constr_x = 0;
            a->constr_y = 0;
            a->constr_z = get_macro_cell_z(b) - dz;
            a->constr_abs_z = b->constr_abs_z;
            log_info("Packed cell %s into cell %s (z=%d)\n", ctx->nameOf(a), ctx->nameOf(b), a->constr_z);
        } else {
            // a is a parent cell or both cells are not part of a cluster
            NPNR_ASSERT(a->cluster == ClusterId() || ctx->getClusterRootCell(a->cluster) == a);
            a->constr_children.push_back(b);
            a->cluster = a->name;
            b->cluster = a->name;
            b->constr_x = 0;
            b->constr_y = 0;
            b->constr_z = get_macro_cell_z(a) + dz;
            b->constr_abs_z = a->constr_abs_z;
            log_info("Packed cell %s into cell %s (z=%d)\n", ctx->nameOf(b), ctx->nameOf(a), b->constr_z);
        }
    }
};
} // namespace

void FABulousImpl::pack()
{

    for (auto &cell : ctx->cells) {
        auto &ci = *cell.second;
        if (ci.type.in(ctx->id("INBUF"), ctx->id("OUTBUF"))){
            NetInfo *i = ci.getPort(ctx->id("I"));
            if (i && i->driver.cell) {
                if (!ioBufTypes.count(i->driver.cell->type))
                    log_info("Detected cell type %s is an IO type cell\n", i->driver.cell->type.c_str(ctx));
                ioBufTypes.insert(i->driver.cell->type);
            }

            NetInfo *o = ci.getPort(ctx->id("O"));
            if (o) {
                for (auto &usr : o->users){
                    if (!ioBufTypes.count(usr.cell->type))
                        log_info("Detected cell type %s is an IO type cell\n", usr.cell->type.c_str(ctx));
                    ioBufTypes.insert(usr.cell->type);
                }
            }
        }
    }

    if (ctx->settings.count(ctx->id("fdc.filename"))) {
        if (!fdc_apply(ctx, ctx->settings[ctx->id("fdc.filename")].as_string()))
            log_error("Fail to apply FABulous Design Constrain");
    }

    for (auto &cell : ctx->cells) {
        auto &ci = *cell.second;
        if (!ci.type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
            continue;
        NetInfo *i = ci.getPort(ctx->id("I"));
        if (i && i->driver.cell) {
            for (auto &attr : ci.attrs) {
                i->driver.cell->setAttr(attr.first, attr.second);
            }
        }
        NetInfo *o = ci.getPort(ctx->id("O"));
        if (o) {
            for (auto &usr : o->users) {
                for (auto &attr : ci.attrs) {
                    usr.cell->setAttr(attr.first, attr.second);
                }
            }
        }
    }

    const pool<CellTypePort> top_ports{
            CellTypePort(id_INBUF, id_PAD),
            CellTypePort(id_OUTBUF, id_PAD),
    };
    h.remove_nextpnr_iobs(top_ports);

    // for (auto &cell : ctx->cells) {
    //     CellInfo *ci = cell.second.get();
    //     std::string cellName = ci->name.c_str(ctx);
    //     if (cellName.substr(0, 9) == "$iopadmap") {
    //         auto newName = extract_after_dot(cellName);
    //         if (newName == "")
    //             log_error("Invalid IO naming");

    //         ci->name = ctx->id(newName);
    //     }
    // }

    if (ctx->settings.count(ctx->id("constrain-pair"))) {
        FABulousPacker packer(ctx, ctx->settings[ctx->id("constrain-pair")].as_string());
        packer.pack();
    }
}

NEXTPNR_NAMESPACE_END