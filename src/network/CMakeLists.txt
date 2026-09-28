#
# MegaPPBox    86Box stripped down to run untouched Merit Megatouch images.
#
#              CMake build script: networking -- the RTL8139 on SLiRP, the one
#              network card a Megatouch image is given (Linux MAXX, 8139too).
#

add_library(net OBJECT
    network.c
    net_slirp.c
    net_pcap.c
    net_switch.c
    net_event.c
    net_null.c
    net_rtl8139.c
    net_dp8390.c
    net_ne2000.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SLIRP REQUIRED IMPORTED_TARGET slirp)
target_link_libraries(86Box PkgConfig::SLIRP)

if(WIN32)
    target_link_libraries(PkgConfig::SLIRP INTERFACE wsock32 ws2_32 iphlpapi)
    if(NOT MSVC)
        target_link_libraries(PkgConfig::SLIRP INTERFACE iconv)
    endif()
    if(STATIC_BUILD)
        add_compile_definitions(LIBSLIRP_STATIC)
    endif()
    target_link_libraries(86Box ws2_32)
endif()

if(HAIKU)
    target_link_libraries(86Box network)
endif()
