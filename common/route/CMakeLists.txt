add_library(nextpnr_route INTERFACE)

target_include_directories(nextpnr_route INTERFACE .)

target_link_libraries(nextpnr_route INTERFACE highs)

target_sources(nextpnr_route PUBLIC
    clock_assign.cc
    clock_assign.h
    clock_network.cc
    clock_network.h
    clock_router.cc
    clock_router.h
    router1.cc
    router1.h
    router2.cc
    router2.h
)
