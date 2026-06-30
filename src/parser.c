#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, char **error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error) *error = ezi_strdup("cannot open script file");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        if (error) *error = ezi_strdup("out of memory");
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static char *strip_comment(const char *line) {
    int in_quote = 0;
    for (size_t i = 0; line[i]; i++) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) in_quote = !in_quote;
        else if (line[i] == '#' && !in_quote) return ezi_strndup(line, i);
    }
    return ezi_strdup(line);
}

static int line_is_blank(const char *s) {
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

static ezi_status push_step(ezi_script *script, ezi_step step) {
    EZI_ARRAY_GROW(script->steps, script->step_count, script->step_cap, ezi_step);
    script->steps[script->step_count++] = step;
    return EZI_OK;
}

void ezi_step_free(ezi_step *s) {
    free(s->s1);
    free(s->s2);
    free(s->s3);
    free(s->content);
    memset(s, 0, sizeof(*s));
}

static void step_free(ezi_step *s) {
    ezi_step_free(s);
}

static ezi_status parse_error(char **error, int line, const char *msg) {
    if (error) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Line %d: %s", line, msg);
        *error = ezi_strdup(buf);
    }
    return EZI_ERR_PARSE;
}

static int match_as_to(const char *rest, char **src, char **dest) {
    const char *as = strstr(rest, " AS ");
    const char *to = strstr(rest, " TO ");
    const char *hit = as;
    if (!hit || (to && to < hit)) hit = to;
    if (!hit) {
        hit = strstr(rest, " as ");
        if (!hit) hit = strstr(rest, " to ");
    }
    if (!hit) return 0;

    *src = ezi_strndup(rest, (size_t)(hit - rest));
    *dest = ezi_strdup(hit + 4);
    ezi_str_trim(*src);
    ezi_str_trim(*dest);
    return 1;
}

