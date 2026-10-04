/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          What a disk or CD image is.  Everything is read out of the image;
 *          nothing is inferred from its file or folder name, which are often
 *          wrong (a "Ruby" image that runs Ruby 2, a "Keyless" one that is
 *          not).
 *
 *          DOS disks (XL Platinum .. MAXX Emerald 2)
 *              The game executable names its release in plain text:
 *              "PG3004-01 V1.02", "PG3002  V3.06 (12/22/99)".  It is read
 *              from MERIT2\EXEC\MEGACDLL.EXE on the first FAT16 partition.
 *          Linux disks (MAXX Ruby .. Crown)
 *              The MD5 of /usr/local/bin/start identifies the exact build;
 *              failing that /etc/version ("Version: V14_00T040816_1652"),
 *              and failing that the version the game wrote to its own log.
 *          XL CDs (XL 6000, XL Gold)
 *              MEGACDLL.EXE on the disc: "PG3001-00-01 R02 (11/23/98)".  The
 *              I/O board's ROM-DOS starts a disc through LAUNCHIT.BAT, so a
 *              disc without one cannot run on this board ROM.
 *
 *          MAXX releases are numbered in one series across both systems, and
 *          the major version names the release: V3 MAXX .. V9 Emerald 2 on
 *          DOS, V10 Ruby .. V16 Crown on Linux (the restore discs agree:
 *          Ruby disc 1 is labelled 10.07, Sapphire v12-01, Crown v16-10).
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#include "qt_megatouch_ident.hpp"

#include <cstring>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <QFileInfo>

extern "C" {
#include <86box/86box.h>
#include <86box/megatouch_keys.h>
}
#include <cstring>

extern "C" {
#include <86box/megatouch.h>
}

namespace {

/* ---- reading an image ------------------------------------------------- */

class Image {
public:
    explicit Image(const QString &path)
        : f(path)
    {
        ok = f.open(QIODevice::ReadOnly);
    }

