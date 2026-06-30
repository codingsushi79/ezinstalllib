/**
 * ezinstall — C install utilities inspired by
 * https://github.com/codingsushi79/ezinstaller
 *
 * Run EziScript (.ezi) installer definitions or call install primitives
 * directly from C.
 */
#ifndef EZINSTALL_H
#define EZINSTALL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Status & platform                                                  */
/* ------------------------------------------------------------------ */

typedef enum ezi_status {
    EZI_OK = 0,
    EZI_ERR_PARSE,
    EZI_ERR_INSTALL,
    EZI_ERR_IO,
    EZI_ERR_NETWORK,
    EZI_ERR_OOM,
    EZI_ERR_UNSUPPORTED
} ezi_status;

typedef enum ezi_os {
    EZI_OS_ALL = 0,
    EZI_OS_LINUX,
    EZI_OS_MACOS,
    EZI_OS_WINDOWS,
    EZI_OS_UNIX
} ezi_os;

typedef enum ezi_on_error {
    EZI_ON_ERROR_STOP = 0,
    EZI_ON_ERROR_CONTINUE
} ezi_on_error;

const char *ezi_status_string(ezi_status status);
ezi_os       ezi_current_os(void);
const char  *ezi_os_name(ezi_os os);
int          ezi_os_matches(ezi_os block, ezi_os runtime);

/* ------------------------------------------------------------------ */
/* Context — path resolution & variable expansion                     */
/* ------------------------------------------------------------------ */

typedef struct ezi_context ezi_context;

ezi_context *ezi_context_create(const char *target, const char *script_dir);
void         ezi_context_destroy(ezi_context *ctx);

void ezi_context_set_home(ezi_context *ctx, const char *home);
void ezi_context_set_cwd(ezi_context *ctx, const char *cwd);
void ezi_context_set_os(ezi_context *ctx, ezi_os os);

const char *ezi_context_target(const ezi_context *ctx);
const char *ezi_context_script_dir(const ezi_context *ctx);

/** Expand {{target}}, {{home}}, {{script}}, {{cwd}}, {{os}} and ~ paths. */
char *ezi_expand_vars(ezi_context *ctx, const char *text);

/** Resolve a destination path relative to target. */
char *ezi_resolve_path(ezi_context *ctx, const char *raw);

/** Resolve a source path (script dir first, then target). */
char *ezi_resolve_source(ezi_context *ctx, const char *raw);

void ezi_free(void *ptr);

/* ------------------------------------------------------------------ */
/* Install primitives                                                 */
/* ------------------------------------------------------------------ */

typedef void (*ezi_log_fn)(void *userdata, const char *message);
typedef void (*ezi_progress_fn)(void *userdata, int64_t downloaded, int64_t total);

typedef struct ezi_ops {
    ezi_log_fn      log;
    ezi_progress_fn progress;
    void           *userdata;
    int             dry_run;
} ezi_ops;

ezi_status ezi_mkdir(ezi_context *ctx, ezi_ops *ops, const char *path);
ezi_status ezi_download(ezi_context *ctx, ezi_ops *ops, const char *url, const char *dest);
ezi_status ezi_extract(ezi_context *ctx, ezi_ops *ops, const char *archive,
                       const char *dest, const char *file_filter);
ezi_status ezi_copy(ezi_context *ctx, ezi_ops *ops, const char *src, const char *dest);
ezi_status ezi_move(ezi_context *ctx, ezi_ops *ops, const char *src, const char *dest);
ezi_status ezi_delete(ezi_context *ctx, ezi_ops *ops, const char *path);
ezi_status ezi_write(ezi_context *ctx, ezi_ops *ops, const char *path, const char *content);
ezi_status ezi_run(ezi_context *ctx, ezi_ops *ops, const char *command);
ezi_status ezi_chmod(ezi_context *ctx, ezi_ops *ops, const char *mode, const char *path);
ezi_status ezi_env(ezi_context *ctx, ezi_ops *ops, const char *var,
                   const char *action, const char *value, char ***env_notes, size_t *env_count);

/* ------------------------------------------------------------------ */
/* Script model & parser                                              */
/* ------------------------------------------------------------------ */

typedef struct ezi_meta {
    char          *name;
    char          *target;
    char          *version;
    ezi_on_error   on_error;
} ezi_meta;

typedef struct ezi_step ezi_step;
typedef struct ezi_script ezi_script;

ezi_status ezi_load_script(const char *path, ezi_script **out, char **error);
ezi_status ezi_parse_script_string(const char *text, const char *source_label,
                                   ezi_script **out, char **error);

void ezi_script_destroy(ezi_script *script);

const ezi_meta   *ezi_script_meta(const ezi_script *script);
const ezi_step  **ezi_script_steps(const ezi_script *script, size_t *count);
const char       *ezi_script_path(const ezi_script *script);

const char *ezi_step_kind_name(const ezi_step *step);
int         ezi_step_line(const ezi_step *step);
ezi_os      ezi_step_os(const ezi_step *step);

/* ------------------------------------------------------------------ */
/* Executor                                                           */
/* ------------------------------------------------------------------ */

typedef struct ezi_result {
    ezi_status  status;
    char       *error;
    char      **env_notes;
    size_t      env_count;
} ezi_result;

void ezi_result_free(ezi_result *result);

ezi_result ezi_run_script(ezi_script *script, ezi_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* EZINSTALL_H */
