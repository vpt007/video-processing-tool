/* test.c — lightweight self-tests (included by the unity build).
 *
 * A small, self-contained test harness. test_run_all() is not wired into the
 * app's startup; it exists so the build has a place to run quick sanity checks
 * during development. It is kept dependency-free so it compiles anywhere in
 * the unity build.
 */
#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
	if (cond) {
		g_pass++;
	} else {
		g_fail++;
		fprintf(stderr, "FAIL: %s\n", name);
	}
}

/* Run a handful of trivial sanity checks. Not called by the app. */
void test_run_all(void)
{
	g_pass = g_fail = 0;

	check(strlen("") == 0, "empty string length");
	check(strlen("abc") == 3, "string length");
	check(strcmp("a", "a") == 0, "strcmp equal");
	check(strcmp("a", "b") != 0, "strcmp not equal");
	check((1 + 1) == 2, "arithmetic");

	if (g_fail == 0)
		printf("test_run_all: %d passed\n", g_pass);
	else
		printf("test_run_all: %d passed, %d failed\n", g_pass, g_fail);
}