static ezi_status parse_command(ezi_script *script, char **lines, size_t count,
                                size_t *index, ezi_os os_block, char **error) {
    size_t i = *index;
    char *line = strip_comment(lines[i]);
    line = ezi_str_trim(line);
    if (line_is_blank(line)) { free(line); (*index)++; return EZI_OK; }

    int line_no = (int)i + 1;

    if (line[0] == '@') {
        const char *body = line + 1;
        char key[32] = {0};
        const char *val = body;
        while (*val && !isspace((unsigned char)*val)) val++;
        size_t klen = (size_t)(val - body);
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, body, klen);
        while (*val && isspace((unsigned char)*val)) val++;

        if (ezi_str_ieq(key, "name")) {
            free(script->meta.name);
            script->meta.name = ezi_strdup(val);
        } else if (ezi_str_ieq(key, "target")) {
            free(script->meta.target);
            script->meta.target = ezi_strdup(val);
        } else if (ezi_str_ieq(key, "version")) {
            free(script->meta.version);
            script->meta.version = ezi_strdup(val);
        } else if (ezi_str_ieq(key, "on_error")) {
            script->meta.on_error = ezi_str_ieq(val, "continue")
                ? EZI_ON_ERROR_CONTINUE : EZI_ON_ERROR_STOP;
        } else {
            free(line);
            return parse_error(error, line_no, "invalid directive");
        }
        free(line);
        (*index)++;
        return EZI_OK;
    }

    if (ezi_str_ieq(line, "END OS") || ezi_str_ieq(line, "END")) {
        free(line);
        return EZI_ERR_PARSE;
    }

    if (strncmp(line, "OS ", 3) == 0 || strncmp(line, "os ", 3) == 0) {
        ezi_os block = ezi_parse_os_name(line + 3);
        if (block == EZI_OS_ALL) {
            free(line);
            return parse_error(error, line_no, "unknown OS name");
        }
        (*index)++;
        while (*index < count) {
            char *trimmed = strip_comment(lines[*index]);
            trimmed = ezi_str_trim(trimmed);
            if (ezi_str_ieq(trimmed, "END OS") || ezi_str_ieq(trimmed, "END")) {
                free(trimmed);
                (*index)++;
                break;
            }
            free(trimmed);
            ezi_status st = parse_command(script, lines, count, index, block, error);
            if (st != EZI_OK) { free(line); return st; }
        }
        free(line);
        return EZI_OK;
    }

    ezi_step step = {0};
    step.line = line_no;
    step.os = os_block;

    if (strncmp(line, "MKDIR ", 6) == 0 || strncmp(line, "mkdir ", 6) == 0) {
        step.kind = EZI_STEP_MKDIR;
        step.s1 = ezi_strdup(line + 6);
        ezi_str_trim(step.s1);
    } else if (strncmp(line, "GET ", 4) == 0 || strncmp(line, "get ", 4) == 0) {
        char *url = NULL, *dest = NULL;
        if (!match_as_to(line + 4, &url, &dest)) {
            free(line);
            return parse_error(error, line_no, "GET requires: GET URL AS path");
        }
        step.kind = EZI_STEP_GET;
        step.s1 = url;
        step.s2 = dest;
    } else if (strncmp(line, "EXTRACT ", 8) == 0) {
        char *arc = NULL, *dest = NULL;
        if (!match_as_to(line + 8, &arc, &dest)) {
            free(line);
            return parse_error(error, line_no, "EXTRACT requires: EXTRACT archive TO dest");
        }
        step.kind = EZI_STEP_EXTRACT;
        step.s1 = arc;
        step.s2 = dest;
    } else if (strncmp(line, "COPY ", 5) == 0) {
        char *src = NULL, *dest = NULL;
        if (!match_as_to(line + 5, &src, &dest)) {
            free(line);
            return parse_error(error, line_no, "COPY requires: COPY src TO dest");
        }
        step.kind = EZI_STEP_COPY;
        step.s1 = src;
        step.s2 = dest;
    } else if (strncmp(line, "MOVE ", 5) == 0) {
        char *src = NULL, *dest = NULL;
        if (!match_as_to(line + 5, &src, &dest)) {
            free(line);
            return parse_error(error, line_no, "MOVE requires: MOVE src TO dest");
        }
        step.kind = EZI_STEP_MOVE;
        step.s1 = src;
        step.s2 = dest;
    } else if (strncmp(line, "DELETE ", 7) == 0) {
        step.kind = EZI_STEP_DELETE;
        step.s1 = ezi_strdup(line + 7);
        ezi_str_trim(step.s1);
    } else if (strncmp(line, "RUN ", 4) == 0) {
        step.kind = EZI_STEP_RUN;
        step.s1 = ezi_strdup(line + 4);
    } else if (strncmp(line, "CHMOD ", 6) == 0) {
        char *mode = line + 6;
        char *sp = strchr(mode, ' ');
        if (!sp) {
            free(line);
            return parse_error(error, line_no, "CHMOD requires: CHMOD mode path");
        }
        *sp = '\0';
        step.kind = EZI_STEP_CHMOD;
        step.s1 = ezi_strdup(mode);
        step.s2 = ezi_strdup(sp + 1);
        ezi_str_trim(step.s2);
    } else if (strncmp(line, "ENV ", 4) == 0) {
        step.kind = EZI_STEP_ENV;
        char *rest = line + 4;
        char *var = rest;
        char *sp = strchr(rest, ' ');
        if (!sp) {
            free(line);
            return parse_error(error, line_no, "ENV requires a variable name");
        }
        *sp = '\0';
        step.s1 = ezi_strdup(var);
        rest = sp + 1;
        if (strstr(rest, "append")) {
            step.s2 = ezi_strdup("append");
            const char *val = strstr(rest, "append") + 6;
            while (*val && isspace((unsigned char)*val)) val++;
            step.s3 = ezi_strdup(val);
        } else if (strstr(rest, "prepend")) {
            step.s2 = ezi_strdup("prepend");
            const char *val = strstr(rest, "prepend") + 7;
            while (*val && isspace((unsigned char)*val)) val++;
            step.s3 = ezi_strdup(val);
        } else {
            step.s2 = ezi_strdup("set");
            step.s3 = ezi_strdup(rest);
            ezi_str_trim(step.s3);
        }
    } else if (strncmp(line, "WRITE ", 6) == 0) {
        step.kind = EZI_STEP_WRITE;
        step.s1 = ezi_strdup(line + 6);
        ezi_str_trim(step.s1);
        (*index)++;
        if (*index >= count || strcmp(lines[*index], "---") != 0) {
            step_free(&step);
            free(line);
            return parse_error(error, line_no, "WRITE must be followed by --- block");
        }
        (*index)++;
        size_t cap = 256, len = 0;
        char *content = malloc(cap);
        if (!content) { step_free(&step); free(line); return EZI_ERR_OOM; }
        content[0] = '\0';
        while (*index < count && strcmp(lines[*index], "---") != 0) {
            const char *ln = lines[*index];
            size_t llen = strlen(ln);
            if (len + llen + 2 >= cap) {
                cap = (len + llen + 2) * 2;
                char *tmp = realloc(content, cap);
                if (!tmp) { free(content); step_free(&step); free(line); return EZI_ERR_OOM; }
                content = tmp;
            }
            if (len > 0) { content[len++] = '\n'; content[len] = '\0'; }
            memcpy(content + len, ln, llen);
            len += llen;
            content[len] = '\0';
            (*index)++;
        }
        if (*index >= count) {
            free(content);
            step_free(&step);
            free(line);
            return parse_error(error, line_no, "unclosed WRITE block");
        }
        step.content = content;
        (*index)++;
        ezi_status st = push_step(script, step);
        free(line);
        return st;
    } else if (strncmp(line, "INCLUDE ", 8) == 0) {
        step.kind = EZI_STEP_INCLUDE;
        step.s1 = ezi_strdup(line + 8);
        ezi_str_trim(step.s1);
    } else {
        free(line);
        return parse_error(error, line_no, "unknown instruction");
    }

    ezi_status st = push_step(script, step);
    free(line);
    (*index)++;
    return st;
}

