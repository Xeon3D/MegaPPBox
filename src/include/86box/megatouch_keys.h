/*
 * MegaPPBox: the security key dumps built into the executable (keys/keys.txt).
 *
 * A key is named in the configuration as "builtin:<id>", the id being the
 * dump's file name in keys/ (e.g. "builtin:MJADE_full_4700400002501B82");
 * anything else is a path to the user's own dump.
 */
#ifndef MEGATOUCH_KEYS_H
#define MEGATOUCH_KEYS_H
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

#ifdef __cplusplus
}
#endif
#endif
