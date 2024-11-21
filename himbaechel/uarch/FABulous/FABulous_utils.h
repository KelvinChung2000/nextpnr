#ifndef FABULOUS_UTILS_H
#define FABULOUS_UTILS_H

#include "design_utils.h"
#include "idstringlist.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct FABulousUtils {
    Context *ctx;

    FABulousUtils() {}

    void init(Context *ctx) { this->ctx = ctx; }

    int get_context_count();
    int get_real_bel_count();
    int get_bel_context(BelId bel) const;
};

NEXTPNR_NAMESPACE_END

#endif