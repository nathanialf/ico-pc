/* json_test.c: port/data/json.h (v0.4.1, M1).
 *
 * Numbers (signs, fractions, exponents, -1.5e3, the is_int and integral
 * views, uint64 overflow, the grammar's rejections), strings (every escape,
 * \u to UTF-8, surrogate pairs and lone surrogates, an embedded \u0000),
 * nesting (objects in arrays in objects, the accessors, empty containers,
 * duplicate keys), the depth limit at ICO_JSON_DEPTH_MAX, malformed input
 * with its error offset, text without a terminating NUL, a byte order
 * mark, a decimal point under another locale when one is installed. */
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int parse(const char *s, IcoJson *j)
{
    return ico_json_parse(s, strlen(s), j);
}

/* the number of one "[x]" document */
static const IcoJsonNode *num(const char *text, IcoJson *j)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s]", text);
    if (parse(buf, j) != 0) {
        return NULL;
    }
    return ico_json_at(j->root, 0);
}

static void numbers(void)
{
    IcoJson j;
    const IcoJsonNode *n;
    uint64_t u;
    int64_t i;
    double d;

    n = num("-1.5e3", &j);
    CHECK(n && n->type == ICO_JSON_NUM && n->d == -1500.0 && n->neg && !n->is_int && !n->integral,
          "-1.5e3");
    CHECK(ico_json_u64(n, &u) == -1 && ico_json_i64(n, &i) == -1, "-1.5e3 has no integer view");
    CHECK(ico_json_double(n, &d) == 0 && d == -1500.0, "-1.5e3 as a double");
    ico_json_free(&j);

    n = num("42", &j);
    CHECK(n && n->is_int && n->integral && !n->neg && n->u == 42 && n->d == 42.0, "42");
    CHECK(ico_json_u64(n, &u) == 0 && u == 42 && ico_json_i64(n, &i) == 0 && i == 42, "42 views");
    ico_json_free(&j);

    n = num("-7", &j);
    CHECK(n && !n->is_int && n->integral && n->neg && n->u == 7 && n->d == -7.0, "-7");
    CHECK(ico_json_u64(n, &u) == -1 && ico_json_i64(n, &i) == 0 && i == -7, "-7 views");
    ico_json_free(&j);

    n = num("0", &j);
    CHECK(n && n->is_int && n->u == 0, "0");
    ico_json_free(&j);
    n = num("-0", &j);
    CHECK(n && n->neg && !n->is_int && n->integral && n->d == 0.0 && signbit(n->d), "-0");
    ico_json_free(&j);

    n = num("1.0", &j);
    CHECK(n && !n->is_int && !n->integral && n->d == 1.0, "1.0 is not an integer");
    ico_json_free(&j);
    n = num("1E2", &j);
    CHECK(n && !n->is_int && n->d == 100.0, "1E2");
    ico_json_free(&j);
    n = num("2.5e-3", &j);
    CHECK(n && n->d == 2.5e-3, "2.5e-3");
    ico_json_free(&j);
    n = num("1e+2", &j);
    CHECK(n && n->d == 100.0, "1e+2");
    ico_json_free(&j);
    /* a float written with %.9g reads back to the same float */
    n = num("0.100000001", &j);
    CHECK(n && (float)n->d == 0.1f, "0.100000001 -> 0.1f");
    ico_json_free(&j);
    n = num("3.40282347e+38", &j);
    CHECK(n && (float)n->d == 3.40282347e+38f, "FLT_MAX");
    ico_json_free(&j);

    n = num("18446744073709551615", &j);
    CHECK(n && n->is_int && n->u == UINT64_MAX, "UINT64_MAX is exact");
    CHECK(ico_json_i64(n, &i) == -1, "UINT64_MAX is no int64");
    ico_json_free(&j);
    n = num("18446744073709551616", &j);
    CHECK(n && !n->is_int && !n->integral && n->d == 18446744073709551616.0, "past UINT64_MAX");
    ico_json_free(&j);
    n = num("-9223372036854775808", &j);
    CHECK(n && ico_json_i64(n, &i) == 0 && i == INT64_MIN, "INT64_MIN");
    ico_json_free(&j);
    n = num("-9223372036854775809", &j);
    CHECK(n && ico_json_i64(n, &i) == -1, "below INT64_MIN");
    ico_json_free(&j);
    n = num("1e400", &j);
    CHECK(n && isinf(n->d), "1e400 is infinite, not an error");
    ico_json_free(&j);
    /* a long number: past the token buffer */
    {
        char big[200];
        memset(big, '1', 150);
        memcpy(big + 150, ".5", 3);
        n = num(big, &j);
        CHECK(n && n->d > 1e149 && n->d < 2e149, "a 150-digit number");
        ico_json_free(&j);
    }

    static const char *bad[] = {"01",  "-",   "+1",  "1.",   ".5",       "1e",  "1e+",
                                "0x1", "NaN", "-.5", "1.e5", "Infinity", "--1", "1 2"};
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        CHECK(num(bad[k], &j) == NULL, "'%s' is rejected", bad[k]);
        ico_json_free(&j);
    }
}

