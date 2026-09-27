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
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2021      Andreas J. Reichel.
#          Copyright 2021-2026 Jasmine Iwanek.
#

add_library(dev OBJECT
    clock_ics9xxx.c
    hwm.c
    hwm_lm75.c
    hwm_lm78.c
    i2c.c
    i2c_gpio.c
    isapnp.c
    kbc_at.c
    kbc_at_dev.c
    keyboard.c
    keyboard_at.c
    lpt.c
    mouse.c
    mouse_microtouch_touchscreen.c
    merit_trace.c
    merit_io.c
    merit_pcic.c
    mouse_ps2.c
    mouse_serial.c
    pci_bridge.c
    serial.c
    softpower.c
    smbus_piix4.c
    smbus_sis5595.c
)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    target_link_libraries(86Box atomic)
endif()
