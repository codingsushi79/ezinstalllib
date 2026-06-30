#include "ezinstalllib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_cb(void *userdata, const char *message) {
    (void)userdata;
    if (message && *message) printf("  · %s\n", message);
}

static void print_header(const ezi_script *script, int dry_run) {
    const ezi_meta *meta = ezi_script_meta(script);
    printf("╭──────────────────────────────────────────╮\n");
    printf("│ ezinstalllib");
    if (dry_run) printf(" · dry run");
    printf("%*s│\n", dry_run ? 18 : 30, "");
    printf("├──────────────────────────────────────────┤\n");
    printf("│ %s v%s\n", meta->name ? meta->name : "Installer",
           meta->version ? meta->version : "1.0");
    printf("│ Script  %s\n", ezi_script_path(script));
    printf("│ Target  %s\n", meta->target ? meta->target : ".");
    printf("│ Platform %s\n", ezi_os_name(ezi_current_os()));
    printf("╰──────────────────────────────────────────╯\n\n");
}

static void print_footer(int dry_run, const ezi_result *result) {
    if (result->env_count > 0) {
        printf("\n! Environment notes\n");
        for (size_t i = 0; i < result->env_count; i++)
            printf("  · %s\n", result->env_notes[i]);
    }
    printf("\n");
    if (dry_run)
        printf("╭ Preview complete — no changes were made. ╮\n");
    else if (result->status == EZI_OK)
        printf("╭ ✓ Installation complete ╮\n");
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [script.ezi] [--dry-run]\n", prog);
    fprintf(stderr, "Run an EziScript (.ezi) installer definition file.\n");
    fprintf(stderr, "Inspired by https://github.com/codingsushi79/ezinstaller\n");
}

int main(int argc, char **argv) {
    const char *script_path = NULL;
    int dry_run = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) dry_run = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (!script_path) script_path = argv[i];
        else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!script_path) {
        char buf[4096];
        printf("Script path (.ezi): ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) return 1;
        buf[strcspn(buf, "\r\n")] = '\0';
        if (!*buf) return 1;
        script_path = buf;
    }

    ezi_script *script = NULL;
    char *error = NULL;
    ezi_status st = ezi_load_script(script_path, &script, &error);
    if (st != EZI_OK) {
        fprintf(stderr, "Parse error: %s\n", error ? error : ezi_status_string(st));
        free(error);
        return 1;
    }

    print_header(script, dry_run);

    ezi_ops ops = {
        .log = log_cb,
        .dry_run = dry_run,
    };

    ezi_result result = ezi_run_script(script, &ops);
    print_footer(dry_run, &result);
    ezi_script_destroy(script);

    if (result.status != EZI_OK) {
        fprintf(stderr, "Install failed: %s\n", result.error ? result.error : "unknown error");
        ezi_result_free(&result);
        return 1;
    }

    ezi_result_free(&result);
    return 0;
}
