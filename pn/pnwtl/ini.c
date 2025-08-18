/*
MIT LICENSE
Copyright 2022 snarmph
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in 
the Software without restriction, including without limitation the rights to 
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
of the Software, and to permit persons to whom the Software is furnished to do 
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
SOFTWARE.
*/
#include "ini.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>

#ifdef __cplusplus
#define CDECL(type) type
#else
#define CDECL(type) (type)
#endif

#define ini__vec_header(vec)         ((unsigned int *)(vec) - 2)
#define ini__vec_cap(vec)            ini__vec_header(vec)[0]
#define ini__vec_len(vec)            ini__vec_header(vec)[1]

#define ini__vec_need_grow(vec, n)   ((vec) == NULL || ini__vec_len(vec) + n >= ini__vec_cap(vec))
#define ini__vec_may_grow(vec, n)    (ini__vec_need_grow(vec, (n)) ? ini__vec_grow(vec, (unsigned int)(n)) : (void)0)
#define ini__vec_grow(vec, n)        ini__vec_grow_impl((void **)&(vec), (n), sizeof(*(vec)))

inline static void ini__vec_grow_impl(void **arr, unsigned int increment, unsigned int itemsize) {
    int newcap = *arr ? 2 * ini__vec_cap(*arr) + increment : increment + 1;
    void *ptr = realloc(*arr ? ini__vec_header(*arr) : 0, itemsize * newcap + sizeof(unsigned int) * 2);
    assert(ptr);
    if (ptr) {
        if (!*arr) ((unsigned int *)ptr)[1] = 0;
        *arr = (void *) ((unsigned int *)ptr + 2);
        ini__vec_cap(*arr) = newcap;
    }
}

static const iniopts_t ini__default_opts = {
    false, // merge_duplicate_tables
    false, // override_duplicate_keys
    '=',   // key_value_divider
};

typedef struct {
    const char *start;
    const char *cur;
    size_t len;
} ini__istream_t;

static ini_t ini__parse_internal(char *text, size_t textlen, const iniopts_t *options);
static char *ini__read_whole_file(FILE *fp, size_t *filelen);
static iniopts_t ini__set_default_opts(const iniopts_t *options);
static initable_t *ini__find_table(ini_t *ctx, inistrv_t name);
static inivalue_t *ini__find_value(initable_t *table, inistrv_t key);
static void ini__add_table(ini_t *ctx, ini__istream_t *in, const iniopts_t *options);
static void ini__add_value(initable_t *table, ini__istream_t *in, const iniopts_t *options);
static char *ini__strdup(const char *src, size_t len);
static int ini__rem_escaped(inistrv_t value, char *buf, size_t buflen);

// string stream helper functions
static ini__istream_t istr__init(const char *str, size_t len);
static bool istr__is_finished(ini__istream_t *in);
static void istr__skip_whitespace(ini__istream_t *in);
static void istr__ignore(ini__istream_t *in, char delim);
static void istr__skip(ini__istream_t *in);
static inistrv_t istr__get_view(ini__istream_t *in, char delim);

// string view helper functions
static inistrv_t strv__from_str(const char *str);
static inistrv_t strv__trim(inistrv_t view);
static inistrv_t strv__sub(inistrv_t view, size_t from, size_t to);
static bool strv__is_empty(inistrv_t view);
static int strv__cmp(inistrv_t a, inistrv_t b);

ini_t ini_parse(const char *filename, const iniopts_t *options) {
    if (!filename) return CDECL(ini_t){0};
    FILE *fp = fopen(filename, "rb");
    size_t filelen = 0;
    char *file_data = ini__read_whole_file(fp, &filelen);
    fclose(fp);
    return ini__parse_internal(file_data, filelen, options);
}

ini_t ini_parse_str(const char *ini_str, const iniopts_t *options) {
    size_t ini_str_len = strlen(ini_str);
    return ini__parse_internal(ini__strdup(ini_str, ini_str_len), ini_str_len, options);
}

ini_t ini_parse_buf(const char *buf, size_t buflen, const iniopts_t *options) {
    return ini__parse_internal(ini__strdup(buf, buflen), buflen, options);
}

