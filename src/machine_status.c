#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/hdd.h>
#include <86box/thread.h>
#include <86box/machine_status.h>

machine_status_t machine_status;

void
machine_status_init(void)
{
    for (size_t i = 0; i < CDROM_NUM; ++i) {
        machine_status.cdrom[i].empty        = (strlen(cdrom[i].image_path) == 0);
        machine_status.cdrom[i].active       = false;
        machine_status.cdrom[i].write_active = false;
        machine_status.cdrom[i].write_prot   = false;
    }

    for (size_t i = 0; i < HDD_BUS_USB; i++) {
        machine_status.hdd[i].active       = false;
        machine_status.hdd[i].write_active = false;
    }
}
