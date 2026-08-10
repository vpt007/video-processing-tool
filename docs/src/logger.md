# logger.h - Minimal File Logger

> **File:** [`src/logger.h`](../../src/logger.h)
> **Package:** `app`
> **Purpose:** Minimal timestamped file logger

## Overview

[`logger.h`](../../src/logger.h) provides a tiny `Logger` type plus `log_info()`/`log_warn()`/`log_error()` helpers that append timestamped lines to a log file. The file is opened in append mode by `logger_init()` and flushed after every write so a crash never loses the tail of the log.

## Usage

```c
Logger logger;
logger_init(&logger, "vpt_log.txt");
log_info(&logger, "Input File: %s", path);
log_error(&logger, "Failed to open %s", path);
logger_close(&logger);
```

## Structs

### Logger
**File:** [`logger.h`](../../src/logger.h:14)

| Field | Type | Description |
|-------|------|-------------|
| `fp` | `FILE *` | The open log file handle (NULL if not open) |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `logger_init(lg, path)` | `lg: Logger*, path: const char*` | `void` | Opens `path` for appending |
| `logger_close(lg)` | `lg: Logger*` | `void` | Closes the log file |
| `log_info(lg, fmt, ...)` | `lg: Logger*, fmt: const char*, ...` | `void` | Writes an INFO line |
| `log_warn(lg, fmt, ...)` | `lg: Logger*, fmt: const char*, ...` | `void` | Writes a WARN line |
| `log_error(lg, fmt, ...)` | `lg: Logger*, fmt: const char*, ...` | `void` | Writes an ERROR line |

## Implementation Notes

- All functions are `static inline` (header-style), safe for multiple inclusion.
- Each line is prefixed with a `[YYYY-MM-DD HH:MM:SS] LEVEL:` timestamp.
- Uses `localtime_r` (POSIX) for thread-safe time formatting.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [vector.h](vector.md) | [src/](index.md) | [icon_font.h](icon_font.md) |