/*
 * port/data/json.c
 *
 * json.h's reader: recursive descent over a NUL-terminated copy of the
 * text, strings unescaped in place, nodes in one growing array linked by
 * index while parsing and by pointer once it is done.  Lifted from
 * archive.c's meta.json reader (v0.4.1, M1), with doubles, negative
 * numbers and the RFC's number grammar.
 */
#include "json.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JLink {
    size_t child, next; /* node index + 1, 0 = none */
} JLink;

typedef struct JParse {
    char *p;
    char *end;
    IcoJsonNode *n;
    JLink *l;
    size_t count, cap;
    int bad;
    const char *why;
    char *at;
} JParse;

static void fail(JParse *j, const char *why)
{
    if (!j->bad) {
        j->bad = 1;
        j->why = why;
        j->at = j->p;
    }
}

/* a new node; SIZE_MAX on no memory */
static size_t jnew(JParse *j, IcoJsonType type)
{
    if (j->count >= ICO_JSON_NODES_MAX) {
        fail(j, "too many values");
        return SIZE_MAX;
    }
    if (j->count == j->cap) {
        size_t cap = j->cap ? j->cap * 2 : 64;
        IcoJsonNode *n = realloc(j->n, cap * sizeof(*n));
        if (n == NULL) {
            fail(j, "out of memory");
            return SIZE_MAX;
        }
        j->n = n;
        JLink *l = realloc(j->l, cap * sizeof(*l));
        if (l == NULL) {
            fail(j, "out of memory");
            return SIZE_MAX;
        }
        j->l = l;
        j->cap = cap;
    }
    memset(&j->n[j->count], 0, sizeof(IcoJsonNode));
    j->n[j->count].type = type;
    j->l[j->count].child = j->l[j->count].next = 0;
    return j->count++;
}

static void jws(JParse *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n') {
        j->p++;
    }
}

int ico_json_hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int h = ico_json_hexval(s[i]);
        if (h < 0) {
            return -1;
        }
        v = v << 4 | (unsigned)h;
    }
    *out = v;
    return 0;
}

static char *utf8(char *w, unsigned v)
{
    if (v < 0x80) {
        *w++ = (char)v;
    } else if (v < 0x800) {
        *w++ = (char)(0xC0 | v >> 6);
        *w++ = (char)(0x80 | (v & 0x3F));
    } else if (v < 0x10000) {
        *w++ = (char)(0xE0 | v >> 12);
        *w++ = (char)(0x80 | (v >> 6 & 0x3F));
        *w++ = (char)(0x80 | (v & 0x3F));
    } else {
        *w++ = (char)(0xF0 | v >> 18);
        *w++ = (char)(0x80 | (v >> 12 & 0x3F));
        *w++ = (char)(0x80 | (v >> 6 & 0x3F));
        *w++ = (char)(0x80 | (v & 0x3F));
    }
    return w;
}

/* At '"': unescapes the string in place (it only shrinks) and returns it,
 * NUL-terminated, its length in *len. */
static const char *jstring(JParse *j, size_t *len)
{
    char *r = j->p + 1;
    char *w = r;
    char *s = r;

    for (;;) {
        unsigned char c = (unsigned char)*r;

        if (c == '\0') {
            j->p = r;
            fail(j, "unterminated string");
            return NULL;
        }
        if (c < 0x20) {
            j->p = r;
            fail(j, "a control character in a string");
            return NULL;
        }
        if (c == '"') {
            break;
        }
        if (c != '\\') {
            *w++ = *r++;
            continue;
        }
        r++;
        switch (*r) {
        case '"':
        case '\\':
        case '/':
            *w++ = *r++;
            break;
        case 'b':
            *w++ = '\b';
            r++;
            break;
        case 'f':
            *w++ = '\f';
            r++;
            break;
        case 'n':
            *w++ = '\n';
            r++;
            break;
        case 'r':
            *w++ = '\r';
            r++;
            break;
        case 't':
            *w++ = '\t';
            r++;
            break;
        case 'u': {
            unsigned v, lo;

            if (hex4(r + 1, &v) != 0) {
                j->p = r;
                fail(j, "a bad \\u escape");
                return NULL;
            }
            r += 5;
            if (v >= 0xD800 && v < 0xDC00 && r[0] == '\\' && r[1] == 'u' && hex4(r + 2, &lo) == 0 &&
                lo >= 0xDC00 && lo < 0xE000) {
                v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                r += 6;
            } else if (v >= 0xD800 && v < 0xE000) {
                v = '?'; /* a lone surrogate */
            }
            w = utf8(w, v);
            break;
        }
        default:
            j->p = r;
            fail(j, "a bad escape");
            return NULL;
        }
    }
    j->p = r + 1;
    *w = '\0';
    *len = (size_t)(w - s);
    return s;
}

