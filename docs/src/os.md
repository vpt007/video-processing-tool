# os.c - Cross-Platform OS Abstraction Layer

> **File:** [`src/os.c`](../../src/os.c)
> **Package:** `app`
> **Purpose:** Single portable API for common OS operations (filesystem, paths, env, timing, process)

## Overview

[`os.c`](../../src/os.c) provides a single portable API for the common OS operations the tool needs: filesystem (stat, copy, mkdir, read/write), paths (join, basename, dirname, extension), environment variables, process info, timing, and directory listing. Every function is `static inline` and branches on `_WIN32` vs POSIX, so the rest of the codebase never deals with platform `#ifdef`s.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `OS_SEP` | `"\\"` / `"/"` | Path separator string |
| `OS_SEP_CHAR` | `'\\'` / `'/'` | Path separator char |
| `OS_NAME` | `"windows"` / `"linux"` / `"darwin"` | Platform name |
| `OS_PATHMAX` | `MAX_PATH` / `PATH_MAX` | Max path length |
| `OS_COPY_BUF` | `65536` | Copy buffer size |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `os_lasterr()` | — | `int` | Last OS error code (GetLastError / errno) |
| `os_get_executable_path(path, size)` | `path: char*, size: size_t` | `char *` | Absolute path of the running executable |
| `os_getcwd(buf, size)` | `buf: char*, size: int` | `char *` | Current working directory |
| `os_chdir(path)` | `path: const char*` | `int` | Change working directory |
| `os_mkdir(path)` | `path: const char*` | `int` | Create a single directory |
| `os_makedirs(path)` | `path: const char*` | `int` | Create a directory and missing parents |
| `os_rename(src, dst)` | `src, dst: const char*` | `int` | Rename/move a file or directory |
| `os_exists(path)` | `path: const char*` | `int` | Non-zero if the path exists |
| `os_isfile(path)` | `path: const char*` | `int` | Non-zero if a regular file |
| `os_isdir(path)` | `path: const char*` | `int` | Non-zero if a directory |
| `os_islink(path)` | `path: const char*` | `int` | Non-zero if a symbolic link |
| `os_filesize(path)` | `path: const char*` | `long long` | File size in bytes (-1 on error) |
| `os_mtime(path)` | `path: const char*` | `long long` | Last modification time (epoch seconds) |
| `os_copy(src, dst)` | `src, dst: const char*` | `int` | Copy a file (streaming) |
| `os_realpath(path, out, size)` | `path: const char*, out: char*, size: int` | `char *` | Resolve to absolute canonical path |
| `os_symlink(target, link)` | `target, link: const char*` | `int` | Create a symbolic link |
| `os_readlink(path, out, size)` | `path: const char*, out: char*, size: int` | `int` | Read a symlink target |
| `os_getenv(name)` | `name: const char*` | `char *` | Read an environment variable |
| `os_path_join(out, size, ...)` | `out: char*, size: int, ...` | `void` | Join path components with the OS separator |
| `os_path_basename(path)` | `path: const char*` | `const char *` | Last path component |
| `os_path_dirname(out, size, path)` | `out: char*, size: int, path: const char*` | `void` | Directory portion of a path |
| `os_path_ext(path)` | `path: const char*` | `const char *` | File extension (including the dot) |
| `os_time_ms()` | — | `long long` | Wall-clock time in ms |
| `os_monotonic_ms()` | — | `long long` | Monotonic time in ms |
| `os_hostname(out, size)` | `out: char*, size: int` | `int` | System hostname |
| `os_dir_read(...)` | directory handle | `int` | Read directory entries |

## Implementation Notes

- All functions are `static inline` and branch on `_WIN32` vs POSIX.
- `os_path_join` is a macro that appends a NULL sentinel so the variadic function knows where to stop.
- `os_get_executable_path` uses `GetModuleFileNameA` (Windows), `/proc/self/exe` (Linux), or `_NSGetExecutablePath` (macOS).
- `os_copy` streams in `OS_COPY_BUF` chunks on POSIX; uses `CopyFileA` on Windows.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [config.h](config.md) | [src/](index.md) | [vector.h](vector.md) |