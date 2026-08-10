# ve/ - Video-Edit Operation Library

> **Path:** [`src/ve/`](../../../src/ve/)
> **Purpose:** A self-contained library (no ffmpeg subprocess) for describing and planning non-linear video edits

## Overview

The `ve/` library models video edits as a pipeline:

```
text (DSL) ──ve_parse──▶ VeOp[] ──ve_plan──▶ VePlan ──ve_execute──▶ file
                              │
              decides, per stream: COPY (lossless remux) vs
              TRANSCODE (decode→filter→encode) vs DROP
```

**Design rule:** a stream is only ever transcoded if some op *forces* a decode. Everything else is a container/metadata edit and stays byte-for-byte.

`ve.h`, `ve_dsl.c`, `ve_plan.c`, and `ve_ops.c` have **no libav dependency** (pure C — easy to test and embed in a CLI). Only `ve_exec.c` links libav.

## Files

| File | Purpose |
|------|---------|
| [`ve.h`](ve.md) | Operation model, classification, and public API |
| [`ve_dsl.c`](ve_dsl.md) | Parse the editing DSL into a `VeScript` |
| [`ve_ops.c`](ve_ops.md) | Classification: does an operation force a re-encode? |
| [`ve_plan.c`](ve_plan.md) | Turn ops into an execution plan (COPY/TRANSCODE/DROP) |
| [`ve_exec.c`](ve_exec.md) | Execute a plan with libav (lossless remux vs transcode) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [cimgui_unity.cpp](../cimgui_unity.md) | [src/](../index.md) | [ve.h](ve.md) |