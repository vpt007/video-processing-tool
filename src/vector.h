/* vector.h — header-only dynamic array (vector) implemented with macros.
 *
 * Usage: declare a struct that embeds the vpopulate() fields, then use the
 * v* macros on it. Example:
 *
 *     typedef struct { vpopulate(char); } String;   // a growable char buffer
 *     String s = {0};
 *     vpush(s, 'a');                                // append one element
 *     vreserve(s, 1024);                            // pre-allocate capacity
 *     char c = vat(s, 0);                           // indexed read
 *     vpop(s);                                      // remove last element
 *     vclear(s);                                    // drop all elements
 *
 * The array grows geometrically (doubling) so amortized append is O(1).
 */
#ifndef VECTOR_H
#define VECTOR_H
#include <stdlib.h>

/* Declares the three fields every vector struct must embed: a pointer to the
   heap buffer, the number of live elements, and the allocated capacity. */
#define vpopulate(t) t* items; size_t count; size_t capacity

/* Append `x` to the end of `arr`, growing the buffer if it is full. */
#define vpush(arr, x) do { \
    if ((arr).count == (arr).capacity) { \
        (arr).capacity = (arr).capacity == 0 ? (1<<10) : (arr).capacity << 1; \
        (arr).items = realloc((arr).items, (arr).capacity * sizeof(*(arr).items)); \
    } \
    (arr).items[(arr).count++] = (x); \
} while(0)

/* Ensure `arr` can hold at least `n` elements without reallocating. */
#define vreserve(arr, n) do { \
    if ((arr).capacity < (n)) { \
        (arr).capacity = (n); \
        (arr).items = realloc((arr).items, (arr).capacity * sizeof(*(arr).items)); \
    } \
} while(0)

/* Read the element at index `i` (no bounds checking). */
#define vat(arr, i) ((arr).items[i])
/* Remove and return the last element (no bounds checking). */
#define vpop(arr)   ((arr).items[--(arr).count])
/* Drop all elements, keeping the allocated buffer for reuse. */
#define vclear(arr) ((arr).count = 0)
#endif