static void strings(void)
{
    IcoJson j;
    const char *s = "[\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\", \"\\u0041\\u00e9\\u20ac\", "
                    "\"\\ud83d\\ude00\", \"x\\udc00y\", \"\\ud800\\u0041\", \"n\\u0000m\", \"\"]";
    CHECK(parse(s, &j) == 0, "escapes parse (%s at %zu)", j.error, j.errorAt);
    const char *e0 = ico_json_str(ico_json_at(j.root, 0));
    CHECK(e0 && strcmp(e0, "a\"b\\c/d\b\f\n\r\t") == 0, "the simple escapes");
    CHECK(ico_json_at(j.root, 0) && ico_json_at(j.root, 0)->len == 12, "their length");
    const char *e1 = ico_json_str(ico_json_at(j.root, 1));
    CHECK(e1 && strcmp(e1, "A\xC3\xA9\xE2\x82\xAC") == 0, "\\u to UTF-8 (1, 2, 3 bytes)");
    const char *e2 = ico_json_str(ico_json_at(j.root, 2));
    CHECK(e2 && strcmp(e2, "\xF0\x9F\x98\x80") == 0, "a surrogate pair, 4 bytes");
    const char *e3 = ico_json_str(ico_json_at(j.root, 3));
    CHECK(e3 && strcmp(e3, "x?y") == 0, "a lone low surrogate is '?'");
    const char *e4 = ico_json_str(ico_json_at(j.root, 4));
    CHECK(e4 && strcmp(e4, "?A") == 0, "a high surrogate without its pair is '?'");
    const IcoJsonNode *e5 = ico_json_at(j.root, 5);
    CHECK(e5 && e5->len == 3 && memcmp(e5->str, "n\0m", 3) == 0, "an embedded \\u0000 keeps len");
    const IcoJsonNode *e6 = ico_json_at(j.root, 6);
    CHECK(e6 && e6->type == ICO_JSON_STR && e6->len == 0 && e6->str[0] == '\0', "the empty string");
    CHECK(ico_json_at(j.root, 7) == NULL, "seven elements");
    ico_json_free(&j);

    static const char *bad[] = {"[\"abc]",       "[\"a\\x\"]",      "[\"\\u12\"]",
                                "[\"\\u12g4\"]", "[\"tab\there\"]", "[\"nl\nhere\"]"};
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        CHECK(parse(bad[k], &j) == -1, "bad string %zu rejected", k);
        ico_json_free(&j);
    }
}

