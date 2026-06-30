# ezinstalllib

**ezinstalllib** is a portable C library for running installer scripts and performing install operations. It is inspired by [ezinstaller](https://github.com/codingsushi79/ezinstaller) — the Python tool that executes **EziScript** (`.ezi`) and **FEZI** (`.fezi`) definition files.

This library provides:

- **EziScript parser** — read `.ezi` files with `MKDIR`, `GET`, `COPY`, `WRITE`, `RUN`, `CHMOD`, `ENV`, `OS` blocks, and more
- **Install primitives** — callable directly from C without a script file
- **Path expansion** — `{{target}}`, `{{home}}`, `{{script}}`, `{{cwd}}`, `{{os}}`, and `~` paths
- **Cross-platform** — Linux, macOS, and Windows (with platform-appropriate behavior)
- **CLI** — `ezi` command compatible with ezinstaller-style `.ezi` scripts

## Build

Requirements:

- C11 compiler
- CMake 3.16+
- **libcurl** (optional but recommended for HTTP downloads; falls back to `curl(1)`)

```bash
cmake -B build
cmake --build build
```

Install (optional):

```bash
cmake --install build --prefix ~/.local
```

## CLI usage

```bash
./build/ezi examples/demo.ezi
./build/ezi examples/demo.ezi --dry-run
./build/run_demo
```

After install, the demo launcher is at `~/.local/share/demo-app/bin/demo` on Unix.

## Library usage

```c
#include <ezinstalllib.h>
#include <stdio.h>

int main(void) {
    ezi_context *ctx = ezi_context_create("~/apps/my-tool", ".");
    ezi_ops ops = { .dry_run = 0 };

    ezi_mkdir(ctx, &ops, "bin");
    ezi_download(ctx, &ops,
        "https://example.com/tool-linux-amd64", "bin/tool");
    ezi_chmod(ctx, &ops, "755", "bin/tool");

    ezi_script *script = NULL;
    char *err = NULL;
    if (ezi_load_script("install.ezi", &script, &err) == EZI_OK) {
        ezi_result r = ezi_run_script(script, &ops);
        if (r.status != EZI_OK)
            fprintf(stderr, "%s\n", r.error);
        ezi_result_free(&r);
        ezi_script_destroy(script);
    }

    ezi_context_destroy(ctx);
    return 0;
}
```

Link with `-lezinstalllib` and include `ezinstalllib.h`.

## EziScript format

Scripts use the same uppercase command format as [ezinstaller GUIDE.md](https://github.com/codingsushi79/ezinstaller/blob/main/GUIDE.md):

```ezi
@name My Application
@target ~/apps/my-app

MKDIR bin
GET https://example.com/app.tar.gz AS tmp/app.tar.gz
EXTRACT tmp/app.tar.gz TO bin/
CHMOD 755 bin/app

OS unix
ENV PATH append {{target}}/bin
END OS
```

Supported commands: `MKDIR`, `GET`, `EXTRACT`, `COPY`, `MOVE`, `DELETE`, `WRITE`, `RUN`, `CHMOD`, `ENV`, `INCLUDE`, and `OS` / `END OS` blocks.

FEZI (`.fezi`) AppleScript-style scripts are not parsed yet — convert to `.ezi` or use the Python ezinstaller for FEZI files.

## Project layout

```
include/ezinstalllib.h   Public API
src/                     Library implementation
tools/ezi.c              Command-line runner
examples/run_demo.c      Example program using demo.ezi
examples/                Sample .ezi installer
```

## License

MIT — see [LICENSE](LICENSE).

## Related

- [codingsushi79/ezinstaller](https://github.com/codingsushi79/ezinstaller) — Python reference implementation with FEZI support, progress UI, and bootstrap installer
