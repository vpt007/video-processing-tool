# util.c - Shared Helpers

> **File:** [`src/util.c`](../../src/util.c)
> **Package:** `app`
> **Purpose:** Small shared helper functions

## Overview

[`util.c`](../../src/util.c) provides small shared helpers. It is kept as a `.c` so it can be `#include`d directly into the unity build the same way [`os.c`](os.md) is. All functions are `static inline` so multiple inclusion is safe.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_duplicate(s)` | `s: const char*` | `char *` | Returns a heap-allocated NUL-terminated copy of `s` (or NULL on failure / when `s` is NULL) |

## Implementation Notes

- `str_duplicate` allocates `strlen(s) + 1` bytes and copies the string.
- The caller is responsible for `free()`-ing the returned pointer.
- Used by [`ffwrap.c`](ffwrap.md) in the `add_args` macro.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [icon_font.h](icon_font.md) | [src/](index.md) | [vp_engine.c](vp_engine.md) |