static void nesting(void)
{
    IcoJson j;
    const char *s = " {\"asset\": {\"version\": \"2.0\"}, \"meshes\": [{\"name\": \"m0\", "
                    "\"primitives\": [{\"attributes\": {\"POSITION\": 3}, \"mode\": 4}]}, "
                    "{\"name\": \"m1\", \"primitives\": []}], \"empty\": {}, \"flags\": "
                    "[true, false, null], \"dup\": 1, \"dup\": 2}\r\n";
    CHECK(parse(s, &j) == 0, "a glTF-like document (%s at %zu)", j.error, j.errorAt);
    CHECK(j.root && j.root->type == ICO_JSON_OBJ && ico_json_count(j.root) == 6,
          "six members (%zu)", ico_json_count(j.root));
    CHECK(strcmp(ico_json_str(ico_json_get(ico_json_get(j.root, "asset"), "version")), "2.0") == 0,
          "asset.version");
    const IcoJsonNode *meshes = ico_json_get(j.root, "meshes");
    CHECK(ico_json_count(meshes) == 2, "two meshes");
    const IcoJsonNode *prim = ico_json_at(ico_json_get(ico_json_at(meshes, 0), "primitives"), 0);
    uint64_t u = 0;
    CHECK(ico_json_u64(ico_json_get(ico_json_get(prim, "attributes"), "POSITION"), &u) == 0 &&
              u == 3,
          "meshes[0].primitives[0].attributes.POSITION");
    CHECK(ico_json_u64(ico_json_get(prim, "mode"), &u) == 0 && u == 4, "mode 4");
    CHECK(strcmp(ico_json_str(ico_json_get(ico_json_at(meshes, 1), "name")), "m1") == 0,
          "meshes[1].name");
    CHECK(ico_json_count(ico_json_get(ico_json_at(meshes, 1), "primitives")) == 0 &&
              ico_json_get(ico_json_at(meshes, 1), "primitives")->child == NULL,
          "an empty array");
    CHECK(ico_json_get(j.root, "empty")->type == ICO_JSON_OBJ &&
              ico_json_count(ico_json_get(j.root, "empty")) == 0,
          "an empty object");
    const IcoJsonNode *flags = ico_json_get(j.root, "flags");
    CHECK(ico_json_bool(ico_json_at(flags, 0)) == 1 && ico_json_bool(ico_json_at(flags, 1)) == 0 &&
              ico_json_bool(ico_json_at(flags, 2)) == -1 &&
              ico_json_at(flags, 2)->type == ICO_JSON_NULL,
          "true, false, null");
    CHECK(ico_json_u64(ico_json_get(j.root, "dup"), &u) == 0 && u == 1, "the first duplicate key");
    /* NULL in, NULL out; wrong types */
    CHECK(ico_json_get(NULL, "x") == NULL && ico_json_at(NULL, 0) == NULL &&
              ico_json_count(NULL) == 0 && ico_json_str(NULL) == NULL &&
              ico_json_bool(NULL) == -1 && ico_json_u64(NULL, &u) == -1,
          "NULL accessors");
    CHECK(ico_json_get(meshes, "name") == NULL && ico_json_at(prim, 9) == NULL &&
              ico_json_str(meshes) == NULL && ico_json_get(j.root, "missing") == NULL,
          "wrong types and missing keys");
    CHECK(ico_json_at(j.root, 1) == meshes && ico_json_at(j.root, 1)->key &&
              strcmp(ico_json_at(j.root, 1)->key, "meshes") == 0,
          "an object's members by position, with their keys");
    CHECK(j.nodes == j.root && j.nodeCount > 20, "the node array, root first");
    ico_json_free(&j);
    ico_json_free(&j); /* twice is fine */
    ico_json_free(NULL);

    /* a scalar root */
    CHECK(parse(" 12 ", &j) == 0 && j.root->type == ICO_JSON_NUM && j.root->u == 12, "a number");
    ico_json_free(&j);
}

static char *nested(int depth)
{
    char *s = malloc((size_t)depth * 2 + 2);
    for (int i = 0; i < depth; i++) {
        s[i] = '[';
        s[depth + 1 + i] = ']';
    }
    s[depth] = '1';
    s[depth * 2 + 1] = '\0';
    return s;
}

