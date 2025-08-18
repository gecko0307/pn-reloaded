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

/*  ini.h - simple single-header ini parser in c99

    in just one c/c++ file do this:
        #define INI_IMPLEMENTATION
        #include "ini.h"

    license:
        see end of file

    options:
        by default, when there are multiple tables with the same name, the
        parser simply keeps adding to the tables list, it wastes memory
        but it is also much faster, especially for bigger files.
        if you want the parser to merge all the tables together use the
        following option:
         - merge_duplicate_tables
        when adding keys, if a table has two same keys, the parser
        adds it to the table anyway, meaning that when it searches for the
        key with ini_get it will get the first one but it will still have
        both in table.values, to override this behaviour and only keep the
        last value use:
         - override_duplicate_keys
        the default key/value divider is '=', if you want to change is use
         - key_value_divider

    usage:
    - simple file:
        ini_t ini = ini_parse("file.ini", NULL);

        char *name = ini_as_str(ini_get(ini_get_table(&ini, INI_ROOT), "name"), false);

        initable_t *server = ini_get_table(&ini, "server");
        int port = (int)ini_as_int(ini_get(server, "port"));
        bool use_threads = ini_as_bool(ini_get(server, "use threads"));
        
        ini_free(&ini);
        free(name);

    - string:
        const char *ini_str = 
            "name : web-server\n"
            "[server]\n"
            "port : 8080\n"
            "ip : localhost\n"
            "use threads : false";
        ini_t ini = ini_parse_str(ini_str, &(iniopts_t){ .key_value_divider = ':' });

        char *name = ini_as_str(ini_get(ini_get_table(&ini, INI_ROOT), "name"), false);

        initable_t *server = ini_get_table(&ini, "server");
        int port = (int)ini_as_int(ini_get(server, "port"));
        bool use_threads = ini_as_bool(ini_get(server, "use threads"));
        char ip[64];
        int iplen = ini_to_str(ini_get(server, "ip"), ip, sizeof(ip), false);
        if (iplen < 0) {
            printf("(err) couldn't get ip: %s\n", ini_explain(iplen));
        }
        
        free(name);
        ini_free(&ini);
*/

#ifndef INI_LIB_HEADER
#define INI_LIB_HEADER

#define _CRT_SECURE_NO_WARNINGS
#include <stdbool.h>
#include <limits.h>
#include <stdint.h> // SIZE_MAX
#include <stddef.h>
#include <stdio.h>

#define inivec_t(T)                     T *

#define ivec_free(vec)                  ((vec) ? free(ini__vec_header(vec)), NULL : NULL)
#define ivec_copy(src, dest)            (ivec_free(dest), ivec_reserve(dest, ivec_len(src)), memcpy(dest, src, ivec_len(src)))

#define ivec_push(vec, ...)             (ini__vec_may_grow(vec, 1), (vec)[ini__vec_len(vec)] = (__VA_ARGS__), ini__vec_len(vec)++)
#define ivec_rem(vec, ind)              ((vec) ? (vec)[(ind)] = (vec)[--ini__vec_len(vec)], NULL : NULL)
#define ivec_rem_it(vec, it)            ivec_rem((vec), (it)-(vec))
#define ivec_len(vec)                   ((vec) ? ini__vec_len(vec) : 0)
#define ivec_cap(vec)                   ((vec) ? ini__vec_cap(vec) : 0)

#define ivec_end(vec)                   ((vec) ? (vec) + ini__vec_len(vec) : NULL)
#define ivec_back(vec)                  ((vec)[ini__vec_len(vec) - 1])

#define ivec_add(vec, n)                (ini__vec_may_grow(vec, (n)), ini__vec_len(vec) += (unsigned int)(n), &(vec)[ini__vec_len(vec)-(n)])
#define ivec_reserve(vec, n)            (ini__vec_may_grow(vec, (n)))

#define ivec_clear(vec)                 ((vec) ? ini__vec_len(vec) = 0 : 0)
#define ivec_pop(vec)                   ((vec)[--ini__vec_len(vec)])

typedef struct {
    const char *buf;
    size_t len;
} inistrv_t;

typedef struct {
    inistrv_t key;
    inistrv_t value;
} inivalue_t;

typedef struct {
    inistrv_t name;
    inivec_t(inivalue_t) values;
} initable_t;

typedef struct {
    char *text;
    inivec_t(initable_t) tables;
} ini_t;

typedef struct {
    bool merge_duplicate_tables;  // default: false
    bool override_duplicate_keys; // default: false
    char key_value_divider;       // default: =
} iniopts_t;

typedef enum {
    INI_NO_ERR = 0,
    INI_INVALID_ARGS = -1,
    INI_BUFFER_TOO_SMALL = -2,
} inierr_t;

#define INI_ROOT NULL

// parses a ini file, if options is NULL it uses the default options
ini_t ini_parse(const char *filename, const iniopts_t *options);
// parses a ini string, if options is NULL it uses the default options
ini_t ini_parse_str(const char *ini_str, const iniopts_t *options);
// parses a ini buffer, this buffer *can* contain '\0', if options is NULL it uses the default options
ini_t ini_parse_buf(const char *buf, size_t buflen, const iniopts_t *options);
// parses a ini file from a file descriptor, if options is NULL it uses the default options
ini_t ini_parse_fp(FILE *fp, const iniopts_t *options);
// checks that the ini file has been parsed correctly
bool ini_is_valid(ini_t *ctx);
void ini_free(ini_t *ctx);

// return a table with name <name>, returns NULL if nothing was found
initable_t *ini_get_table(ini_t *ctx, const char *name);
// return a value with key <key>, returns NULL if nothing was found or if <ctx> is NULL
inivalue_t *ini_get(initable_t *ctx, const char *key);

// returns an allocated vector of values divided by <delim>
// if <delim> is 0 then it defaults to ' ', must be freed with ivec_free
inivec_t(inistrv_t) ini_as_array(const inivalue_t *value, char delim);
unsigned long long ini_as_uint(const inivalue_t *value);
long long ini_as_int(const inivalue_t *value);
double ini_as_num(const inivalue_t *value);
bool ini_as_bool(const inivalue_t *value);
// copies a value into a c string, must be freed
// copies the value to an allocated c string, if remove_escape_chars is true 
// it also removes escape characters from a string (e.g. \; or \#)
// returns the allocated string
char *ini_as_str(const inivalue_t *value, bool remove_escape_chars);

// divides the inivalue_t <value> by <delim> and puts the resulting values in <arr>
// returns the number of items on success or <0 on failure (check inierr_t)
inierr_t ini_to_array(const inivalue_t *value, inistrv_t *arr, size_t len, char delim);
// copies the value to a buffer, buflen includes the null character, 
// if remove_escape_chars is true it also removes escape characters from a string (e.g. \; or \#)
// returns the number of characters written on succes or <0 on failure (check inierr_t)
inierr_t ini_to_str(const inivalue_t *value, char *buf, size_t buflen, bool remove_escape_chars);

// returns a human readable version of <error>
const char *ini_explain(inierr_t error);

#endif
