/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          The four hardware profiles, and the image / key this run boots.
 *
 *          XL (CD Boot)   the 486 XL cabinet starting a CD the way the real
 *                         one did: the I/O board's U12 option ROM boots
 *                         ROM-DOS from its ROM disk, loads the CD driver and
 *                         runs LAUNCHIT.BAT from the disc.
 *          XL (HDD Boot)  the same cabinet booting its hard disk (XL
 *                         Platinum, Titanium, Titanium 2).  The U12 window
 *                         stays shut: its INT 19h hook would boot ROM-DOS.
 *          MAXX (Old)     MAXX 1st .. Double Diamond, on the hardware those
 *                         releases are known to run on.
 *          MAXX (New)     Emerald onward: a Pentium, 64 MB and the C-Media PCI
 *                         audio the later releases probe.
 *
 *          None of the original motherboards is in 86Box; each profile names
 *          the nearest board of the same chipset generation (m_megatouch.c).
 *          Both MAXX profiles carry an ATI 264VT3 (the cabinet had a Rage
 *          IIC): the Linux releases refuse any 430TX board without one of
 *          the two in /proc/pci.  They can be moved to the spare i430VX board.
 *
 *          As in PeepeeBox, the profile is stamped over the configuration at
 *          the end of config_load() and before every hard reset, rather than
 *          left to the file: a stale or hand-edited 86box.cfg cannot produce
 *          a machine that is not a Megatouch cabinet.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nvr.h>
#include <86box/video.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/mouse.h>
#include <86box/keyboard.h>
#include <86box/gameport.h>
#include <86box/serial.h>
#include <86box/hdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/fdd.h>
#include <86box/megatouch.h>
#include <86box/megatouch_keys.h>
#include "cpu.h"

#define MT_SECTION     "MegaPPBox"
#define MT_MERIT_BOARD "Merit Megatouch I/O Board"
#define MT_TOUCH       "microtouch_touchpen"
#define MT_TOUCH_NAME  "3M MicroTouch (Serial)"
#define MT_U12_ROM     "roms/megatouch/sa3014-04_u12-r00.u12"
#define MT_U12_ROM_R3  "roms/megatouch/sa3014-03_u12-r3.u12"
#define MT_CMOS_DIR    "roms/megatouch/cmos/"

/* Drives run as fast as they can.  Hard disks use presets with no seek or
   rotation timings -- "ramdisk", or the Merit presets, which are the same drive
   with the model string the XL games check -- and the CD drive runs at Turbo. */
#define MT_CD_TURBO 99

/* Every Megatouch disk was written under 16 heads and 63 sectors; cylinders
   follow from the image size. */
#define MT_SPT 63
#define MT_HPC 16

