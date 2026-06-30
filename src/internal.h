#ifndef EZI_INTERNAL_H
#define EZI_INTERNAL_H

#include "ezinstall.h"

#include <stdio.h>

#define EZI_ARRAY_GROW(arr, count, cap, type) \
    do { \
        if ((count) >= (cap)) { \
            size_t _new_cap = (cap) ? (cap) * 2 : 8; \
            type *_tmp = realloc((arr), _new_cap * sizeof(type)); \
            if (!_tmp) return EZI_ERR_OOM; \
            (arr) = _tmp; \
            (cap) = _new_cap; \
        } \
    } while (0)

char *ezi_strdup(const char *s);
char *ezi_strndup(const char *s, size_t n);
char *ezi_str_trim(char *s);
int   ezi_str_ieq(const char *a, const char *b);
int   ezi_path_is_absolute(const char *path);
int   ezi_mkdir_p(const char *path);
int   ezi_copy_file(const char *src, const char *dest);
int   ezi_copy_tree(const char *src, const char *dest);
int   ezi_remove_tree(const char *path);
char *ezi_path_join(const char *a, const char *b);
char *ezi_path_dirname(const char *path);
char *ezi_home_dir(void);
char *ezi_cwd_dir(void);
void  ezi_log(ezi_ops *ops, const char *fmt, ...);

ezi_os ezi_parse_os_name(const char *name);

typedef enum ezi_step_kind {
    EZI_STEP_MKDIR,
    EZI_STEP_GET,
    EZI_STEP_EXTRACT,
    EZI_STEP_COPY,
    EZI_STEP_MOVE,
    EZI_STEP_DELETE,
    EZI_STEP_WRITE,
    EZI_STEP_RUN,
    EZI_STEP_CHMOD,
    EZI_STEP_ENV,
    EZI_STEP_INCLUDE
} ezi_step_kind;

struct ezi_step {
    ezi_step_kind kind;
    int           line;
    ezi_os        os;
    char         *s1;
    char         *s2;
    char         *s3;
    char         *content;
};

struct ezi_script {
    ezi_meta  meta;
    char     *source_path;
    ezi_step *steps;
    size_t    step_count;
    size_t    step_cap;
};

ezi_step_kind ezi_step_get_kind(const ezi_step *step);
const char   *ezi_step_arg1(const ezi_step *step);
const char   *ezi_step_arg2(const ezi_step *step);
const char   *ezi_step_arg3(const ezi_step *step);
const char   *ezi_step_content(const ezi_step *step);
void          ezi_step_free(ezi_step *step);

struct ezi_context {
    char  *target;
    char  *script_dir;
    char  *home;
    char  *cwd;
    ezi_os os;
};

#endif /* EZI_INTERNAL_H */
