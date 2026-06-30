#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef EZI_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef EZI_HAVE_CURL
#include <curl/curl.h>
#endif

struct dl_ctx {
    FILE           *file;
    ezi_progress_fn progress;
    void           *userdata;
    int64_t         total;
    int64_t         downloaded;
};

#ifdef EZI_HAVE_CURL
static size_t dl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct dl_ctx *ctx = userdata;
    size_t n = size * nmemb;
    if (fwrite(ptr, 1, n, ctx->file) != n) return 0;
    ctx->downloaded += (int64_t)n;
    if (ctx->progress) ctx->progress(ctx->userdata, ctx->downloaded, ctx->total);
    return n;
}
#endif

static ezi_status download_curl(ezi_context *ctx, ezi_ops *ops, const char *url, const char *dest) {
#ifdef EZI_HAVE_CURL
    (void)ctx;
    char *parent = ezi_path_dirname(dest);
    ezi_mkdir_p(parent);
    free(parent);

    FILE *out = fopen(dest, "wb");
    if (!out) return EZI_ERR_IO;

    struct dl_ctx dl = { out, ops ? ops->progress : NULL, ops ? ops->userdata : NULL, 0, 0 };
    CURL *curl = curl_easy_init();
    if (!curl) { fclose(out); return EZI_ERR_NETWORK; }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ezinstall/0.1");

    CURLcode rc = curl_easy_perform(curl);
    double cl = 0;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
    if (cl > 0) dl.total = (int64_t)cl;

    curl_easy_cleanup(curl);
    fclose(out);

    if (rc != CURLE_OK) return EZI_ERR_NETWORK;
    return EZI_OK;
#else
    (void)ctx; (void)ops;
    char *parent = ezi_path_dirname(dest);
    ezi_mkdir_p(parent);
    free(parent);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -fsSL -o \"%s\" \"%s\"", dest, url);
    ezi_log(ops, "would run: %s", cmd);
    if (ops && ops->dry_run) return EZI_OK;
    if (system(cmd) != 0) return EZI_ERR_NETWORK;
    return EZI_OK;
#endif
}

ezi_status ezi_mkdir(ezi_context *ctx, ezi_ops *ops, const char *path) {
    char *resolved = ezi_resolve_path(ctx, path);
    if (!resolved) return EZI_ERR_OOM;

    if (ops && ops->dry_run) {
        ezi_log(ops, "would create directory: %s", resolved);
        free(resolved);
        return EZI_OK;
    }
    if (ezi_mkdir_p(resolved) != 0) {
        free(resolved);
        return EZI_ERR_IO;
    }
    free(resolved);
    return EZI_OK;
}

ezi_status ezi_download(ezi_context *ctx, ezi_ops *ops, const char *url, const char *dest) {
    char *expanded_url = ezi_expand_vars(ctx, url);
    char *resolved = ezi_resolve_path(ctx, dest);
    if (!expanded_url || !resolved) {
        free(expanded_url);
        free(resolved);
        return EZI_ERR_OOM;
    }

    if (ops && ops->dry_run) {
        ezi_log(ops, "would download %s -> %s", expanded_url, resolved);
        free(expanded_url);
        free(resolved);
        return EZI_OK;
    }

    ezi_status st = download_curl(ctx, ops, expanded_url, resolved);
    free(expanded_url);
    free(resolved);
    return st;
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t sl = strlen(s), su = strlen(suffix);
    if (su > sl) return 0;
#ifdef _WIN32
    return _stricmp(s + sl - su, suffix) == 0;
#else
    return strcasecmp(s + sl - su, suffix) == 0;
#endif
}

static ezi_status extract_via_command(ezi_ops *ops, const char *archive, const char *dest) {
    char cmd[4096];
#ifdef EZI_PLATFORM_WINDOWS
    if (ends_with_ci(archive, ".zip")) {
        snprintf(cmd, sizeof(cmd),
                 "powershell -NoProfile -Command \"Expand-Archive -Force -Path '%s' -DestinationPath '%s'\"",
                 archive, dest);
    } else {
        return EZI_ERR_UNSUPPORTED;
    }
#else
    if (ends_with_ci(archive, ".zip")) {
        snprintf(cmd, sizeof(cmd), "unzip -oq \"%s\" -d \"%s\"", archive, dest);
    } else if (strstr(archive, ".tar") || ends_with_ci(archive, ".tgz")) {
        snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\"", archive, dest);
    } else {
        return EZI_ERR_UNSUPPORTED;
    }