const mt_profile_t mt_profiles[MT_PROFILE_COUNT] = {
    [MT_PROFILE_XL_CD] = {
        .name          = "XL (CD Boot)",
        .internal      = "xl_cd",
        .machine       = "486sp3c",
        .cpu_family    = "am486dx4_slenh", /* must be CPUID-capable (CauseWay) */
        .cpu_speed     = 100000000,
        .mem_kb        = 32768,
        .gfxcard       = "cl_gd5430_pci",
        .sndcard       = "none",
        .sound_gain    = 6,
        .merit_variant = 0,
        .board_rom     = MT_U12_ROM,
        .battery_ram   = 1,
        .drive_preset  = "merit000",
        .boots_cd      = 1,
        .description   = "Enhanced Am486DX4 100, 32 MB, Cirrus CL-GD5430, CS4231A on the "
                         "I/O board; boots the CD from the board's ROM-DOS"
    },
    [MT_PROFILE_XL_HDD] = {
        .name          = "XL (HDD Boot)",
        .internal      = "xl_hdd",
        .machine       = "486sp3c",
        .cpu_family    = "am486dx4_slenh",
        .cpu_speed     = 100000000,
        .mem_kb        = 32768,
        .gfxcard       = "cl_gd5430_pci",
        .sndcard       = "none",
        .sound_gain    = 6,
        .merit_variant = 0,
        .board_rom     = "",
        .drive_preset  = "merit000",       /* games check chars 11-18 = MERIT000 */
        .boots_cd      = 0,
        .description   = "Enhanced Am486DX4 100, 32 MB, Cirrus CL-GD5430, CS4231A on the "
                         "I/O board; boots the hard disk"
    },
    [MT_PROFILE_MAXX_OLD] = {
        .name          = "MAXX (Old)",
        .internal      = "maxx_old",
        .machine       = "tx97",
        .cpu_family    = "winchip",
        .cpu_speed     = 200000000,
        .mem_kb        = 32768,
        .gfxcard       = "mach64vt3",      /* Linux releases want a Rage IIC or 264VT3 */
        .sndcard       = "cs4236b",
        .sound_gain    = 12,
        .merit_variant = 1,
        .board_rom     = "",
        .drive_preset  = "ramdisk",        /* MAXX checks no drive name */
        .boots_cd      = 0,
        .description   = "IDT WinChip C6 200, 32 MB, ATI 264VT3, Crystal CS4236B"
    },
    [MT_PROFILE_MAXX_NEW] = {
        .name          = "MAXX (New)",
        .internal      = "maxx_new",
        .machine       = "tx97",
        .cpu_family    = "pentium_p54c",
        .cpu_speed     = 200000000,
        .mem_kb        = 65536,
        .gfxcard       = "mach64vt3",
        .sndcard       = "cmi8738",
        .sound_gain    = 12,
        .merit_variant = 1,
        .board_rom     = "",
        .drive_preset  = "ramdisk",
        .boots_cd      = 0,
        .description   = "Pentium 200, 64 MB, ATI 264VT3, C-Media CMI8738"
    },
    [MT_PROFILE_XL_CD_EARLY] = {
        .name          = "XL (CD Boot, early)",
        .internal      = "xl_cd_early",
        .machine       = "486sp3c",
        .cpu_family    = "am486dx4_slenh",
        .cpu_speed     = 100000000,
        .mem_kb        = 32768,
        .gfxcard       = "cl_gd5430_pci",
        .sndcard       = "none",
        .sound_gain    = 6,
        .merit_variant = 0,
        .board_rom     = MT_U12_ROM_R3,
        .battery_ram   = 1,
        .drive_preset  = "merit000",
        .boots_cd      = 1,
        .description   = "XL R0-R3 and Super 5000 discs: the earlier U12 ROM (SA3014-03 R3), "
                         "DS1205 MultiKey"
    },
};

static const char *mt_boards[MT_BOARD_COUNT] = {
    [MT_BOARD_DEFAULT] = NULL,
    [MT_BOARD_P55TVP4] = "p55tvp4",
};

static int  mt_profile = MT_PROFILE_XL_HDD;
static int  mt_board   = MT_BOARD_DEFAULT;
static char mt_image[1024];
static char mt_key[1024];
static char mt_title[256];
static char mt_library[1024];

#ifdef ENABLE_MEGATOUCH_LOG
int megatouch_do_log = ENABLE_MEGATOUCH_LOG;
#endif

static void
mt_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

int
megatouch_profile(void)
{
    return mt_profile;
}

const char *
megatouch_image(void)
{
    return mt_image;
}

const char *
megatouch_key(void)
{
    return mt_key;
}

int
megatouch_board(void)
{
    return mt_board;
}

const char *
megatouch_title(void)
{
    return mt_title;
}

const char *
megatouch_library(void)
{
    return mt_library;
}

void
megatouch_set_library(const char *dir)
{
    snprintf(mt_library, sizeof(mt_library), "%s", dir ? dir : "");
}

const mt_builtin_key_t *
mt_builtin_key_find(const char *ref)
{
    size_t n = strlen(MT_BUILTIN_PREFIX);

    if (!ref)
        return NULL;
    if (!strncmp(ref, MT_BUILTIN_PREFIX, n))
        ref += n;
    for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++)
        if (!strcmp(k->id, ref))
            return k;
    return NULL;
}

/* Test switches (MEGAPPBOX_MACHINE, _VIDEO, _NV_SEED): unset or empty = off. */
static const char *
mt_testenv(const char *name)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : NULL;
}

const char *
megatouch_machine(void)
{
    /* Test runs: MEGAPPBOX_MACHINE=<internal name> puts a MAXX profile on any
       board in this build. */
    if (MT_IS_MAXX(mt_profile) && mt_testenv("MEGAPPBOX_MACHINE"))
        return mt_testenv("MEGAPPBOX_MACHINE");

    /* The XL profiles are the 486 cabinet; only the MAXX ones can change board. */
    if (MT_IS_MAXX(mt_profile) && (mt_board > MT_BOARD_DEFAULT) && (mt_board < MT_BOARD_COUNT))
        return mt_boards[mt_board];

    return mt_profiles[mt_profile].machine;
}

