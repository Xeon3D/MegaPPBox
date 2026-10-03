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
#include <strings.h>
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
#include <86box/char.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/random.h>
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

/* The key families, as the releases' key checks know them (keyflasher's
   names).  Some releases share a family: one key runs both. */
const mt_key_family_t mt_key_families[] = {
    { "XLR1",      "MegaTouch XL (R1)"                },
    { "XL5K",      "MegaTouch XL Super 5000"          },
    { "XL6K",      "MegaTouch XL 6000"                },
    { "XGOLDCD",   "MegaTouch XL Gold"                },
    { "XPLAT",     "XL Platinum, Double Platinum"     },
    { "XTIT",      "XL Titanium, Titanium 2"          },
    { "M1",        "MAXX"                             },
    { "M2K",       "MAXX 2000, 2000 Plus"             },
    { "MDIAMOND",  "MAXX Diamond, Double Diamond"     },
    { "MEMERALD",  "MAXX Emerald, Emerald 2"          },
    { "MRUBY",     "MAXX Ruby"                        },
    { "MRUBY2",    "MAXX Ruby 2"                      },
    { "MSAPPHIRE", "MAXX Sapphire, Sapphire 2"        },
    { "MJADE",     "MAXX Jade, Jade 2"                },
    { "MCROWN",    "MAXX Crown"                       },
    { NULL,        NULL                               }
};

int
mt_key_kind(size_t len)
{
    return (len == 264) ? MT_KEY_DS1991 : (len == 192) ? MT_KEY_MULTIKEY : 0;
}

const mt_key_family_t *
mt_key_family_find(const char *prefix)
{
    for (const mt_key_family_t *f = mt_key_families; prefix && f->prefix; f++)
        if (!strcmp(f->prefix, prefix))
            return f;
    return NULL;
}

/* The longest prefix wins: MRUBY2_... is Ruby 2's, not Ruby's. */
const mt_key_family_t *
mt_key_family_from_name(const char *file_name)
{
    const mt_key_family_t *best = NULL;

    for (const mt_key_family_t *f = mt_key_families; file_name && f->prefix; f++) {
        const size_t n = strlen(f->prefix);
        if (!strncasecmp(file_name, f->prefix, n) && (file_name[n] == '_') &&
            (!best || (n > strlen(best->prefix))))
            best = f;
    }
    return best;
}

/* Which release a dump is for.  A DS1991 dump (keyflasher's: 3 subkeys of
   [ID 8][password 8][data 48], a 64-byte scratchpad, the ROM ID) is enciphered
   with its release's scheme, keyed by the ROM ID (keyflasher's DS91Decrypter
   and the per-release parameters found by the MTKeyWork study).  Only the
   right scheme gives a plausible first subkey -- a Merit part number "SAnnnnnn"
   and, in the second subkey's ID, a date -- and the release's signature in
   the first subkey's data then names it; where two releases share a scheme
   and a signature, the part number decides.  A DS1205 MultiKey's subkey names
   are not enciphered: the first is the part number. */