static int isdigit_(char c)
{
    return c >= '0' && c <= '9';
}

/* RFC 8259's number at j->p: -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE][+-]?[0-9]+)? */
static size_t jnumber(JParse *j)
{
    char *start = j->p;
    char *q = j->p;
    uint64_t u = 0;
    int neg = 0, exact = 1, frac = 0;

    if (*q == '-') {
        neg = 1;
        q++;
    }
    if (!isdigit_(*q)) {
        fail(j, "an unexpected character");
        return SIZE_MAX;
    }
    if (*q == '0' && isdigit_(q[1])) {
        j->p = q;
        fail(j, "a number with a leading zero");
        return SIZE_MAX;
    }
    while (isdigit_(*q)) {
        unsigned d = (unsigned)(*q - '0');

        if (u > (UINT64_MAX - d) / 10) {
            exact = 0;
        }
        u = u * 10 + d;
        q++;
    }
    if (*q == '.') {
        frac = 1;
        q++;
        if (!isdigit_(*q)) {
            j->p = q;
            fail(j, "no digit after the decimal point");
            return SIZE_MAX;
        }
        while (isdigit_(*q)) {
            q++;
        }
    }
    if (*q == 'e' || *q == 'E') {
        frac = 1;
        q++;
        if (*q == '+' || *q == '-') {
            q++;
        }
        if (!isdigit_(*q)) {
            j->p = q;
            fail(j, "no digit in the exponent");
            return SIZE_MAX;
        }
        while (isdigit_(*q)) {
            q++;
        }
    }
    /* the value: strtod on a copy with the locale's decimal point */
    size_t tl = (size_t)(q - start);
    char small[64];
    char *tok = tl < sizeof(small) ? small : malloc(tl + 1);
    if (tok == NULL) {
        fail(j, "out of memory");
        return SIZE_MAX;
    }
    memcpy(tok, start, tl);
    tok[tl] = '\0';
    const struct lconv *lc = localeconv();
    if (lc && lc->decimal_point && lc->decimal_point[0] && lc->decimal_point[0] != '.' &&
        lc->decimal_point[1] == '\0') {
        char *dot = strchr(tok, '.');
        if (dot) {
            *dot = lc->decimal_point[0];
        }
    }
    double d = strtod(tok, NULL);
    if (tok != small) {
        free(tok);
    }
    j->p = q;

    size_t idx = jnew(j, ICO_JSON_NUM);
    if (idx != SIZE_MAX) {
        IcoJsonNode *n = &j->n[idx];
        n->d = d;
        n->u = u;
        n->neg = neg;
        n->integral = exact && !frac;
        n->is_int = n->integral && !neg;
    }
    return idx;
}

