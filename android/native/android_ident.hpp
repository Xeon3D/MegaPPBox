#ifndef ANDROID_IDENT_HPP
#define ANDROID_IDENT_HPP

#include <string>

/* What an image is (android_ident.cpp): src/qt/qt_megatouch_ident.hpp's MtIdent,
   without Qt, for the Android Machine Manager. */
struct MtIdentAndroid {
    enum Kind {
        Unknown,    /* not recognised: no profile is guessed        */
        DosDisk,    /* DOS release on a hard disk image             */
        LinuxDisk,  /* Linux MAXX on a hard disk image              */
        XlCd,       /* XL CD generation: runs from the disc         */
        NotRunnable /* a restore set or an older XL disc: skipped   */
    };

    int         kind    = Unknown;
    std::string release;   /* "MAXX Emerald 2"                          */
    std::string version;   /* "PG3002 V9.01"                            */
    std::string evidence;  /* "MERIT2\EXEC\MEGACDLL.EXE"                */
    int         profile = -1;
    std::string keyPrefix; /* "MEMERALD"; empty = none                  */
    std::string note;      /* anything the user should know             */
    bool        modem = false; /* has an on-line client (TournaMAXX on the DOS releases,
                                  Merit's own on Linux): the modem starts fitted */

    bool runnable() const { return (kind == DosDisk) || (kind == LinuxDisk) || (kind == XlCd); }
};

/* Look at an image file (.img / .iso).  Reads only what it needs; never writes. */
MtIdentAndroid mt_identify_android(const std::string &path);

/* The key that suits the identification: a built-in one ("builtin:<id>"),
   else an imported dump in keys_dir (its full path), or empty. */
std::string mt_default_key_android(const MtIdentAndroid &id, const std::string &keys_dir);

#endif