void
megatouch_select(int profile, const char *image, const char *key, int board, const char *title)
{
    if ((profile >= 0) && (profile < MT_PROFILE_COUNT))
        mt_profile = profile;
    mt_board = ((board >= 0) && (board < MT_BOARD_COUNT)) ? board : MT_BOARD_DEFAULT;
    snprintf(mt_image, sizeof(mt_image), "%s", image ? image : "");
    snprintf(mt_key, sizeof(mt_key), "%s", key ? key : "");
    snprintf(mt_title, sizeof(mt_title), "%s", title ? title : "");
}

void
megatouch_set_key(const char *key)
{
    snprintf(mt_key, sizeof(mt_key), "%s", key ? key : "");
}

void
megatouch_load_config(void)
{
    const char *p = config_get_string(MT_SECTION, "profile", (char *) mt_profiles[MT_PROFILE_XL_HDD].internal);
    char        old_list[1024];

    /* Earlier builds kept the Machine Manager's scan in its own file. */
    path_append_filename(old_list, usr_path, "megappbox-library.ini");
    if (plat_file_check(old_list))
        plat_remove(old_list);

    mt_profile = MT_PROFILE_XL_HDD;
    for (int i = 0; i < MT_PROFILE_COUNT; i++) {
        if (!strcmp(p, mt_profiles[i].internal))
            mt_profile = i;
    }

    p = config_get_string(MT_SECTION, "board", "");
    mt_board = MT_BOARD_DEFAULT;
    for (int i = 1; i < MT_BOARD_COUNT; i++) {
        if (!strcmp(p, mt_boards[i]))
            mt_board = i;
    }

    snprintf(mt_image, sizeof(mt_image), "%s", config_get_string(MT_SECTION, "image", ""));
    snprintf(mt_key, sizeof(mt_key), "%s", config_get_string(MT_SECTION, "key", ""));
    /* An earlier build's keys\<dump> that is now built in and not on disk. */
    if (mt_key[0] && strncmp(mt_key, MT_BUILTIN_PREFIX, strlen(MT_BUILTIN_PREFIX))) {
        char full[1024];
        if (path_abs(mt_key))
            snprintf(full, sizeof(full), "%s", mt_key);
        else
            path_append_filename(full, usr_path, mt_key);
        const mt_builtin_key_t *k = mt_builtin_key_find(path_get_filename(mt_key));
        if (k && !plat_file_check(full))
            snprintf(mt_key, sizeof(mt_key), MT_BUILTIN_PREFIX "%s", k->id);
    }
    snprintf(mt_title, sizeof(mt_title), "%s", config_get_string(MT_SECTION, "title", ""));
    snprintf(mt_library, sizeof(mt_library), "%s", config_get_string(MT_SECTION, "library", ""));
}

void
megatouch_save_config(void)
{
    config_set_string(MT_SECTION, "profile", (char *) mt_profiles[mt_profile].internal);

    if (mt_board > MT_BOARD_DEFAULT)
        config_set_string(MT_SECTION, "board", (char *) mt_boards[mt_board]);
    else
        config_delete_var(MT_SECTION, "board");

#define MT_SAVE_STR(key, val)                          \
    if ((val)[0])                                      \
        config_set_string(MT_SECTION, key, val);       \
    else                                               \
        config_delete_var(MT_SECTION, key)

    MT_SAVE_STR("image", mt_image);
    MT_SAVE_STR("key", mt_key);
    MT_SAVE_STR("title", mt_title);
    MT_SAVE_STR("library", mt_library);
#undef MT_SAVE_STR
}

/* A board booted with no CMOS stops at "CMOS checksum error - press F1", a
   fresh MicroTouch controller is uncalibrated, and a fresh CS4236B has not been
   through CWDINIT's PnP setup.  The ROM set carries settled
   copies (primary master Auto + LBA, which fits every Megatouch disk; touch
   calibrated); put them in nvr\ the first time a board is used. */