static int
mt_lm(int scheme, const uint8_t *k, int *lm)
{
    switch (scheme) {
        case 0: /* M1 (XL R1 .. MAXX 3.06) */
            lm[0] = lm[1] = lm[2] = lm[3] = 0;
            break;
        case 1: /* XL Gold, MAXX 2000 */
            lm[0] = k[2] ^ ((k[0] + k[1]) | k[4]);
            lm[1] = k[2] | ((k[0] - k[1]) ^ k[3]);
            lm[2] = k[1] & (k[3] + k[2] + k[4]);
            lm[3] = k[4] | ((k[3] + k[1]) - k[2]);
            break;
        case 2: /* XL Platinum, MAXX Diamond */
            lm[0] = k[2] ^ ((k[2] + k[1]) | k[4]);
            lm[1] = k[2] | ((k[3] - k[0]) ^ k[3]);
            lm[2] = k[1] & (k[3] + k[1] + k[4]);
            lm[3] = k[4] | ((k[0] + k[1]) - k[2]);
            break;
        case 3: /* XL Titanium, MAXX Emerald */
            lm[0] = k[3] ^ ((k[1] + k[4]) | k[2]);
            lm[1] = k[1] | ((k[2] - k[3]) ^ k[4]);
            lm[2] = k[4] ^ (k[3] + k[2] + k[1]);
            lm[3] = k[2] | ((k[4] + k[1]) - k[3]);
            break;
        case 4: /* MAXX Ruby 2 */
            lm[0] = ((k[1] + k[3]) | k[4]) ^ k[3];
            lm[1] = ((k[3] - k[4]) ^ k[3]) | k[1];
            lm[2] = (k[2] + k[1] + k[3]) ^ k[4];
            lm[3] = ((k[4] + k[2]) - k[1]) | k[2];
            break;
        case 5: /* MAXX Sapphire */
            lm[0] = ((k[1] ^ k[3]) - k[5]) + k[3];
            lm[1] = ((k[0] - k[5]) - k[3]) - k[1];
            lm[2] = ((k[2] - k[1]) + k[3]) ^ k[5];
            lm[3] = ((k[5] ^ k[2]) + k[1]) ^ k[2];
            break;
        case 6: /* MAXX Jade */
            lm[0] = ((k[1] ^ k[5]) - k[3]) + k[2];
            lm[1] = ((k[1] - k[5]) - k[4]) - k[2];
            lm[2] = k[5] ^ k[1];
            lm[3] = k[2] ^ k[4];
            break;
        case 7: /* MAXX Crown */
            lm[0] = ((k[3] - k[4]) - k[5]) ^ k[1] ^ 0x7f;
            lm[1] = (((k[1] + k[2]) - k[3]) - k[4]) ^ 0xf3;
            lm[2] = (k[4] + 0xbc) ^ (k[2] + k[2] + k[1]);
            lm[3] = k[4] + k[5] + k[1] + k[4] + 0x7f;
            break;
        default:
            return 0;
    }
    for (int i = 0; i < 4; i++)
        lm[i] &= 0xff;
    return 1;
}

#define MT_SCHEMES 8
static const struct {
    uint8_t inc;
    uint8_t magic[4];
    int     gen3; /* the signature at D0+0x28, not D0+0 */
} mt_schemes[MT_SCHEMES] = {
    { 0x00, { 0x00, 0x00, 0x00, 0x00 }, 0 },
    { 0x31, { 0x55, 0x54, 0x53, 0x52 }, 0 },
    { 0x31, { 0x55, 0x54, 0x53, 0x52 }, 0 },
    { 0x31, { 0x73, 0x3c, 0xaa, 0xe7 }, 0 },
    { 0x17, { 0x61, 0xe3, 0x5a, 0x13 }, 0 },
    { 0x53, { 0xa7, 0x67, 0x9a, 0xaa }, 1 },
    { 0x16, { 0xff, 0x35, 0x5a, 0x4b }, 1 },
    { 0x16, { 0x12, 0x78, 0x37, 0xf3 }, 1 },
};

/* Scheme, signature (D0), part number prefix (NULL: any) -> families, best first. */
static const struct {
    int         scheme;
    const char *sig;
    const char *part;
    const char *fam[2];
} mt_key_rules[] = {
    { 0, "1122334455667788", NULL,     { "XLR1", NULL } },
    { 0, "5152535455667788", NULL,     { "XL5K", NULL } },
    { 0, "5052535455667789", NULL,     { "M1", "XL6K" } },
    { 1, "5052535455667789", "SA3039", { "XGOLDCD", NULL } },
    { 1, "5052535455667789", "SA3035", { "M2K", NULL } },
    { 1, "5052535455667789", NULL,     { "M2K", "XGOLDCD" } },
    { 2, "5052535455667789", "SA3046", { "XPLAT", NULL } },
    { 2, "5052535455667789", "SA3042", { "MDIAMOND", NULL } },
    { 2, "5052535455667789", NULL,     { "MDIAMOND", "XPLAT" } },
    { 3, "E842088667400282", NULL,     { "XTIT", NULL } },
    { 3, "DD62104321100293", NULL,     { "MEMERALD", NULL } },
    { 4, "DE62104321100293", NULL,     { "MRUBY2", "MRUBY" } },
    { 5, "1234567890ABCDEF", NULL,     { "MSAPPHIRE", NULL } },
    { 6, "8723879FE2432498", NULL,     { "MJADE", NULL } },
    { 7, "8723879FE2432498", NULL,     { "MCROWN", NULL } },
};