    bool       isOpen() const { return ok; }
    qint64     size() const { return f.size(); }
    QByteArray read(quint64 off, qint64 n)
    {
        if (!ok || !f.seek(static_cast<qint64>(off)))
            return {};
        return f.read(n);
    }

private:
    QFile f;
    bool  ok = false;
};

/* Qt 5's fromLatin1(QByteArray) stops at the first NUL; file contents have many. */
inline QString latin(const QByteArray &b)
{
    return QString::fromLatin1(b.constData(), b.size());
}

inline quint16 le16(const QByteArray &b, int o)
{
    return static_cast<quint8>(b[o]) | (static_cast<quint8>(b[o + 1]) << 8);
}

inline quint32 le32(const QByteArray &b, int o)
{
    return le16(b, o) | (static_cast<quint32>(le16(b, o + 2)) << 16);
}

struct Part {
    quint8  type;
    quint64 lba;
};

/* Primary partitions, then the logical ones along the extended chain. */
QVector<Part> partitions(Image &img)
{
    QVector<Part> parts;
    const QByteArray mbr = img.read(0, 512);
    if ((mbr.size() < 512) || (static_cast<quint8>(mbr[510]) != 0x55) || (static_cast<quint8>(mbr[511]) != 0xaa))
        return parts;

    quint64 ext = 0;
    for (int i = 0; i < 4; i++) {
        const int     e    = 446 + 16 * i;
        const quint8  type = static_cast<quint8>(mbr[e + 4]);
        const quint32 lba  = le32(mbr, e + 8);
        if (!type || !lba)
            continue;
        if ((type == 0x05) || (type == 0x0f) || (type == 0x85))
            ext = lba;
        else
            parts.append({ type, lba });
    }

    quint64 cur = ext;
    for (int guard = 0; cur && (guard < 64); guard++) {
        const QByteArray ebr = img.read(cur * 512, 512);
        if (ebr.size() < 512)
            break;
        const quint8 t1 = static_cast<quint8>(ebr[446 + 4]);
        if (t1)
            parts.append({ t1, cur + le32(ebr, 446 + 8) });
        const quint8 t2 = static_cast<quint8>(ebr[462 + 4]);
        cur             = t2 ? (ext + le32(ebr, 462 + 8)) : 0;
    }
    return parts;
}

/* ---- FAT16 ------------------------------------------------------------ */

/* Read a file by path ("MERIT2/EXEC/MEGACDLL.EXE") off a FAT12/16 partition. */
QByteArray fatRead(Image &img, quint64 partLba, const QString &path)
{
    const quint64    base = partLba * 512;
    const QByteArray bs   = img.read(base, 512);
    if (bs.size() < 512)
        return {};

    const quint32 bps     = le16(bs, 11);
    const quint32 spc     = static_cast<quint8>(bs[13]);
    const quint32 rsvd    = le16(bs, 14);
    const quint32 nfats   = static_cast<quint8>(bs[16]);
    const quint32 rootEnt = le16(bs, 17);
    const quint32 fatSz   = le16(bs, 22);
    if ((bps != 512) || !spc || !nfats || !fatSz || !rootEnt)
        return {};

    const quint64 fatOff  = base + rsvd * bps;
    const quint64 rootOff = fatOff + static_cast<quint64>(nfats) * fatSz * bps;
    const quint64 dataOff = rootOff + ((rootEnt * 32 + bps - 1) / bps) * bps;
    const quint32 clBytes = spc * bps;

    auto chain = [&](quint16 first, quint32 limit) {
        QByteArray out;
        quint16    cl = first;
        for (int guard = 0; (cl >= 2) && (cl < 0xfff0) && (guard < 65536); guard++) {
            out += img.read(dataOff + static_cast<quint64>(cl - 2) * clBytes, clBytes);
            if (limit && (static_cast<quint32>(out.size()) >= limit))
                break;
            cl = le16(img.read(fatOff + cl * 2ULL, 2), 0);
        }
        return out;
    };

    QByteArray dir     = img.read(rootOff, rootEnt * 32);
    const auto parts   = path.toUpper().split('/', Qt::SkipEmptyParts);
    for (int p = 0; p < parts.size(); p++) {
        const QString want = parts[p];
        const QString name = want.section('.', 0, 0).leftJustified(8, ' ');
        const QString ext  = want.section('.', 1, 1).leftJustified(3, ' ');
        const QByteArray key = (name + ext).toLatin1();
        bool found = false;

        for (int o = 0; o + 32 <= dir.size(); o += 32) {
            if (dir.at(o) == 0)
                break;
            if ((static_cast<quint8>(dir[o]) == 0xe5) || ((static_cast<quint8>(dir[o + 11]) & 0x0f) == 0x0f))
                continue;
            if (dir.mid(o, 11) != key)
                continue;
            const quint16 cl   = le16(dir, o + 26);
            const quint32 size = le32(dir, o + 28);
            if (p + 1 == parts.size())
                return chain(cl, size).left(size);
            dir   = chain(cl, 0);
            found = true;
            break;
        }
        if (!found)
            return {};
    }
    return {};
}

/* ---- ext2 ------------------------------------------------------------- */

class Ext2 {
public:
    Ext2(Image &img, quint64 partLba)
        : img(img)
        , base(partLba * 512)
    {
        const QByteArray sb = img.read(base + 1024, 1024);
        if ((sb.size() < 1024) || (le16(sb, 56) != 0xef53))
            return;
        bs       = 1024u << le32(sb, 24);
        ipg      = le32(sb, 40);
        firstDb  = le32(sb, 20);
        inodeSz  = (le32(sb, 76) >= 1) ? le16(sb, 88) : 128;
        ok       = (bs >= 1024) && (bs <= 65536) && ipg && inodeSz;
    }

    bool isOpen() const { return ok; }

    /* The file at path, or empty.  limit > 0 reads only its last `limit` bytes. */
    QByteArray readFile(const QString &path, quint32 limit = 0)
    {
        quint32 ino = 2;
        for (const auto &c : path.split('/', Qt::SkipEmptyParts)) {
            ino = lookup(ino, c.toLatin1());
            if (!ino)
                return {};
        }
        return readInode(ino, limit, nullptr);
    }

private:
    QByteArray inode(quint32 n)
    {
        const quint32    group = (n - 1) / ipg;
        const quint32    index = (n - 1) % ipg;
        const QByteArray gd    = img.read(base + static_cast<quint64>(firstDb + 1) * bs + group * 32ULL, 32);
        if (gd.size() < 32)
            return {};
        const quint64 table = le32(gd, 8);
        return img.read(base + table * bs + static_cast<quint64>(index) * inodeSz, 128);
    }