static void
mt_seed_nvr(const char *machine_nm)
{
    char names[4][64];

    snprintf(names[0], sizeof(names[0]), "%s.nvr", machine_nm);
    snprintf(names[1], sizeof(names[1]), "%s.bin", machine_nm);
    snprintf(names[2], sizeof(names[2]), "mtouch_A30100.nvr"); /* touch calibration */
    snprintf(names[3], sizeof(names[3]), "cs4236b.nvr");       /* the CS4236B's PnP state */

    for (int i = 0; i < 4; i++) {
        char  dst[1024];
        char  src[256];
        FILE *in;
        FILE *out;
        char  buf[4096];
        size_t n;

        snprintf(dst, sizeof(dst), "%s", nvr_path(names[i]));
        /* The board's CMOS belongs to the profile: it is put back at every
           start.  The MAXX releases write the cabinet's own board settings into
           it (Linux 2butclr -q after a hardware change, DOS MTCMOS), which the
           stand-in board's BIOS cannot boot from, and the three XL profiles
           share one board with different drive settings.  The clock is synced
           from the host anyway. */
        if (plat_file_check(dst) && (i != 0))
            continue;

        /* The XL CD boards' CMOS has every IDE drive set to None: POST skips
           the hard-disk detection and ROM-DOS finds the CD by itself. */
        if ((i == 0) && mt_profiles[mt_profile].boots_cd)
            snprintf(src, sizeof(src), MT_CMOS_DIR "%s-cd.nvr", machine_nm);
        else
            snprintf(src, sizeof(src), MT_CMOS_DIR "%s", names[i]);
        in = rom_fopen(src, "rb");
        if (in == NULL)
            continue;
        out = plat_fopen(dst, "wb");
        if (out != NULL) {
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
                (void) !fwrite(buf, 1, n, out);
            fclose(out);
            mt_log("MegaPPBox: seeded %s from the ROM set\n", names[i]);
        }
        fclose(in);
    }
}

static void
mt_apply_machine(const mt_profile_t *p)
{
    const char *machine_nm = megatouch_machine();

    machine = machine_get_machine_from_internal_name((char *) machine_nm);
    if (machine < 0)
        fatal("MegaPPBox: the %s machine is missing from this build\n", machine_nm);

    cpu_f = cpu_get_family(p->cpu_family);
    if (cpu_f == NULL)
        fatal("MegaPPBox: the %s CPU family is missing from this build\n", p->cpu_family);

    /* Pick the part by speed, not by index, so the table can grow. */
    cpu = 0;
    while (cpu_f->cpus[cpu].cpu_type && (cpu_f->cpus[cpu].rspeed != (uint32_t) p->cpu_speed))
        cpu++;
    if (!cpu_f->cpus[cpu].cpu_type)
        fatal("MegaPPBox: no %d Hz part in the %s family\n", p->cpu_speed, p->cpu_family);
    cpu_s = (CPU *) &cpu_f->cpus[cpu];

    fpu_type                 = fpu_get_type(cpu_f, cpu, "internal");
    fpu_softfloat            = 0;
    cpu_waitstates           = 0;
    cpu_use_dynarec          = 1;
    cpu_override             = 0;
    cpu_override_interpreter = 0;

    mem_size = p->mem_kb;

    mt_seed_nvr(machine_nm);
}

static void
mt_apply_video_sound(const mt_profile_t *p)
{
    /* Test runs: MEGAPPBOX_VIDEO=<internal name> ("internal" = the board's own). */
    if (MT_IS_MAXX(mt_profile) && mt_testenv("MEGAPPBOX_VIDEO"))
        gfxcard[0] = video_get_video_from_internal_name(mt_testenv("MEGAPPBOX_VIDEO"));
    else
        gfxcard[0] = video_get_video_from_internal_name((char *) p->gfxcard);
    if (!gfxcard[0])
        fatal("MegaPPBox: the %s video card is missing from this build\n", p->gfxcard);
    for (int i = 1; i < GFXCARD_MAX; i++)
        gfxcard[i] = 0;

    sound_card_current[0] = sound_card_get_from_internal_name((char *) p->sndcard);
    for (int i = 1; i < SOUND_CARD_MAX; i++)
        sound_card_current[i] = 0;
    sound_gain = p->sound_gain;

    midi_output_device_current = 0;
    midi_input_device_current  = 0;
    mpu401_standalone_enable   = 0;
}

/* The touchscreen is the only thing a player ever touches: a MicroTouch
   controller on COM1, which every release probes. */
static void
mt_apply_input(void)
{
    keyboard_type = KEYBOARD_TYPE_PS2;
    mouse_type    = 0;
    tablet_type   = tablet_get_from_internal_name((char *) MT_TOUCH);
    if (!tablet_type)
        fatal("MegaPPBox: the " MT_TOUCH " touchscreen is missing from this build\n");

    if (config_get_int(MT_TOUCH_NAME, "port", -1) < 0)
        config_set_int(MT_TOUCH_NAME, "port", 0);

    for (int i = 0; i < GAMEPORT_MAX; i++)
        joystick_type[i] = 0;

    /* COM1 is the touch screen.  MAXX has nothing on COM2, and the Linux
       releases' modem probe (modemdetectforce on /dev/modem = ttyS1) waits
       forever on a port that is there but silent; absent, it gives up. */
    for (int i = 0; i < SERIAL_MAX; i++) {
        com_ports[i].enabled = (i < (MT_IS_MAXX(mt_profile) ? 1 : 2));
        com_ports[i].device  = 0;
    }
}

