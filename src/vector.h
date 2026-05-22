#include <stdio.h>

#define vpopulate(t) t* items; size_t count; size_t capacity

#define vpush(arr, x) do { \
    if ((arr).count == (arr).capacity) { \
        (arr).capacity = (arr).capacity == 0 ? (1<<10) : (arr).capacity << 1; \
        (arr).items = realloc((arr).items, (arr).capacity * sizeof(*(arr).items)); \
    } \
    (arr).items[(arr).count++] = (x); \
} while(0)

#define vreserve(arr, n) do { \
    if ((arr).capacity < (n)) { \
        (arr).capacity = (n); \
        (arr).items = realloc((arr).items, (arr).capacity * sizeof(*(arr).items)); \
    } \
} while(0)

#define vat(arr, i) ((arr).items[i])
#define vpop(arr)   ((arr).items[--(arr).count])
#define vclear(arr) ((arr).count = 0)