/* Subkey 0's ID and first 48 data bytes, and subkey 1's ID, deciphered. */
static void
mt_key_decipher(const uint8_t *d, int scheme, uint8_t *id0, uint8_t *d0, uint8_t *id1)
{
    uint8_t k[8];
    int     lm[4];

    for (int i = 0; i < 8; i++)
        k[i] = d[263 - i]; /* the ROM ID, family code first */
    mt_lm(scheme, k, lm);
    for (int s = 0; s < 2; s++) {
        int            seed = (mt_schemes[scheme].magic[s] + mt_schemes[scheme].inc * 8) & 0xff;
        const uint8_t *src  = d + 64 * s;
        uint8_t       *id   = s ? id1 : id0;
        for (int i = 0; i < 8; i++) {
            id[i] = (uint8_t) (((src[i] - seed) & 0xff) ^ lm[i % 4]);
            seed  = (seed + mt_schemes[scheme].inc) & 0xff;
        }
        if (s)
            break;
        for (int i = 0; i < 48; i++) {
            d0[i] = (uint8_t) (((src[16 + i] - seed) & 0xff) ^ lm[i % 4]);
            seed  = (seed + mt_schemes[scheme].inc) & 0xff;
        }
    }
}

static int
mt_key_plausible(const uint8_t *id0, const uint8_t *id1)
{
    if ((id0[0] != 'S') || (id0[1] != 'A'))
        return 0;
    for (int i = 2; i < 8; i++)
        if ((id0[i] < '0') || (id0[i] > '9'))
            return 0;
    for (int i = 0; i < 8; i++) {
        const int slash = (i == 2) || (i == 5);
        if (slash ? (id1[i] != '/') : ((id1[i] < '0') || (id1[i] > '9')))
            return 0;
    }
    return 1;
}

static int
mt_key_add(const char **out, int n, int max, const char *fam)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i], fam))
            return n;
    if (n < max)
        out[n++] = fam;
    return n;
}

int
mt_key_identify(const uint8_t *d, size_t len, const char **out, int max)
{
    int n = 0;

    if (mt_key_kind(len) == MT_KEY_MULTIKEY) {
        static const struct {
            const char *part;
            const char *fam[2];
        } multi[] = {
            { "SA3022", { "XLR1", NULL } },
            { "SA3008", { "XL5K", NULL } },
            { "SA3019", { "XL6K", "XLR1" } },
            { "SA3033", { "XGOLDCD", NULL } },
        };
        for (size_t r = 0; r < sizeof(multi) / sizeof(multi[0]); r++)
            if (!memcmp(d, multi[r].part, 6))
                for (int j = 0; (j < 2) && multi[r].fam[j]; j++)
                    n = mt_key_add(out, n, max, multi[r].fam[j]);
        return n;
    }
    if (mt_key_kind(len) != MT_KEY_DS1991)
        return 0;

    for (int s = 0; s < MT_SCHEMES; s++) {
        uint8_t id0[8], d0[48], id1[8];
        char    sig[17];
        mt_key_decipher(d, s, id0, d0, id1);
        if (!mt_key_plausible(id0, id1))
            continue;
        for (int i = 0; i < 8; i++)
            snprintf(sig + 2 * i, 3, "%02X", d0[(mt_schemes[s].gen3 ? 0x28 : 0) + i]);
        /* Rules with a part number first: they are the sure ones. */
        for (int pass = 0; pass < 2; pass++)
            for (size_t r = 0; r < sizeof(mt_key_rules) / sizeof(mt_key_rules[0]); r++) {
                if ((mt_key_rules[r].scheme != s) || strcmp(mt_key_rules[r].sig, sig))
                    continue;
                if (pass ? (mt_key_rules[r].part != NULL)
                         : ((mt_key_rules[r].part == NULL) || memcmp(id0, mt_key_rules[r].part, 6)))
                    continue;
                for (int j = 0; (j < 2) && mt_key_rules[r].fam[j]; j++)
                    n = mt_key_add(out, n, max, mt_key_rules[r].fam[j]);
                if (!pass)
                    pass = 2; /* a part number match is the answer */
                break;
            }
    }
    return n;
}