    void collect(quint32 blk, int depth, QVector<quint32> &out, quint32 want)
    {
        if (!blk || (static_cast<quint32>(out.size()) >= want))
            return;
        if (depth == 0) {
            out.append(blk);
            return;
        }
        const QByteArray ptrs = img.read(base + static_cast<quint64>(blk) * bs, bs);
        for (quint32 i = 0; (i + 4 <= static_cast<quint32>(ptrs.size())) && (static_cast<quint32>(out.size()) < want); i += 4)
            collect(le32(ptrs, static_cast<int>(i)), depth - 1, out, want);
    }

    QByteArray readInode(quint32 n, quint32 limit, bool *isDir)
    {
        const QByteArray in = inode(n);
        if (in.size() < 128)
            return {};
        const quint16 mode = le16(in, 0);
        const quint32 size = le32(in, 4);
        if (isDir)
            *isDir = (mode & 0xf000) == 0x4000;
        if (size > (64u << 20))
            return {}; /* nothing identified here is that large */

        const quint32    nblocks = (size + bs - 1) / bs;
        QVector<quint32> blocks;
        for (int i = 0; i < 12; i++)
            collect(le32(in, 40 + 4 * i), 0, blocks, nblocks);
        collect(le32(in, 40 + 48), 1, blocks, nblocks);
        collect(le32(in, 40 + 52), 2, blocks, nblocks);
        collect(le32(in, 40 + 56), 3, blocks, nblocks);

        const quint32 skip = (limit && (size > limit)) ? ((size - limit) / bs) : 0;
        QByteArray    out;
        for (int i = static_cast<int>(skip); i < blocks.size(); i++)
            out += img.read(base + static_cast<quint64>(blocks[i]) * bs, bs);
        return out.left(static_cast<int>(size - skip * bs));
    }

    quint32 lookup(quint32 dirIno, const QByteArray &name)
    {
        bool             isDir = false;
        const QByteArray d     = readInode(dirIno, 0, &isDir);
        if (!isDir)
            return 0;
        for (int o = 0; o + 8 <= d.size();) {
            const quint32 ino  = le32(d, o);
            const quint16 rec  = le16(d, o + 4);
            const quint8  nlen = static_cast<quint8>(d[o + 6]);
            if (rec < 8)
                break;
            if (ino && (nlen == name.size()) && (d.mid(o + 8, nlen) == name))
                return ino;
            o += rec;
        }
        return 0;
    }

    Image  &img;
    quint64 base;
    quint32 bs      = 0;
    quint32 ipg     = 0;
    quint32 firstDb = 0;
    quint32 inodeSz = 0;
    bool    ok      = false;
};

/* ---- ISO 9660 --------------------------------------------------------- */

class Iso {
public:
    explicit Iso(Image &img)
        : img(img)
    {
        const QByteArray pvd = img.read(16 * 2048, 2048);
        ok = (pvd.size() == 2048) && (pvd.at(0) == 1) && (pvd.mid(1, 5) == "CD001");
        if (ok) {
            rootLba  = le32(pvd, 156 + 2);
            rootSize = le32(pvd, 156 + 10);
            volume   = QString::fromLatin1(pvd.mid(40, 32)).trimmed();
        }
    }

    bool    isOpen() const { return ok; }
    QString label() const { return volume; }

