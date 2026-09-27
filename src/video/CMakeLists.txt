#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2025 starfrost
#

add_library(vid OBJECT

    # Video core
    agpgart.c
    video.c
    vid_table.c

    # RAMDACs and clock generators the kept cards use
    ramdac/vid_ramdac_ati68860.c
    ramdac/vid_ramdac_att20c49x.c
    ramdac/vid_ramdac_att2xc498.c
    ramdac/vid_ramdac_bt48x.c
    ramdac/vid_ramdac_ibm_rgb528.c
    ramdac/vid_ramdac_sc1148x.c
    ramdac/vid_ramdac_sc1502x.c
    ramdac/vid_ramdac_sdac.c
    ramdac/vid_ramdac_tvp3026.c
    clockgen/vid_clockgen_av9194.c
    clockgen/vid_clockgen_icd2061.c
    clockgen/vid_clockgen_ics2494.c
    clockgen/vid_clockgen_ics2595.c

    # DDC / monitor identification
    vid_ddc.c
    vid_ddc_edid_custom.c

    # Super VGA core, and inert 8514/A and XGA hooks for it (see the file)
    vid_svga.c
    vid_svga_render.c
    vid_svga_addons.c

    # Timer helper the emulator core uses for frame pacing
    nv/nv_rivatimer.c

    # The cards: Cirrus Logic CL-GD5430 (XL), ATI Mach64 VT (MAXX Old),
    # S3 Trio64V2/DX (MAXX New)
    vid_cl54xx.c
    vid_ati_eeprom.c
    vid_ati_mach64.c
    vid_ati_mach64_accel.c
    vid_s3.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_include_directories(vid PRIVATE /usr/local/include)
endif()

if(G100)
    target_compile_definitions(vid PRIVATE USE_G100)
endif()

if(XL24)
    target_compile_definitions(vid PRIVATE USE_XL24)
endif()
