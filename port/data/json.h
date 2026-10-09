/*
 * port/data/json.h
 *
 * A small JSON reader (RFC 8259) for the port's own files: the archive's
 * meta.json (archive.c) and, from v0.4.1, glTF model files (port/render
 * gltf.c).  Library ico_json: json.c alone, no dependencies.
 *
 * Use:
 *
 *     IcoJson j;
 *     if (ico_json_parse(text, n, &j) != 0) {
 *         log("bad JSON at byte %zu: %s", j.errorAt, j.error);
 *     } else {
 *         const IcoJsonNode *acc = ico_json_get(j.root, "accessors");
 *         for (size_t i = 0; i < ico_json_count(acc); i++) {
 *             const IcoJsonNode *a = ico_json_at(acc, i);
 *             uint64_t count;
 *             if (ico_json_u64(ico_json_get(a, "count"), &count) != 0) ...
 *         }
 *     }
 *     ico_json_free(&j);   (also after a failed parse; idempotent)
 *
 * The parse copies the n bytes of text (which need not be NUL-terminated),
 * so the caller may free its buffer at once; every node and string lives
 * until ico_json_free.  Nodes are in one array (j.nodes, document order:
 * a container before its members); child/next link them, so a node pointer
 * is all the accessors need.  Every accessor accepts NULL and returns
 * NULL / 0 / -1 for it, so lookups chain without checks in between:
 * ico_json_get(ico_json_at(ico_json_get(root, "meshes"), 0), "name").
 *
 * What is accepted: exactly one value with optional whitespace around it
 * (space, tab, CR, LF) and an optional leading UTF-8 byte order mark; the
 * RFC's grammar strictly otherwise (no comments, no trailing commas, no
 * leading zeros or '+', no NaN/Infinity, no control characters in strings,
 * no NUL byte anywhere in the text).  At most ICO_JSON_DEPTH_MAX containers
 * nest ("[[1]]" nests 2); deeper fails.  UTF-8 is
 * not validated.  Duplicate keys are kept; ico_json_get returns the first.
 *
 * Strings: unescaped into UTF-8, NUL-terminated, len bytes (a "\u0000"
 * escape stays in the string, so len may exceed strlen); a surrogate pair
 * becomes its 4-byte sequence, a lone surrogate the byte '?'.
 *
 * Numbers: every number has d, the value as strtod reads it (correctly
 * rounded, in the C locale's sense whatever the process locale), and neg,
 * 1 when it had a leading '-' ("-0" too).  The integer views:
 *   is_int    a non-negative integer (no '-', '.', exponent) whose value
 *             fits uint64_t exactly: u holds it.  archive.c's meaning.
 *   integral  the same without the sign condition: no '.' or exponent and
 *             the magnitude fits uint64_t; u holds the magnitude
 *             (ico_json_i64 gives the signed value when it fits int64_t).
 * "1.0" and "1e2" are not integers in either view (d is 1 and 100);
 * a number too large for a double gives d = +-HUGE_VAL, not an error.
 */
#ifndef PORT_DATA_JSON_H
#define PORT_DATA_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICO_JSON_DEPTH_MAX 64
#define ICO_JSON_NODES_MAX 4000000u   /* values in one document; more fails */
#define ICO_JSON_TEXT_MAX (64u << 20) /* bytes of text; more fails */

typedef enum IcoJsonType {
    ICO_JSON_NULL = 0,
    ICO_JSON_FALSE,
    ICO_JSON_TRUE,
    ICO_JSON_NUM,
    ICO_JSON_STR,
    ICO_JSON_ARR,
    ICO_JSON_OBJ
} IcoJsonType;

typedef struct IcoJsonNode {
    IcoJsonType type;
    const char *key; /* a member of an object: its key (unescaped, NUL-terminated); else NULL */
    size_t keyLen;
    const char *str; /* ICO_JSON_STR: the string; else NULL */
    size_t len;      /* ICO_JSON_STR: its length in bytes */
    double d;        /* ICO_JSON_NUM: the value */
    uint64_t u;      /* ICO_JSON_NUM: the magnitude, exact when integral */
    int neg;         /* ICO_JSON_NUM: a leading '-' */
    int is_int;      /* ICO_JSON_NUM: non-negative integer, u exact */
    int integral;    /* ICO_JSON_NUM: integer of either sign, u the exact magnitude */
    size_t count;    /* ICO_JSON_ARR / ICO_JSON_OBJ: the number of members */
    const struct IcoJsonNode *child;        /* ARR / OBJ: the first member, NULL if none */
    const struct IcoJsonNode *next;         /* the next member of the parent, NULL */
    const struct IcoJsonNode *const *items; /* ARR / OBJ: the members by position (count) */
} IcoJsonNode;

typedef struct IcoJson {
    const IcoJsonNode *root; /* NULL after a failed parse */
    IcoJsonNode *nodes;      /* nodeCount nodes, root first */
    size_t nodeCount;
    const IcoJsonNode **index; /* the members of every container, by position */
    char *text;                /* the parser's copy, strings unescaped in place */
    char error[64];            /* a failed parse: what; "" on success */
    size_t errorAt;            /* a failed parse: the byte offset in the text */
} IcoJson;

/* Parses n bytes of text into out (zeroed first).  0 on success; -1 on
 * malformed text, too deep nesting or no memory, with out->error and
 * out->errorAt set and nothing left allocated (ico_json_free is still
 * safe). */
int ico_json_parse(const char *text, size_t n, IcoJson *out);

/* Frees what a parse allocated and zeroes *j; NULL and repeated calls are
 * fine. */
void ico_json_free(IcoJson *j);

/* The first member of object obj with this key (byte comparison; a key
 * with an embedded NUL never matches), NULL if none or obj is not an
 * object. */
const IcoJsonNode *ico_json_get(const IcoJsonNode *obj, const char *key);

/* Member i of an array (or object, in document order), NULL when out of
 * range or not a container.  O(1). */
const IcoJsonNode *ico_json_at(const IcoJsonNode *arr, size_t i);

/* The member count of an array or object, 0 for anything else. */
size_t ico_json_count(const IcoJsonNode *node);

/* Typed views.  ico_json_str: the string, NULL if not a string.  The
 * number views return 0 and store the value, -1 (nothing stored) when the
 * node is not a number of that kind: ico_json_u64 needs is_int,
 * ico_json_i64 integral within int64_t, ico_json_double any number.
 * ico_json_bool: 1 for true, 0 for false, -1 otherwise. */
const char *ico_json_str(const IcoJsonNode *node);
int ico_json_u64(const IcoJsonNode *node, uint64_t *out);
int ico_json_i64(const IcoJsonNode *node, int64_t *out);
int ico_json_double(const IcoJsonNode *node, double *out);
int ico_json_bool(const IcoJsonNode *node);

/* The value of one hexadecimal digit (0-9, a-f, A-F), -1 for any other
 * character; the parser's own \u escape helper, shared with the callers
 * that check hex strings. */
int ico_json_hexval(char c);

#ifdef __cplusplus
}
#endif

#endif
