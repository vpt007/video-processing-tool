/* logger.h — minimal file logger (header-style, included by the unity build).
 *
 * Provides a tiny Logger type plus log_info()/log_warn()/log_error() helpers
 * that append timestamped lines to a log file. The file is opened in append
 * mode by logger_init() and flushed after every write so a crash never loses
 * the tail of the log.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

typedef struct {
	FILE *fp;
} Logger;

/* Open `path` for appending. Safe to call with a NULL logger. */
static inline void logger_init(Logger *lg, const char *path)
{
	if (!lg)
		return;
	lg->fp = fopen(path, "a");
}

/* Close the log file (no-op if not open). */
static inline void logger_close(Logger *lg)
{
	if (lg && lg->fp) {
		fclose(lg->fp);
		lg->fp = NULL;
	}
}

/* Core write: prefix a timestamp, then the formatted message + newline.
 * Always echoes to the terminal (stdout) so logs are visible when the app is
 * run from a console; also appends to the log file if one is open. */
static inline void logger_write(Logger *lg, const char *level,
				const char *fmt, va_list ap)
{
	time_t t = time(NULL);
	struct tm tmv;
	localtime_r(&t, &tmv);
	char ts[32];
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

	/* Echo to the terminal. */
	printf("[%s] %s: ", ts, level);
	{
		va_list ap2;
		va_copy(ap2, ap);
		vprintf(fmt, ap2);
		va_end(ap2);
	}
	printf("\n");
	fflush(stdout);

	/* Append to the log file if one is open. */
	if (lg && lg->fp) {
		fprintf(lg->fp, "[%s] %s: ", ts, level);
		vfprintf(lg->fp, fmt, ap);
		fputc('\n', lg->fp);
		fflush(lg->fp);
	}
}

static inline void log_info(Logger *lg, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	logger_write(lg, "INFO", fmt, ap);
	va_end(ap);
}

static inline void log_warn(Logger *lg, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	logger_write(lg, "WARN", fmt, ap);
	va_end(ap);
}

static inline void log_error(Logger *lg, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	logger_write(lg, "ERROR", fmt, ap);
	va_end(ap);
}

#endif /* LOGGER_H */