int
mt_key_file_name(const uint8_t *data, size_t len, const char *prefix, char *out, size_t out_len)
{
    const mt_key_family_t *f    = mt_key_family_find(prefix);
    const int              kind = mt_key_kind(len);
    char                   id[24];

    if (!data || !f || !kind)
        return 0;
    if (kind == MT_KEY_DS1991) {
        /* The ROM ID as keyflasher prints it: the last 8 bytes in order. */
        for (int i = 0; i < 8; i++)
            snprintf(id + 2 * i, 3, "%02X", data[256 + i]);
        snprintf(out, out_len, "%s_full_%s", f->prefix, id);
    } else {
        /* The MultiKey's part number: its first subkey's name ("SA301901"). */
        int n = 0;
        for (int i = 0; i < 8; i++)
            if (((data[i] >= '0') && (data[i] <= '9')) || ((data[i] >= 'A') && (data[i] <= 'Z')))
                id[n++] = (char) data[i];
        id[n] = '\0';
        if (!n)
            snprintf(id, sizeof(id), "%02X%02X%02X%02X", data[0], data[1], data[2], data[3]);
        snprintf(out, out_len, "%s_multikey_%s", f->prefix, id);
    }
    return 1;
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

/* Per-image options.  An image path can be longer than an ini key, so each
   image gets a section of its own: [MegaPPBox image N] with path = and the
   options.  Sections are numbered from 1 with no gaps. */
#define MT_IMAGE_SECTION "MegaPPBox image %d"
#define MT_IMAGE_MAX     1024

static int
mt_image_section(const char *image, int create, char *sec, size_t len)
{
    if (!image || !image[0])
        return 0;

    for (int n = 1; n <= MT_IMAGE_MAX; n++) {
        snprintf(sec, len, MT_IMAGE_SECTION, n);
        const char *p = config_get_string(sec, "path", NULL);
        if (!p) {
            if (!create)
                return 0;
            config_set_string(sec, "path", (char *) image);
            return 1;
        }
        if (!strcasecmp(p, image))
            return 1;
    }
    return 0;
}

int
megatouch_image_option(const char *image, const char *name)
{
    char sec[64];

    if (!mt_image_section(image, 0, sec, sizeof(sec)))
        return 0;
    return !!config_get_int(sec, (char *) name, 0);
}

/* The image's saved choice: 0 or 1, or -1 when it has none (the release's
   default then applies -- the modem is on for releases with an on-line client). */
int
megatouch_image_option_saved(const char *image, const char *name)
{
    char sec[64];
    int  v;

    if (!mt_image_section(image, 0, sec, sizeof(sec)))
        return -1;
    v = config_get_int(sec, (char *) name, -1);
    return (v < 0) ? -1 : !!v;
}

void
megatouch_set_image_option(const char *image, const char *name, int val)
{
    char sec[64];

    /* Off is saved too: a release may default to on. */
    if (mt_image_section(image, 1, sec, sizeof(sec)))
        config_set_int(sec, (char *) name, !!val);
}

static volatile int mt_modem_sounds = -1;

int
megatouch_modem_sounds(void)
{
    if (mt_modem_sounds < 0)
        mt_modem_sounds = !!config_get_int(MT_SECTION, "modem_sounds", 1);
    return mt_modem_sounds;
}

void
megatouch_set_modem_sounds(int on)
{
    mt_modem_sounds = !!on;
    config_set_int(MT_SECTION, "modem_sounds", !!on);
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

    /* Test runs: MEGAPPBOX_CPU=<family>:<Hz> replaces the profile's CPU. */
    char        family[64];
    const char *cpu_family = p->cpu_family;
    uint32_t    cpu_speed  = p->cpu_speed;
    const char *t          = mt_testenv("MEGAPPBOX_CPU");
    if (t && strchr(t, ':') && ((strchr(t, ':') - t) < (int) sizeof(family))) {
        snprintf(family, sizeof(family), "%.*s", (int) (strchr(t, ':') - t), t);
        cpu_family = family;
        cpu_speed  = (uint32_t) strtoul(strchr(t, ':') + 1, NULL, 10);
    }

    cpu_f = cpu_get_family(cpu_family);
    if (cpu_f == NULL)
        fatal("MegaPPBox: the %s CPU family is missing from this build\n", cpu_family);

    /* Pick the part by speed, not by index, so the table can grow. */
    cpu = 0;
    while (cpu_f->cpus[cpu].cpu_type && (cpu_f->cpus[cpu].rspeed != cpu_speed))
        cpu++;
    if (!cpu_f->cpus[cpu].cpu_type)
        fatal("MegaPPBox: no %u Hz part in the %s family\n", cpu_speed, cpu_family);
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

    /* Test runs: MEGAPPBOX_SOUND=<internal name> replaces the profile's sound card. */
    sound_card_current[0] = sound_card_get_from_internal_name(
        (char *) (mt_testenv("MEGAPPBOX_SOUND") ? mt_testenv("MEGAPPBOX_SOUND") : p->sndcard));
    for (int i = 1; i < SOUND_CARD_MAX; i++)
        sound_card_current[i] = 0;
    sound_gain = p->sound_gain;

    midi_output_device_current = 0;
    midi_input_device_current  = 0;
    mpu401_standalone_enable   = 0;
}

static int mt_com2_hidden; /* MAXX without the modem: no COM2 */

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

    /* COM1 is the touch screen.  COM2 is the modem when the image has one
       fitted.  Otherwise MAXX has nothing there: the Linux releases' modem
       probe (on /dev/modem = ttyS1) waits forever on a port that is there but
       silent; absent, it gives up.  The UART is still built, and hidden once
       the machine is (megatouch_machine_built()), so that plugging the modem
       in later can bring it back. */
    const int modem = megatouch_image_option(mt_image, MT_OPT_MODEM);
    mt_com2_hidden  = MT_IS_MAXX(mt_profile) && !modem;
    for (int i = 0; i < SERIAL_MAX; i++) {
        com_ports[i].enabled = (i < 2);
        com_ports[i].device  = 0;
    }
    if (modem) {
        com_ports[1].device = char_get_from_internal_name("modem_fm560lk", DEVICE_COM);
        if (!com_ports[1].device)
            fatal("MegaPPBox: the modem is missing from this build\n");
    }
}