#endif
    if (ops && ops->dry_run) {
        ezi_log(ops, "would run: %s", cmd);
        return EZI_OK;
    }
    return system(cmd) == 0 ? EZI_OK : EZI_ERR_IO;
}

ezi_status ezi_extract(ezi_context *ctx, ezi_ops *ops, const char *archive,
                       const char *dest, const char *file_filter) {
    (void)file_filter;
    char *resolved_arc = ezi_resolve_path(ctx, archive);
    char *resolved_dest = ezi_resolve_path(ctx, dest);
    if (!resolved_arc || !resolved_dest) {
        free(resolved_arc);
        free(resolved_dest);
        return EZI_ERR_OOM;
    }

    struct stat st;
    if (stat(resolved_arc, &st) != 0) {
        free(resolved_arc);
        free(resolved_dest);
        return EZI_ERR_IO;
    }

    if (ops && ops->dry_run) {
        ezi_log(ops, "would extract %s -> %s", resolved_arc, resolved_dest);
        free(resolved_arc);
        free(resolved_dest);
        return EZI_OK;
    }

    ezi_mkdir_p(resolved_dest);
    ezi_status rc = extract_via_command(ops, resolved_arc, resolved_dest);
    free(resolved_arc);
    free(resolved_dest);
    return rc;
}

static int path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

ezi_status ezi_copy(ezi_context *ctx, ezi_ops *ops, const char *src, const char *dest) {
    char *resolved_src = ezi_resolve_source(ctx, src);
    char *resolved_dest = ezi_resolve_path(ctx, dest);
    if (!resolved_src || !resolved_dest) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_ERR_OOM;
    }

    struct stat st;
    if (stat(resolved_src, &st) != 0) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_ERR_IO;
    }

    if (ops && ops->dry_run) {
        ezi_log(ops, "would copy %s -> %s", resolved_src, resolved_dest);
        free(resolved_src);
        free(resolved_dest);
        return EZI_OK;
    }

    char *parent = ezi_path_dirname(resolved_dest);
    ezi_mkdir_p(parent);
    free(parent);

    int rc = path_is_dir(resolved_src)
        ? ezi_copy_tree(resolved_src, resolved_dest)
        : ezi_copy_file(resolved_src, resolved_dest);

    free(resolved_src);
    free(resolved_dest);
    return rc == 0 ? EZI_OK : EZI_ERR_IO;
}

ezi_status ezi_move(ezi_context *ctx, ezi_ops *ops, const char *src, const char *dest) {
    char *resolved_src = ezi_resolve_source(ctx, src);
    char *resolved_dest = ezi_resolve_path(ctx, dest);
    if (!resolved_src || !resolved_dest) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_ERR_OOM;
    }

    struct stat st;
    if (stat(resolved_src, &st) != 0) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_ERR_IO;
    }

    if (ops && ops->dry_run) {
        ezi_log(ops, "would move %s -> %s", resolved_src, resolved_dest);
        free(resolved_src);
        free(resolved_dest);
        return EZI_OK;
    }

    char *parent = ezi_path_dirname(resolved_dest);
    ezi_mkdir_p(parent);
    free(parent);

#ifdef EZI_PLATFORM_WINDOWS
    if (MoveFileExA(resolved_src, resolved_dest, MOVEFILE_REPLACE_EXISTING)) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_OK;
    }
    free(resolved_src);
    free(resolved_dest);
    return EZI_ERR_IO;
#else
    if (rename(resolved_src, resolved_dest) == 0) {
        free(resolved_src);
        free(resolved_dest);
        return EZI_OK;
    }
    ezi_status cp = ezi_copy(ctx, ops, src, dest);
    if (cp == EZI_OK) ezi_delete(ctx, ops, src);
    free(resolved_src);
    free(resolved_dest);
    return cp;
#endif
}

ezi_status ezi_delete(ezi_context *ctx, ezi_ops *ops, const char *path) {
    char *resolved = ezi_resolve_path(ctx, path);
    if (!resolved) return EZI_ERR_OOM;

    if (ops && ops->dry_run) {
        ezi_log(ops, "would delete %s", resolved);
        free(resolved);
        return EZI_OK;
    }

    struct stat st;
    if (stat(resolved, &st) != 0) {
        free(resolved);
        return EZI_OK;
    }

    int rc = S_ISDIR(st.st_mode) ? ezi_remove_tree(resolved) : remove(resolved);
    free(resolved);
    return rc == 0 ? EZI_OK : EZI_ERR_IO;
}