ini_t ini_parse_fp(FILE *fp, const iniopts_t *options) {
    size_t filelen = 0;
    char *file_data = ini__read_whole_file(fp, &filelen);
    return ini__parse_internal(file_data, filelen, options);
}

bool ini_is_valid(ini_t *ctx) {
    return ctx && ctx->text != NULL;
}

void ini_free(ini_t *ctx) {
    if (!ctx) return;
    free(ctx->text);
    for (initable_t *tab = ctx->tables; tab != ivec_end(ctx->tables); ++tab) {
        ivec_free(tab->values);
    }
    ivec_free(ctx->tables);
    *ctx = (ini_t){0};
}

initable_t *ini_get_table(ini_t *ctx, const char *name) {
    if (!name) return ctx->tables;
    else       return ini__find_table(ctx, strv__from_str(name));
}

inivalue_t *ini_get(initable_t *ctx, const char *key) {
    return ctx ? ini__find_value(ctx, strv__from_str(key)) : NULL;
}   

inivec_t(inistrv_t) ini_as_array(const inivalue_t *value, char delim) {
    if (!value) return NULL;
    if (strv__is_empty(value->value)) return 0;
    if (!delim) delim = ' ';

    inivec_t(inistrv_t) out = NULL;
    inistrv_t v = value->value;

    size_t start = 0;
    for (size_t i = 0; i < v.len; ++i) {
        if (v.buf[i] == delim) {
            inistrv_t arr_val = strv__trim(strv__sub(v, start, i));
            if (!strv__is_empty(arr_val)) {
                ivec_push(out, arr_val);
            }
            start = i + 1;
        }
    }
    inistrv_t last = strv__trim(strv__sub(v, start, SIZE_MAX));
    if (!strv__is_empty(last)) {
        ivec_push(out, last);
    }
    return out;
}

unsigned long long ini_as_uint(const inivalue_t *value) {
    if (!value || strv__is_empty(value->value)) return 0;
    unsigned long long out = strtoull(value->value.buf, NULL, 0);
    if (out == ULLONG_MAX) {
        out = 0;
    }
    return out;
}

long long ini_as_int(const inivalue_t *value) {
    if (!value || strv__is_empty(value->value)) return 0;
    long long out = strtoll(value->value.buf, NULL, 0);
    if (out == LONG_MAX || out == LONG_MIN) {
        out = 0;
    }
    return out;
}

double ini_as_num(const inivalue_t *value) {
    if (!value || strv__is_empty(value->value)) return 0;
    double out = strtod(value->value.buf, NULL);
    if (out == HUGE_VAL || out == -HUGE_VAL) {
        out = 0;
    }
    return out;
}

bool ini_as_bool(const inivalue_t *value) {
    if (!value) return false;
    if (value->value.len == 4 && strncmp(value->value.buf, "true", 4) == 0) {
        return true;
    }
    return false;
}

char *ini_as_str(const inivalue_t *value, bool remove_escape_chars) {
    if (!value) return NULL;
    inistrv_t val_strv = strv__trim(value->value);
    char *str = ini__strdup(val_strv.buf, val_strv.len);
    if (remove_escape_chars) {
        inistrv_t strv = { str, val_strv.len };
        // we do +1 to include the null pointer at the end
        ini__rem_escaped(strv, str, strv.len + 1);
    }
    return str;
}

inierr_t ini_to_array(const inivalue_t *value, inistrv_t *arr, size_t len, char delim) {
    if (!value || !arr || len == 0) return INI_INVALID_ARGS;
    if (strv__is_empty(value->value)) return 0;
    if (!delim) delim = ' ';

    inistrv_t strv = value->value;
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i < strv.len; ++i) {
        if (strv.buf[i] == delim) {
            inistrv_t arr_val = strv__trim(strv__sub(strv, start, i));
            if (!strv__is_empty(arr_val)) {
                if (count >= len) {
                    return INI_BUFFER_TOO_SMALL;
                }
                arr[count++] = arr_val;
            }
            start = i + 1;
        }
    }
    inistrv_t last = strv__trim(strv__sub(strv, start, SIZE_MAX));
    if (!strv__is_empty(last)) {
        if (count >= len) {
            return INI_BUFFER_TOO_SMALL;
        }
        arr[count++] = last;
    }
    return count;
}

