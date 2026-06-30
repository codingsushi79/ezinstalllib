#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *resolve_target_path(ezi_context *ctx, const char *raw_target) {
    char *expanded = ezi_expand_vars(ctx, raw_target);
    if (!expanded) return NULL;

    if (ezi_path_is_absolute(expanded)) return expanded;

    char *joined = ezi_path_join(ctx->cwd, expanded);
    free(expanded);
    return joined;
}

static ezi_step step_dup(const ezi_step *src) {
    ezi_step d = *src;
    d.s1 = ezi_strdup(src->s1);
    d.s2 = ezi_strdup(src->s2);
    d.s3 = ezi_strdup(src->s3);
    d.content = ezi_strdup(src->content);
    return d;
}

static ezi_status flatten_steps(ezi_script *script, ezi_context *ctx,
                                ezi_step **out, size_t *count, char **error) {
    ezi_step *flat = NULL;
    size_t flat_count = 0, flat_cap = 0;

    for (size_t i = 0; i < script->step_count; i++) {
        ezi_step *step = &script->steps[i];

        if (step->os != EZI_OS_ALL && !ezi_os_matches(step->os, ctx->os))
            continue;

        if (step->kind == EZI_STEP_INCLUDE) {
            char *include_path = ezi_resolve_source(ctx, step->s1);
            if (!include_path) return EZI_ERR_OOM;

            struct stat st;
            if (stat(include_path, &st) != 0) {
                if (error) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "included script not found: %s", include_path);
                    *error = ezi_strdup(buf);
                }
                free(include_path);
                return EZI_ERR_PARSE;
            }

            ezi_script *nested = NULL;
            ezi_status st_load = ezi_load_script(include_path, &nested, error);
            free(include_path);
            if (st_load != EZI_OK) return st_load;

            ezi_step *nested_flat = NULL;
            size_t nested_count = 0;
            ezi_status st_flat = flatten_steps(nested, ctx, &nested_flat, &nested_count, error);
            ezi_script_destroy(nested);
            if (st_flat != EZI_OK) {
                for (size_t j = 0; j < nested_count; j++) ezi_step_free(&nested_flat[j]);
                free(nested_flat);
                return st_flat;
            }

            for (size_t j = 0; j < nested_count; j++) {
                EZI_ARRAY_GROW(flat, flat_count, flat_cap, ezi_step);
                flat[flat_count++] = nested_flat[j];
            }
            free(nested_flat);
            continue;
        }

        EZI_ARRAY_GROW(flat, flat_count, flat_cap, ezi_step);
        flat[flat_count++] = step_dup(step);
    }

    if (flat_count == 0) {
        free(flat);
        if (error) {
            *error = ezi_strdup("no steps apply to this platform");
        }
        return EZI_ERR_INSTALL;
    }

    *out = flat;
    *count = flat_count;
    return EZI_OK;
}

static ezi_status run_step(ezi_context *ctx, ezi_ops *ops, const ezi_step *step,
                           char ***env_notes, size_t *env_count) {
    switch (ezi_step_get_kind(step)) {
    case EZI_STEP_MKDIR:
        return ezi_mkdir(ctx, ops, ezi_step_arg1(step));
    case EZI_STEP_GET:
        return ezi_download(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step));
    case EZI_STEP_EXTRACT:
        return ezi_extract(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step), ezi_step_arg3(step));
    case EZI_STEP_COPY:
        return ezi_copy(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step));
    case EZI_STEP_MOVE:
        return ezi_move(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step));
    case EZI_STEP_DELETE:
        return ezi_delete(ctx, ops, ezi_step_arg1(step));
    case EZI_STEP_WRITE:
        return ezi_write(ctx, ops, ezi_step_arg1(step), ezi_step_content(step));
    case EZI_STEP_RUN:
        return ezi_run(ctx, ops, ezi_step_arg1(step));
    case EZI_STEP_CHMOD:
        return ezi_chmod(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step));
    case EZI_STEP_ENV:
        return ezi_env(ctx, ops, ezi_step_arg1(step), ezi_step_arg2(step),
                       ezi_step_arg3(step), env_notes, env_count);
    default:
        return EZI_ERR_UNSUPPORTED;
    }
}

void ezi_result_free(ezi_result *result) {
    if (!result) return;
    free(result->error);
    if (result->env_notes) {
        for (size_t i = 0; i < result->env_count; i++) free(result->env_notes[i]);
        free(result->env_notes);
    }
    memset(result, 0, sizeof(*result));
}

ezi_result ezi_run_script(ezi_script *script, ezi_ops *ops) {
    ezi_result result = {0};

    if (!script) {
        result.status = EZI_ERR_PARSE;
        result.error = ezi_strdup("no script");
        return result;
    }

    char *script_dir = ezi_path_dirname(script->source_path);
    ezi_context *ctx = ezi_context_create(script->meta.target, script_dir);
    free(script_dir);
    if (!ctx) {
        result.status = EZI_ERR_OOM;
        result.error = ezi_strdup("out of memory");
        return result;
    }

    char *resolved_target = resolve_target_path(ctx, script->meta.target);
    if (resolved_target) {
        free(ctx->target);
        ctx->target = resolved_target;
    }

    ezi_step *steps = NULL;
    size_t step_count = 0;
    result.status = flatten_steps(script, ctx, &steps, &step_count, &result.error);
    if (result.status != EZI_OK) {
        ezi_context_destroy(ctx);
        return result;
    }

    for (size_t i = 0; i < step_count; i++) {
        ezi_status st = run_step(ctx, ops, &steps[i], &result.env_notes, &result.env_count);
        if (st != EZI_OK) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[line %d] %s", steps[i].line, ezi_status_string(st));
            result.error = ezi_strdup(buf);
            result.status = st;
            if (script->meta.on_error == EZI_ON_ERROR_CONTINUE) {
                ezi_log(ops, "WARNING: %s", buf);
                result.status = EZI_OK;
                free(result.error);
                result.error = NULL;
                continue;
            }
            break;
        }
    }

    for (size_t i = 0; i < step_count; i++)
        ezi_step_free(&steps[i]);
    free(steps);
    ezi_context_destroy(ctx);
    if (result.status == EZI_OK && result.error) {
        free(result.error);
        result.error = NULL;
    }
    return result;
}
