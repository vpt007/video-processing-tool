# ve_dsl.c - DSL Parser

> **File:** [`src/ve/ve_dsl.c`](../../../src/ve/ve_dsl.c)
> **Package:** `ve`
> **Purpose:** Parse the editing DSL into a `VeScript` (input/output + `VeOp[]`)

## Overview

[`ve_dsl.c`](../../../src/ve/ve_dsl.c) parses the editing DSL text into a `VeScript`. The grammar is one statement per line; tokens are whitespace-separated (use `"double quotes"` for paths/values with spaces); `#` starts a comment; verb names are case-insensitive.

## DSL Grammar

```text
in   "clip.mp4"
trim 00:00:10  1:05.5      # keyframe-accurate, lossless when possible
rotate 90                  # lossless (display matrix)
mute                       # lossless (drop audio)
crop 1280 720 100 60       # forces video re-encode
volume 150%                # audio re-encode only
out  "out.mp4"
```

### Supported Verbs

| Verb | Arguments | Notes |
|------|-----------|-------|
| `in` / `input` / `load` | path | Sets the input |
| `out` / `output` / `export` | path | Sets the output |
| `rotate` | angle [`ccw`] | Rotation in degrees |
| `flip` | `h` / `v` | Flip axis |
| `mute` | — | Drop all audio |
| `add-audio` / `addaudio` | path [lang] | Add an audio track |
| `remove-audio` | index | Remove an audio track |
| `volume` | factor | Volume (allows `%`) |
| `mono` | — | Stereo → mono |
| `scale` / `resize` | W H or WxH | Resize |
| `crop` | W H X Y | Crop |
| `trim` / `cut` | start [end] | Trim window |
| `merge` / `concat` | paths... | Concatenate |
| `reverse` | `audio`/`a` or video | Reverse a stream |
| `thumbnail` / `cover` | path | Add cover thumbnail |
| `remove-thumbnail` | — | Remove cover |
| `subtitle` / `sub` | path [lang] | Add subtitle |
| `remove-subtitle` | index | Remove subtitle |
| `brightness` / `contrast` / `hue` / `saturation` / `sat` / `gamma` | value | Color adjust |
| `grayscale` / `gray` | — | Grayscale |
| `aspect` | N D or N:D | Set display aspect |
| `fps` | value | Set frame rate |
| `speed` | factor | Speed (video + audio) |
| `fade` | in/out dur [target] | Fade in/out |
| `denoise` | — | Denoise (hqdn3d) |
| `sharpen` | — | Sharpen (unsharp) |
| `pad` / `letterbox` | W H | Letterbox/pillarbox |
| `metadata` / `meta` | key value | Set metadata |
| `format` / `container` | name | Force output container |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ve_parse(text, out, err, errlen)` | `text: const char*, out: VeScript*, err: char*, errlen: int` | `bool` | Parses DSL text; on failure fills `err` and returns false |
| `ve_script_free(s)` | `s: VeScript*` | `void` | Frees the parsed ops array |

## Implementation Notes

- `tokenize` splits a line into tokens, respecting quotes and stopping at `#`.
- `parse_time` parses `[HH:]MM:SS[.ms]` or `SS[.ms]` into seconds.
- `parse_num` parses a number, allowing a trailing `%` (returns a fraction).
- Errors are reported as `line N: <message>`.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve.h](ve.md) | [ve/](index.md) | [ve_ops.c](ve_ops.md) |