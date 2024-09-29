#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include "tjstream.h"

static event_log_cb log_fn = NULL;
static int tjs_role = 0;

int tjs_debug_get_loglevel(void)
{
	extern int loglevel;
	return loglevel;
}
static void tjs_log(int severity, const char *msg)
{
	if (log_fn)
		log_fn(severity, msg);
	else {
		const char *severity_str;
		switch (severity) {
		case LVERBOSE:
			severity_str = "debug";
			break;
		case LINFO:
			severity_str = "info";
			break;
		case LWARN:
			severity_str = "warn";
			break;
		case LERROR:
			severity_str = "err";
			break;
		default:
			severity_str = "???";
			break;
		}
		const char *role;
		if (tjs_role == 0 ) role = "";
		else if (tjs_role == 1) role = "client ";
		else role = "server ";
		(void)fprintf(stderr, "[%s] %s%s\n", severity_str, role, msg);
	}
}

void event_logv_(int severity, const char *fmt, va_list ap)
{
	char buf[1024];

	if (severity > tjs_debug_get_loglevel())
		return;

	if (fmt != NULL)
		vsnprintf(buf, sizeof(buf), fmt, ap);
	else
		buf[0] = '\0';

	tjs_log(severity, buf);
}
void tjs_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(LERROR, fmt, ap);
	va_end(ap);
}

void tjs_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(LWARN, fmt, ap);
	va_end(ap);
}

void tjs_msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(LINFO, fmt, ap);
	va_end(ap);
}

void
tjs_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	event_logv_(LVERBOSE, fmt, ap);
	va_end(ap);
}
