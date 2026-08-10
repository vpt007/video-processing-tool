# vector.h - Dynamic Array (Vector) Macros

> **File:** [`src/vector.h`](../../src/vector.h)
> **Package:** `app`
> **Purpose:** Header-only dynamic array (vector) implemented with macros

## Overview

[`vector.h`](../../src/vector.h) provides a growable dynamic array via macros. A struct embeds the `vpopulate()` fields, then the `v*` macros operate on it. The array grows geometrically (doubling), so amortized append is O(1).

## Usage

```c
typedef struct { vpopulate(char); } String;   // a growable char buffer
String s = {0};
vpush(s, 'a');                                // append one element
vreserve(s, 1024);                            // pre-allocate capacity
char c = vat(s, 0);                           // indexed read
vpop(s);                                      // remove last element
vclear(s);                                    // drop all elements
```

## Macros

| Macro | Input | Description |
|-------|-------|-------------|
| `vpopulate(t)` | `t: type` | Declares the three fields every vector struct embeds: `items`, `count`, `capacity` |
| `vpush(arr, x)` | `arr: vector, x: element` | Appends `x`, growing the buffer if full |
| `vreserve(arr, n)` | `arr: vector, n: size` | Ensures capacity for at least `n` elements |
| `vat(arr, i)` | `arr: vector, i: index` | Reads the element at index `i` (no bounds check) |
| `vpop(arr)` | `arr: vector` | Removes and returns the last element (no bounds check) |
| `vclear(arr)` | `arr: vector` | Drops all elements, keeping the buffer for reuse |

## Implementation Notes

- `vpush` doubles capacity when full (starting at `1 << 10`).
- `vreserve` reallocates only when the current capacity is insufficient.
- No bounds checking is performed by `vat`/`vpop`; the caller is responsible.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [os.c](os.md) | [src/](index.md) | [logger.h](logger.md) |