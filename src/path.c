#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EZI_PLATFORM_WINDOWS
#include <windows.h>
#endif

void ezi_free(void *ptr) {
    free(ptr);
}

ezi_context *ezi_context_create(const char *target, const char *script_dir) {
    ezi_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->target = ezi_strdup(target ? target : ".");
    ctx->script_dir = ezi_strdup(script_dir ? script_dir : ".");
    ctx->home = ezi_home_dir();
    ctx->cwd = ezi_cwd_dir();
    ctx->os = ezi_current_os();

    if (!ctx->target || !ctx->script_dir || !ctx->home || !ctx->cwd) {
        ezi_context_destroy(ctx);
        return NULL;
    }
    return ctx;
}

void ezi_context_destroy(ezi_context *ctx) {
    if (!ctx) return;
    free(ctx->target);
    free(ctx->script_dir);
    free(ctx->home);
    free(ctx->cwd);
    free(ctx);
}

void ezi_context_set_home(ezi_context *ctx, const char *home) {
    if (!ctx || !home) return;
    free(ctx->home);
    ctx->home = ezi_strdup(home);
}

void ezi_context_set_cwd(ezi_context *ctx, const char *cwd) {
    if (!ctx || !cwd) return;
    free(ctx->cwd);
    ctx->cwd = ezi_strdup(cwd);
}

void ezi_context_set_os(ezi_context *ctx, ezi_os os) {
    if (ctx) ctx->os = os;
}

const char *ezi_context_target(const ezi_context *ctx) {
    return ctx ? ctx->target : NULL;
}

const char *ezi_context_script_dir(const ezi_context *ctx) {
    return ctx ? ctx->script_dir : NULL;
}

static void replace_all(char **out, const char *key, const char *value) {
    const char *src = *out;
    size_t key_len = strlen(key);
    size_t val_len = strlen(value);
    size_t cap = strlen(src) + 1;
    char *buf = malloc(cap);
    if (!buf) return;

    size_t len = 0;
    const char *cursor = src;
    while (*cursor) {
        const char *hit = strstr(cursor, key);
        if (!hit) {
            size_t rest = strlen(cursor);
            if (len + rest + 1 > cap) {
                cap = len + rest + 64;
                char *tmp = realloc(buf, cap);
                if (!tmp) { free(buf); return; }
                buf = tmp;
            }
            memcpy(buf + len, cursor, rest);
            len += rest;
            break;
        }
        size_t chunk = (size_t)(hit - cursor);
        if (len + chunk + val_len + 1 > cap) {
            cap = len + chunk + val_len + 64;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return; }
            buf = tmp;
        }
        memcpy(buf + len, cursor, chunk);
        len += chunk;
        memcpy(buf + len, value, val_len);
        len += val_len;
        cursor = hit + key_len;
    }
    buf[len] = '\0';
    free(*out);
    *out = buf;
}

char *ezi_expand_vars(ezi_context *ctx, const char *text) {
    if (!text) return NULL;

    char *result = ezi_strdup(text);
    if (!result) return NULL;

    if (ctx) {
        replace_all(&result, "{{target}}", ctx->target);
        replace_all(&result, "{{script}}", ctx->script_dir);
        replace_all(&result, "{{home}}", ctx->home);
        replace_all(&result, "{{cwd}}", ctx->cwd);
        replace_all(&result, "{{os}}", ezi_os_name(ctx->os));
    }

    if (result[0] == '~') {
        const char *home = ctx ? ctx->home : NULL;
        char *owned_home = NULL;
        if (!home) {
            owned_home = ezi_home_dir();
            home = owned_home;
        }
        if (home) {
            const char *suffix = result + 1;
            if (*suffix == '/' || *suffix == '\\') suffix++;
            char *joined = ezi_path_join(home, suffix);
            free(result);
            free(owned_home);
            return joined;
        }
    }

    return result;
}

char *ezi_resolve_path(ezi_context *ctx, const char *raw) {
    char *expanded = ezi_expand_vars(ctx, raw);
    if (!expanded) return NULL;

    if (ezi_path_is_absolute(expanded)) return expanded;

    char *joined = ezi_path_join(ctx->target, expanded);
    free(expanded);
    return joined;
}

static int path_exists(const char *path) {
#ifdef EZI_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
#endif
}

char *ezi_resolve_source(ezi_context *ctx, const char *raw) {
    char *expanded = ezi_expand_vars(ctx, raw);
    if (!expanded) return NULL;

    if (ezi_path_is_absolute(expanded)) return expanded;

    char *from_script = ezi_path_join(ctx->script_dir, expanded);
    if (path_exists(from_script)) {
        free(expanded);
        return from_script;
    }
    free(from_script);
    return ezi_path_join(ctx->target, expanded);
}