ezi_status ezi_write(ezi_context *ctx, ezi_ops *ops, const char *path, const char *content) {
    char *resolved = ezi_resolve_path(ctx, path);
    char *expanded = ezi_expand_vars(ctx, content);
    if (!resolved || !expanded) {
        free(resolved);
        free(expanded);
        return EZI_ERR_OOM;
    }

    if (ops && ops->dry_run) {
        ezi_log(ops, "would write %s (%zu bytes)", resolved, strlen(expanded));
        free(resolved);
        free(expanded);
        return EZI_OK;
    }

    char *parent = ezi_path_dirname(resolved);
    ezi_mkdir_p(parent);
    free(parent);

    FILE *f = fopen(resolved, "wb");
    if (!f) {
        free(resolved);
        free(expanded);
        return EZI_ERR_IO;
    }
    size_t n = strlen(expanded);
    if (fwrite(expanded, 1, n, f) != n) {
        fclose(f);
        free(resolved);
        free(expanded);
        return EZI_ERR_IO;
    }
    fclose(f);
    free(resolved);
    free(expanded);
    return EZI_OK;
}

ezi_status ezi_run(ezi_context *ctx, ezi_ops *ops, const char *command) {
    char *cmd = ezi_expand_vars(ctx, command);
    if (!cmd) return EZI_ERR_OOM;

    if (ops && ops->dry_run) {
        ezi_log(ops, "would run: %s", cmd);
        free(cmd);
        return EZI_OK;
    }

    int rc = system(cmd);
    free(cmd);
    return rc == 0 ? EZI_OK : EZI_ERR_INSTALL;
}

ezi_status ezi_chmod(ezi_context *ctx, ezi_ops *ops, const char *mode, const char *path) {
#ifndef EZI_PLATFORM_WINDOWS
    char *resolved = ezi_resolve_path(ctx, path);
    if (!resolved) return EZI_ERR_OOM;

    if (ops && ops->dry_run) {
        ezi_log(ops, "would chmod %s %s", mode, resolved);
        free(resolved);
        return EZI_OK;
    }

    struct stat st;
    if (stat(resolved, &st) != 0) {
        free(resolved);
        return EZI_ERR_IO;
    }

    unsigned m = (unsigned)strtoul(mode, NULL, 8);
    if (chmod(resolved, (mode_t)m) != 0) {
        free(resolved);
        return EZI_ERR_IO;
    }
    free(resolved);
    return EZI_OK;
#else
    (void)ctx; (void)ops; (void)mode; (void)path;
    return EZI_OK;
#endif
}

ezi_status ezi_env(ezi_context *ctx, ezi_ops *ops, const char *var,
                   const char *action, const char *value,
                   char ***env_notes, size_t *env_count) {
    char *expanded = ezi_expand_vars(ctx, value);
    if (!expanded) return EZI_ERR_OOM;

    char note[1024];
    if (ezi_str_ieq(action, "append")) {
        snprintf(note, sizeof(note), "export %s=\"${%s}:%s\"", var, var, expanded);
    } else if (ezi_str_ieq(action, "prepend")) {
        snprintf(note, sizeof(note), "export %s=\"%s:${%s}\"", var, expanded, var);
    } else {
        snprintf(note, sizeof(note), "export %s='%s'", var, expanded);
    }

    if (env_notes && env_count) {
        char **notes = *env_notes;
        size_t cap = *env_count + 1;
        notes = realloc(notes, cap * sizeof(char *));
        if (!notes) {
            free(expanded);
            return EZI_ERR_OOM;
        }
        notes[*env_count] = ezi_strdup(note);
        (*env_count)++;
        *env_notes = notes;
    }

    if (!(ops && ops->dry_run)) {
        if (ezi_str_ieq(action, "set")) {
#ifdef EZI_PLATFORM_WINDOWS
            SetEnvironmentVariableA(var, expanded);
#else
            setenv(var, expanded, 1);
#endif
        }
    }

    ezi_log(ops, "%s", note);
    free(expanded);
    return EZI_OK;
}
