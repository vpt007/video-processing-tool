/* util.c — small shared helpers (header-style, included by ffwrap.c).
 *
 * Kept as a .c so it can be #included directly into the unity build the same
 * way os.c is. All functions are static inline so multiple inclusion is safe.
 */
#ifndef UTIL_C
#define UTIL_C

#include <stdlib.h>
#include <string.h>

/* Return a heap-allocated NUL-terminated copy of `s` (or NULL on failure /
 * when s is NULL). Caller frees with free(). */
static inline char *str_duplicate(const char *s)
{
	if (!s)
		return NULL;
	size_t n = strlen(s) + 1;
	char *d = (char *)malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

#endif /* UTIL_C */