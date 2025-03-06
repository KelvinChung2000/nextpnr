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

void FABulousImpl::pack() {
    const pool<CellTypePort> top_ports{
                CellTypePort(id_INBUF, id_PAD),
                CellTypePort(id_OUTBUF, id_PAD),
        };
    h.remove_nextpnr_iobs(top_ports);
    h.replace_constants(CellTypePort(id_VCC_DRV, id_VCC), CellTypePort(id_GND_DRV, id_GND), {}, {}, id_VCC, id_GND);
    
    if (ctx->settings.count(ctx->id("constrain-pair"))) {
        std::string filename = ctx->settings[ctx->id("constrain-pair")].as_string();
        log_info("Reading constrain pair file '%s'...\n", filename.c_str());
        
        std::ifstream file(filename);
        if (!file) {
            log_error("Failed to open constrain pair file '%s'.\n", filename.c_str());
        }
        
        std::string line;
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
                int src_wire_val = std::stoi(src_wire);
                int sink_wire_val = std::stoi(sink_wire);
                
                IdString src_cell = ctx->id(src.substr(0, colon_pos1));
                std::string src_port = src.substr(colon_pos1 + 1);
                std::vector<IdString> src_wires;
                if (src_wire_val == 1){
                    src_wires.push_back(ctx->id(src_port));
                }
                else{
                    for (int i = 0; i < src_wire_val; i++){
                        src_wires.push_back(ctx->id(src_port + "[" + std::to_string(i) + "]"));
                    }
                }
                
                IdString sink_cell = ctx->id(sink.substr(0, colon_pos2));
                std::string sink_port = sink.substr(colon_pos2 + 1);
                std::vector<IdString> sink_wires;
                if (sink_wire_val == 1) {
                    sink_wires.push_back(ctx->id(sink_port));
                } else {
                    for (int i = 0; i < sink_wire_val; i++) {
                        sink_wires.push_back(ctx->id(sink_port + "[" + std::to_string(i) + "]"));
                    }
                }

                pool<CellTypePort> src_ports, sink_ports;
                for (auto &src_wire : src_wires){
                    src_ports.insert(CellTypePort(src_cell, src_wire));
                    // log_info("%s, %s\n", src_cell.c_str(ctx), src_wire.c_str(ctx));
                }
                for (auto &sink_wire : sink_wires){
                    sink_ports.insert(CellTypePort(sink_cell, sink_wire));
                    // log_info("%s, %s\n", sink_cell.c_str(ctx), sink_wire.c_str(ctx));
                }


                int count = h.constrain_cell_pairs(src_ports, sink_ports, delta_z_val, true);
                log_info("Constrained %d pairs of %s:%s to %s:%s with delta_z=%d\n", 
                    count, 
                    src_cell.c_str(ctx), src_port.c_str(), 
                    sink_cell.c_str(ctx), sink_port.c_str(), 
                    delta_z_val
                );
            }
        }
        file.close();
    }
}

NEXTPNR_NAMESPACE_END