/* After the machine is built, before it runs: on MAXX without the modem, COM2
   goes, and stays gone whatever the board's I/O chip is told. */
void
megatouch_machine_built(void)
{
    serial_t *uart = com_ports[1].serial;

    if (mt_com2_hidden && uart) {
        serial_remove(uart);
        com_ports[1].enabled = 0;
    }
}

/* The modem in or out with the machine running (the emulation held by the
   caller): 86Box's hot-plug of a COM device; on MAXX COM2 comes and goes with
   it.  The guest sees the port at once; a release that looked for a modem
   at boot only sees the change at its next one. */
int
megatouch_modem_plugged(void)
{
    return com_ports[1].enabled && com_ports[1].serial && com_ports[1].device;
}

void
megatouch_modem_plug(int on)
{
    serial_t *uart = com_ports[1].serial;

    if (!uart)
        return;
    if (on) {
        if (!com_ports[1].enabled) {
            com_ports[1].enabled = 1;
            serial_setup(uart, COM2_ADDR, COM2_IRQ);
        }
        com_ports[1].device = char_get_from_internal_name("modem_fm560lk", DEVICE_COM);
    } else
        com_ports[1].device = 0;
    serial_devices_reset();
    if (!on) {
        /* The cable is out: no carrier, no modem. */
        serial_set_dcd(uart, 0);
        serial_set_dsr(uart, 0);
        serial_set_cts(uart, 0);
        serial_set_ri(uart, 0);
        if (MT_IS_MAXX(mt_profile)) {
            serial_remove(uart);
            com_ports[1].enabled = 0;
        }
    }
    mt_com2_hidden = !on && MT_IS_MAXX(mt_profile);
}

