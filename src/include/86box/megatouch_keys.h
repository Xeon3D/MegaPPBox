/*
 * MegaPPBox: security key dumps -- the user's own, imported into the keys
 * folder, and any built into the executable from a local keys/keys.txt (the
 * repository carries none: see keys/README.md).
 *
 * A key is named in the configuration as "builtin:<id>", the id being the
 * dump's file name in keys/ (e.g. "builtin:MJADE_full_4700400002501B82");
 * anything else is a path to the user's own dump.
 *
 * Dumps are named <family>_full_<ROM ID> (a 264-byte DS1991, as keyflasher
 * saves it) or <family>_multikey_<part> (a 192-byte DS1205 MultiKey); the
 * family is the release's (mt_key_families[]), which is how a release finds
 * its key.  The dump itself does not say which release it is for: the
 * DS1991's subkey names are scrambled.
 */
#ifndef MEGATOUCH_KEYS_H
#define MEGATOUCH_KEYS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MT_BUILTIN_PREFIX "builtin:"

typedef struct mt_builtin_key_t {
    const char    *id;   /* file name in keys/: "MJADE_full_4700400002501B82" */
    const char    *name; /* shown to the user: "MAXX Jade (USA) 1"           */
    const uint8_t *data;
    int            len;  /* 264: DS1991 (keyflasher), 192: DS1205 MultiKey  */
} mt_builtin_key_t;

extern const mt_builtin_key_t mt_builtin_keys[]; /* ends with id == NULL */
extern const int              mt_builtin_key_count;

/* "builtin:<id>" or "<id>" -> the key, or NULL. */
extern const mt_builtin_key_t *mt_builtin_key_find(const char *ref);

/* The key families: the prefix a release's dumps are named with. */
typedef struct mt_key_family_t {
    const char *prefix;   /* "MEMERALD"                          */
    const char *releases; /* "MAXX Emerald, Emerald 2"           */
} mt_key_family_t;

extern const mt_key_family_t mt_key_families[]; /* ends with prefix == NULL */

#define MT_KEY_DS1991   1 /* 264 bytes: keyflasher's full dump */
#define MT_KEY_MULTIKEY 2 /* 192 bytes: a DS1205 MultiKey      */

/* What a dump is, by its size: MT_KEY_DS1991, MT_KEY_MULTIKEY, or 0. */
extern int mt_key_kind(size_t len);

/* The family by prefix, or NULL. */
extern const mt_key_family_t *mt_key_family_find(const char *prefix);

/* The family a file name starts with ("MEMERALD_full_..." -> MEMERALD), or NULL. */
extern const mt_key_family_t *mt_key_family_from_name(const char *file_name);

/* Which release family a dump is for, read from the dump itself: up to max
   prefixes into out, best first; 0 when it is not a key this build knows.
   More than one when releases share a key format (MAXX and XL 6000). */
extern int mt_key_identify(const uint8_t *data, size_t len, const char **out, int max);

/* The name a dump is kept under in the keys folder: <prefix>_full_<ROM ID>
   for a DS1991, <prefix>_multikey_<part> for a DS1205.  0 when the data is
   not a dump or the family unknown. */
extern int mt_key_file_name(const uint8_t *data, size_t len, const char *prefix, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
#endif