inierr_t ini_to_str(const inivalue_t *value, char *buf, size_t buflen, bool remove_escape_chars) {
    if (!value || !buf || buflen == 0) return INI_INVALID_ARGS;
    if (strv__is_empty(value->value)) {
        buf[0] = '\0';
        return 0;
    }
    inistrv_t strv = strv__trim(value->value);
    if (remove_escape_chars) {
        return ini__rem_escaped(strv, buf, buflen);
    }
    else {
        if (buflen < (strv.len + 1)) return INI_BUFFER_TOO_SMALL;
        memcpy(buf, strv.buf, strv.len);
        buf[strv.len] = '\0';
        return strv.len;
    }
}

const char *ini_explain(inierr_t error) {
    switch (error) {
        case INI_NO_ERR:           return "no error";
        case INI_INVALID_ARGS:     return "invalid arguments";
        case INI_BUFFER_TOO_SMALL: return "buffer too small";
    }
    return "unknown";
}

static ini_t ini__parse_internal(char *text, size_t textlen, const iniopts_t *options) {
    ini_t ini = { text, NULL };
    if (!text) return ini;
    iniopts_t opts = ini__set_default_opts(options);
    // add root table
    initable_t root = { CDECL(inistrv_t){ "root", 4 }, NULL };
    ivec_push(ini.tables, root);
    ini__istream_t in = istr__init(text, textlen);
    while (!istr__is_finished(&in)) {
        switch (*in.cur) {
            case '[':
                ini__add_table(&ini, &in, &opts);
                break;
            case '#': case ';':
                istr__ignore(&in, '\n');
                break;
            default:
                ini__add_value(ini.tables, &in, &opts);
                break;
        }
        istr__skip_whitespace(&in);
    }
    return ini;
}

static char *ini__read_whole_file(FILE *fp, size_t *filelen) {
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    size_t len = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    size_t read = fread(buf, 1, len, fp);
    if (read != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    if (filelen) *filelen = len;
    return buf;
}

static iniopts_t ini__set_default_opts(const iniopts_t *options) {
    if (!options) return ini__default_opts;

    iniopts_t opts = ini__default_opts;

    if (options->override_duplicate_keys)
        opts.override_duplicate_keys = options->override_duplicate_keys;

    if (options->merge_duplicate_tables)
        opts.merge_duplicate_tables = options->merge_duplicate_tables;

    if (options->key_value_divider)
        opts.key_value_divider = options->key_value_divider;

    return opts;
}

static initable_t *ini__find_table(ini_t *ctx, inistrv_t name) {
    if (strv__is_empty(name)) return NULL;
    for (unsigned int i = 0; i < ivec_len(ctx->tables); ++i) {
        if (strv__cmp(ctx->tables[i].name, name) == 0) {
            return ctx->tables + i;
        }
    }
    return NULL;
}

static inivalue_t *ini__find_value(initable_t *table, inistrv_t key) {
    if (strv__is_empty(key)) return NULL;
    for (unsigned int i = 0; i < ivec_len(table->values); ++i) {
        if (strv__cmp(table->values[i].key, key) == 0) {
            return table->values + i;
        }
    }
    return NULL;
}

static void ini__add_table(ini_t *ctx, ini__istream_t *in, const iniopts_t *options) {
    istr__skip(in); // skip [
    inistrv_t name = istr__get_view(in, ']');
    istr__skip(in); // skip ]

    if (strv__is_empty(name)) return;

    initable_t *table = options->merge_duplicate_tables ? ini__find_table(ctx, name) : NULL;
    if (!table) {
        ivec_push(ctx->tables, CDECL(initable_t){ name, NULL });
        table = &ivec_back(ctx->tables);
    }
    istr__ignore(in, '\n');
    istr__skip(in);
    while (!istr__is_finished(in)) {
        switch (*in->cur) {
            case '\n': case '\r':
                return;
            case '#': case ';':
                istr__ignore(in, '\n');
                break;
            default:
                ini__add_value(table, in, options);
                break;
        }
    }
}

static void ini__add_value(initable_t *table, ini__istream_t *in, const iniopts_t *options) {
    if (!table) return;

    inistrv_t key = strv__trim(istr__get_view(in, options->key_value_divider));
    istr__skip(in); // skip divider
    inistrv_t val = strv__trim(istr__get_view(in, '\n'));

    if (strv__is_empty(key)) return;

    // find inline comments
    for (size_t i = 0; i < val.len; ++i) {
        if (val.buf[i] == '#' || val.buf[i] == ';') {
            // can escape # and ; with a backslash (e.g. \#)
            if (i > 0 && val.buf[i-1] == '\\') {
                continue;
            }
            val.len = i;
            break;
        }
    }

    // value might be until EOF, in that case no use in skipping
    if (!istr__is_finished(in)) istr__skip(in); // skip \n
    inivalue_t *new_val = options->override_duplicate_keys ? ini__find_value(table, key) : NULL;
    if (new_val) {
        new_val->value = val;
    }
    else {
        ivec_push(table->values, CDECL(inivalue_t){ key, val });
    }
}

static char *ini__strdup(const char *src, size_t len) {
    if (!src || len == 0) return NULL;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, src, len);
    buf[len] = '\0';
    return buf;
}

