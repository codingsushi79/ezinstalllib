/*
 * Run examples/demo.ezi using ezinstalllib.
 *
 * Build (from repo root):
 *   cmake -B build && cmake --build build
 *
 * Run:
 *   ./build/run_demo
 *   ./build/run_demo examples/demo.ezi
 *   ./build/run_demo examples/demo.ezi --dry-run
 */
#include <ezinstalllib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_step(void *userdata, const char *message) {
    (void)userdata;
    if (message && *message)
        printf("  · %s\n", message);
}

static void print_header(const ezi_script *script, int dry_run) {
    const ezi_meta *meta = ezi_script_meta(script);
    printf("Running %s v%s%s\n",
           meta->name ? meta->name : "installer",
           meta->version ? meta->version : "1.0",
           dry_run ? " (dry run)" : "");
    printf("  script:   %s\n", ezi_script_path(script));
    printf("  target:   %s\n", meta->target ? meta->target : ".");
    printf("  platform: %s\n\n", ezi_os_name(ezi_current_os()));
}

int main(int argc, char **argv) {
    const char *script_path = "examples/demo.ezi";
    int dry_run = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [demo.ezi] [--dry-run]\n", argv[0]);
            return 0;
        } else {
            script_path = argv[i];
        }
    }

    ezi_script *script = NULL;
    char *parse_error = NULL;
    ezi_status st = ezi_load_script(script_path, &script, &parse_error);
    if (st != EZI_OK) {
        fprintf(stderr, "Failed to load %s: %s\n",
                script_path, parse_error ? parse_error : ezi_status_string(st));
        free(parse_error);
        return 1;
    }

    print_header(script, dry_run);

    ezi_ops ops = {
        .log = log_step,
        .dry_run = dry_run,
    };

    ezi_result result = ezi_run_script(script, &ops);
    ezi_script_destroy(script);

    if (result.env_count > 0) {
        printf("\nEnvironment notes:\n");
        for (size_t i = 0; i < result.env_count; i++)
            printf("  · %s\n", result.env_notes[i]);
    }

    if (result.status != EZI_OK) {
        fprintf(stderr, "\nInstall failed: %s\n",
                result.error ? result.error : ezi_status_string(result.status));
        ezi_result_free(&result);
        return 1;
    }

    printf("\nDone%s.\n", dry_run ? " (no changes made)" : "");
    ezi_result_free(&result);
    return 0;
}