/* The network cards, when the image has one fitted.  By default the first is
   on SLiRP (NAT): for the Linux MAXX releases an RTL8139, which they drive
   with 8139too and ask DHCP for an address; for the DOS releases an ISA
   TRENDnet TE-16XP, jumperless at 0x340, IRQ 11.  MAXX 2K V4.00/V4.01 load
   its driver (C:\ETHERNET\ODI.COM) unconditionally; 2K Plus onward run
   "wtrend 340" first, which names it the "old" TE-16XP/T and loads the same
   ODI.COM.  (They also knew the "new" TE-16PT, an RTL8019AS driven by
   PNPODI; 2K did not, so MegaPPBox fits the one card that works for all.)

   Mega-Link -- up to 8 cabinets on a crossover cable or a 10Base-T hub --
   is the switch: every cabinet on the same local switch (or the same remote
   one) is on one Ethernet segment.  The Network dialog sets it all, per
   image, in its [MegaPPBox image N] section, card slot K = 1-4:
     network        whether any card is fitted (the Machine Manager's box)
     netK_card      the card's internal name, or none
     netK_type      none, slirp, pcap, lswitch, rswitch
     netK_host      the host network card PCap uses
     netK_secret    the switch's shared secret: cabinets with the same one link
     netK_promisc   the switch passes every frame on, not only the card's own
     netK_switch    the remote switch, host[:port]
     netK_mac       the card's address (the low three bytes), made once per
                    image, so two cabinets never share one */
static int      mt_apricot;    /* 1 = wanted, 2 = in place at mt_apricot_at, -1 = no room */
static uint32_t mt_apricot_at;

static const char *mt_net_types[] = {
    [NET_TYPE_NONE] = "none", [NET_TYPE_SLIRP] = "slirp", [NET_TYPE_PCAP] = "pcap",
    [NET_TYPE_VDE] = "vde", [NET_TYPE_TAP] = "tap",
    [NET_TYPE_NLSWITCH] = "lswitch", [NET_TYPE_NRSWITCH] = "rswitch"
};

static void
mt_net_key(char *key, size_t len, int k, const char *name)
{
    snprintf(key, len, "net%d_%s", k + 1, name);
}

/* The configuration section of card slot k's device, where it reads its MAC. */
static void
mt_net_dev_section(char *sec, size_t len, int k)
{
    const device_t *dev = network_card_getdevice(net_cards_conf[k].device_num);

    snprintf(sec, len, "%s #%i", dev ? dev->name : "", k + 1);
}

/* The one network card a profile's cabinet takes: the TE-16XP for the DOS
   releases (MAXX (Old)), the RTL8139 for the Linux ones (MAXX (New)); NULL
   when the profile has none. */
const char *
megatouch_network_card_for(int profile)
{
    if (!MT_IS_MAXX(profile))
        return NULL;
    return (profile == MT_PROFILE_MAXX_OLD) ? "te16xp" : "rtl8139c+";
}

const char *
megatouch_network_card(void)
{
    return megatouch_network_card_for(mt_profile);
}

/* Card slot k of the image's saved network settings into nc (card, what it is
   plugged into); 0 when the slot has no card. */
static int
mt_read_net_slot(const char *sec, int profile, int k, netcard_conf_t *nc)
{
    char        key[32];
    const char *card = megatouch_network_card_for(profile);
    const char *s;

    memset(nc, 0, sizeof(*nc));
    mt_net_key(key, sizeof(key), k, "card");
    card = config_get_string((char *) sec, key, (char *) ((k == 0) ? card : "none"));
    /* The TE-16PT is gone: one card serves every DOS release. */
    if (!strcmp(card, "te16pt"))
        card = "te16xp";
    if (!strcmp(card, "none"))
        return 0;
    nc->device_num = network_card_get_from_internal_name((char *) card);
    if (!nc->device_num) {
        pclog("MegaPPBox: network card %s is not in this build\n", card);
        return 0;
    }

    nc->net_type = NET_TYPE_SLIRP;
    mt_net_key(key, sizeof(key), k, "type");
    s = config_get_string((char *) sec, key, "slirp");
    for (int t = 0; t < (int) (sizeof(mt_net_types) / sizeof(mt_net_types[0])); t++)
        if (mt_net_types[t] && !strcmp(s, mt_net_types[t]))
            nc->net_type = t;

    mt_net_key(key, sizeof(key), k, "host");
    snprintf(nc->host_dev_name, sizeof(nc->host_dev_name), "%s", config_get_string((char *) sec, key, ""));
    mt_net_key(key, sizeof(key), k, "secret");
    snprintf(nc->secret, sizeof(nc->secret), "%s", config_get_string((char *) sec, key, ""));
    mt_net_key(key, sizeof(key), k, "switch");
    snprintf(nc->nrs_hostname, sizeof(nc->nrs_hostname), "%s", config_get_string((char *) sec, key, ""));
    mt_net_key(key, sizeof(key), k, "promisc");
    nc->promisc_mode = !!config_get_int((char *) sec, key, 0);
    return 1;
}