    /* Returns false if absent; out gets the file (up to limit bytes). */
    bool read(const QString &path, QByteArray *out = nullptr, quint32 limit = 8u << 20)
    {
        quint32 lba = rootLba, size = rootSize;
        const auto parts = path.toUpper().split('/', Qt::SkipEmptyParts);
        for (int p = 0; p < parts.size(); p++) {
            const QByteArray dir = img.read(lba * 2048ULL, size);
            bool             found = false;
            for (int o = 0; o < dir.size();) {
                const quint8 len = static_cast<quint8>(dir[o]);
                if (!len) {
                    o = ((o / 2048) + 1) * 2048; /* records do not cross sectors */
                    continue;
                }
                const quint8 nlen = static_cast<quint8>(dir[o + 32]);
                QString      name = QString::fromLatin1(dir.mid(o + 33, nlen)).toUpper();
                name              = name.section(';', 0, 0);
                if (name.endsWith('.'))
                    name.chop(1);
                if (name == parts[p]) {
                    lba   = le32(dir, o + 2);
                    size  = le32(dir, o + 10);
                    found = true;
                    break;
                }
                o += len;
            }
            if (!found)
                return false;
        }
        if (out)
            *out = img.read(lba * 2048ULL, qMin(size, limit));
        return true;
    }

private:
    Image  &img;
    bool    ok = false;
    quint32 rootLba = 0, rootSize = 0;
    QString volume;
};

/* ---- releases --------------------------------------------------------- */

struct MaxxRelease {
    const char *name;
    const char *key;
};

/* Indexed by the major version. */
const MaxxRelease maxx_releases[] = {
    { nullptr, nullptr }, { nullptr, nullptr }, { nullptr, nullptr },
    { "MAXX", "M1" },                      /*  3 */
    { "MAXX 2000", "M2K" },                /*  4 */
    { "MAXX 2000 Plus", "M2K" },           /*  5 */
    { "MAXX Diamond", "MDIAMOND" },        /*  6 */
    { "MAXX Double Diamond", "MDIAMOND" }, /*  7 */
    { "MAXX Emerald", "MEMERALD" },        /*  8 */
    { "MAXX Emerald 2", "MEMERALD" },      /*  9 */
    { "MAXX Ruby", "MRUBY" },              /* 10 */
    { "MAXX Ruby 2", "MRUBY2" },           /* 11: no XOR on the password seeds, see keys/ */
    { "MAXX Sapphire", "MSAPPHIRE" },      /* 12 */
    { "MAXX Sapphire 2", "MSAPPHIRE" },    /* 13 */
    { "MAXX Jade", "MJADE" },              /* 14 */
    { "MAXX Jade 2", "MJADE" },            /* 15 */
    { "MAXX Crown", "MCROWN" },            /* 16 */
};
const int maxx_majors = static_cast<int>(sizeof(maxx_releases) / sizeof(maxx_releases[0]));

/* Builds of /usr/local/bin/start seen so far, by MD5. */
struct StartBuild {
    const char *md5;
    int         major;
    const char *version;
};
const StartBuild start_builds[] = {
    { "750c1cf606a392ac5fdde8f08d4e04d6", 11, "V11.00" },
    { "827be041497260c409203344f460084f", 11, "V11.05" },
    { "783edbc0cedc1f640bc608e5c01ed117", 13, "V13.00" },
    { "235774df306e1f6cb73f1af0ba0d4542", 14, "V14.00" },
    { "5efe3b3f2566e5fe78d1629709c20bc0", 14, "V14.21" },
    { "3b8a83c9c017f942bab32f5eb079d1be", 15, "V15.10" },
    { "c59c26a3801d528651023bc4b29d3c15", 16, "V16.00" },
    { "0d77191535e7b6c28938d1028a5fa44a", 16, "V16.10" },
};

void setMaxx(MtIdent &id, int major, const QString &version, MtIdent::Kind kind)
{
    id.kind    = kind;
    id.version = version;
    if ((major > 0) && (major < maxx_majors) && maxx_releases[major].name) {
        id.release   = QString::fromLatin1(maxx_releases[major].name);
        id.keyPrefix = QString::fromLatin1(maxx_releases[major].key);
    } else
        id.release = QStringLiteral("MAXX (V%1)").arg(major);
    /* The DOS releases (to Emerald 2, V9) run on the cabinet's own mix: CS4236B,
       no PCI audio (with the C-Media chip Emerald's DIP tools send it into the
       burn-in suite).  The Linux releases want 64 MB. */
    id.profile = (major <= 9) ? MT_PROFILE_MAXX_OLD : MT_PROFILE_MAXX_NEW;
    /* Diamond (V6) on dial in: TournaMAXX on the DOS releases (MAXX, 2K and 2K
       Plus have no client), Merit's own client on the Linux ones. */
    id.modem = (major >= 6);
    /* MAXX 1st (V3) links cabinets over RS-485 on COM2, as XL does; 2K on
       over Ethernet (the TE-16 cards). */
    id.link485 = (major <= 3);

    if (kind == MtIdent::LinuxDisk)
        id.note = QStringLiteral("Linux releases restart once on first boot or after a hardware change, "
                                 "and a new image starts with touch calibration.");
    if (version.contains(QStringLiteral("V3.02")))
        id.note = QStringLiteral("V3.02's key bit timing is erratic here; its key reads fail (open).");
    if (version.contains(QStringLiteral("V8.05")))
        id.note = QStringLiteral("The only known V8.05 disk had an altered game executable (genuine "
                                 "Emerald keys fail on it); a restored copy runs with the Emerald key.");
}

bool identifyDos(Image &img, const QVector<Part> &parts, MtIdent &id)
{
    static const QRegularExpression re(QStringLiteral("PG(\\d{4})(?:-\\d\\d)?\\s+V(\\d+)\\.(\\d+)"));

    for (const auto &p : parts) {
        if ((p.type != 0x04) && (p.type != 0x06) && (p.type != 0x0e) && (p.type != 0x01))
            continue;
        const QByteArray exe = fatRead(img, p.lba, QStringLiteral("MERIT2/EXEC/MEGACDLL.EXE"));
        if (exe.isEmpty())
            continue;
        const auto m = re.match(latin(exe));
        if (!m.hasMatch())
            continue;

        const int     pg    = m.captured(1).toInt();
        const int     major = m.captured(2).toInt();
        const QString ver   = QStringLiteral("PG%1 V%2.%3").arg(m.captured(1), m.captured(2), m.captured(3));
        id.evidence         = QStringLiteral("MERIT2\\EXEC\\MEGACDLL.EXE");

        if (pg == 3004) { /* XL hard-disk generation */
            id.kind      = MtIdent::DosDisk;
            id.version   = ver;
            id.profile   = MT_PROFILE_XL_HDD;
            id.release   = (major <= 2) ? QStringLiteral("XL Platinum")
                         : (major == 3) ? QStringLiteral("XL Titanium")
                                        : QStringLiteral("XL Titanium 2");
            id.keyPrefix = (major <= 2) ? QStringLiteral("XPLAT") : QStringLiteral("XTIT");
            return true;
        }
        if (pg == 3002) {
            setMaxx(id, major, ver, MtIdent::DosDisk);
            return true;
        }
        id.kind    = MtIdent::Unknown;
        id.version = ver;
        id.note    = QStringLiteral("Unrecognised part number PG%1.").arg(pg);
        return true;
    }
    return false;
}

bool identifyLinux(Image &img, const QVector<Part> &parts, MtIdent &id)
{
    static const QRegularExpression etcRe(QStringLiteral("V(\\d+)_(\\d+)T"));
    static const QRegularExpression logRe(QStringLiteral("PG3002\\s+V(\\d+)\\.(\\d+)"));
    bool anyExt = false;

    /* 1. The game binary, by MD5. */
    for (const auto &p : parts) {
        if (p.type != 0x83)
            continue;
        Ext2 fs(img, p.lba);
        if (!fs.isOpen())
            continue;
        anyExt = true;
        const QByteArray start = fs.readFile(QStringLiteral("/usr/local/bin/start"));
        if (start.isEmpty())
            continue;
        const QString md5 = QString::fromLatin1(QCryptographicHash::hash(start, QCryptographicHash::Md5).toHex());
        for (const auto &b : start_builds) {
            if (md5 == QLatin1String(b.md5)) {
                setMaxx(id, b.major, QString::fromLatin1(b.version), MtIdent::LinuxDisk);
                id.evidence = QStringLiteral("MD5 of /usr/local/bin/start (%1)").arg(md5);
                return true;
            }
        }
    }
    if (!anyExt)
        return false;

    /* 2. /etc/version. */
    for (const auto &p : parts) {
        if (p.type != 0x83)
            continue;
        Ext2 fs(img, p.lba);
        const auto m = etcRe.match(latin(fs.readFile(QStringLiteral("/etc/version"))));
        if (m.hasMatch()) {
            setMaxx(id, m.captured(1).toInt(), QStringLiteral("V%1.%2").arg(m.captured(1), m.captured(2)),
                    MtIdent::LinuxDisk);
            id.evidence = QStringLiteral("/etc/version (game binary not recognised)");
            return true;
        }
    }

    /* 3. The version the game logged when it last ran (the tail of the log). */
    for (const auto &p : parts) {
        if (p.type != 0x83)
            continue;
        Ext2       fs(img, p.lba);
        const auto m = logRe.match(latin(fs.readFile(QStringLiteral("/merit/log"), 256 * 1024)));
        if (m.hasMatch()) {
            setMaxx(id, m.captured(1).toInt(), QStringLiteral("PG3002 V%1.%2").arg(m.captured(1), m.captured(2)),
                    MtIdent::LinuxDisk);
            id.evidence = QStringLiteral("the game's log (/var/merit/log)");
            return true;
        }
    }

    id.note = QStringLiteral("A Linux disk, but no Megatouch release was found on it.");
    return true;
}

void identifyIso(Image &img, MtIdent &id)
{
    Iso iso(img);
    static const QRegularExpression re(QStringLiteral("PG3001-(\\d\\d)-\\d\\d\\s+R(\\w\\w)\\s*(\\(\\d\\d/\\d\\d/\\d\\d\\))?"));
    QByteArray exe;

    if (iso.read(QStringLiteral("MEGACDLL.EXE"), &exe) || iso.read(QStringLiteral("EXEC/MEGACDLL.EXE"), &exe)) {
        const auto m = re.match(latin(exe));
        if (m.hasMatch()) {
            const int series = m.captured(1).toInt();
            id.release       = (series == 0) ? QStringLiteral("XL 6000") : QStringLiteral("XL Gold");
            id.version       = QStringLiteral("PG3001-%1 R%2 %3").arg(m.captured(1), m.captured(2), m.captured(3)).trimmed();
            id.evidence      = QStringLiteral("MEGACDLL.EXE on the disc");
            id.profile       = MT_PROFILE_XL_CD;
            id.keyPrefix     = (series == 0) ? QStringLiteral("XL6K") : QStringLiteral("XGOLDCD");
            if (!iso.read(QStringLiteral("LAUNCHIT.BAT"))) {
                id.kind = MtIdent::NotRunnable;
                id.note = QStringLiteral("No LAUNCHIT.BAT: the I/O board's ROM-DOS cannot start this disc.");
            } else {
                id.kind = MtIdent::XlCd;
            }
            return;
        }
        /* XL R0-R3 ("R1  (02/19/97)") and Super 5000 ("R5B   (02/11/98)"): the
           earlier U12 ROM starts MEGACDLL.EXE itself. */
        static const QRegularExpression early(QStringLiteral("R(\\d\\w?)\\s+\\((\\d\\d/\\d\\d/\\d\\d)\\)"));
        const auto e   = early.match(latin(exe));
        id.release     = QStringLiteral("Megatouch XL (early)");
        id.evidence    = QStringLiteral("MEGACDLL.EXE on the disc");
        if (!e.hasMatch()) {
            id.kind = MtIdent::NotRunnable;
            id.note = QStringLiteral("An earlier XL disc without a version string.");
            return;
        }
        const bool s5k = e.captured(1).startsWith(QLatin1Char('5'));
        id.kind        = MtIdent::XlCd;
        id.release     = s5k ? QStringLiteral("XL Super 5000") : QStringLiteral("XL");
        id.version     = QStringLiteral("R%1 (%2)").arg(e.captured(1), e.captured(2));
        id.profile     = MT_PROFILE_XL_CD_EARLY;
        id.keyPrefix   = s5k ? QStringLiteral("XL5K") : QStringLiteral("XLR1");
        return;
    }

    QByteArray diskId;
    if (iso.read(QStringLiteral("DISK.ID"), &diskId, 256)) {
        id.kind    = MtIdent::NotRunnable;
        id.release = QString::fromLatin1(diskId).section('\n', 0, 0).trimmed();
        id.note    = QStringLiteral("A MAXX restore or upgrade disc: it installs onto a disk, it does not run.");
        return;
    }
    if (iso.read(QStringLiteral("ISOLINUX")) || iso.read(QStringLiteral("RESCUECD"))) {
        id.kind    = MtIdent::NotRunnable;
        id.release = QStringLiteral("Linux MAXX restore disc (%1)").arg(iso.label());
        id.note    = QStringLiteral("A restore disc: it installs onto a disk, it does not run.");
        return;
    }
    id.note = QStringLiteral("Not a Megatouch disc.");
}

} // namespace