static int ini__rem_escaped(inistrv_t value, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return INI_INVALID_ARGS;
    if (strv__is_empty(value)) {
        buf[0] = '\0';
        return 0;
    }
    size_t len = value.len - 1;
    size_t dest_pos = 0;
    const char *src = value.buf;
    
    for (
        size_t src_pos = 0; 
        src_pos < len; 
        ++src_pos, ++dest_pos
    ) {
        if (dest_pos >= buflen) return INI_BUFFER_TOO_SMALL;

        if (src[src_pos] == '\\' &&
           (src[src_pos + 1] == ';' || src[src_pos + 1] == '#')
        ) {
            src_pos++;
        }

        buf[dest_pos] = src[src_pos];
    }
    
    if (dest_pos >= buflen) return INI_BUFFER_TOO_SMALL;
    buf[dest_pos++] = value.buf[len];
    buf[dest_pos] = '\0';
    return dest_pos;
}

static ini__istream_t istr__init(const char *str, size_t len) {
    return CDECL(ini__istream_t) { str, str, len };
}

static bool istr__is_finished(ini__istream_t *in) {
    return (size_t)(in->cur - in->start) >= in->len;
}

static void istr__skip_whitespace(ini__istream_t *in) {
    const char *end = in->start + in->len;
    while (in->cur < end && isspace(*in->cur)) {
        ++in->cur;
    }
}

static void istr__ignore(ini__istream_t *in, char delim) {
    const char *end = in->start + in->len;
    for (; in->cur < end && *in->cur != delim; ++in->cur);
}

static void istr__skip(ini__istream_t *in) {
    if (!istr__is_finished(in)) ++in->cur;
}

static inistrv_t istr__get_view(ini__istream_t *in, char delim) {
    const char *from = in->cur;
    istr__ignore(in, delim);
    size_t len = in->cur - from;
    return CDECL(inistrv_t){ from, len };
}

static inistrv_t strv__from_str(const char *str) {
    return CDECL(inistrv_t){ str, str ? strlen(str) : 0 };
}

static inistrv_t strv__trim(inistrv_t view) {
    if (strv__is_empty(view)) return view;
    inistrv_t out = view;
    // trim left
    for (size_t i = 0; i < view.len && isspace(view.buf[i]); ++i) {
        ++out.buf;
        --out.len;
    }
    if (strv__is_empty(out)) return view;
    // trim right
    for (long long i = view.len - 1; i >= 0 && isspace(view.buf[i]); --i) {
        --out.len;
    }
    return out;
}

static inistrv_t strv__sub(inistrv_t view, size_t from, size_t to) {
    if (to > view.len) to = view.len;
    if (from > to) from = to;
    return CDECL(inistrv_t){ view.buf + from, to - from };
}

static bool strv__is_empty(inistrv_t view) {
    return view.len == 0;
}

static int strv__cmp(inistrv_t a, inistrv_t b) {
    if(a.len < b.len) return -1;
    if(a.len > b.len) return  1;
    return memcmp(a.buf, b.buf, a.len);
}
