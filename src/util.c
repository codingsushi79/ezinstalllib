#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef EZI_PLATFORM_WINDOWS
#include <direct.h>
#include <io.h>
#include <windows.h>
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

char *ezi_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

char *ezi_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *ezi_str_trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

int ezi_str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

int ezi_path_is_absolute(const char *path) {
    if (!path || !*path) return 0;
#ifdef EZI_PLATFORM_WINDOWS
    if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) {
        return path[1] == ':';
    }
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

char *ezi_path_join(const char *a, const char *b) {
    if (!a || !*a) return ezi_strdup(b);
    if (!b || !*b) return ezi_strdup(a);

    size_t alen = strlen(a);
    int needs_sep = a[alen - 1] != '/' && a[alen - 1] != '\\';
    size_t blen = strlen(b);
    char *out = malloc(alen + needs_sep + blen + 1);
    if (!out) return NULL;

    memcpy(out, a, alen);
    size_t pos = alen;
    if (needs_sep) out[pos++] = PATH_SEP;
    memcpy(out + pos, b, blen);
    out[pos + blen] = '\0';
    return out;
}

char *ezi_path_dirname(const char *path) {
    if (!path) return ezi_strdup(".");
    const char *last = strrchr(path, '/');
    const char *last2 = strrchr(path, '\\');
    if (last2 && (!last || last2 > last)) last = last2;
    if (!last) return ezi_strdup(".");
    if (last == path) return ezi_strdup(PATH_SEP_STR);
    return ezi_strndup(path, (size_t)(last - path));
}

char *ezi_home_dir(void) {
#ifdef EZI_PLATFORM_WINDOWS
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return ezi_strdup(buf);
    return ezi_strdup("C:\\Users\\Public");
#else
    const char *home = getenv("HOME");
    return ezi_strdup(home ? home : "/");
#endif
}

char *ezi_cwd_dir(void) {
#ifdef EZI_PLATFORM_WINDOWS
    char buf[MAX_PATH];
    if (_getcwd(buf, MAX_PATH)) return ezi_strdup(buf);
#else
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) return ezi_strdup(buf);
#endif
    return ezi_strdup(".");
}

void ezi_log(ezi_ops *ops, const char *fmt, ...) {
    if (!ops || !ops->log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ops->log(ops->userdata, buf);
}

int ezi_mkdir_p(const char *path) {
    if (!path || !*path) return -1;

    char *copy = ezi_strdup(path);
    if (!copy) return -1;

    for (char *p = copy + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
#ifdef EZI_PLATFORM_WINDOWS
            _mkdir(copy);
#else
            mkdir(copy, 0755);
#endif
            *p = PATH_SEP;
        }
    }
#ifdef EZI_PLATFORM_WINDOWS
    int rc = _mkdir(copy);
#else
    int rc = mkdir(copy, 0755);
#endif
    free(copy);
    return rc == 0 || errno == EEXIST ? 0 : -1;
}

int ezi_copy_file(const char *src, const char *dest) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dest, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

#ifndef EZI_PLATFORM_WINDOWS
static int copy_tree_unix(const char *src, const char *dest) {
    struct stat st;
    if (stat(src, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        if (ezi_mkdir_p(dest) != 0 && errno != EEXIST) return -1;
        DIR *dir = opendir(src);
        if (!dir) return -1;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char *child_src = ezi_path_join(src, ent->d_name);
            char *child_dest = ezi_path_join(dest, ent->d_name);
            int rc = copy_tree_unix(child_src, child_dest);
            free(child_src);
            free(child_dest);
            if (rc != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
        return 0;
    }
    char *parent = ezi_path_dirname(dest);
    ezi_mkdir_p(parent);
    free(parent);
    return ezi_copy_file(src, dest);
}
#endif

int ezi_copy_tree(const char *src, const char *dest) {
#ifdef EZI_PLATFORM_WINDOWS
    (void)src;
    (void)dest;
    return -1;
#else
    return copy_tree_unix(src, dest);
#endif
}

int ezi_remove_tree(const char *path) {
#ifdef EZI_PLATFORM_WINDOWS
    (void)path;
    return -1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return -1;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char *child = ezi_path_join(path, ent->d_name);
            int rc = ezi_remove_tree(child);
            free(child);
            if (rc != 0) { closedir(dir); return -1; }
        }
        closedir(dir);
        return rmdir(path);
    }
    return unlink(path);
#endif
}