static void depth(void)
{
    IcoJson j;
    char *ok = nested(ICO_JSON_DEPTH_MAX);
    CHECK(parse(ok, &j) == 0, "%d nested arrays", ICO_JSON_DEPTH_MAX);
    const IcoJsonNode *n = j.root;
    int d = 0;
    while (n && n->type == ICO_JSON_ARR) {
        n = ico_json_at(n, 0);
        d++;
    }
    CHECK(d == ICO_JSON_DEPTH_MAX && n && n->u == 1, "the innermost value");
    ico_json_free(&j);
    free(ok);
    char *deep = nested(ICO_JSON_DEPTH_MAX + 1);
    CHECK(parse(deep, &j) == -1 && strstr(j.error, "deep") && j.root == NULL,
          "%d nested arrays fail (%s)", ICO_JSON_DEPTH_MAX + 1, j.error);
    ico_json_free(&j);
    free(deep);
    /* objects count too */
    char buf[1024] = "";
    for (int i = 0; i <= ICO_JSON_DEPTH_MAX; i++) {
        strcat(buf, "{\"a\":");
    }
    strcat(buf, "1");
    for (int i = 0; i <= ICO_JSON_DEPTH_MAX; i++) {
        strcat(buf, "}");
    }
    CHECK(parse(buf, &j) == -1, "nested objects past the limit fail");
    ico_json_free(&j);
}

static void malformed(void)
{
    IcoJson j;

    static const struct {
        const char *text;
        size_t at;
    } bad[] = {
        {"", 0},           {"   ", 3},     {"[1,]", 3},    {"[1 2]", 3},    {"{\"a\" 1}", 5},
        {"{\"a\":1,}", 7}, {"{a:1}", 1},   {"[1] x", 4},   {"[tru]", 1},    {"nul", 0},
        {"[", 1},          {"{\"a\":", 5}, {"// c\n1", 0}, {"[1]/*c*/", 3},
    };

    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        int r = parse(bad[k].text, &j);
        CHECK(r == -1 && j.root == NULL && j.error[0] && j.errorAt == bad[k].at,
              "'%s' fails at %zu (r %d, %s at %zu)", bad[k].text, bad[k].at, r, j.error, j.errorAt);
        ico_json_free(&j);
    }
    /* a NUL inside the n bytes */
    CHECK(ico_json_parse("[1]\0", 4, &j) == -1 && j.errorAt == 3, "a NUL byte in the text");
    ico_json_free(&j);
    CHECK(ico_json_parse("[1,\0 2]", 7, &j) == -1, "a NUL inside an array");
    ico_json_free(&j);
    /* not NUL-terminated: only n bytes are read */
    const char unterminated[3] = {'[', '7', ']'};
    CHECK(ico_json_parse(unterminated, 3, &j) == 0 && ico_json_at(j.root, 0)->u == 7,
          "text without a terminating NUL");
    ico_json_free(&j);
    CHECK(ico_json_parse("[7] garbage", 3, &j) == 0, "bytes past n are not read");
    ico_json_free(&j);
    CHECK(ico_json_parse(NULL, 0, &j) == -1 && ico_json_parse(NULL, 5, &j) == -1, "no text");
    ico_json_free(&j);
    CHECK(ico_json_parse("[1]", 3, NULL) == -1, "no output");
    /* a byte order mark */
    CHECK(parse("\xEF\xBB\xBF{\"k\": -2}", &j) == 0 && ico_json_get(j.root, "k")->d == -2.0,
          "a UTF-8 byte order mark is skipped");
    ico_json_free(&j);
}

/* a process locale with a decimal comma does not change the numbers */
static void locale(void)
{
    static const char *names[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "German", NULL};
    const char *got = NULL;
    for (int i = 0; names[i] && !got; i++) {
        got = setlocale(LC_NUMERIC, names[i]);
    }
    if (!got || strcmp(localeconv()->decimal_point, ".") == 0) {
        printf("  locale: no decimal-comma locale installed, skipped\n");
        setlocale(LC_NUMERIC, "C");
        return;
    }
    IcoJson j;
    const IcoJsonNode *n = num("-1.5e3", &j);
    CHECK(n && n->d == -1500.0, "-1.5e3 under %s", got);
    ico_json_free(&j);
    n = num("0.25", &j);
    CHECK(n && n->d == 0.25, "0.25 under %s", got);
    ico_json_free(&j);
    setlocale(LC_NUMERIC, "C");
}

int main(void)
{
    numbers();
    strings();
    nesting();
    depth();
    malformed();
    locale();
    if (failures) {
        printf("json_test: %d failures\n", failures);
        return 1;
    }
    printf("json_test: ok\n");
    return 0;
}
