#
# MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
#
#             CMake build script: PC Card (PCMCIA) -- the MAXX I/O board's
#             CL-PD6722 socket controller.
#

add_library(pcmcia OBJECT
    merit_pcic.c
    net_te100pc16.c
)
