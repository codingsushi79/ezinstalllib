#include "internal.h"

#include <string.h>

#ifdef EZI_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

const char *ezi_status_string(ezi_status status) {
    switch (status) {
    case EZI_OK:              return "ok";
    case EZI_ERR_PARSE:       return "parse error";
    case EZI_ERR_INSTALL:     return "install error";
    case EZI_ERR_IO:          return "I/O error";
    case EZI_ERR_NETWORK:     return "network error";
    case EZI_ERR_OOM:         return "out of memory";
    case EZI_ERR_UNSUPPORTED: return "unsupported";
    default:                  return "unknown error";
    }
}

ezi_os ezi_current_os(void) {
#ifdef EZI_PLATFORM_WINDOWS
    return EZI_OS_WINDOWS;
#else
    struct utsname info;
    if (uname(&info) == 0) {
        if (strstr(info.sysname, "Linux") != NULL) return EZI_OS_LINUX;
        if (strstr(info.sysname, "Darwin") != NULL) return EZI_OS_MACOS;
    }
#if defined(__linux__)
    return EZI_OS_LINUX;
#elif defined(__APPLE__)
    return EZI_OS_MACOS;
#else
    return EZI_OS_LINUX;
#endif
#endif
}

const char *ezi_os_name(ezi_os os) {
    switch (os) {
    case EZI_OS_LINUX:   return "linux";
    case EZI_OS_MACOS:   return "macos";
    case EZI_OS_WINDOWS: return "windows";
    case EZI_OS_UNIX:    return "unix";
    default:             return "all";
    }
}

int ezi_os_matches(ezi_os block, ezi_os runtime) {
    if (block == EZI_OS_ALL) return 1;
    if (block == EZI_OS_UNIX) return runtime == EZI_OS_LINUX || runtime == EZI_OS_MACOS;
    return block == runtime;
}

ezi_os ezi_parse_os_name(const char *name) {
    if (ezi_str_ieq(name, "linux"))   return EZI_OS_LINUX;
    if (ezi_str_ieq(name, "macos") || ezi_str_ieq(name, "mac") ||
        ezi_str_ieq(name, "darwin") || ezi_str_ieq(name, "osx"))
        return EZI_OS_MACOS;
    if (ezi_str_ieq(name, "windows") || ezi_str_ieq(name, "win") ||
        ezi_str_ieq(name, "win32"))
        return EZI_OS_WINDOWS;
    if (ezi_str_ieq(name, "unix"))    return EZI_OS_UNIX;
    return EZI_OS_ALL;
}
