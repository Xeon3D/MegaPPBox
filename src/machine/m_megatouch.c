/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          The two motherboards the Megatouch cabinets run on.  Neither
 *          original board is in 86Box, so each is stood in for by the
 *          nearest board 86Box has, with the same generation of chipset:
 *
 *            XL   (Telco 486 board)             -> ASUS PVI-486SP3 (SiS 496)
 *            MAXX (Mitsubishi/Unicorn Socket 7) -> ASUS TX97 (Intel 430TX)
 *            spare Socket 7                     -> ASUS P/I-P55TVP4 (Intel 430VX)
 *
 *          The init functions are upstream's, moved here unchanged from
 *          m_at_socket3_pci.c and m_at_socket7.c.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          MegaPPBox contributors
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>

/* Megatouch XL: ASUS PVI-486SP3 (SiS 85C496/497). */
static void
machine_at_sis_85c496_common_init(UNUSED(const machine_t *model))
{
    device_add(&ide_pci_2ch_device);

    pci_init(PCI_CONFIG_TYPE_1 | FLAG_TRC_CONTROLS_CPURST);
    pci_register_slot(0x05, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
}

int
machine_at_486sp3c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/486sp3c/SI4I0306.AWD",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    machine_at_sis_85c496_common_init(model);
    device_add(&sis_85c496_device);

    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);

    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    device_add(&intel_flash_bxt_device);

    return ret;
}

/* Megatouch MAXX: ASUS TX97 (Intel 430TX + PIIX4). */
int
machine_at_tx97_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tx97/0112.001",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* PIIX4 */
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&i430tx_device);
    device_add(&piix4_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877TF | W83877_3F0));
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0x3, 128);
    device_add(&w83781d_device);    /* fans: Chassis, CPU, Power; temperatures: MB, unused, CPU */
    hwm_values.temperatures[1] = 0; /* unused */
    /* CPU offset */
    if (hwm_values.temperatures[2] < 32) /* prevent underflow */
        hwm_values.temperatures[2] = 0;
    else
        hwm_values.temperatures[2] -= 32;

    return ret;
}

/* Spare Socket 7 board: ASUS P/I-P55TVP4 (Intel 430VX + PIIX3), one chipset
   generation older than the TX97, kept in case a MAXX release wants it. */
int
machine_at_p55tvp4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p55tvp4/0204_128.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x0A, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);

    device_add(&i430vx_device);
    device_add(&piix3_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&w83877_device, (void *) (W83877F | W83877_3F0));
    device_add(&intel_flash_bxt_device);

    return ret;
}