/* For the Network dialog: the image's saved settings for card slot k, even
   with the network option off (the dialog then shows what the card would be
   plugged into, and choosing the card turns the option on). */
int
megatouch_network_saved(int k, netcard_conf_t *nc)
{
    char sec[64];

    memset(nc, 0, sizeof(*nc));
    if (!MT_IS_MAXX(mt_profile) || !mt_image_section(mt_image, 0, sec, sizeof(sec)))
        return 0;
    return mt_read_net_slot(sec, mt_profile, k, nc);
}

/* The Network dialog on an image that is not running (the Machine Manager's):
   what its cards are plugged into, as profile would fit them; a card only
   when the image's network option is on. */
void
megatouch_network_load(const char *image, int profile, netcard_conf_t *confs)
{
    char      sec[64];
    const int on = megatouch_image_option(image, MT_OPT_NETWORK);

    memset(confs, 0, NET_CARD_MAX * sizeof(netcard_conf_t));
    if (!MT_IS_MAXX(profile) || !mt_image_section(image, 0, sec, sizeof(sec)))
        return;
    for (int k = 0; k < NET_CARD_MAX; k++)
        if (mt_read_net_slot(sec, profile, k, &confs[k]) && !on)
            confs[k].device_num = 0;
}

static void
mt_apply_network(void)
{
    char sec[64];
    char key[32];
    char devsec[128];

    memset(net_cards_conf, 0, sizeof(net_cards_conf));
    mt_apricot = 0;
    if (!MT_IS_MAXX(mt_profile) || !megatouch_image_option(mt_image, MT_OPT_NETWORK) ||
        !mt_image_section(mt_image, 0, sec, sizeof(sec)))
        return;

    for (int k = 0; k < NET_CARD_MAX; k++) {
        netcard_conf_t *nc = &net_cards_conf[k];
        int             mac;

        if (!mt_read_net_slot(sec, mt_profile, k, nc))
            continue;

        /* The card's MAC is the image's, not the folder's. */
        mt_net_key(key, sizeof(key), k, "mac");
        mac = config_get_mac(sec, key, -1);
        if (mac & 0xff000000) {
            mac = (random_generate() << 16) | (random_generate() << 8) | random_generate();
            config_set_mac(sec, key, mac);
        }
        mt_net_dev_section(devsec, sizeof(devsec), k);
        config_set_mac(devsec, "mac", mac);

        if ((mt_profile == MT_PROFILE_MAXX_OLD) && !strcmp(network_card_get_internal_name(nc->device_num), "te16xp"))
            mt_apricot = 1;
    }
}

/* After the Network dialog: keep what it set (confs) with the image.  The
   card's MAC is taken from the card's settings only for the running image:
   another image's is made the first time it runs. */
static void
mt_network_store(const char *image, int profile, const netcard_conf_t *confs, int running)
{
    char sec[64];
    char key[32];
    char devsec[128];
    int  any = 0;

    if (!MT_IS_MAXX(profile))
        return;
    for (int k = 0; k < NET_CARD_MAX; k++)
        any |= (confs[k].device_num > 0);
    /* No card at all is the network option off; what the cards were stays
       for when it is back on. */
    if (!any) {
        megatouch_set_image_option(image, MT_OPT_NETWORK, 0);
        return;
    }
    if (!mt_image_section(image, 1, sec, sizeof(sec)))
        return;

    for (int k = 0; k < NET_CARD_MAX; k++) {
        const netcard_conf_t *nc = &confs[k];
        const int             on = (nc->device_num > 0);
        int                   mac;

        mt_net_key(key, sizeof(key), k, "card");
        config_set_string(sec, key, (char *) (on ? network_card_get_internal_name(nc->device_num) : "none"));
        if (!on)
            continue;
        mt_net_key(key, sizeof(key), k, "type");
        config_set_string(sec, key, (char *) (((nc->net_type >= 0) && (nc->net_type <= NET_TYPE_NRSWITCH)) ? mt_net_types[nc->net_type] : "slirp"));
        mt_net_key(key, sizeof(key), k, "host");
        config_set_string(sec, key, (char *) nc->host_dev_name);
        mt_net_key(key, sizeof(key), k, "secret");
        config_set_string(sec, key, (char *) nc->secret);
        mt_net_key(key, sizeof(key), k, "switch");
        config_set_string(sec, key, (char *) nc->nrs_hostname);
        mt_net_key(key, sizeof(key), k, "promisc");
        config_set_int(sec, key, nc->promisc_mode);

        /* A MAC set with Configure... goes with the image too. */
        if (!running)
            continue;
        mt_net_dev_section(devsec, sizeof(devsec), k);
        mac = config_get_mac(devsec, "mac", -1);
        if (!(mac & 0xff000000)) {
            mt_net_key(key, sizeof(key), k, "mac");
            config_set_mac(sec, key, mac);
        }
    }
    config_set_int(sec, MT_OPT_NETWORK, 1);
}

