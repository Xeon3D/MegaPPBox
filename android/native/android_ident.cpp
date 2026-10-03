/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          What a disk or CD image is, for the Android Machine Manager: the
 *          desktop's src/qt/qt_megatouch_ident.cpp in plain C++ (no Qt), same
 *          rules, same results.  Everything is read out of the image; nothing
 *          is inferred from its file or folder name.
 *
 *          DOS disks: "PG3004-01 V1.02" / "PG3002  V3.06" in
 *              MERIT2\EXEC\MEGACDLL.EXE on the first FAT16 partition.
 *          Linux disks: the MD5 of /usr/local/bin/start, else /etc/version,
 *              else the version the game wrote to its own log.
 *          XL CDs: MEGACDLL.EXE on the disc ("PG3001-00-01 R02 (11/23/98)").
 *
 *          The regular expressions of the Qt version are hand-written matchers
 *          here (std::regex is slow on multi-megabyte executables).
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#include "android_ident.hpp"

#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include <86box/86box.h>
#include <86box/megatouch.h>
#include <86box/megatouch_keys.h>
}

namespace {

using Bytes = std::string; /* raw bytes; std::string holds NULs fine */

/* ---- reading an image ------------------------------------------------- */

class Image {
public:
    explicit Image(const std::string &path) { f = fopen(path.c_str(), "rb"); }
    ~Image()
    {
        if (f)
            fclose(f);
    }
    bool  isOpen() const { return f != nullptr; }
    Bytes read(uint64_t off, uint64_t n)
    {
        Bytes out;
        if (!f || (fseeko(f, (off_t) off, SEEK_SET) != 0))
            return out;
        out.resize((size_t) n);
        const size_t got = fread(&out[0], 1, (size_t) n, f);
        out.resize(got);
        return out;
    }

private:
    FILE *f = nullptr;
};

inline uint16_t le16(const Bytes &b, size_t o)
{
    return (uint16_t) ((uint8_t) b[o] | ((uint8_t) b[o + 1] << 8));
}

inline uint32_t le32(const Bytes &b, size_t o)
{
    return le16(b, o) | ((uint32_t) le16(b, o + 2) << 16);
}

std::string upper(std::string s)
{
    for (auto &c : s)
        c = (char) toupper((unsigned char) c);
    return s;
}

std::vector<std::string> splitPath(const std::string &path)
{
    std::vector<std::string> parts;
    std::string              cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty())
                parts.push_back(cur);
            cur.clear();
        } else
            cur += c;
    }
    if (!cur.empty())
        parts.push_back(cur);
    return parts;
}

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while ((a < b) && isspace((unsigned char) s[a]))
        a++;
    while ((b > a) && (isspace((unsigned char) s[b - 1]) || (s[b - 1] == '\0')))
        b--;
    return s.substr(a, b - a);
}

struct Part {
    uint8_t  type;
    uint64_t lba;
};

/* Primary partitions, then the logical ones along the extended chain. */
std::vector<Part> partitions(Image &img)
{
    std::vector<Part> parts;
    const Bytes       mbr = img.read(0, 512);
    if ((mbr.size() < 512) || ((uint8_t) mbr[510] != 0x55) || ((uint8_t) mbr[511] != 0xaa))
        return parts;

    uint64_t ext = 0;
    for (int i = 0; i < 4; i++) {
        const size_t   e    = 446 + 16 * i;
        const uint8_t  type = (uint8_t) mbr[e + 4];
        const uint32_t lba  = le32(mbr, e + 8);
        if (!type || !lba)
            continue;
        if ((type == 0x05) || (type == 0x0f) || (type == 0x85))
            ext = lba;
        else
            parts.push_back({ type, lba });
    }

    uint64_t cur = ext;
    for (int guard = 0; cur && (guard < 64); guard++) {
        const Bytes ebr = img.read(cur * 512, 512);
        if (ebr.size() < 512)
            break;
        const uint8_t t1 = (uint8_t) ebr[446 + 4];
        if (t1)
            parts.push_back({ t1, cur + le32(ebr, 446 + 8) });
        const uint8_t t2 = (uint8_t) ebr[462 + 4];
        cur              = t2 ? (ext + le32(ebr, 462 + 8)) : 0;
    }
    return parts;
}

/* ---- FAT16 ------------------------------------------------------------ */

