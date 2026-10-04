/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          What a disk or CD image is: which Megatouch release, which
 *          hardware profile runs it, and which key it wants.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#ifndef QT_MEGATOUCH_IDENT_HPP
#define QT_MEGATOUCH_IDENT_HPP

#include <QString>

struct MtIdent {
    enum Kind {
        Unknown,   /* not recognised: no profile is guessed               */
        DosDisk,   /* DOS release on a hard disk image                    */
        LinuxDisk, /* Linux MAXX on a hard disk image                     */
        XlCd,      /* XL CD generation: runs from the disc                */
        NotRunnable /* a restore set or an older XL disc: skipped          */
    };

    Kind    kind    = Unknown;
    QString release;  /* "MAXX Emerald 2"                                  */
    QString version;  /* as the image states it: "PG3002 V9.01"            */
    QString evidence; /* where that came from: "MERIT2\EXEC\MEGACDLL.EXE"  */
    int     profile = -1;
    QString keyPrefix; /* keys\<prefix>_full_*: "MEMERALD"; empty = none   */
    QString note;     /* anything the user should know                     */
    bool    modem = false; /* has an on-line client (TournaMAXX on the DOS
                              releases, Merit's own on Linux): the modem
                              option starts on for it                      */
    bool    link485 = false; /* links over RS-485 on COM2 (MAXX 1st; XL by
                                its profile) rather than Ethernet           */

    bool runnable() const { return (kind == DosDisk) || (kind == LinuxDisk) || (kind == XlCd); }
};

/* Look at an image file (.img / .iso).  Reads only what it needs; never writes. */
MtIdent mt_identify(const QString &path);

/* Security keys.  A key is named by a reference: "builtin:<id>" for a dump
   built into the executable, else a path (relative to the cabinet folder or
   absolute) to the user's own dump. */
struct MtKeyChoice {
    QString ref;  /* what goes into the configuration              */
    QString name; /* what the user sees: "MAXX Jade (USA) 1"        */
};

/* The built-in keys (release and territory), then any dumps in the cabinet
   folder's keys\ (264-byte DS1991 or 192-byte DS1205). */
QList<MtKeyChoice> mt_key_choices();

/* How a key reference is shown: the built-in's name, else the file name. */
QString mt_key_display(const QString &ref);

/* The key that suits the identification: a built-in one ("builtin:<id>"),
   else an imported dump in the keys folder (keys/<prefix>_full_...), or
   empty. */
QString mt_default_key(const MtIdent &id);

/* The cabinet folder's keys folder, where imported dumps go, and the
   reference to one of them. */
QString mt_keys_dir();
QString mt_own_key_ref(const QString &file_name);

#endif