void
megatouch_network_to_image(void)
{
    mt_network_store(mt_image, mt_profile, net_cards_conf, 1);
}

void
megatouch_network_store(const char *image, int profile, const netcard_conf_t *confs)
{
    mt_network_store(image, profile, confs, 0);
}

/* Which cabinet board Emerald's C:\MTOOLS\CMOS\WBOARD.EXE sees.  It looks for
   an RTL8139 on the PCI bus, then for strings in the BIOS segment (F000:0000
   to FEFF, from TEST.DAT): "4.51G", "4.04", "-NOUSB", "Apricot" (types 1-4)
   and "REV: MERIT" (5, Unicorn).  STARTTCP.BAT runs "wtrend 340" and the
   TE-16 drivers for types 1-4, and the RTL8139 ODI driver for everything
   else, "not found" (255) included.  The original MAXX's Mitsubishi board
   says "@(#)Apricot BIOS Version 10.83" (MAXXBIOS.BIO); the ASUS TX97 BIOS
   MAXX (Old) runs says none of them.  So with the TE-16 fitted, the Apricot
   identity is written into a blank stretch of the shadowed F000 segment --
   after POST, at the first access to the Merit I/O board, and again should
   a reset have copied the BIOS back over it. */
static const char mt_apricot_id[] = "@(#)Apricot BIOS";

void
megatouch_board_ident(void)
{
    const uint32_t len  = sizeof(mt_apricot_id) - 1;
    uint32_t       run  = 0;
    uint32_t       best = 0;
    uint8_t        prev = 0x55;

    if (mt_apricot <= 0)
        return;
    if ((mt_apricot == 2) && !memcmp(&ram[mt_apricot_at], mt_apricot_id, len))
        return;

    /* The middle of the longest stretch of padding (0x00 or 0xFF, at least
       256 bytes; the TX97's longest is 2489 bytes at F000:7647). */
    mt_apricot_at = 0;
    for (uint32_t a = 0xf0000; a < 0xfff00; a++) {
        const uint8_t b = mem_readb_phys(a);

        run  = ((b == prev) && ((b == 0x00) || (b == 0xff))) ? (run + 1) : 1;
        prev = b;
        if ((run >= 256) && (run > best)) {
            best          = run;
            mt_apricot_at = (a - (run / 2)) & ~0x0f;
        }
    }
    if (mt_apricot_at) {
        memcpy(&ram[mt_apricot_at], mt_apricot_id, len);
        for (uint32_t i = 0; i < len; i++)
            if (mem_readb_phys(mt_apricot_at + i) != (uint8_t) mt_apricot_id[i])
                mt_apricot_at = 0;
    }
    if (mt_apricot_at) {
        mt_apricot = 2;
        pclog("MegaPPBox: BIOS segment says \"%s\" at %05X (the Mitsubishi board)\n", mt_apricot_id, mt_apricot_at);
    } else {
        mt_apricot = -1;
        pclog("MegaPPBox: no room in the BIOS segment for the Apricot identity\n");
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
    mt_apply_network();
    mt_apply_merit_board(p);
    mt_apply_media(p);

    if (mt_title[0])
        snprintf(vm_name, sizeof(vm_name), "%s", mt_title);

    mt_log("MegaPPBox: profile %s on %s, %s @ %d MHz, %d MB\n", p->name, megatouch_machine(),
           p->cpu_family, p->cpu_speed / 1000000, p->mem_kb / 1024);
}