/* Read a file by path ("MERIT2/EXEC/MEGACDLL.EXE") off a FAT12/16 partition. */
Bytes fatRead(Image &img, uint64_t partLba, const std::string &path)
{
    const uint64_t base = partLba * 512;
    const Bytes    bs   = img.read(base, 512);
    if (bs.size() < 512)
        return {};

    const uint32_t bps     = le16(bs, 11);
    const uint32_t spc     = (uint8_t) bs[13];
    const uint32_t rsvd    = le16(bs, 14);
    const uint32_t nfats   = (uint8_t) bs[16];
    const uint32_t rootEnt = le16(bs, 17);
    const uint32_t fatSz   = le16(bs, 22);
    if ((bps != 512) || !spc || !nfats || !fatSz || !rootEnt)
        return {};

    const uint64_t fatOff  = base + (uint64_t) rsvd * bps;
    const uint64_t rootOff = fatOff + (uint64_t) nfats * fatSz * bps;
    const uint64_t dataOff = rootOff + ((rootEnt * 32 + bps - 1) / bps) * bps;
    const uint32_t clBytes = spc * bps;

    auto chain = [&](uint16_t first, uint32_t limit) {
        Bytes    out;
        uint16_t cl = first;
        for (int guard = 0; (cl >= 2) && (cl < 0xfff0) && (guard < 65536); guard++) {
            out += img.read(dataOff + (uint64_t) (cl - 2) * clBytes, clBytes);
            if (limit && (out.size() >= limit))
                break;
            const Bytes fe = img.read(fatOff + cl * 2ULL, 2);
            if (fe.size() < 2)
                break;
            cl = le16(fe, 0);
        }
        return out;
    };

    Bytes      dir   = img.read(rootOff, rootEnt * 32);
    const auto parts = splitPath(upper(path));
    for (size_t p = 0; p < parts.size(); p++) {
        const std::string &want = parts[p];
        const size_t       dot  = want.find('.');
        std::string        name = want.substr(0, dot);
        std::string        ext  = (dot == std::string::npos) ? "" : want.substr(dot + 1);
        name.resize(8, ' ');
        ext.resize(3, ' ');
        const std::string key   = name + ext;
        bool              found = false;

        for (size_t o = 0; o + 32 <= dir.size(); o += 32) {
            if (dir[o] == 0)
                break;
            if (((uint8_t) dir[o] == 0xe5) || (((uint8_t) dir[o + 11] & 0x0f) == 0x0f))
                continue;
            if (dir.compare(o, 11, key) != 0)
                continue;
            const uint16_t cl   = le16(dir, o + 26);
            const uint32_t size = le32(dir, o + 28);
            if (p + 1 == parts.size())
                return chain(cl, size).substr(0, size);
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
    Ext2(Image &img, uint64_t partLba)
        : img(img)
        , base(partLba * 512)
    {
        const Bytes sb = img.read(base + 1024, 1024);
        if ((sb.size() < 1024) || (le16(sb, 56) != 0xef53))
            return;
        bs      = 1024u << le32(sb, 24);
        ipg     = le32(sb, 40);
        firstDb = le32(sb, 20);
        inodeSz = (le32(sb, 76) >= 1) ? le16(sb, 88) : 128;
        ok      = (bs >= 1024) && (bs <= 65536) && ipg && inodeSz;
    }

    bool isOpen() const { return ok; }

    /* The file at path, or empty.  limit > 0 reads only its last `limit` bytes. */
    Bytes readFile(const std::string &path, uint32_t limit = 0)
    {
        uint32_t ino = 2;
        for (const auto &c : splitPath(path)) {
            ino = lookup(ino, c);
            if (!ino)
                return {};
        }
        return readInode(ino, limit, nullptr);
    }

private:
    Bytes inode(uint32_t n)
    {
        const uint32_t group = (n - 1) / ipg;
        const uint32_t index = (n - 1) % ipg;
        const Bytes    gd    = img.read(base + (uint64_t) (firstDb + 1) * bs + group * 32ULL, 32);
        if (gd.size() < 32)
            return {};
        const uint64_t table = le32(gd, 8);
        return img.read(base + table * bs + (uint64_t) index * inodeSz, 128);
    }

    void collect(uint32_t blk, int depth, std::vector<uint32_t> &out, uint32_t want)
    {
        if (!blk || (out.size() >= want))
            return;
        if (depth == 0) {
            out.push_back(blk);
            return;
        }
        const Bytes ptrs = img.read(base + (uint64_t) blk * bs, bs);
        for (size_t i = 0; (i + 4 <= ptrs.size()) && (out.size() < want); i += 4)
            collect(le32(ptrs, i), depth - 1, out, want);
    }

    Bytes readInode(uint32_t n, uint32_t limit, bool *isDir)
    {
        const Bytes in = inode(n);
        if (in.size() < 128)
            return {};
        const uint16_t mode = le16(in, 0);
        const uint32_t size = le32(in, 4);
        if (isDir)
            *isDir = (mode & 0xf000) == 0x4000;
        if (size > (64u << 20))
            return {}; /* nothing identified here is that large */

        const uint32_t        nblocks = (size + bs - 1) / bs;
        std::vector<uint32_t> blocks;
        for (int i = 0; i < 12; i++)
            collect(le32(in, 40 + 4 * i), 0, blocks, nblocks);
        collect(le32(in, 40 + 48), 1, blocks, nblocks);
        collect(le32(in, 40 + 52), 2, blocks, nblocks);
        collect(le32(in, 40 + 56), 3, blocks, nblocks);

        const uint32_t skip = (limit && (size > limit)) ? ((size - limit) / bs) : 0;
        Bytes          out;
        for (size_t i = skip; i < blocks.size(); i++)
            out += img.read(base + (uint64_t) blocks[i] * bs, bs);
        return out.substr(0, size - skip * bs);
    }

    uint32_t lookup(uint32_t dirIno, const std::string &name)
    {
        bool        isDir = false;
        const Bytes d     = readInode(dirIno, 0, &isDir);
        if (!isDir)
            return 0;
        for (size_t o = 0; o + 8 <= d.size();) {
            const uint32_t ino  = le32(d, o);
            const uint16_t rec  = le16(d, o + 4);
            const uint8_t  nlen = (uint8_t) d[o + 6];
            if (rec < 8)
                break;
            if (ino && (nlen == name.size()) && (d.compare(o + 8, nlen, name) == 0))
                return ino;
            o += rec;
        }
        return 0;
    }

    Image   &img;
    uint64_t base;
    uint32_t bs      = 0;
    uint32_t ipg     = 0;
    uint32_t firstDb = 0;
    uint32_t inodeSz = 0;
    bool     ok      = false;
};

/* ---- ISO 9660 --------------------------------------------------------- */

class Iso {
public:
    explicit Iso(Image &img)
        : img(img)
    {
        const Bytes pvd = img.read(16 * 2048, 2048);
        ok              = (pvd.size() == 2048) && (pvd[0] == 1) && (pvd.compare(1, 5, "CD001") == 0);
        if (ok) {
            rootLba  = le32(pvd, 156 + 2);
            rootSize = le32(pvd, 156 + 10);
            volume   = trim(pvd.substr(40, 32));
        }
    }

    bool        isOpen() const { return ok; }
    std::string label() const { return volume; }

    /* Returns false if absent; out gets the file (up to limit bytes). */
    bool read(const std::string &path, Bytes *out = nullptr, uint32_t limit = 8u << 20)
    {
        uint32_t   lba = rootLba, size = rootSize;
        const auto parts = splitPath(upper(path));
        for (size_t p = 0; p < parts.size(); p++) {
            const Bytes dir   = img.read(lba * 2048ULL, size);
            bool        found = false;
            for (size_t o = 0; o < dir.size();) {
                const uint8_t len = (uint8_t) dir[o];
                if (!len) {
                    o = ((o / 2048) + 1) * 2048; /* records do not cross sectors */
                    continue;
                }
                if (o + 33 > dir.size())
                    break;
                const uint8_t nlen = (uint8_t) dir[o + 32];
                std::string   name = upper(dir.substr(o + 33, nlen));
                name               = name.substr(0, name.find(';'));
                if (!name.empty() && (name.back() == '.'))
                    name.pop_back();
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
            *out = img.read(lba * 2048ULL, std::min(size, limit));
        return true;
    }

private:
    Image      &img;
    bool        ok      = false;
    uint32_t    rootLba = 0, rootSize = 0;
    std::string volume;
};

/* ---- MD5 (RFC 1321) ---------------------------------------------------- */

std::string md5hex(const Bytes &msg)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    static const uint8_t R[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };

    Bytes          m    = msg;
    const uint64_t bits = (uint64_t) msg.size() * 8;
    m += (char) 0x80;
    while ((m.size() % 64) != 56)
        m += (char) 0;
    for (int i = 0; i < 8; i++)
        m += (char) ((bits >> (8 * i)) & 0xff);

    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = le32(m, off + 4 * i);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int      g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const uint32_t t = d;
            d                = c;
            c                = b;
            const uint32_t x = a + f + K[i] + w[g];
            b                = b + ((x << R[i]) | (x >> (32 - R[i])));
            a                = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }

    static const char *hex = "0123456789abcdef";
    std::string        out;
    for (uint32_t v : h)
        for (int i = 0; i < 4; i++) {
            const uint8_t byte = (v >> (8 * i)) & 0xff;
            out += hex[byte >> 4];
            out += hex[byte & 15];
        }
    return out;
}

/* ---- the version strings (the Qt version's regular expressions) --------- */

/* Digits at s[i..]: how many (at most max), appending them to out. */
size_t digits(const Bytes &s, size_t i, size_t max, std::string *out)
{
    size_t n = 0;
    while ((i + n < s.size()) && (n < max) && isdigit((unsigned char) s[i + n])) {
        if (out)
            *out += s[i + n];
        n++;
    }
    return n;
}

size_t spaces(const Bytes &s, size_t i)
{
    size_t n = 0;
    while ((i + n < s.size()) && isspace((unsigned char) s[i + n]))
        n++;
    return n;
}

/* PG(\d{4})(?:-\d\d)?\s+V(\d+)\.(\d+) */
bool matchPg(const Bytes &s, std::string &pg, std::string &maj, std::string &min)
{
    for (size_t at = s.find("PG"); at != std::string::npos; at = s.find("PG", at + 1)) {
        std::string p, a, b;
        size_t      i = at + 2;
        if (digits(s, i, 4, &p) != 4)
            continue;
        i += 4;
        if ((i + 3 <= s.size()) && (s[i] == '-') && isdigit((unsigned char) s[i + 1]) && isdigit((unsigned char) s[i + 2]))
            i += 3;
        const size_t sp = spaces(s, i);
        if (!sp)
            continue;
        i += sp;
        if ((i >= s.size()) || (s[i] != 'V'))
            continue;
        i++;
        const size_t na = digits(s, i, 10, &a);
        if (!na || (i + na >= s.size()) || (s[i + na] != '.'))
            continue;
        i += na + 1;
        if (!digits(s, i, 10, &b))
            continue;
        pg  = p;
        maj = a;
        min = b;
        return true;
    }
    return false;
}

/* V(\d+)_(\d+)T */
bool matchEtcVersion(const Bytes &s, std::string &maj, std::string &min)
{
    for (size_t at = s.find('V'); at != std::string::npos; at = s.find('V', at + 1)) {
        std::string a, b;
        size_t      i  = at + 1;
        const size_t na = digits(s, i, 10, &a);
        if (!na || (i + na >= s.size()) || (s[i + na] != '_'))
            continue;
        i += na + 1;
        const size_t nb = digits(s, i, 10, &b);
        if (!nb || (i + nb >= s.size()) || (s[i + nb] != 'T'))
            continue;
        maj = a;
        min = b;
        return true;
    }
    return false;
}

/* PG3002\s+V(\d+)\.(\d+): the last one in the log (its tail is what is read). */
bool matchLogVersion(const Bytes &s, std::string &maj, std::string &min)
{
    bool found = false;
    for (size_t at = s.find("PG3002"); at != std::string::npos; at = s.find("PG3002", at + 1)) {
        std::string a, b;
        size_t      i  = at + 6;
        const size_t sp = spaces(s, i);
        if (!sp)
            continue;
        i += sp;
        if ((i >= s.size()) || (s[i] != 'V'))
            continue;
        i++;
        const size_t na = digits(s, i, 10, &a);
        if (!na || (i + na >= s.size()) || (s[i + na] != '.'))
            continue;
        i += na + 1;
        if (!digits(s, i, 10, &b))
            continue;
        maj   = a;
        min   = b;
        found = true;
        break; /* the Qt version takes the first match */
    }
    return found;
}

/* PG3001-(\d\d)-\d\d\s+R(\w\w)\s*(\(\d\d/\d\d/\d\d\))? */
bool matchCd(const Bytes &s, std::string &series, std::string &rev, std::string &date)
{
    for (size_t at = s.find("PG3001-"); at != std::string::npos; at = s.find("PG3001-", at + 1)) {
        std::string se;
        size_t      i = at + 7;
        if (digits(s, i, 2, &se) != 2)
            continue;
        i += 2;
        if ((i + 3 > s.size()) || (s[i] != '-') || (digits(s, i + 1, 2, nullptr) != 2))
            continue;
        i += 3;
        const size_t sp = spaces(s, i);
        if (!sp)
            continue;
        i += sp;
        if ((i + 3 > s.size()) || (s[i] != 'R') || !(isalnum((unsigned char) s[i + 1]) || (s[i + 1] == '_'))
            || !(isalnum((unsigned char) s[i + 2]) || (s[i + 2] == '_')))
            continue;
        const std::string r = s.substr(i + 1, 2);
        i += 3;
        i += spaces(s, i);
        std::string d;
        if ((i + 10 <= s.size()) && (s[i] == '(') && (s[i + 3] == '/') && (s[i + 6] == '/') && (s[i + 9] == ')')
            && (digits(s, i + 1, 2, nullptr) == 2) && (digits(s, i + 4, 2, nullptr) == 2) && (digits(s, i + 7, 2, nullptr) == 2))
            d = s.substr(i, 10);
        series = se;
        rev    = r;
        date   = d;
        return true;
    }
    return false;
}

/* R(\d\w?)\s+\((\d\d/\d\d/\d\d)\) */
bool matchEarlyCd(const Bytes &s, std::string &rev, std::string &date)
{
    for (size_t at = s.find('R'); at != std::string::npos; at = s.find('R', at + 1)) {
        size_t i = at + 1;
        if ((i >= s.size()) || !isdigit((unsigned char) s[i]))
            continue;
        std::string r(1, s[i]);
        i++;
        if ((i < s.size()) && (isalnum((unsigned char) s[i]) || (s[i] == '_')) && !isspace((unsigned char) s[i])) {
            /* \w? is greedy: take it, unless what follows then fails (below) */
            const size_t sp2 = spaces(s, i + 1);
            if (sp2 && (i + 1 + sp2 < s.size()) && (s[i + 1 + sp2] == '(')) {
                r += s[i];
                i++;
            }
        }
        const size_t sp = spaces(s, i);
        if (!sp)
            continue;
        i += sp;
        if ((i + 10 <= s.size()) && (s[i] == '(') && (s[i + 3] == '/') && (s[i + 6] == '/') && (s[i + 9] == ')')
            && (digits(s, i + 1, 2, nullptr) == 2) && (digits(s, i + 4, 2, nullptr) == 2) && (digits(s, i + 7, 2, nullptr) == 2)) {
            rev  = r;
            date = s.substr(i + 1, 8);
            return true;
        }
    }
    return false;
}

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
    { "MAXX Ruby 2", "MRUBY2" },           /* 11 */
    { "MAXX Sapphire", "MSAPPHIRE" },      /* 12 */
    { "MAXX Sapphire 2", "MSAPPHIRE" },    /* 13 */
    { "MAXX Jade", "MJADE" },              /* 14 */
    { "MAXX Jade 2", "MJADE" },            /* 15 */
    { "MAXX Crown", "MCROWN" },            /* 16 */
};
const int maxx_majors = (int) (sizeof(maxx_releases) / sizeof(maxx_releases[0]));

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

void setMaxx(MtIdentAndroid &id, int major, const std::string &version, int kind)
{
    id.kind    = kind;
    id.version = version;
    if ((major > 0) && (major < maxx_majors) && maxx_releases[major].name) {
        id.release   = maxx_releases[major].name;
        id.keyPrefix = maxx_releases[major].key;
    } else
        id.release = "MAXX (V" + std::to_string(major) + ")";
    /* DOS releases (to Emerald 2, V9) on the cabinet's own mix; Linux ones want 64 MB. */
    id.profile = (major <= 9) ? MT_PROFILE_MAXX_OLD : MT_PROFILE_MAXX_NEW;
    /* Diamond (V6) on dial in: TournaMAXX on the DOS releases (MAXX, 2K and 2K
       Plus have no client), Merit's own client on the Linux ones. */
    id.modem = (major >= 6);

    if (kind == MtIdentAndroid::LinuxDisk)
        id.note = "Linux releases restart once on first boot or after a hardware change, "
                  "and a new image starts with touch calibration.";
    if (version.find("V3.02") != std::string::npos)
        id.note = "V3.02's key bit timing is erratic here; its key reads fail (open).";
    if (version.find("V8.05") != std::string::npos)
        id.note = "The only known V8.05 disk had an altered game executable (genuine Emerald keys fail on it); "
                  "a restored copy runs with the Emerald key.";
}

bool identifyDos(Image &img, const std::vector<Part> &parts, MtIdentAndroid &id)
{
    for (const auto &p : parts) {
        if ((p.type != 0x04) && (p.type != 0x06) && (p.type != 0x0e) && (p.type != 0x01))
            continue;
        const Bytes exe = fatRead(img, p.lba, "MERIT2/EXEC/MEGACDLL.EXE");
        if (exe.empty())
            continue;
        std::string pgs, majs, mins;
        if (!matchPg(exe, pgs, majs, mins))
            continue;

        const int         pg    = atoi(pgs.c_str());
        const int         major = atoi(majs.c_str());
        const std::string ver   = "PG" + pgs + " V" + majs + "." + mins;
        id.evidence             = "MERIT2\\EXEC\\MEGACDLL.EXE";

        if (pg == 3004) { /* XL hard-disk generation */
            id.kind      = MtIdentAndroid::DosDisk;
            id.version   = ver;
            id.profile   = MT_PROFILE_XL_HDD;
            id.release   = (major <= 2) ? "XL Platinum" : ((major == 3) ? "XL Titanium" : "XL Titanium 2");
            id.keyPrefix = (major <= 2) ? "XPLAT" : "XTIT";
            return true;
        }
        if (pg == 3002) {
            setMaxx(id, major, ver, MtIdentAndroid::DosDisk);
            return true;
        }
        id.kind    = MtIdentAndroid::Unknown;
        id.version = ver;
        id.note    = "Unrecognised part number PG" + pgs + ".";
        return true;
    }
    return false;
}

bool identifyLinux(Image &img, const std::vector<Part> &parts, MtIdentAndroid &id)
{
    bool anyExt = false;

    /* 1. The game binary, by MD5. */
    for (const auto &p : parts) {
        if (p.type != 0x83)
            continue;
        Ext2 fs(img, p.lba);
        if (!fs.isOpen())
            continue;
        anyExt            = true;
        const Bytes start = fs.readFile("/usr/local/bin/start");
        if (start.empty())
            continue;
        const std::string md5 = md5hex(start);
        for (const auto &b : start_builds) {
            if (md5 == b.md5) {
                setMaxx(id, b.major, b.version, MtIdentAndroid::LinuxDisk);
                id.evidence = "MD5 of /usr/local/bin/start (" + md5 + ")";
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
        Ext2        fs(img, p.lba);
        std::string maj, min;
        if (matchEtcVersion(fs.readFile("/etc/version"), maj, min)) {
            setMaxx(id, atoi(maj.c_str()), "V" + maj + "." + min, MtIdentAndroid::LinuxDisk);
            id.evidence = "/etc/version (game binary not recognised)";
            return true;
        }
    }

    /* 3. The version the game logged when it last ran (the tail of the log). */
    for (const auto &p : parts) {
        if (p.type != 0x83)
            continue;
        Ext2        fs(img, p.lba);
        std::string maj, min;
        if (matchLogVersion(fs.readFile("/merit/log", 256 * 1024), maj, min)) {
            setMaxx(id, atoi(maj.c_str()), "PG3002 V" + maj + "." + min, MtIdentAndroid::LinuxDisk);
            id.evidence = "the game's log (/var/merit/log)";
            return true;
        }
    }

    id.note = "A Linux disk, but no Megatouch release was found on it.";
    return true;
}

void identifyIso(Image &img, MtIdentAndroid &id)
{
    Iso   iso(img);
    Bytes exe;

    if (iso.read("MEGACDLL.EXE", &exe) || iso.read("EXEC/MEGACDLL.EXE", &exe)) {
        std::string series, rev, date;
        if (matchCd(exe, series, rev, date)) {
            const int s  = atoi(series.c_str());
            id.release   = (s == 0) ? "XL 6000" : "XL Gold";
            id.version   = trim("PG3001-" + series + " R" + rev + " " + date);
            id.evidence  = "MEGACDLL.EXE on the disc";
            id.profile   = MT_PROFILE_XL_CD;
            id.keyPrefix = (s == 0) ? "XL6K" : "XGOLDCD";
            if (!iso.read("LAUNCHIT.BAT")) {
                id.kind = MtIdentAndroid::NotRunnable;
                id.note = "No LAUNCHIT.BAT: the I/O board's ROM-DOS cannot start this disc.";
            } else
                id.kind = MtIdentAndroid::XlCd;
            return;
        }
        /* XL R0-R3 and Super 5000: the earlier U12 ROM starts MEGACDLL.EXE itself. */
        id.release  = "Megatouch XL (early)";
        id.evidence = "MEGACDLL.EXE on the disc";
        if (!matchEarlyCd(exe, rev, date)) {
            id.kind = MtIdentAndroid::NotRunnable;
            id.note = "An earlier XL disc without a version string.";
            return;
        }
        const bool s5k = !rev.empty() && (rev[0] == '5');
        id.kind        = MtIdentAndroid::XlCd;
        id.release     = s5k ? "XL Super 5000" : "XL";
        id.version     = "R" + rev + " (" + date + ")";
        id.profile     = MT_PROFILE_XL_CD_EARLY;
        id.keyPrefix   = s5k ? "XL5K" : "XLR1";
        return;
    }

    Bytes diskId;
    if (iso.read("DISK.ID", &diskId, 256)) {
        id.kind    = MtIdentAndroid::NotRunnable;
        id.release = trim(diskId.substr(0, diskId.find('\n')));
        id.note    = "A MAXX restore or upgrade disc: it installs onto a disk, it does not run.";
        return;
    }
    if (iso.read("ISOLINUX") || iso.read("RESCUECD")) {
        id.kind    = MtIdentAndroid::NotRunnable;
        id.release = "Linux MAXX restore disc (" + iso.label() + ")";
        id.note    = "A restore disc: it installs onto a disk, it does not run.";
        return;
    }
    id.note = "Not a Megatouch disc.";
}

} // namespace

MtIdentAndroid
mt_identify_android(const std::string &path)
{
    MtIdentAndroid id;
    Image          img(path);
    if (!img.isOpen()) {
        id.note = "Cannot be opened.";
        return id;
    }

    if (Iso(img).isOpen()) {
        identifyIso(img, id);
        return id;
    }

    const auto parts = partitions(img);
    if (parts.empty()) {
        const bool iso = (path.size() >= 4) && (upper(path.substr(path.size() - 4)) == ".ISO");
        id.note        = iso ? "Not an ISO 9660 image (a raw or UDF disc image?)." : "No partition table: not a disk image.";
        return id;
    }
    if (identifyDos(img, parts, id) || identifyLinux(img, parts, id))
        return id;

    id.note = "No Megatouch release found on this disk.";
    return id;
}

std::string
mt_default_key_android(const MtIdentAndroid &id, const std::string &keys_dir)
{
    if (id.keyPrefix.empty())
        return {};
    auto names_family = [&id](const std::string &n) {
        return (n.rfind(id.keyPrefix + "_full_", 0) == 0) || (n.rfind(id.keyPrefix + "_multikey_", 0) == 0);
    };
    /* A built-in key first (a local build's keys.txt lists the dump known to
       work with each release first), then the user's imported dumps. */
    for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++)
        if (names_family(k->id))
            return std::string(MT_BUILTIN_PREFIX) + k->id;

    std::vector<std::string> own;
    if (DIR *d = opendir(keys_dir.c_str())) {
        while (const struct dirent *e = readdir(d)) {
            const std::string path = keys_dir + "/" + e->d_name;
            struct stat       st;
            if (names_family(e->d_name) && !stat(path.c_str(), &st) && S_ISREG(st.st_mode) &&
                mt_key_kind((size_t) st.st_size))
                own.push_back(path);
        }
        closedir(d);
    }
    std::sort(own.begin(), own.end());
    return own.empty() ? std::string() : own.front();
}
