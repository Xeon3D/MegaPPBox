/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          The four hardware profiles, and which image / key this run uses.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#ifndef EMU_MEGATOUCH_H
#define EMU_MEGATOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MT_PROFILE_XL_CD = 0, /* XL, boots a CD through the I/O board's U12 ROM-DOS */
    MT_PROFILE_XL_HDD,    /* XL, boots its hard disk                            */
    MT_PROFILE_MAXX_OLD,  /* MAXX 1st .. Emerald 2 (the DOS releases)          */
    MT_PROFILE_MAXX_NEW,  /* Ruby onward (the Linux releases)                   */
    MT_PROFILE_XL_CD_EARLY, /* XL R0-R3, Super 5000: earlier U12 ROM (R3)       */
    MT_PROFILE_COUNT
};

/* Motherboard override for the MAXX profiles: 0 = the profile's own (TX97). */
enum {
    MT_BOARD_DEFAULT = 0,
    MT_BOARD_P55TVP4,     /* ASUS P/I-P55TVP4, i430VX: the spare Socket 7 board */
    MT_BOARD_COUNT
};

typedef struct mt_profile_t {
    const char *name;         /* shown to the user: "MAXX (New)"               */
    const char *internal;     /* ini value: "maxx_new"                         */
    const char *machine;      /* 86Box machine internal name                   */
    const char *cpu_family;
    int         cpu_speed;    /* Hz                                            */
    int         mem_kb;
    const char *gfxcard;
    const char *sndcard;      /* "none": the XL's codec is on the I/O board   */
    int         sound_gain;   /* dB                                            */
    int         merit_variant;/* 0 = XL board (CRT-500), 1 = MAXX board        */
    const char *board_rom;    /* the XL board's U12 ROM window, or ""          */
    int         battery_ram;  /* the early XL board's 32 KB RAM at C8000       */
    const char *drive_preset; /* IDE model the guest sees                      */
    int         boots_cd;     /* 1: the image is a CD, on the secondary master */
    const char *description;
} mt_profile_t;

extern const mt_profile_t mt_profiles[MT_PROFILE_COUNT];

/* The MAXX profiles (MAXX I/O board): the ones that can change motherboard. */
#define MT_IS_MAXX(p) (((p) >= 0) && ((p) < MT_PROFILE_COUNT) && mt_profiles[(p)].merit_variant)

/* Machine name the image is booted on, honouring the board override. */
extern const char *megatouch_machine(void);

/* What this run uses.  Kept in the [MegaPPBox] section of the config file, so
   the next start boots the same image. */
extern int         megatouch_profile(void);
extern const char *megatouch_image(void);
extern const char *megatouch_key(void);
extern int         megatouch_board(void);
extern const char *megatouch_title(void);

/* Change the pick.  Takes effect at the next hard reset (or start). */
extern void megatouch_select(int profile, const char *image, const char *key,
                             int board, const char *title);

/* The key alone, from the key menu: the board takes it at its next key access. */
extern void megatouch_set_key(const char *key);

extern void megatouch_load_config(void);
extern void megatouch_save_config(void);

/* Stamp the selected profile over the loaded configuration.  Called at the end
   of config_load() and again before every hard reset. */
extern void megatouch_apply_profile(void);

/* The folder of images the Machine Manager scans. */
extern const char *megatouch_library(void);
extern void        megatouch_set_library(const char *dir);

#ifdef __cplusplus
}
#endif

#endif /*EMU_MEGATOUCH_H*/
