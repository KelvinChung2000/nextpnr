add_library(nextpnr_route INTERFACE)

target_include_directories(nextpnr_route INTERFACE .)

target_sources(nextpnr_route PUBLIC
    clock_network.cc
    clock_network.h
    clock_router.cc
    clock_router.h
    router1.cc
    router1.h
    router2.cc
    router2.h
)