static size_t jvalue(JParse *j, int depth)
{
    size_t idx;

    jws(j);
    switch (*j->p) {
    case '{':
    case '[': {
        int obj = *j->p == '{';
        char close = obj ? '}' : ']';
        size_t last = SIZE_MAX, count = 0;

        if (depth >= ICO_JSON_DEPTH_MAX) {
            fail(j, "nested too deep");
            return SIZE_MAX;
        }
        idx = jnew(j, obj ? ICO_JSON_OBJ : ICO_JSON_ARR);
        if (idx == SIZE_MAX) {
            return SIZE_MAX;
        }
        j->p++;
        jws(j);
        if (*j->p == close) {
            j->p++;
            return idx;
        }
        for (;;) {
            const char *key = NULL;
            size_t keyLen = 0, v;

            jws(j);
            if (obj) {
                if (*j->p != '"') {
                    fail(j, "a member without a key");
                    return SIZE_MAX;
                }
                if ((key = jstring(j, &keyLen)) == NULL) {
                    return SIZE_MAX;
                }
                jws(j);
                if (*j->p != ':') {
                    fail(j, "no ':' after a key");
                    return SIZE_MAX;
                }
                j->p++;
            }
            v = jvalue(j, depth + 1);
            if (v == SIZE_MAX) {
                return SIZE_MAX;
            }
            j->n[v].key = key;
            j->n[v].keyLen = keyLen;
            if (last == SIZE_MAX) {
                j->l[idx].child = v + 1;
            } else {
                j->l[last].next = v + 1;
            }
            last = v;
            count++;
            j->n[idx].count = count;
            jws(j);
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            if (*j->p == close) {
                j->p++;
                return idx;
            }
            fail(j, obj ? "no ',' or '}' after a member" : "no ',' or ']' after an element");
            return SIZE_MAX;
        }
    }
    case '"': {
        size_t len;
        const char *s = jstring(j, &len);

        if (s == NULL) {
            return SIZE_MAX;
        }
        idx = jnew(j, ICO_JSON_STR);
        if (idx != SIZE_MAX) {
            j->n[idx].str = s;
            j->n[idx].len = len;
        }
        return idx;
    }
    case 't':
        if (strncmp(j->p, "true", 4) == 0) {
            j->p += 4;
            return jnew(j, ICO_JSON_TRUE);
        }
        break;
    case 'f':
        if (strncmp(j->p, "false", 5) == 0) {
            j->p += 5;
            return jnew(j, ICO_JSON_FALSE);
        }
        break;
    case 'n':
        if (strncmp(j->p, "null", 4) == 0) {
            j->p += 4;
            return jnew(j, ICO_JSON_NULL);
        }
        break;
    case '\0':
        fail(j, "unexpected end of the text");
        return SIZE_MAX;
    default:
        return jnumber(j);
    }
    fail(j, "an unexpected character");
    return SIZE_MAX;
}