MtIdent
mt_identify(const QString &path)
{
    MtIdent id;
    Image   img(path);
    if (!img.isOpen()) {
        id.note = QStringLiteral("Cannot be opened.");
        return id;
    }

    if (Iso(img).isOpen()) {
        identifyIso(img, id);
        return id;
    }

    const auto parts = partitions(img);
    if (parts.isEmpty()) {
        id.note = path.endsWith(QStringLiteral(".iso"), Qt::CaseInsensitive)
                      ? QStringLiteral("Not an ISO 9660 image (a raw or UDF disc image?).")
                      : QStringLiteral("No partition table: not a disk image.");
        return id;
    }
    if (identifyDos(img, parts, id) || identifyLinux(img, parts, id))
        return id;

    id.note = QStringLiteral("No Megatouch release found on this disk.");
    return id;
}

QString
mt_keys_dir()
{
    return QDir(QString::fromUtf8(usr_path)).filePath("keys");
}

QString
mt_own_key_ref(const QString &file_name)
{
    /* Relative to the cabinet folder, so the folder can move. */
    return QDir::toNativeSeparators(QStringLiteral("keys/") + file_name);
}

namespace {

/* The dumps in the keys folder (264-byte DS1991 or 192-byte DS1205), by name. */
QFileInfoList
own_keys()
{
    QFileInfoList list;
    for (const QFileInfo &fi : QDir(mt_keys_dir()).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase))
        if (mt_key_kind((size_t) fi.size()))
            list.append(fi);
    return list;
}

