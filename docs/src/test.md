# test.c - Self-Test Harness

> **File:** [`src/test.c`](../../src/test.c)
> **Package:** `app`
> **Purpose:** Lightweight self-tests (included by the unity build)

## Overview

[`test.c`](../../src/test.c) provides a small, self-contained test harness. `test_run_all()` is **not** wired into the app's startup; it exists so the build has a place to run quick sanity checks during development. It is dependency-free so it compiles anywhere in the unity build.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `test_run_all()` | — | `void` | Runs a handful of trivial sanity checks and prints pass/fail counts |

## Implementation Notes

- Uses a static pass/fail counter and a `check()` helper.
- Prints `test_run_all: N passed` on success, or `N passed, M failed` on failure.
- Not called by the application; intended for development-time verification.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [vp_thumb.c](vp_thumb.md) | [src/](index.md) | [cimgui_unity.cpp](cimgui_unity.md) |