int ico_json_parse(const char *text, size_t n, IcoJson *out)
{
    JParse j;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memset(&j, 0, sizeof(j));
    if (text == NULL && n != 0) {
        snprintf(out->error, sizeof(out->error), "no text");
        return -1;
    }
    if (n > ICO_JSON_TEXT_MAX) {
        snprintf(out->error, sizeof(out->error), "the text is too long");
        return -1;
    }
    out->text = malloc(n + 1);
    if (out->text == NULL) {
        snprintf(out->error, sizeof(out->error), "out of memory");
        return -1;
    }
    if (n) {
        memcpy(out->text, text, n);
    }
    out->text[n] = '\0';
    j.p = out->text;
    j.end = out->text + n;
    if (n >= 3 && (unsigned char)j.p[0] == 0xEF && (unsigned char)j.p[1] == 0xBB &&
        (unsigned char)j.p[2] == 0xBF) {
        j.p += 3; /* a UTF-8 byte order mark */
    }
    size_t root = jvalue(&j, 0);
    if (!j.bad) {
        jws(&j);
        if (j.p != j.end) {
            fail(&j, *j.p == '\0' ? "a NUL byte in the text" : "text after the value");
        }
    }
    if (j.bad || root == SIZE_MAX) {
        snprintf(out->error, sizeof(out->error), "%s", j.why ? j.why : "malformed");
        out->errorAt = j.at ? (size_t)(j.at - out->text) : 0;
        free(j.n);
        free(j.l);
        free(out->text);
        out->text = NULL;
        return -1;
    }
    for (size_t i = 0; i < j.count; i++) {
        j.n[i].child = j.l[i].child ? &j.n[j.l[i].child - 1] : NULL;
        j.n[i].next = j.l[i].next ? &j.n[j.l[i].next - 1] : NULL;
    }
    free(j.l);
    /* the member index: every container's members in one pointer array, so
       ico_json_at is O(1) (the slices total at most nodeCount - 1) */
    out->index = malloc((j.count ? j.count : 1) * sizeof(*out->index));
    if (out->index == NULL) {
        snprintf(out->error, sizeof(out->error), "out of memory");
        free(j.n);
        free(out->text);
        out->text = NULL;
        return -1;
    }
    for (size_t i = 0, at = 0; i < j.count; i++) {
        if (j.n[i].type != ICO_JSON_ARR && j.n[i].type != ICO_JSON_OBJ) {
            continue;
        }
        j.n[i].items = out->index + at;
        for (const IcoJsonNode *c = j.n[i].child; c != NULL; c = c->next) {
            out->index[at++] = c;
        }
    }
    out->nodes = j.n;
    out->nodeCount = j.count;
    out->root = &j.n[root];
    return 0;
}

void ico_json_free(IcoJson *j)
{
    if (j == NULL) {
        return;
    }
    free(j->nodes);
    free(j->index);
    free(j->text);
    memset(j, 0, sizeof(*j));
}

const IcoJsonNode *ico_json_get(const IcoJsonNode *obj, const char *key)
{
    if (obj == NULL || key == NULL || obj->type != ICO_JSON_OBJ) {
        return NULL;
    }
    const size_t kl = strlen(key);
    for (const IcoJsonNode *c = obj->child; c != NULL; c = c->next) {
        if (c->keyLen == kl && memcmp(c->key, key, kl) == 0) {
            return c;
        }
    }
    return NULL;
}

const IcoJsonNode *ico_json_at(const IcoJsonNode *arr, size_t i)
{
    if (arr == NULL || (arr->type != ICO_JSON_ARR && arr->type != ICO_JSON_OBJ) ||
        i >= arr->count) {
        return NULL;
    }
    return arr->items[i];
}

size_t ico_json_count(const IcoJsonNode *node)
{
    return node != NULL && (node->type == ICO_JSON_ARR || node->type == ICO_JSON_OBJ) ? node->count
                                                                                      : 0;
}

const char *ico_json_str(const IcoJsonNode *node)
{
    return node != NULL && node->type == ICO_JSON_STR ? node->str : NULL;
}

int ico_json_u64(const IcoJsonNode *node, uint64_t *out)
{
    if (node == NULL || node->type != ICO_JSON_NUM || !node->is_int) {
        return -1;
    }
    *out = node->u;
    return 0;
}

int ico_json_i64(const IcoJsonNode *node, int64_t *out)
{
    if (node == NULL || node->type != ICO_JSON_NUM || !node->integral) {
        return -1;
    }
    if (!node->neg) {
        if (node->u > (uint64_t)INT64_MAX) {
            return -1;
        }
        *out = (int64_t)node->u;
        return 0;
    }
    if (node->u > (uint64_t)INT64_MAX + 1) {
        return -1;
    }
    *out = node->u == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)node->u;
    return 0;
}

int ico_json_double(const IcoJsonNode *node, double *out)
{
    if (node == NULL || node->type != ICO_JSON_NUM) {
        return -1;
    }
    *out = node->d;
    return 0;
}

int ico_json_bool(const IcoJsonNode *node)
{
    if (node == NULL) {
        return -1;
    }
    return node->type == ICO_JSON_TRUE ? 1 : node->type == ICO_JSON_FALSE ? 0 : -1;
}