bool
names_family(const QString &file_name, const QString &prefix)
{
    return file_name.startsWith(prefix + "_full_", Qt::CaseInsensitive) ||
           file_name.startsWith(prefix + "_multikey_", Qt::CaseInsensitive);
}

} // namespace

QString
mt_default_key(const MtIdent &id)
{
    if (id.keyPrefix.isEmpty())
        return {};

    /* A built-in key first (a local keys.txt lists the dump known to work
       with each release first), then the user's own, imported dumps.  DS1991
       dumps are <prefix>_full_<ROM ID>; DS1205 MultiKeys
       <prefix>_multikey_<part>. */
    for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++)
        if (names_family(QString::fromLatin1(k->id), id.keyPrefix))
            return QStringLiteral(MT_BUILTIN_PREFIX) + QString::fromLatin1(k->id);
    for (const QFileInfo &fi : own_keys())
        if (names_family(fi.fileName(), id.keyPrefix))
            return mt_own_key_ref(fi.fileName());
    return {};
}

QList<MtKeyChoice>
mt_key_choices()
{
    QList<MtKeyChoice> list;
    for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++)
        list.append({ QStringLiteral(MT_BUILTIN_PREFIX) + QString::fromLatin1(k->id), QString::fromUtf8(k->name) });
    for (const QFileInfo &fi : own_keys())
        list.append({ mt_own_key_ref(fi.fileName()), mt_key_display(fi.fileName()) });
    return list;
}

QString
mt_key_display(const QString &ref)
{
    if (ref.isEmpty())
        return {};
    if (const mt_builtin_key_t *k = mt_builtin_key_find(ref.toUtf8().constData()))
        return QString::fromUtf8(k->name);
    /* An imported dump: its releases, and which key it is. */
    const QString name = QFileInfo(QString(ref).replace('\\', '/')).fileName();
    if (const mt_key_family_t *f = mt_key_family_from_name(name.toUtf8().constData())) {
        QString which = name.mid((int) strlen(f->prefix) + 1);
        which.remove(QRegularExpression(QStringLiteral("^(full|multikey)_")));
        return QString("%1 (%2)").arg(QString::fromUtf8(f->releases), which);
    }
    return name;
}