/* The Merit I/O board: coins, the two operator buttons, the DS1991 key, and on
   the XL board the CS4231A codec and the U12 ROM window. */
static void
mt_apply_merit_board(const mt_profile_t *p)
{
    merit_io_enabled = 1;
    config_set_int(MT_MERIT_BOARD, "variant", p->merit_variant);
    config_set_string(MT_MERIT_BOARD, "board_rom", (char *) p->board_rom);
    config_set_string(MT_MERIT_BOARD, "key_file", mt_key);
    config_set_int(MT_MERIT_BOARD, "battery_ram", p->battery_ram);
    /* Test runs: MEGAPPBOX_NV_SEED=<rom path> seeds a fresh battery RAM. */
    config_set_string(MT_MERIT_BOARD, "battery_ram_seed",
                      mt_testenv("MEGAPPBOX_NV_SEED") ? mt_testenv("MEGAPPBOX_NV_SEED") : "");
}

static void
mt_apply_media(const mt_profile_t *p)
{
    for (int i = 0; i < HDD_NUM; i++)
        memset(&hdd[i], 0, sizeof(hard_disk_t));

    for (int i = 0; i < CDROM_NUM; i++)
        cdrom[i].bus_type = CDROM_BUS_DISABLED;

    /* No cabinet had a floppy drive; one attached by hand runs with turbo
       timings, like every other drive here. */
    for (int i = 0; i < FDD_NUM; i++) {
        fdd_set_type(i, 0);
        fdd_set_turbo(i, 1);
    }

    if (p->boots_cd) {
        /* A generic ATAPI drive on the secondary master, where the XL board's
           ROM-DOS CD driver looks for it, at Turbo speed (upstream's value 99:
           anything above 72x). */
        cdrom[0].bus_type    = CDROM_BUS_ATAPI;
        cdrom[0].ide_channel = 2;
        cdrom[0].type        = cdrom_get_from_internal_name("86cd");
        cdrom[0].speed       = MT_CD_TURBO;
        cdrom[0].cur_speed   = MT_CD_TURBO;
        cdrom[0].sound_on    = 1;
        snprintf(cdrom[0].image_path, sizeof(cdrom[0].image_path), "%s", mt_image);
        mt_log("MegaPPBox: CD %s\n", mt_image[0] ? mt_image : "(empty)");
        return;
    }

    if (!mt_image[0])
        return;

    FILE    *fp    = plat_fopen64(mt_image, "rb");
    uint64_t bytes = 0;

    if (fp == NULL) {
        /* Do not let 86Box create a blank multi-gigabyte image in its place:
           that boots a dead machine and looks like a corrupt disk. */
        mt_log("MegaPPBox: %s not found -- no disk attached\n", mt_image);
        return;
    }
    if (!fseeko64(fp, 0, SEEK_END))
        bytes = (uint64_t) ftello64(fp);
    fclose(fp);

    const uint32_t sectors = (uint32_t) (bytes >> 9);
    if (sectors < (MT_SPT * MT_HPC)) {
        mt_log("MegaPPBox: %s is too small to be a disk image\n", mt_image);
        return;
    }

    hdd[0].bus_type     = HDD_BUS_IDE;
    hdd[0].ide_channel  = 0; /* primary master */
    hdd[0].spt          = MT_SPT;
    hdd[0].hpc          = MT_HPC;
    hdd[0].tracks       = sectors / (MT_SPT * MT_HPC);
    hdd[0].wp           = 0;
    hdd[0].speed_preset = hdd_preset_get_from_internal_name((char *) p->drive_preset);
    snprintf(hdd[0].fn, sizeof(hdd[0].fn), "%s", mt_image);

    mt_log("MegaPPBox: disk %s, %u/%u/%u, drive %s\n", mt_image, hdd[0].tracks, hdd[0].hpc,
           hdd[0].spt, p->drive_preset);
}

void
megatouch_apply_profile(void)
{
    const mt_profile_t *p = &mt_profiles[mt_profile];

    mt_apply_machine(p);
    mt_apply_video_sound(p);
    mt_apply_input();
    mt_apply_merit_board(p);
    mt_apply_media(p);

    if (mt_title[0])
        snprintf(vm_name, sizeof(vm_name), "%s", mt_title);

    mt_log("MegaPPBox: profile %s on %s, %s @ %d MHz, %d MB\n", p->name, megatouch_machine(),
           p->cpu_family, p->cpu_speed / 1000000, p->mem_kb / 1024);
}
