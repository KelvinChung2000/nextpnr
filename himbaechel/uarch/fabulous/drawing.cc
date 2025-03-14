#include "log.h"
#include "nextpnr.h"
#include "FABulous.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_GFXIDS "uarch/example/gfxids.inc"
#include "himbaechel_gfxids.h"


NEXTPNR_NAMESPACE_BEGIN

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