static ezi_status parse_lines(ezi_script *script, char **lines, size_t count, char **error) {
    size_t i = 0;
    while (i < count) {
        ezi_status st = parse_command(script, lines, count, &i, EZI_OS_ALL, error);
        if (st != EZI_OK) return st;
    }
    if (script->step_count == 0) {
        if (error) *error = ezi_strdup("script contains no steps");
        return EZI_ERR_PARSE;
    }
    return EZI_OK;
}

static char **split_lines(char *text, size_t *count) {
    size_t cap = 64, n = 0;
    char **lines = malloc(cap * sizeof(char *));
    if (!lines) return NULL;

    char *cursor = text;
    while (*cursor) {
        char *nl = strchr(cursor, '\n');
        if (nl) *nl = '\0';
        if (n >= cap) {
            cap *= 2;
            char **tmp = realloc(lines, cap * sizeof(char *));
            if (!tmp) { free(lines); return NULL; }
            lines = tmp;
        }
        lines[n++] = cursor;
        if (!nl) break;
        *nl = '\0';
        cursor = nl + 1;
        if (*cursor == '\r') cursor++;
    }
    *count = n;
    return lines;
}

ezi_status ezi_parse_script_string(const char *text, const char *source_label,
                                   ezi_script **out, char **error) {
    if (!text || !out) return EZI_ERR_PARSE;
    *out = NULL;
    if (error) *error = NULL;

    ezi_script *script = calloc(1, sizeof(*script));
    if (!script) return EZI_ERR_OOM;

    script->meta.name = ezi_strdup("EziScript Installer");
    script->meta.target = ezi_strdup(".");
    script->meta.version = ezi_strdup("1.0");
    script->meta.on_error = EZI_ON_ERROR_STOP;
    script->source_path = ezi_strdup(source_label ? source_label : "<string>");

    char *copy = ezi_strdup(text);
    if (!copy) { ezi_script_destroy(script); return EZI_ERR_OOM; }

    size_t count = 0;
    char **lines = split_lines(copy, &count);
    if (!lines) {
        free(copy);
        ezi_script_destroy(script);
        return EZI_ERR_OOM;
    }

    ezi_status st = parse_lines(script, lines, count, error);
    free(lines);
    free(copy);

    if (st != EZI_OK) {
        ezi_script_destroy(script);
        return st;
    }

    *out = script;
    return EZI_OK;
}

ezi_status ezi_load_script(const char *path, ezi_script **out, char **error) {
    char *text = read_file(path, error);
    if (!text) return EZI_ERR_IO;
    ezi_status st = ezi_parse_script_string(text, path, out, error);
    free(text);
    return st;
}

void ezi_script_destroy(ezi_script *script) {
    if (!script) return;
    free(script->meta.name);
    free(script->meta.target);
    free(script->meta.version);
    free(script->source_path);
    for (size_t i = 0; i < script->step_count; i++) step_free(&script->steps[i]);
    free(script->steps);
    free(script);
}

const ezi_meta *ezi_script_meta(const ezi_script *script) {
    return script ? &script->meta : NULL;
}

const ezi_step **ezi_script_steps(const ezi_script *script, size_t *count) {
    if (count) *count = script ? script->step_count : 0;
    return (const ezi_step **)(script ? script->steps : NULL);
}

const char *ezi_script_path(const ezi_script *script) {
    return script ? script->source_path : NULL;
}

const char *ezi_step_kind_name(const ezi_step *step) {
    if (!step) return "";
    switch (step->kind) {
    case EZI_STEP_MKDIR:   return "mkdir";
    case EZI_STEP_GET:     return "get";
    case EZI_STEP_EXTRACT: return "extract";
    case EZI_STEP_COPY:    return "copy";
    case EZI_STEP_MOVE:    return "move";
    case EZI_STEP_DELETE:  return "delete";
    case EZI_STEP_WRITE:   return "write";
    case EZI_STEP_RUN:     return "run";
    case EZI_STEP_CHMOD:   return "chmod";
    case EZI_STEP_ENV:     return "env";
    case EZI_STEP_INCLUDE: return "include";
    default:               return "unknown";
    }
}

int ezi_step_line(const ezi_step *step) {
    return step ? step->line : 0;
}

ezi_os ezi_step_os(const ezi_step *step) {
    return step ? step->os : EZI_OS_ALL;
}

/* Expose step internals to executor via accessor in internal header */
ezi_step_kind ezi_step_get_kind(const ezi_step *step) { return step->kind; }
const char *ezi_step_arg1(const ezi_step *step) { return step ? step->s1 : NULL; }
const char *ezi_step_arg2(const ezi_step *step) { return step ? step->s2 : NULL; }
const char *ezi_step_arg3(const ezi_step *step) { return step ? step->s3 : NULL; }
const char *ezi_step_content(const ezi_step *step) { return step ? step->content : NULL; }
